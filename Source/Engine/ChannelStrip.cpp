#include "ChannelStrip.h"

namespace sr
{
void ChannelStrip::bind (juce::AudioProcessorValueTreeState& state, int stripIndex)
{
    index = stripIndex;
    auto get = [&] (const char* name)
    {
        auto* v = state.getRawParameterValue (stripId (index, name));
        jassert (v != nullptr);
        return v;
    };

    p = { get ("src"),     get ("stereo"),   get ("trim"),     get ("hpfOn"),   get ("hpfFreq"),
          get ("gateOn"),  get ("gateThr"),  get ("eqOn"),     get ("lowGain"), get ("lowFreq"),
          get ("m1Gain"),  get ("m1Freq"),   get ("m1Q"),      get ("m2Gain"),  get ("m2Freq"),
          get ("m2Q"),     get ("highGain"), get ("highFreq"), get ("compOn"),  get ("compThr"),
          get ("compRatio"), get ("compAtk"), get ("compRel"), get ("compMakeup"), get ("send"),
          get ("pan"),     get ("fader"),    get ("mute") };

    for (int i = 0; i < slotsPerStrip; ++i)
        slots[(size_t) i] = std::make_unique<PluginSlot> (stripId (index, "slot") + juce::String (i));
}

void ChannelStrip::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = maxBlockSize;
    work.setSize (2, maxBlock);

    for (auto& stage : hpf)
        for (auto& b : stage)
            b.reset();
    for (auto& band : eq)
        for (auto& b : band)
            b.reset();

    filterKey.fill (-1.0f);
    updateFilters();
    gate.prepare (fs);
    comp.prepare (fs);
    delay.prepare();

    for (auto* s : { &gainL, &gainR, &sendGain })
        s->reset (fs, 0.02);

    for (auto& s : slots)
        s->prepare (fs, maxBlock);
}

void ChannelStrip::updateFilters() noexcept
{
    const std::array<float, 11> key { p.hpfFreq->load(), p.lowGain->load(),  p.lowFreq->load(), p.m1Gain->load(),
                                      p.m1Freq->load(),  p.m1Q->load(),      p.m2Gain->load(),  p.m2Freq->load(),
                                      p.m2Q->load(),     p.highGain->load(), p.highFreq->load() };
    if (key == filterKey)
        return;
    filterKey = key;

    // Two cascaded sections make a 4th-order (24 dB/oct) Butterworth high-pass.
    const auto hp1 = rbj::highPass (fs, p.hpfFreq->load(), 0.5412);
    const auto hp2 = rbj::highPass (fs, p.hpfFreq->load(), 1.3066);
    const auto low = rbj::lowShelf (fs, p.lowFreq->load(), p.lowGain->load());
    const auto m1 = rbj::peak (fs, p.m1Freq->load(), p.m1Q->load(), p.m1Gain->load());
    const auto m2 = rbj::peak (fs, p.m2Freq->load(), p.m2Q->load(), p.m2Gain->load());
    const auto high = rbj::highShelf (fs, p.highFreq->load(), p.highGain->load());

    for (int ch = 0; ch < 2; ++ch)
    {
        hpf[0][(size_t) ch].c = hp1;
        hpf[1][(size_t) ch].c = hp2;
        eq[0][(size_t) ch].c = low;
        eq[1][(size_t) ch].c = m1;
        eq[2][(size_t) ch].c = m2;
        eq[3][(size_t) ch].c = high;
    }
}

int ChannelStrip::getLatencySamples() const noexcept
{
    int total = 0;
    for (auto& s : slots)
        total += s->getLatencySamples();
    return total;
}

void ChannelStrip::process (const float* const* inputs, int numInputs, int n, int compensationTarget,
                            float* mixL, float* mixR, float* sendL, float* sendR) noexcept
{
    jassert (n <= maxBlock);

    const int src = (int) p.src->load() - 1; // choice 0 means "no input"
    const bool stereo = p.stereo->load() > 0.5f;

    if (src < 0 || src >= numInputs)
    {
        compReductionDb = 0.0f;
        return;
    }

    auto* L = work.getWritePointer (0);
    auto* R = work.getWritePointer (1);
    juce::FloatVectorOperations::copy (L, inputs[src], n);
    juce::FloatVectorOperations::copy (R, (stereo && src + 1 < numInputs) ? inputs[src + 1] : inputs[src], n);

    const float trim = juce::Decibels::decibelsToGain (p.trim->load());
    juce::FloatVectorOperations::multiply (L, trim, n);
    juce::FloatVectorOperations::multiply (R, trim, n);

    updateFilters();

    if (p.hpfOn->load() > 0.5f)
        for (auto& stage : hpf)
        {
            stage[0].processBlock (L, n);
            stage[1].processBlock (R, n);
        }

    if (p.gateOn->load() > 0.5f)
    {
        gate.process (L, R, n, p.gateThr->load());
        gateOpen = gate.isOpen();
    }
    else
    {
        gateOpen = true;
    }

    if (p.eqOn->load() > 0.5f)
        for (auto& band : eq)
        {
            band[0].processBlock (L, n);
            band[1].processBlock (R, n);
        }

    if (p.compOn->load() > 0.5f)
    {
        comp.process (L, R, n, p.compThr->load(), p.compRatio->load(), p.compAtk->load(), p.compRel->load(),
                      p.compMakeup->load());
        compReductionDb = comp.getReductionDb();
    }
    else
    {
        compReductionDb = 0.0f;
    }

    for (auto& s : slots)
        s->process (L, R, n);

    delay.process (L, R, n, compensationTarget - getLatencySamples());

    const float blockPeak = juce::jmax (work.getMagnitude (0, 0, n), work.getMagnitude (1, 0, n));
    if (blockPeak > peak.load())
        peak = blockPeak;

    // -3 dB pan law for mono sources, balance control for stereo sources.
    const float pan = p.pan->load();
    float panL, panR;
    if (stereo)
    {
        panL = juce::jmin (1.0f, 1.0f - pan);
        panR = juce::jmin (1.0f, 1.0f + pan);
    }
    else
    {
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        panL = std::cos (angle);
        panR = std::sin (angle);
    }

    const float fader = p.mute->load() > 0.5f ? 0.0f : dbToGainWithOff (p.fader->load());
    gainL.setTargetValue (fader * panL);
    gainR.setTargetValue (fader * panR);
    sendGain.setTargetValue (dbToGainWithOff (p.send->load()));

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i] * gainL.getNextValue();
        const float r = R[i] * gainR.getNextValue();
        const float s = sendGain.getNextValue();
        mixL[i] += l;
        mixR[i] += r;
        sendL[i] += l * s;
        sendR[i] += r * s;
    }
}
} // namespace sr
