#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace sr
{
// One insert slot that can hold a hosted VST3/AU plugin. Plugins are always fed a
// stereo signal. The plugin is swapped on the message thread under a spin lock; the
// audio thread only try-locks, so a swap in progress just passes audio through dry
// for that block instead of blocking.
class PluginSlot
{
public:
    explicit PluginSlot (juce::String slotId) : id (std::move (slotId)) {}

    const juce::String& getId() const noexcept { return id; }

    // Audio or message thread, before processing starts or while stopped.
    void prepare (double sampleRate, int maxBlockSize);

    // Message thread. Returns the previous plugin so the caller can destroy it
    // outside the lock (and after closing its editor).
    std::unique_ptr<juce::AudioPluginInstance> setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin);

    // Message thread.
    juce::AudioPluginInstance* getPlugin() const noexcept { return plugin.get(); }
    juce::String getPluginName() const { return plugin != nullptr ? plugin->getName() : juce::String(); }

    std::atomic<bool> bypassed { false };

    // Any thread.
    bool hasPlugin() const noexcept { return loaded.load(); }

    // Audio thread. Returns true if the plugin processed this block (false when the
    // slot is empty, bypassed, or being swapped).
    bool process (float* L, float* R, int n) noexcept;
    int getLatencySamples() const noexcept { return latency.load(); }

private:
    void configure (juce::AudioPluginInstance&);

    const juce::String id;
    juce::SpinLock lock;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    double fs = 48000.0;
    int maxBlock = 512;
    std::atomic<int> latency { 0 };
    std::atomic<bool> loaded { false };
};
} // namespace sr
