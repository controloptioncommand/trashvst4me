#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// Phase-vocoder pitch shifter with independent formant control.
//
// Each frame: analyse with a Hann window, estimate the true frequency of every bin,
// separate the spectral envelope (formants) from the fine structure (harmonics) with
// cepstral liftering, then rebuild the spectrum with
//   fine structure read at  k / pitchRatio   (moves the harmonics)
//   envelope read at        k / formantRatio (moves the formants)
// An optional "shadow" voice an octave below the main shift is synthesised from the
// same analysis and summed into the output.
class SpectralShifter
{
public:
    void prepare (double sampleRate)
    {
        order = sampleRate > 100000.0 ? 13 : (sampleRate > 50000.0 ? 12 : 11);
        N = 1 << order;
        half = N / 2;
        hop = N / 4;
        expct = juce::MathConstants<float>::twoPi * (float) hop / (float) N;

        fft = std::make_unique<juce::dsp::FFT> (order);

        window.resize ((size_t) N);
        for (int n = 0; n < N; ++n)
            window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N);

        // Cepstral lifter: follows the detected pitch period (0.6 * period) so the envelope
        // resolves individual formants without picking up the harmonics.
        minPeriod = (int) (sampleRate / 1000.0);
        maxPeriod = juce::jmin (N / 2 - 1, (int) (sampleRate / 60.0));
        minLifter = (int) (sampleRate / 1500.0);
        maxLifter = (int) (sampleRate / 220.0);
        defaultLifter = (int) (sampleRate / 500.0);

        inFifo.assign ((size_t) N, 0.0f);
        outFifo.assign ((size_t) N, 0.0f);
        outAccum.assign ((size_t) (2 * N), 0.0f);
        fftBuf.assign ((size_t) (2 * N), 0.0f);
        cepBuf.assign ((size_t) (2 * N), 0.0f);
        synBuf.assign ((size_t) (2 * N), 0.0f);

        const auto bins = (size_t) (half + 1);
        logMag.assign (bins, 0.0f);
        logEnv.assign (bins, 0.0f);
        work.assign (bins, 0.0f);
        trueBin.assign (bins, 0.0f);
        anaPhase.assign (bins, 0.0f);
        lastPhase.assign (bins, 0.0f);
        peaks.reserve (bins);
        regionHi.reserve (bins);
        for (auto* v : { &mainVoice, &shadowVoice })
        {
            v->peakBins.reserve (bins); v->nextBins.reserve (bins);
            v->peakPhases.reserve (bins); v->nextPhases.reserve (bins);
        }

        reset();
    }

    void reset()
    {
        std::fill (inFifo.begin(), inFifo.end(), 0.0f);
        std::fill (outFifo.begin(), outFifo.end(), 0.0f);
        std::fill (outAccum.begin(), outAccum.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
        mainVoice.clear();
        shadowVoice.clear();
        rover = N - hop;
        lifterSmoothed = (float) defaultLifter;
    }

    // input sample t comes out at t + N
    int getLatency() const noexcept { return N; }

    void setParameters (float pitchSemitones, float formantSemitones, float shadowAmount) noexcept
    {
        pitchRatio   = std::pow (2.0f, pitchSemitones / 12.0f);
        formantRatio = std::pow (2.0f, formantSemitones / 12.0f);
        shadow       = shadowAmount;
    }

    void process (float* data, int numSamples) noexcept
    {
        const int latency = N - hop; // FIFO fill level

        for (int i = 0; i < numSamples; ++i)
        {
            inFifo[(size_t) rover] = data[i];
            data[i] = outFifo[(size_t) (rover - latency)];

            if (++rover >= N)
            {
                rover = latency;
                processFrame();

                for (int k = 0; k < hop; ++k)
                    outFifo[(size_t) k] = outAccum[(size_t) k];

                std::memmove (outAccum.data(), outAccum.data() + hop, sizeof (float) * (size_t) N);
                std::fill (outAccum.begin() + N, outAccum.end(), 0.0f);

                std::memmove (inFifo.data(), inFifo.data() + hop, sizeof (float) * (size_t) latency);
            }
        }
    }

private:
    static float wrapPhase (float p) noexcept
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        p = std::fmod (p + juce::MathConstants<float>::pi, twoPi);
        if (p < 0.0f) p += twoPi;
        return p - juce::MathConstants<float>::pi;
    }

    float interp (const std::vector<float>& v, float pos) const noexcept
    {
        const int i0 = (int) pos;
        const float frac = pos - (float) i0;
        const int i1 = juce::jmin (i0 + 1, half);
        return v[(size_t) i0] + frac * (v[(size_t) i1] - v[(size_t) i0]);
    }

    void processFrame() noexcept
    {
        // ---- analysis -------------------------------------------------------------
        for (int n = 0; n < N; ++n)
            fftBuf[(size_t) n] = inFifo[(size_t) n] * window[(size_t) n];
        std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);

        fft->performRealOnlyForwardTransform (fftBuf.data(), true);

        for (int k = 0; k <= half; ++k)
        {
            const float re = fftBuf[(size_t) (2 * k)];
            const float im = fftBuf[(size_t) (2 * k + 1)];
            const float mag = std::sqrt (re * re + im * im);
            const float ph = std::atan2 (im, re);

            float delta = ph - lastPhase[(size_t) k];
            lastPhase[(size_t) k] = ph;
            delta = wrapPhase (delta - (float) k * expct);

            trueBin[(size_t) k] = (float) k + delta / expct;
            anaPhase[(size_t) k] = ph;
            logMag[(size_t) k] = std::log (mag + 1.0e-9f);
        }

        // ---- spectral envelope: "true envelope" (iterated cepstral smoothing) ------------
        // Plain cepstral smoothing follows the mean of the log spectrum, so harmonics that sit
        // far above their valleys get exaggerated. Iterating  A = max (A, smooth (A))  pushes the
        // envelope up until it rests on the harmonic peaks.
        for (int k = 0; k <= half; ++k)
            work[(size_t) k] = logMag[(size_t) k];

        loadCepstrum (work);

        // pitch period from the cepstral peak (voiced frames only) sets the lifter
        {
            int bestQ = 0;
            float best = 0.0f;
            for (int q = minPeriod; q <= maxPeriod; ++q)
                if (cepBuf[(size_t) q] > best) { best = cepBuf[(size_t) q]; bestQ = q; }

            const float target = best > 0.06f ? juce::jlimit ((float) minLifter, (float) maxLifter, 0.5f * (float) bestQ)
                                              : (float) defaultLifter;
            lifterSmoothed += 0.35f * (target - lifterSmoothed);
        }
        const int lifter = juce::roundToInt (lifterSmoothed);

        for (int it = 0; it < envelopeIterations; ++it)
        {
            if (it > 0)
                loadCepstrum (work);
            lifterAndTransform (lifter);

            for (int k = 0; k <= half; ++k)
            {
                const float e = cepBuf[(size_t) (2 * k)];
                logEnv[(size_t) k] = e;
                work[(size_t) k] = juce::jmax (work[(size_t) k], e);
            }
        }

        // ---- peak picking for identity phase locking ------------------------------------
        peaks.clear();
        for (int k = 2; k <= half - 2; ++k)
        {
            const float m = logMag[(size_t) k];
            if (m > logMag[(size_t) k - 1] && m >= logMag[(size_t) k + 1]
                && m > logMag[(size_t) k - 2] && m >= logMag[(size_t) k + 2])
                peaks.push_back (k);
        }
        if (peaks.empty())
            peaks.push_back (half / 2);

        // each peak owns the bins up to the deepest valley before the next peak
        regionHi.resize (peaks.size());
        for (size_t pi = 0; pi < peaks.size(); ++pi)
        {
            int boundary = half;
            if (pi + 1 < peaks.size())
            {
                boundary = peaks[pi];
                for (int j = peaks[pi]; j <= peaks[pi + 1]; ++j)
                    if (logMag[(size_t) j] < logMag[(size_t) boundary]) boundary = j;
            }
            regionHi[pi] = boundary;
        }

        // ---- synthesis --------------------------------------------------------------
        const bool identity = std::abs (pitchRatio - 1.0f) < 1.0e-4f;
        const float mainGain = 1.0f / std::sqrt (1.0f + shadow * shadow); // keep loudness with the shadow voice
        std::fill (synBuf.begin(), synBuf.end(), 0.0f);

        if (identity)
        {
            mainVoice.clear();
            for (int k = 0; k <= half; ++k)
            {
                const float fine = juce::jmin (logMag[(size_t) k] - logEnv[(size_t) k], 6.0f);
                const float mag = mainGain * std::exp (fine + formantEnvelope (k));
                const float ph = anaPhase[(size_t) k];
                synBuf[(size_t) (2 * k)]     = mag * std::cos (ph);
                synBuf[(size_t) (2 * k + 1)] = mag * std::sin (ph);
            }
        }
        else
        {
            synthesiseVoice (pitchRatio, mainGain, mainVoice);
        }

        if (shadow > 0.001f)
            synthesiseVoice (pitchRatio * 0.5f, shadow * mainGain, shadowVoice);
        else
            shadowVoice.clear();

        synBuf[1] = 0.0f;                       // DC and Nyquist are purely real
        synBuf[(size_t) (2 * half + 1)] = 0.0f;

        fft->performRealOnlyInverseTransform (synBuf.data());

        // Hann analysis * Hann synthesis at 75 % overlap sums to 1.5
        constexpr float olaGain = 1.0f / 1.5f;
        for (int n = 0; n < N; ++n)
            outAccum[(size_t) n] += synBuf[(size_t) n] * window[(size_t) n] * olaGain;
    }

    void loadCepstrum (const std::vector<float>& logSpectrum) noexcept
    {
        for (int k = 0; k <= half; ++k)
        {
            cepBuf[(size_t) (2 * k)] = logSpectrum[(size_t) k];
            cepBuf[(size_t) (2 * k + 1)] = 0.0f;
        }
        std::fill (cepBuf.begin() + 2 * (half + 1), cepBuf.end(), 0.0f);
        fft->performRealOnlyInverseTransform (cepBuf.data()); // real cepstrum in [0, N)
    }

    // keep quefrencies below `lifter` (cosine taper), back to a smoothed log spectrum in cepBuf[2k]
    void lifterAndTransform (int lifter) noexcept
    {
        const int taper = juce::jmax (1, lifter / 4);
        for (int n = 1; n < half; ++n)
        {
            float g = 0.0f;
            if (n < lifter - taper)  g = 1.0f;
            else if (n < lifter)     g = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (n - (lifter - taper)) / (float) taper);
            cepBuf[(size_t) n] *= g;
            cepBuf[(size_t) (N - n)] *= g;
        }
        cepBuf[(size_t) half] = 0.0f;
        std::fill (cepBuf.begin() + N, cepBuf.end(), 0.0f);
        fft->performRealOnlyForwardTransform (cepBuf.data(), true);
    }

    struct VoiceState
    {
        std::vector<int> peakBins, nextBins;      // output bins of last frame's peaks (sorted)
        std::vector<float> peakPhases, nextPhases;
        void clear() { peakBins.clear(); peakPhases.clear(); }
    };

    // Peak-region shifting (Laroche & Dolson): every peak region is moved as a block by an
    // integer number of bins, its phase advances at the exact target frequency, and the bins
    // around the peak keep their phase relation to it. The fine structure is re-imposed on the
    // (possibly formant-shifted) envelope at the destination.
    void synthesiseVoice (float ratio, float gain, VoiceState& v) noexcept
    {
        v.nextBins.clear();
        v.nextPhases.clear();

        int lo = 0;
        for (size_t pi = 0; pi < peaks.size(); lo = regionHi[pi] + 1, ++pi)
        {
            const int p = peaks[pi];
            const float target = trueBin[(size_t) p] * ratio;
            const int shift = juce::roundToInt (target - (float) p);
            const int kOut = p + shift;
            if (kOut > half) break;
            if (kOut < 1) continue;

            // continue the phase of the nearest peak from the previous frame
            float phPeak = anaPhase[(size_t) p];
            {
                auto it = std::lower_bound (v.peakBins.begin(), v.peakBins.end(), kOut);
                int bestIdx = -1, bestDist = 3;
                if (it != v.peakBins.end())
                    if (const int d = *it - kOut; d < bestDist) { bestDist = d; bestIdx = (int) (it - v.peakBins.begin()); }
                if (it != v.peakBins.begin())
                    if (const int d = kOut - *(it - 1); d < bestDist) { bestDist = d; bestIdx = (int) (it - v.peakBins.begin()) - 1; }
                if (bestIdx >= 0)
                    phPeak = v.peakPhases[(size_t) bestIdx] + target * expct;
            }
            phPeak = wrapPhase (phPeak);
            v.nextBins.push_back (kOut);
            v.nextPhases.push_back (phPeak);

            for (int k = lo; k <= regionHi[pi]; ++k)
            {
                const int o = k + shift;
                if (o < 0 || o > half) continue;

                const float ph = phPeak + anaPhase[(size_t) k] - anaPhase[(size_t) p];
                const float fine = juce::jmin (logMag[(size_t) k] - logEnv[(size_t) k], 6.0f);
                const float mag = gain * std::exp (fine + formantEnvelope (o));

                synBuf[(size_t) (2 * o)]     += mag * std::cos (ph);
                synBuf[(size_t) (2 * o + 1)] += mag * std::sin (ph);
            }
        }

        std::swap (v.peakBins, v.nextBins);
        std::swap (v.peakPhases, v.nextPhases);
    }

    float formantEnvelope (int k) const noexcept
    {
        const float pos = (float) k / formantRatio;
        if (pos >= (float) half)
            return logEnv[(size_t) half] - 6.0f; // fade out what would come from above Nyquist
        return interp (logEnv, pos);
    }

    int order = 11, N = 2048, half = 1024, hop = 512, rover = 0;
    int minPeriod = 48, maxPeriod = 800, minLifter = 32, maxLifter = 218, defaultLifter = 96;
    float lifterSmoothed = 96.0f;
public:
    int envelopeIterations = 3; // 3 matches 8 within 0.2 dB in the offline test
private:
    float expct = 0.0f;
    float pitchRatio = 1.0f, formantRatio = 1.0f, shadow = 0.0f;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, inFifo, outFifo, outAccum, fftBuf, cepBuf, synBuf;
    std::vector<float> work, logMag, logEnv, trueBin, anaPhase, lastPhase;
    std::vector<int> peaks, regionHi;
    VoiceState mainVoice, shadowVoice;
};
