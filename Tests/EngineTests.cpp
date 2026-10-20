// Headless checks for the DSP and the engine. Run: ./StreamRackTests (exit code 0 = pass).

#include "Engine/Dsp.h"
#include "Engine/LoudnessMeter.h"
#include "Engine/Leveler.h"
#include "Engine/PitchCorrector.h"
#include "Engine/RackProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const char* what, double value)
{
    std::printf ("%s  %-58s %10.3f\n", ok ? "PASS" : "FAIL", what, value);
    if (! ok)
        ++failures;
}

constexpr double fs = 48000.0;

void testLoudness()
{
    // EBU Tech 3341 case 1: stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS (+/- 0.1).
    sr::LoudnessMeter meter;
    meter.prepare (fs);
    const float amp = juce::Decibels::decibelsToGain (-23.0f);
    std::vector<float> L (480), R (480);
    double phase = 0;
    for (int block = 0; block < 2000; ++block) // 20 s
    {
        for (size_t i = 0; i < L.size(); ++i)
        {
            L[i] = R[i] = amp * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 1000.0 / fs;
        }
        meter.process (L.data(), R.data(), (int) L.size());
    }
    check (std::abs (meter.momentary.load() + 23.0f) < 0.1f, "loudness: momentary of -23 dBFS 1k stereo sine", meter.momentary.load());
    check (std::abs (meter.shortTerm.load() + 23.0f) < 0.1f, "loudness: short-term", meter.shortTerm.load());
    check (std::abs (meter.integrated.load() + 23.0f) < 0.1f, "loudness: integrated", meter.integrated.load());

    // Relative gating: 10 s at -20 then 10 s at -40 -> the quiet part is gated out (~-20 + ~-0.0).
    sr::LoudnessMeter gated;
    gated.prepare (fs);
    for (int block = 0; block < 2000; ++block)
    {
        const float a = juce::Decibels::decibelsToGain (block < 1000 ? -20.0f : -40.0f);
        for (size_t i = 0; i < L.size(); ++i)
        {
            L[i] = R[i] = a * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 1000.0 / fs;
        }
        gated.process (L.data(), R.data(), (int) L.size());
    }
    check (std::abs (gated.integrated.load() + 20.0f) < 0.3f, "loudness: relative gate ignores quiet section", gated.integrated.load());
}

void testLimiter()
{
    sr::Limiter lim;
    lim.prepare (fs);
    juce::Random rng (42);
    float maxOut = 0;
    std::vector<float> L (256), R (256);
    for (int block = 0; block < 2000; ++block)
    {
        for (size_t i = 0; i < L.size(); ++i)
        {
            // Noise with occasional +12 dB spikes.
            const float spike = rng.nextInt (500) == 0 ? 4.0f : 1.0f;
            L[i] = spike * (rng.nextFloat() * 2 - 1);
            R[i] = spike * (rng.nextFloat() * 2 - 1);
        }
        lim.process (L.data(), R.data(), (int) L.size(), -1.0f, 80.0f);
        for (size_t i = 0; i < L.size(); ++i)
            maxOut = juce::jmax (maxOut, std::abs (L[i]), std::abs (R[i]));
    }
    const float ceiling = juce::Decibels::decibelsToGain (-1.0f);
    check (maxOut <= ceiling + 1e-6f, "limiter: output never exceeds -1 dBFS ceiling", juce::Decibels::gainToDecibels (maxOut));

    // Without the final safety clip the gain curve alone should already hold the ceiling,
    // so the reported reduction must be non-trivial on this hot signal.
    check (lim.getReductionDb() < -1.0f, "limiter: applies gain reduction on hot input", lim.getReductionDb());
}

double sineGainDb (const sr::BiquadCoeffs& c, double freq)
{
    sr::Biquad b { c };
    double peak = 0;
    for (int i = 0; i < 48000; ++i)
    {
        const float y = b.process ((float) std::sin (juce::MathConstants<double>::twoPi * freq * i / fs));
        if (i > 24000)
            peak = juce::jmax (peak, (double) std::abs (y));
    }
    return juce::Decibels::gainToDecibels (peak);
}

void testEq()
{
    check (std::abs (sineGainDb (sr::rbj::peak (fs, 1000, 1.0, 6.0), 1000) - 6.0) < 0.1, "eq: +6 dB peak at its centre", sineGainDb (sr::rbj::peak (fs, 1000, 1.0, 6.0), 1000));
    check (std::abs (sineGainDb (sr::rbj::lowShelf (fs, 100, -6.0), 30) + 6.0) < 0.5, "eq: -6 dB low shelf well below corner", sineGainDb (sr::rbj::lowShelf (fs, 100, -6.0), 30));
    check (std::abs (sineGainDb (sr::rbj::highShelf (fs, 8000, 4.0), 18000) - 4.0) < 0.5, "eq: +4 dB high shelf above corner", sineGainDb (sr::rbj::highShelf (fs, 8000, 4.0), 18000));
    check (sineGainDb (sr::rbj::highPass (fs, 100, 0.7071), 25) < -20.0, "eq: high-pass rejects 2 octaves below", sineGainDb (sr::rbj::highPass (fs, 100, 0.7071), 25));
}

void testCompressor()
{
    sr::Compressor comp;
    comp.prepare (fs);
    std::vector<float> L (480), R (480);
    double phase = 0;
    for (int block = 0; block < 200; ++block)
    {
        for (size_t i = 0; i < L.size(); ++i)
        {
            L[i] = R[i] = (float) std::sin (phase); // 0 dBFS peak
            phase += juce::MathConstants<double>::twoPi * 1000.0 / fs;
        }
        comp.process (L.data(), R.data(), (int) L.size(), -20.0f, 4.0f, 5.0f, 100.0f, 0.0f);
    }
    // 20 dB over a -20 threshold at 4:1 -> about -15 dB of reduction (peak detector).
    check (std::abs (comp.getReductionDb() + 15.0f) < 1.0f, "compressor: 4:1, 20 dB over -> ~15 dB reduction", comp.getReductionDb());
}

void setParam (sr::RackProcessor& rack, const juce::String& id, float value)
{
    auto* p = rack.state.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void testEngine()
{
    sr::PluginHost host (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("streamrack-test"));
    sr::RackProcessor rack (host);

    const int numIn = 64, block = 256;
    rack.setPlayConfigDetails (numIn, 2, fs, block);
    rack.prepareToPlay (fs, block);

    // Advanced mode, leveler off. Channel 1 (input 1) at 0 dB fader, centre; everything else at -inf.
    setParam (rack, sr::masterId ("busMode"), 0.0f);
    setParam (rack, sr::masterId ("levOn"), 0.0f);
    setParam (rack, sr::stripId (0, "fader"), 0.0f);
    setParam (rack, sr::stripId (0, "hpfOn"), 0.0f);
    setParam (rack, sr::stripId (0, "eqOn"), 0.0f);
    setParam (rack, sr::masterId ("limOn"), 0.0f);

    juce::AudioBuffer<float> buffer (numIn, block);
    juce::MidiBuffer midi;
    double phase = 0;
    float outPeak = 0;
    bool finite = true;
    for (int b = 0; b < 400; ++b)
    {
        buffer.clear();
        for (int i = 0; i < block; ++i)
        {
            buffer.setSample (0, i, 0.5f * (float) std::sin (phase));
            buffer.setSample (5, i, 0.9f); // input 6 feeds strip 6, whose fader is down
            phase += juce::MathConstants<double>::twoPi * 440.0 / fs;
        }
        rack.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
        if (b > 100)
            outPeak = juce::jmax (outPeak, buffer.getMagnitude (0, 0, block));
    }
    check (finite, "engine: output is finite", 0);
    // -3 dB pan law: 0.5 * 0.7071 = 0.354
    check (std::abs (outPeak - 0.3536f) < 0.01f, "engine: centred mono channel at unity, -3 dB pan law", outPeak);

    // Mute must remove it.
    setParam (rack, sr::stripId (0, "mute"), 1.0f);
    for (int b = 0; b < 50; ++b)
    {
        buffer.clear();
        for (int i = 0; i < block; ++i)
            buffer.setSample (0, i, 0.5f);
        rack.processBlock (buffer, midi);
    }
    check (buffer.getMagnitude (0, 0, block) < 1e-5f, "engine: mute silences the channel", buffer.getMagnitude (0, 0, block));

    // A tuned channel and an untuned one stay time-aligned (delay compensation): the same
    // click sent through both comes out as one click, not two.
    {
        sr::PluginHost host2 (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("streamrack-test"));
        sr::RackProcessor rack2 (host2);
        rack2.setPlayConfigDetails (numIn, 2, fs, block);
        rack2.prepareToPlay (fs, block);
        setParam (rack2, sr::masterId ("busMode"), 0.0f);
        setParam (rack2, sr::masterId ("levOn"), 0.0f);
        setParam (rack2, sr::masterId ("limOn"), 0.0f);
        for (int s = 0; s < 2; ++s)
        {
            setParam (rack2, sr::stripId (s, "src"), 1.0f); // both strips listen to input 1
            setParam (rack2, sr::stripId (s, "fader"), 0.0f);
            setParam (rack2, sr::stripId (s, "hpfOn"), 0.0f);
            setParam (rack2, sr::stripId (s, "eqOn"), 0.0f);
        }
        setParam (rack2, sr::stripId (0, "tuneOn"), 1.0f);

        juce::AudioBuffer<float> buf (numIn, block);
        std::vector<int> peaks;
        int sampleIndex = 0;
        for (int b = 0; b < 60; ++b)
        {
            buf.clear();
            if (b == 20)
                buf.setSample (0, 0, 1.0f);
            rack2.processBlock (buf, midi);
            for (int i = 0; i < block; ++i, ++sampleIndex)
                if (std::abs (buf.getSample (0, i)) > 0.2f)
                    peaks.push_back (sampleIndex);
        }
        const int spread = peaks.empty() ? 99999 : peaks.back() - peaks.front();
        check (! peaks.empty() && spread < 8, "engine: tuned + untuned channel stay aligned (sample spread)", spread);
    }

    // State round trip keeps parameters and names.
    rack.setStripName (2, "Pastor");
    setParam (rack, sr::stripId (2, "lowGain"), 3.5f);
    auto saved = rack.createStateTree();
    setParam (rack, sr::stripId (2, "lowGain"), 0.0f);
    rack.setStripName (2, "x");
    rack.restoreStateTree (saved);
    const float restored = rack.state.getRawParameterValue (sr::stripId (2, "lowGain"))->load();
    check (std::abs (restored - 3.5f) < 0.01f && rack.getStripName (2) == "Pastor", "engine: state save/restore", restored);
}
void runSine (sr::Leveler& lev, sr::LoudnessMeter* meter, float amplitudeDb, float seconds, double& phase,
              float target, float range)
{
    const float amp = juce::Decibels::decibelsToGain (amplitudeDb);
    std::vector<float> L (480), R (480);
    for (int block = 0; block < (int) (seconds * 100); ++block)
    {
        for (size_t i = 0; i < L.size(); ++i)
        {
            L[i] = R[i] = amp * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 1000.0 / fs;
        }
        lev.process (L.data(), R.data(), (int) L.size(), true, target, range, 2.0f);
        if (meter != nullptr)
            meter->process (L.data(), R.data(), (int) L.size());
    }
}

void testLeveler()
{
    // A -30 LUFS programme is brought up to a -16 LUFS target when the range allows it...
    sr::Leveler lev;
    lev.prepare (fs);
    sr::LoudnessMeter meter;
    meter.prepare (fs);
    double phase = 0;
    runSine (lev, nullptr, -30.0f, 30.0f, phase, -16.0f, 18.0f);
    runSine (lev, &meter, -30.0f, 5.0f, phase, -16.0f, 18.0f);
    check (std::abs (meter.shortTerm.load() + 16.0f) < 0.5f, "leveler: brings -30 LUFS up to -16 LUFS target", meter.shortTerm.load());

    // ...but never by more than its range.
    sr::Leveler limited;
    limited.prepare (fs);
    runSine (limited, nullptr, -30.0f, 30.0f, phase, -16.0f, 9.0f);
    check (std::abs (limited.getGainDb() - 9.0f) < 0.2f, "leveler: boost capped at +9 dB range", limited.getGainDb());

    // Silence (below the gate) holds the gain instead of pumping it up.
    const float before = lev.getGainDb();
    runSine (lev, nullptr, -100.0f, 10.0f, phase, -16.0f, 18.0f);
    check (std::abs (lev.getGainDb() - before) < 1.0f, "leveler: holds gain through silence", lev.getGainDb() - before);

    // When the same programme returns after the pause, it doesn't surge.
    float maxGain = -100.0f;
    for (int s = 0; s < 10; ++s)
    {
        runSine (lev, nullptr, -30.0f, 1.0f, phase, -16.0f, 18.0f);
        maxGain = juce::jmax (maxGain, lev.getGainDb());
    }
    check (maxGain < before + 1.5f, "leveler: no surge when music resumes after a pause", maxGain - before);

    // A loud section is turned down.
    sr::Leveler down;
    down.prepare (fs);
    runSine (down, nullptr, -6.0f, 30.0f, phase, -16.0f, 18.0f);
    check (std::abs (down.getGainDb() + 10.0f) < 0.5f, "leveler: turns -6 LUFS down by 10 dB", down.getGainDb());
}

void testBusMode()
{
    sr::PluginHost host (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("streamrack-test"));
    sr::RackProcessor rack (host);
    const int numIn = 8, block = 256;
    rack.setPlayConfigDetails (numIn, 2, fs, block);
    rack.prepareToPlay (fs, block);

    // Bus on inputs 3-4, leveler and limiter off: it should pass through untouched.
    setParam (rack, sr::masterId ("busSrc"), 1.0f);
    setParam (rack, sr::masterId ("levOn"), 0.0f);
    setParam (rack, sr::masterId ("limOn"), 0.0f);

    juce::AudioBuffer<float> buffer (numIn, block);
    juce::MidiBuffer midi;
    float maxErr = 0;
    for (int b = 0; b < 50; ++b)
    {
        buffer.clear();
        for (int i = 0; i < block; ++i)
        {
            buffer.setSample (0, i, 0.9f); // inputs 1-2 must be ignored
            buffer.setSample (2, i, 0.25f);
            buffer.setSample (3, i, -0.5f);
        }
        rack.processBlock (buffer, midi);
        if (b > 10)
            for (int i = 0; i < block; ++i)
                maxErr = juce::jmax (maxErr, std::abs (buffer.getSample (0, i) - 0.25f),
                                     std::abs (buffer.getSample (1, i) + 0.5f));
    }
    check (maxErr < 1e-4f, "bus mode: inputs 3-4 pass to outputs 1-2 unchanged", maxErr);

    // Switching to another bus (inputs 1-2) crossfades instead of jumping.
    setParam (rack, sr::masterId ("busSrc"), 0.0f);
    float maxStep = 0, last = buffer.getSample (0, block - 1), settled = 0;
    for (int b = 0; b < 30; ++b)
    {
        buffer.clear();
        for (int i = 0; i < block; ++i)
        {
            buffer.setSample (0, i, 0.9f);
            buffer.setSample (1, i, 0.9f);
            buffer.setSample (2, i, 0.25f);
            buffer.setSample (3, i, -0.5f);
        }
        rack.processBlock (buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            maxStep = juce::jmax (maxStep, std::abs (buffer.getSample (0, i) - last));
            last = buffer.getSample (0, i);
        }
        settled = buffer.getSample (0, block - 1);
    }
    check (maxStep < 0.01f, "bus mode: switching buses crossfades (largest sample step)", maxStep);
    check (std::abs (settled - 0.9f) < 1e-4f, "bus mode: switch ends on the new bus", settled);

    // Pair names survive loading a scene saved before they were set.
    const auto oldScene = rack.createStateTree();
    rack.setPairName (1, "Stream bus");
    rack.restoreStateTree (oldScene);
    check (rack.getPairName (1) == "Stream bus", "bus mode: pair names kept when an older scene loads", 0);
}
//==============================================================================
// A voice-like test tone: a fundamental plus decaying harmonics.
float voiceSample (double& phase, double freq)
{
    float v = 0;
    for (int h = 1; h <= 6; ++h)
        v += (0.5f / (float) h) * (float) std::sin (phase * h);
    phase += juce::MathConstants<double>::twoPi * freq / fs;
    return 0.3f * v;
}

// Sings a sustained note `centsOff` away from A3 (220 Hz) through the tuner and returns the
// pitch of the output (in cents relative to A3), measured with an independent detector.
float tunedOutputCents (float centsOff, float amount, double seconds = 3.0)
{
    sr::PitchCorrector tuner;
    tuner.prepare (fs, 256);
    sr::PitchDetector measure;
    measure.prepare (fs);

    const double freq = 220.0 * std::pow (2.0, centsOff / 1200.0);
    std::vector<float> L (256), R (256);
    double phase = 0, sum = 0;
    int count = 0;
    const int blocks = (int) (seconds * fs / 256);
    for (int b = 0; b < blocks; ++b)
    {
        for (size_t i = 0; i < L.size(); ++i)
            L[i] = R[i] = voiceSample (phase, freq);
        tuner.process (L.data(), R.data(), 256, amount, 20.0f);
        for (size_t i = 0; i < L.size(); ++i)
            if (measure.push (L[i]) && b > blocks / 2 && measure.getMidi() > 0)
            {
                sum += measure.getMidi();
                ++count;
            }
    }
    return count > 0 ? (float) ((sum / count - 57.0) * 100.0) : 9999.0f; // MIDI 57 = A3
}

void testPitchCorrection()
{
    // The detector itself: a 220 Hz voice tone reads as A3 (MIDI 57).
    {
        sr::PitchDetector d;
        d.prepare (fs);
        double phase = 0;
        float midi = -1;
        for (int i = 0; i < 48000; ++i)
            if (d.push (voiceSample (phase, 220.0)))
                midi = d.getMidi();
        check (std::abs (midi - 57.0f) < 0.05f, "tuner: detector reads 220 Hz voice tone as A3", midi);
    }

    const float sharp = tunedOutputCents (40.0f, 1.0f);
    check (std::abs (sharp) < 6.0f, "tuner: +40 cents sharp corrected to within 6 cents", sharp);

    const float flat = tunedOutputCents (-35.0f, 1.0f);
    check (std::abs (flat) < 6.0f, "tuner: -35 cents flat corrected to within 6 cents", flat);

    const float half = tunedOutputCents (40.0f, 0.5f);
    check (std::abs (half - 20.0f) < 6.0f, "tuner: 50% amount takes +40 to about +20 cents", half);

    const float inTune = tunedOutputCents (0.0f, 1.0f);
    check (std::abs (inTune) < 3.0f, "tuner: an in-tune note is left alone", inTune);

    // While it is actively correcting, the level must stay steady (no crossfade "wobble").
    {
        sr::PitchCorrector tuner;
        tuner.prepare (fs, 256);
        const double freq = 196.0 * std::pow (2.0, 45.0 / 1200.0); // G3, 45 cents sharp
        std::vector<float> L (480), R (480);
        double phase = 0;
        float lo = 1e9f, hi = 0;
        for (int b = 0; b < 400; ++b) // 4 s in 10 ms blocks
        {
            for (size_t i = 0; i < L.size(); ++i)
                L[i] = R[i] = voiceSample (phase, freq);
            tuner.process (L.data(), R.data(), 480, 1.0f, 20.0f);
            if (b > 100)
            {
                double e = 0;
                for (float v : L)
                    e += (double) v * v;
                const float rms = (float) std::sqrt (e / 480.0);
                lo = juce::jmin (lo, rms);
                hi = juce::jmax (hi, rms);
            }
        }
        const float wobbleDb = juce::Decibels::gainToDecibels (hi / lo);
        check (wobbleDb < 1.0f, "tuner: level stays steady while correcting (dB swing)", wobbleDb);
    }

    // A singer sliding between notes (with silence gaps) never produces garbage.
    {
        sr::PitchCorrector tuner;
        tuner.prepare (fs, 256);
        std::vector<float> L (256), R (256);
        double phase = 0;
        bool finite = true;
        float peak = 0;
        for (int b = 0; b < 1500; ++b)
        {
            const double t = b * 256.0 / fs;
            const double freq = 150.0 * std::pow (2.0, std::sin (t * 0.7) * 1.5); // slow slides over +/-1.5 octaves
            const bool gap = std::fmod (t, 2.0) > 1.7;
            for (size_t i = 0; i < L.size(); ++i)
                L[i] = R[i] = gap ? 0.0f : voiceSample (phase, freq);
            tuner.process (L.data(), R.data(), 256, 1.0f, 15.0f);
            for (float v : L)
            {
                finite = finite && std::isfinite (v);
                peak = juce::jmax (peak, std::abs (v));
            }
        }
        check (finite && peak < 1.0f, "tuner: sliding notes and gaps stay clean (peak)", peak);
    }

    // Its reported latency matches reality, so delay compensation lines channels up.
    {
        sr::PitchCorrector tuner;
        tuner.prepare (fs, 256);
        std::vector<float> L (256, 0.0f), R (256, 0.0f);
        int peakAt = -1, sample = 0;
        float peak = 0;
        for (int b = 0; b < 40; ++b)
        {
            std::fill (L.begin(), L.end(), 0.0f);
            std::fill (R.begin(), R.end(), 0.0f);
            if (b == 4)
                L[0] = R[0] = 1.0f;
            tuner.process (L.data(), R.data(), 256, 0.0f, 20.0f);
            for (int i = 0; i < 256; ++i, ++sample)
                if (std::abs (L[(size_t) i]) > peak)
                {
                    peak = std::abs (L[(size_t) i]);
                    peakAt = sample;
                }
        }
        const int measured = peakAt - 4 * 256;
        check (std::abs (measured - tuner.getLatencySamples()) < 48, "tuner: reported latency matches (samples)",
               (double) measured - tuner.getLatencySamples());
    }
}

//==============================================================================
// Network audio devices (Dante Virtual Soundcard, Waves SoundGrid Driver) reach the app as an
// ordinary multichannel audio device. This fake device lets the tests drive the real
// device -> AudioProcessorPlayer -> RackProcessor path with different channel counts,
// sample rates and buffer sizes, the way a driver would.
class FakeNetworkDevice : public juce::AudioIODevice
{
public:
    FakeNetworkDevice (int ins, int outs, double rate, int block)
        : AudioIODevice ("Fake network device", "Test"), numIns (ins), numOuts (outs), sampleRate (rate), blockSize (block)
    {
    }

    juce::StringArray getOutputChannelNames() override { return names (numOuts); }
    juce::StringArray getInputChannelNames() override { return names (numIns); }
    juce::Array<double> getAvailableSampleRates() override { return { sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { blockSize }; }
    int getDefaultBufferSize() override { return blockSize; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return bits (numOuts); }
    juce::BigInteger getActiveInputChannels() const override { return bits (numIns); }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    static juce::StringArray names (int n)
    {
        juce::StringArray a;
        for (int i = 0; i < n; ++i)
            a.add ("Ch " + juce::String (i + 1));
        return a;
    }

    static juce::BigInteger bits (int n)
    {
        juce::BigInteger b;
        b.setRange (0, n, true);
        return b;
    }

    int numIns, numOuts;
    double sampleRate;
    int blockSize;
};

struct DeviceCase
{
    const char* name;
    int ins, outs;
    double rate;
    int block;
};

void testNetworkDevices()
{
    const DeviceCase cases[] = {
        { "64x64 @ 48 kHz, 128 samples (typical Dante VSC)", 64, 64, 48000.0, 128 },
        { "64x64 @ 48 kHz, 32 samples (low-latency)", 64, 64, 48000.0, 32 },
        { "32x32 @ 96 kHz, 64 samples (SoundGrid at 96k)", 32, 32, 96000.0, 64 },
        { "16x16 @ 44.1 kHz, 512 samples", 16, 16, 44100.0, 512 },
        { "8x8 @ 48 kHz, 1024 samples (large buffer)", 8, 8, 48000.0, 1024 },
        { "4x2 @ 48 kHz, 480 samples (odd buffer, stereo out)", 4, 2, 48000.0, 480 },
    };

    for (const auto& c : cases)
    {
        sr::PluginHost host (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("streamrack-test"));
        sr::RackProcessor rack (host);
        juce::AudioProcessorPlayer player;
        player.setProcessor (&rack);

        // Bus mode, bus on Dante/SoundGrid inputs 3-4, leveler and limiter on (defaults).
        setParam (rack, sr::masterId ("busSrc"), 1.0f);

        FakeNetworkDevice device (c.ins, c.outs, c.rate, c.block);
        player.audioDeviceAboutToStart (&device);

        std::vector<std::vector<float>> in ((size_t) c.ins, std::vector<float> ((size_t) c.block)),
            out ((size_t) c.outs, std::vector<float> ((size_t) c.block));
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& v : in)
            inPtrs.push_back (v.data());
        for (auto& v : out)
            outPtrs.push_back (v.data());

        // 20 s of a stereo 1 kHz sine at -23 dBFS (= -23 LUFS) on inputs 3-4, noise on inputs 1-2.
        juce::Random rng (7);
        const float amp = juce::Decibels::decibelsToGain (-23.0f);
        double phase = 0;
        float maxOut = 0, pairDiff = 0;
        bool finite = true;
        const int blocks = (int) (20.0 * c.rate / c.block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < c.block; ++i)
            {
                const float v = amp * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 1000.0 / c.rate;
                in[0][(size_t) i] = in[1][(size_t) i] = 0.5f * (rng.nextFloat() - 0.5f);
                in[2][(size_t) i] = in[3][(size_t) i] = v;
            }

            juce::AudioIODeviceCallbackContext context;
            player.audioDeviceIOCallbackWithContext (inPtrs.data(), c.ins, outPtrs.data(), c.outs, c.block, context);

            for (int ch = 0; ch < c.outs; ++ch)
                for (int i = 0; i < c.block; ++i)
                {
                    finite = finite && std::isfinite (out[(size_t) ch][(size_t) i]);
                    maxOut = juce::jmax (maxOut, std::abs (out[(size_t) ch][(size_t) i]));
                    // Every output pair carries the same stream mix.
                    pairDiff = juce::jmax (pairDiff, std::abs (out[(size_t) ch][(size_t) i] - out[(size_t) (ch % 2)][(size_t) i]));
                }
        }
        player.audioDeviceStopped();
        player.setProcessor (nullptr);

        // The leveler should have taken the -23 LUFS bus up toward its -16 LUFS target (+7 dB, within range).
        const float shortTerm = rack.loudness.shortTerm.load();
        const bool ok = finite && maxOut <= juce::Decibels::decibelsToGain (-1.0f) + 1e-5f && pairDiff < 1e-6f
                        && std::abs (shortTerm + 16.0f) < 0.6f;
        check (ok, (juce::String ("device: ") + c.name + "  -> LUFS").toRawUTF8(), shortTerm);
    }

    // A bus pair the device doesn't have (e.g. 5-6 on a 4-input device) gives silence, not a crash.
    {
        sr::PluginHost host (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("streamrack-test"));
        sr::RackProcessor rack (host);
        juce::AudioProcessorPlayer player;
        player.setProcessor (&rack);
        setParam (rack, sr::masterId ("busSrc"), 2.0f);
        FakeNetworkDevice device (4, 2, 48000.0, 256);
        player.audioDeviceAboutToStart (&device);
        std::vector<float> a (256, 0.5f), l (256), r (256);
        const float* ins[] = { a.data(), a.data(), a.data(), a.data() };
        float* outs[] = { l.data(), r.data() };
        juce::AudioIODeviceCallbackContext context;
        float maxOut = 0;
        for (int b = 0; b < 100; ++b)
        {
            player.audioDeviceIOCallbackWithContext (ins, 4, outs, 2, 256, context);
            maxOut = juce::jmax (maxOut, juce::FloatVectorOperations::findMaximum (l.data(), 256));
        }
        player.audioDeviceStopped();
        player.setProcessor (nullptr);
        check (maxOut < 1e-6f, "device: bus pair beyond the device's inputs is silent", maxOut);
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testLoudness();
    testLimiter();
    testEq();
    testCompressor();
    testLeveler();
    testPitchCorrection();
    testEngine();
    testBusMode();
    testNetworkDevices();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
