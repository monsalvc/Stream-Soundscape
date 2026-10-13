// Headless checks for the DSP and the engine. Run: ./StreamRackTests (exit code 0 = pass).

#include "Engine/Dsp.h"
#include "Engine/LoudnessMeter.h"
#include "Engine/RackProcessor.h"
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

    // Channel 1 (input 1) at 0 dB fader, centre; everything else at -inf.
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
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testLoudness();
    testLimiter();
    testEq();
    testCompressor();
    testEngine();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
