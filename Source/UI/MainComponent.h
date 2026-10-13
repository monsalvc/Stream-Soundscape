#pragma once

#include "DetailPanel.h"
#include "MasterPanel.h"
#include "StripComponent.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_cryptography/juce_cryptography.h>

namespace sr::ui
{
class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ValueTree::Listener
{
public:
    MainComponent (RackProcessor&, juce::AudioDeviceManager&, juce::PropertiesFile& settings);
    ~MainComponent() override;

    // Scenes are full snapshots (channel names, all settings and insert plugins).
    static juce::File getScenesFolder();
    void loadScene (const juce::File&);
    void saveScene (const juce::String& name);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override { refreshAll(); }

    void selectStrip (int index);
    void setLocked (bool);
    void refreshAll();
    void updateStatus();

    void showAudioSettings();
    void showPluginScanner();
    void showScenesMenu();
    void lockClicked();
    void askForText (const juce::String& title, const juce::String& message, bool password,
                     std::function<void (juce::String)> onOk);

    RackProcessor& rack;
    juce::AudioDeviceManager& deviceManager;
    juce::PropertiesFile& settings;
    LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    PluginWindows pluginWindows;

    juce::Label title, status;
    juce::TextButton audioButton { "Audio..." }, pluginsButton { "Plugins..." }, scenesButton { "Scenes" },
        lockButton { "Lock" };

    juce::Component stripContainer;
    juce::Viewport stripViewport, detailViewport;
    juce::OwnedArray<StripComponent> strips;
    DetailPanel detail;
    MasterPanel master;

    bool locked = false;
    int selected = -1;
    int statusCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace sr::ui
