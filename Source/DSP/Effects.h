#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

namespace bv
{

//==============================================================================
// Simple circular delay line with linear-interpolated fractional reads.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        size = juce::nextPowerOfTwo (maxSamples + 4);
        mask = size - 1;
        buf.assign ((size_t) size, 0.0f);
        write = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); write = 0; }

    void push (float x) noexcept
    {
        buf[(size_t) write] = x;
        write = (write + 1) & mask;
    }

    // read (0) returns the most recently pushed sample, read (d) the one pushed d samples earlier
    float read (float delay) const noexcept
    {
        const float pos = (float) (write - 1) - delay;
        const int i0 = (int) std::floor (pos);
        const float frac = pos - (float) i0;
        const float a = buf[(size_t) (i0 & mask)];
        const float b = buf[(size_t) ((i0 + 1) & mask)];
        return a + frac * (b - a);
    }

    float readInt (int delay) const noexcept { return buf[(size_t) ((write - 1 - delay) & mask)]; }

private:
    std::vector<float> buf;
    int size = 0, mask = 0, write = 0;
};

//==============================================================================
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float hz, double fs) { a = std::exp (-juce::MathConstants<float>::twoPi * hz / (float) fs); }
    float lp (float x) noexcept { z = x + a * (z - x); return z; }
    float hp (float x) noexcept { return x - lp (x); }
    void reset() { z = 0.0f; }
};

//==============================================================================
// Tape wow + flutter + slow random drift, shared modulation source for all channels.
class Wobble
{
public:
    static constexpr float baseDelayMs = 7.0f;

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        lines.resize ((size_t) numChannels);
        for (auto& l : lines)
            l.prepare ((int) (fs * 0.05));
        driftFilter.setCutoff (0.35f, fs);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.reset();
        wowPhase = flutterPhase = 0.0f;
        drift = driftTarget = 0.0f;
        driftFilter.reset();
        driftCounter = 0;
    }

    int getLatency() const noexcept { return (int) std::round (baseDelayMs * 0.001 * fs); }

    // Advance the shared LFO by one sample; returns the delay in samples.
    float tick (float depth) noexcept
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        wowPhase += twoPi * 0.55f / (float) fs;
        if (wowPhase > twoPi) wowPhase -= twoPi;
        flutterPhase += twoPi * 6.8f / (float) fs;
        if (flutterPhase > twoPi) flutterPhase -= twoPi;

        if (--driftCounter <= 0)
        {
            driftTarget = rng.nextFloat() * 2.0f - 1.0f;
            driftCounter = (int) (fs * (0.2 + 0.6 * rng.nextDouble()));
        }
        drift = driftFilter.lp (driftTarget);

        const float ms = baseDelayMs
                       + depth * (2.8f * std::sin (wowPhase)
                                + 0.22f * std::sin (flutterPhase)
                                + 1.6f * drift);
        return ms * 0.001f * (float) fs;
    }

    float process (int channel, float x, float delaySamples) noexcept
    {
        auto& l = lines[(size_t) channel];
        l.push (x);
        return l.read (delaySamples);
    }

private:
    double fs = 44100.0;
    std::vector<DelayLine> lines;
    float wowPhase = 0.0f, flutterPhase = 0.0f, drift = 0.0f, driftTarget = 0.0f;
    int driftCounter = 0;
    OnePole driftFilter;
    juce::Random rng { 0x5eed };
};

//==============================================================================
// Saturation, then sample-rate / bit-depth reduction for the upper half of the range.
class Grit
{
public:
    void prepare (int numChannels)
    {
        held.assign ((size_t) numChannels, 0.0f);
        counter.assign ((size_t) numChannels, 0);
    }

    void reset()
    {
        std::fill (held.begin(), held.end(), 0.0f);
        std::fill (counter.begin(), counter.end(), 0);
    }

    float process (int ch, float x, float amount) noexcept
    {
        if (amount <= 0.0005f)
            return x;

        const float drive = 1.0f + 7.0f * amount;
        float y = std::tanh (drive * x) / std::pow (drive, 0.75f);

        if (amount > 0.4f)
        {
            const float c = (amount - 0.4f) / 0.6f;
            const int hold = 1 + (int) (c * 3.0f);
            auto& cnt = counter[(size_t) ch];
            auto& h = held[(size_t) ch];
            if (cnt <= 0)
            {
                const float levels = std::pow (2.0f, 15.0f - 8.0f * c);
                h = std::round (y * levels) / levels;
                cnt = hold;
            }
            --cnt;
            y = y + c * (h - y);
        }
        return y;
    }

private:
    std::vector<float> held;
    std::vector<int> counter;
};

//==============================================================================
// Dark dub echo: filtered feedback, slightly different times per side.
class Echo
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        lines.resize ((size_t) numChannels);
        lp.resize ((size_t) numChannels);
        hp.resize ((size_t) numChannels);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            lines[i].prepare ((int) (fs * 2.2));
            lp[i].setCutoff (2600.0f, fs);
            hp[i].setCutoff (280.0f, fs);
        }
        time.reset (fs, 0.25);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
    }

    void setTimeMs (float ms) { time.setTargetValue (ms); }

    float nextTimeSamples() noexcept { return time.getNextValue() * 0.001f * (float) fs; }

    float process (int ch, float x, float amount, float delaySamples) noexcept
    {
        auto& l = lines[(size_t) ch];
        const float d = delaySamples * (ch == 1 ? 1.035f : 1.0f);
        const float wet = l.read (juce::jmax (0.0f, d - 1.0f)); // read before push: 1 extra sample
        const float fb = 0.2f + 0.5f * amount;
        l.push (x * amount * 0.8f + hp[(size_t) ch].hp (lp[(size_t) ch].lp (wet)) * fb);
        return x + wet;
    }

private:
    double fs = 44100.0;
    std::vector<DelayLine> lines;
    std::vector<OnePole> lp, hp;
    juce::SmoothedValue<float> time { 375.0f };
};

//==============================================================================
// Vinyl surface: hiss, crackle and the occasional pop.
class Crackle
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        ch.resize ((size_t) numChannels);
        for (size_t i = 0; i < ch.size(); ++i)
        {
            auto& c = ch[i];
            c.hissLp.setCutoff (5500.0f, fs);
            c.hissHp.setCutoff (700.0f, fs);
            c.clickHp.setCutoff (900.0f, fs);
            c.rng.setSeed ((juce::int64) (1234 + 777 * i));
        }
        decay = std::exp (-1.0f / (0.00035f * (float) fs));
        popDecay = std::exp (-1.0f / (0.0025f * (float) fs));
    }

    void reset()
    {
        for (auto& c : ch)
        {
            c.hissLp.reset(); c.hissHp.reset(); c.clickHp.reset();
            c.env = c.popEnv = 0.0f;
        }
    }

    float next (int channel, float amount) noexcept
    {
        if (amount <= 0.0005f)
            return 0.0f;

        auto& c = ch[(size_t) channel];
        auto& r = c.rng;

        const float white = r.nextFloat() * 2.0f - 1.0f;
        const float hiss = c.hissHp.hp (c.hissLp.lp (white)) * 0.006f;

        if (r.nextFloat() < amount * 28.0f / (float) fs)
        {
            const float u = r.nextFloat();
            c.env = (0.04f + 0.35f * u * u * u * u) * (r.nextBool() ? 1.0f : -1.0f);
        }
        if (r.nextFloat() < amount * 0.6f / (float) fs)
            c.popEnv = 0.25f + 0.25f * r.nextFloat();

        const float click = c.clickHp.hp (c.env * (0.6f + 0.4f * white));
        const float pop = c.popEnv * white;
        c.env *= decay;
        c.popEnv *= popDecay;

        return amount * (hiss + click + pop * 0.6f);
    }

private:
    struct State
    {
        OnePole hissLp, hissHp, clickHp;
        float env = 0.0f, popEnv = 0.0f;
        juce::Random rng;
    };

    double fs = 44100.0;
    float decay = 0.9f, popDecay = 0.99f;
    std::vector<State> ch;
};

} // namespace bv
