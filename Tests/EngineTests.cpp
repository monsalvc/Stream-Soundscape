// Headless checks for the DSP and the engine. Run: ./StreamRackTests (exit code 0 = pass).

#include "Engine/Dsp.h"
#include "Engine/LoudnessMeter.h"
#include "Engine/Leveler.h"
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
    testEngine();
    testBusMode();
    testNetworkDevices();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
