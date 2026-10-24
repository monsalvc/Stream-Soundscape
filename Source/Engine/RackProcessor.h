#pragma once

#include "ChannelStrip.h"
#include "Leveler.h"
#include "LoudnessMeter.h"
#include "PluginHost.h"
#include "Recorder.h"

namespace sr
{
// The whole engine: up to 64 Dante inputs in, stereo stream mix out.
// Bus mode: one stereo input pair (the console's stream bus) -> master chain.
// Advanced mode: 24 channel strips + reverb bus -> master chain.
// Master chain: leveler -> bus compressor -> inserts -> fader -> limiter.
// It is an AudioProcessor so it can be driven by juce::AudioProcessorPlayer and keep
// its settings in an AudioProcessorValueTreeState.
class RackProcessor : public juce::AudioProcessor
{
public:
    explicit RackProcessor (PluginHost& host);
    ~RackProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "StreamRack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Full state: parameters, channel names and insert plugins with their settings.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int size) override;

    juce::ValueTree createStateTree();
    void restoreStateTree (const juce::ValueTree&);

    //==========================================================================
    juce::AudioProcessorValueTreeState state;

    ChannelStrip& getStrip (int i) { return strips[(size_t) i]; }
    PluginSlot& getMasterSlot (int i) { return *masterSlots[(size_t) i]; }

    // A reverb plugin loaded here replaces the built-in reverb on the send bus.
    // It should be set to 100 % wet.
    PluginSlot& getReverbSlot() { return *reverbSlot; }

    juce::String getStripName (int i) const;
    void setStripName (int i, const juce::String& name);

    // Optional names for the Dante input pairs (e.g. "Stream bus", "Youth bus"), so the bus
    // you want can be picked by name. Empty when the pair hasn't been named.
    juce::String getPairName (int pair) const;
    void setPairName (int pair, const juce::String& name);

    // Message thread. Loads a plugin into a slot (or clears it when desc is null).
    bool loadPlugin (PluginSlot&, const juce::PluginDescription* desc, juce::String& error);

    // Called on the message thread just before a slot's plugin is replaced, so the UI
    // can close that plugin's editor window.
    std::function<void (PluginSlot&)> onSlotAboutToChange;

    PluginHost& getHost() noexcept { return host; }

    //==========================================================================
    // Records the stream mix (and optionally the raw bus) to WAV files.
    Recorder recorder;

    //==========================================================================
    // Metering, read by the UI.
    LoudnessMeter loudness;
    std::atomic<float> masterPeakL { 0.0f }, masterPeakR { 0.0f }; // UI resets after reading
    std::atomic<float> limiterReductionDb { 0.0f }, busCompReductionDb { 0.0f }, levelerGainDb { 0.0f };
    std::atomic<float> busInPeakL { 0.0f }, busInPeakR { 0.0f }; // bus mode input, UI resets after reading
    std::array<std::atomic<float>, maxInputs> inputPeaks {};       // every input, UI resets after reading

private:
    PluginSlot* findSlot (const juce::String& id);
    void forEachSlot (const std::function<void (PluginSlot&)>&);
    void processChunk (const float* const* inputs, int numInputs, float* outL, float* outR, int n) noexcept;

    PluginHost& host;
    std::array<ChannelStrip, numStrips> strips;
    std::array<std::unique_ptr<PluginSlot>, numMasterSlots> masterSlots;
    std::unique_ptr<PluginSlot> reverbSlot;

    juce::AudioBuffer<float> mix, send;
    std::array<const float*, maxInputs> inputPtrs {};
    juce::Reverb reverb;
    Compressor busComp;
    Limiter limiter;
    Leveler leveler;
    juce::SmoothedValue<float> masterGain, verbReturn, busTrim;
    int maxBlock = 512;

    // Changing the bus input crossfades from the old pair so the switch doesn't click.
    int activePair = -1, fadeFromPair = -1, fadePos = 0, fadeLength = 2400;

    struct MasterRefs
    {
        std::atomic<float>*compOn, *compThr, *compRatio, *compAtk, *compRel, *compMakeup, *verbSize, *verbDamp,
            *verbReturn, *limOn, *ceiling, *limRel, *fader, *busMode, *busSrc, *busTrim, *levOn, *levTarget,
            *levRange, *levSpeed;
    } m {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackProcessor)
};
} // namespace sr
