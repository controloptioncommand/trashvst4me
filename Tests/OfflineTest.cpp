// Offline checks for the Burial Voice DSP: renders a synthetic sung vowel through the
// shifter / full plugin and measures pitch, formant position, level and stability.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <complex>

static constexpr double fs = 48000.0;

// Glottal pulse train through three formant resonators (vowel "ah").
static std::vector<float> makeVowel (float f0, int numSamples)
{
    struct Res { double b0, a1, a2, z1 = 0, z2 = 0; };
    auto res = [] (double f, double bw)
    {
        const double r = std::exp (-juce::MathConstants<double>::pi * bw / fs);
        const double th = juce::MathConstants<double>::twoPi * f / fs;
        return Res { 1.0 - r, 2.0 * r * std::cos (th), -r * r };
    };
    Res rs[] = { res (700, 110), res (1220, 120), res (2600, 160) };
    const double gains[] = { 1.0, 0.6, 0.25 };

    std::vector<float> out ((size_t) numSamples);
    double phase = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double vib = 1.0 + 0.004 * std::sin (juce::MathConstants<double>::twoPi * 5.0 * i / fs);
        phase += f0 * vib / fs;
        double src = 0.0;
        if (phase >= 1.0) { phase -= 1.0; src = 1.0; }
        double y = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            auto& r = rs[k];
            const double v = r.b0 * src + r.a1 * r.z1 + r.a2 * r.z2;
            r.z2 = r.z1; r.z1 = v;
            y += gains[k] * v;
        }
        out[(size_t) i] = (float) y;
    }
    float peak = 0.0f;
    for (auto v : out) peak = std::max (peak, std::abs (v));
    for (auto& v : out) v *= 0.5f / peak;
    return out;
}

static double rms (const float* x, int n)
{
    double s = 0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / n);
}

// f0 via normalised autocorrelation in 70..1000 Hz
static double estimateF0 (const float* x, int n)
{
    const int minLag = (int) (fs / 1000.0), maxLag = (int) (fs / 70.0);
    double best = -1, bestLag = 0;
    std::vector<double> ac ((size_t) maxLag + 2);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { s += x[i] * x[i + lag]; e1 += x[i] * x[i]; e2 += x[i + lag] * x[i + lag]; }
        ac[(size_t) lag] = s / std::sqrt (e1 * e2 + 1e-12);
    }
    double top = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) top = std::max (top, ac[(size_t) lag]);
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (ac[(size_t) lag] >= 0.9 * top && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1])
        { best = ac[(size_t) lag]; bestLag = lag; break; }
    // parabolic refinement
    const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
    const double d = 0.5 * (a - c) / (a - 2 * b + c);
    return fs / (bestLag + d);
}

// spectral centroid between 300 Hz and 4 kHz (tracks formant position)
static double centroid (const float* x, int n)
{
    juce::dsp::FFT fft (14);
    std::vector<float> buf (2 << 14, 0.0f);
    for (int i = 0; i < std::min (n, 1 << 14); ++i)
        buf[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (1 << 14)));
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    double num = 0, den = 0;
    for (int k = 0; k <= (1 << 13); ++k)
    {
        const double f = k * fs / (1 << 14);
        if (f < 300 || f > 4000) continue;
        num += f * buf[(size_t) k] * buf[(size_t) k];
        den += buf[(size_t) k] * buf[(size_t) k];
    }
    return num / den;
}


// magnitude response of the synthetic vowel filter at frequency f
static double vowelResponse (double f)
{
    const double fr[] = { 700, 1220, 2600 }, bw[] = { 110, 120, 160 }, gains[] = { 1.0, 0.6, 0.25 };
    std::complex<double> sum = 0;
    const auto z1 = std::polar (1.0, -juce::MathConstants<double>::twoPi * f / fs);
    for (int k = 0; k < 3; ++k)
    {
        const double r = std::exp (-juce::MathConstants<double>::pi * bw[k] / fs);
        const double th = juce::MathConstants<double>::twoPi * fr[k] / fs;
        const double b0 = 1.0 - r, a1 = 2.0 * r * std::cos (th), a2 = -r * r;
        sum += gains[k] * b0 / (1.0 - a1 * z1 - a2 * z1 * z1);
    }
    return std::abs (sum);
}

// RMS dB error between measured harmonic amplitudes (f0 .. 4 kHz) and the vowel response
// read at f / envScale, after removing the best constant offset.
static double envelopeError (const float* x, int n, double f0, double envScale)
{
    constexpr int order = 15, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf (2 * size, 0.0f);
    for (int i = 0; i < std::min (n, size); ++i)
        buf[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / size));
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);

    // source spectrum is not flat: remove the best straight line (dB vs Hz), not just an offset
    std::vector<double> diff, freq;
    for (int h = 1; h * f0 < 4000.0; ++h)
    {
        const double f = h * f0;
        const int c = (int) std::round (f * size / fs);
        float m = 0; for (int k = c - 3; k <= c + 3; ++k) m = std::max (m, buf[(size_t) k]);
        diff.push_back (20 * std::log10 (m + 1e-12) - 20 * std::log10 (vowelResponse (f / envScale)));
        freq.push_back (f);
    }
    const double cnt = (double) diff.size();
    double sf = 0, sd = 0, sff = 0, sfd = 0;
    for (size_t i = 0; i < diff.size(); ++i) { sf += freq[i]; sd += diff[i]; sff += freq[i] * freq[i]; sfd += freq[i] * diff[i]; }
    const double slope = (cnt * sfd - sf * sd) / (cnt * sff - sf * sf);
    const double icpt = (sd - slope * sf) / cnt;
    for (size_t i = 0; i < diff.size(); ++i) diff[i] -= icpt + slope * freq[i];
    if (std::getenv ("BV_DEBUG")) { for (auto d : diff) std::printf ("%5.1f ", d); std::printf ("\n"); }
    double e = 0; for (auto d : diff) e += d * d;
    return std::sqrt (e / (double) diff.size());
}

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++failures;
}

static std::vector<float> runShifter (const std::vector<float>& in, float pitch, float formant, float shadow)
{
    SpectralShifter s;
    s.prepare (fs);
    s.setParameters (pitch, formant, shadow);
    if (auto* it = std::getenv ("BV_ITERS")) s.envelopeIterations = std::atoi (it);
    auto out = in;
    for (size_t i = 0; i < out.size(); i += 512)
        s.process (out.data() + i, (int) std::min<size_t> (512, out.size() - i));
    return out;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), fs,
                                                                      (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();

    const int n = (int) fs * 3;
    const auto vowel = makeVowel (150.0f, n);
    const int skip = (int) fs; // let everything settle
    const int len = n - skip;

    const double inF0 = estimateF0 (vowel.data() + skip, 8192);
    const double inC = centroid (vowel.data() + skip, len);
    std::printf ("input: f0 %.1f Hz, centroid %.0f Hz, rms %.3f\n", inF0, inC, rms (vowel.data() + skip, len));

    {
        std::printf ("shifter identity (0 st / 0 st):\n");
        auto out = runShifter (vowel, 0, 0, 0);
        SpectralShifter tmp; tmp.prepare (fs);
        const int lat = tmp.getLatency();
        double err = 0, ref = 0;
        for (int i = skip; i < n; ++i) { const double e = out[(size_t) i] - vowel[(size_t) (i - lat)]; err += e * e; ref += (double) vowel[(size_t) (i - lat)] * vowel[(size_t) (i - lat)]; }
        const double snr = 10 * std::log10 (ref / err);
        std::printf ("  latency %d samples, reconstruction SNR %.1f dB\n", lat, snr);
        check (snr > 40.0, "transparent when pitch and formant are 0");
    }

    std::printf ("input envelope error vs vowel filter: %.1f dB\n", envelopeError (vowel.data() + skip, len, inF0, 1.0));

    for (float st : { 3.0f, 5.0f, 7.0f, -5.0f, 12.0f, -12.0f })
    {
        auto out = runShifter (vowel, st, 0, 0);
        const double r = std::pow (2.0, st / 12.0);
        const double f0 = estimateF0 (out.data() + skip, 8192);
        const double expected = inF0 * r;
        const double keep = envelopeError (out.data() + skip, len, f0, 1.0);
        const double follow = envelopeError (out.data() + skip, len, f0, r);
        const double lvl = 20 * std::log10 (rms (out.data() + skip, len) / rms (vowel.data() + skip, len));
        std::printf ("pitch %+3.0f st, formant 0: f0 %.1f (expected %.1f), env err %.1f dB (if formants had moved: %.1f), level %+.1f dB\n",
                     st, f0, expected, keep, follow, lvl);
        check (std::abs (f0 / expected - 1.0) < 0.02, "pitch lands within 2 %");
        check (keep < 5.5 && keep < 0.7 * follow, "formants stay put (error well below the moved-formant case)");
        check (std::abs (lvl) < 8.0, "level within 8 dB");
    }

    for (float fm : { 5.0f, -5.0f, 9.0f })
    {
        auto out = runShifter (vowel, 0, fm, 0);
        const double fr = std::pow (2.0, fm / 12.0);
        const double f0 = estimateF0 (out.data() + skip, 8192);
        const double moved = envelopeError (out.data() + skip, len, f0, fr);
        const double stayed = envelopeError (out.data() + skip, len, f0, 1.0);
        std::printf ("pitch 0, formant %+.0f st: f0 %.1f, env err vs shifted formants %.1f dB (vs original %.1f)\n", fm, f0, moved, stayed);
        check (std::abs (f0 / inF0 - 1.0) < 0.02, "pitch unchanged");
        check (moved < 4.0 && moved < stayed, "formants moved by the right amount");
    }

    {
        auto out = runShifter (vowel, 5, 5, 0);
        const double r = std::pow (2.0, 5 / 12.0);
        const double f0 = estimateF0 (out.data() + skip, 8192);
        const double err = envelopeError (out.data() + skip, len, f0, r);
        std::printf ("pitch +5, formant +5 (chipmunk): f0 %.1f, env err vs moved formants %.1f dB\n", f0, err);
        check (std::abs (f0 / (inF0 * r) - 1.0) < 0.02 && err < 4.0, "behaves like a plain resample");
    }

    // ---- full plugin, every preset -------------------------------------------------------
    BurialVoiceProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, 512);
    for (int p = 0; p < proc.getNumPrograms(); ++p)
    {
        proc.setCurrentProgram (p);
        proc.prepareToPlay (fs, 512);

        juce::AudioBuffer<float> all (2, n);
        for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, 0, vowel.data(), n);

        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < n; i += 512)
        {
            const int m = std::min (512, n - i);
            juce::AudioBuffer<float> view (all.getArrayOfWritePointers(), 2, i, m);
            proc.processBlock (view, midi);
        }

        const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / (1000.0 * n / fs) * 100.0;
        bool finite = true; float peak = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) { const float v = all.getSample (ch, i); finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        const double lvl = 20 * std::log10 (rms (all.getReadPointer (0) + skip, len) / rms (vowel.data() + skip, len));
        std::printf ("preset %-13s latency %d, peak %.2f, level %+.1f dB, cpu %.1f %% of one core (stereo)\n", proc.getProgramName (p).toRawUTF8(), proc.getLatencySamples(), peak, lvl, cpu);
        check (finite && peak < 2.0f, "finite and not clipping hard");

        writeWav (outDir.getChildFile ("preset_" + juce::String (p) + ".wav"), all);
    }

    juce::AudioBuffer<float> dry (1, n);
    dry.copyFrom (0, 0, vowel.data(), n);
    writeWav (outDir.getChildFile ("input.wav"), dry);

    // ---- editor snapshot -------------------------------------------------------------------
    {
        proc.setCurrentProgram (0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile ("ui.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL OK" : "SOME CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
