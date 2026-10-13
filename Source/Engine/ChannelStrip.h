#pragma once

#include "Dsp.h"
#include "Params.h"
#include "PluginSlot.h"
#include <array>
#include <memory>

namespace sr
{
// One input channel: input select -> trim -> HPF -> gate -> 4-band EQ -> compressor
// -> 2 plugin inserts -> delay compensation -> pan/fader -> stream mix (+ reverb send).
// Everything runs in stereo; a mono source is duplicated to both sides.
class ChannelStrip
{
public:
    void bind (juce::AudioProcessorValueTreeState& state, int stripIndex);
    void prepare (double sampleRate, int maxBlockSize);

    // Audio thread. Adds this strip's output into the mix and send buses.
    void process (const float* const* inputs, int numInputs, int n, int compensationTarget,
                  float* mixL, float* mixR, float* sendL, float* sendR) noexcept;

    // Combined latency of this strip's inserts (audio thread).
    int getLatencySamples() const noexcept;

    PluginSlot& getSlot (int i) { return *slots[(size_t) i]; }
    int getIndex() const noexcept { return index; }

    // Metering, read by the UI.
    std::atomic<float> peak { 0.0f };         // pre-fader peak (linear), UI resets it after reading
    std::atomic<float> compReductionDb { 0.0f };
    std::atomic<bool> gateOpen { true };

private:
    struct Refs
    {
        std::atomic<float>*src, *stereo, *trim, *hpfOn, *hpfFreq, *gateOn, *gateThr, *eqOn, *lowGain, *lowFreq,
            *m1Gain, *m1Freq, *m1Q, *m2Gain, *m2Freq, *m2Q, *highGain, *highFreq, *compOn, *compThr, *compRatio,
            *compAtk, *compRel, *compMakeup, *send, *pan, *fader, *mute;
    };

    void updateFilters() noexcept;

    int index = 0;
    Refs p {};
    double fs = 48000.0;
    int maxBlock = 512;

    juce::AudioBuffer<float> work;
    std::array<std::array<Biquad, 2>, 2> hpf {}; // [stage][channel]
    std::array<std::array<Biquad, 2>, 4> eq {};  // [band][channel]
    std::array<float, 11> filterKey {};
    Gate gate;
    Compressor comp;
    CompensationDelay delay;
    std::array<std::unique_ptr<PluginSlot>, slotsPerStrip> slots;
    juce::SmoothedValue<float> gainL, gainR, sendGain;
};
} // namespace sr
