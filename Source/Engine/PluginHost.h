#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sr
{
// Owns the plugin formats (VST3, and AU on macOS) and the list of scanned plugins.
class PluginHost : private juce::ChangeListener
{
public:
    explicit PluginHost (juce::File dataDirectory);
    ~PluginHost() override;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;

    // Effects only (instruments are filtered out), sorted for menus.
    juce::Array<juce::PluginDescription> getEffects() const;

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, double sampleRate,
                                                               int blockSize, juce::String& error);

    // Used by the scanner to skip plugins that crashed during a previous scan.
    juce::File getDeadMansPedalFile() const { return dataDir.getChildFile ("scan-in-progress.txt"); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::File dataDir;
    juce::File listFile;
};
} // namespace sr
