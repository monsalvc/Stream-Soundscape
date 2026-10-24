#pragma once

#include "BusPanel.h"
#include "ConsolePanel.h"
#include "Console/WingSimulator.h"
#include "MasterPanel.h"
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

    void setLocked (bool);
    void setView (bool wingMix);
    void refreshAll();
    void updateStatus();

    void showAudioSettings();
    void showPluginScanner();
    void showScenesMenu();
    void lockClicked();
    void showConnectDialog();
    void connectConsole (const juce::String& host);
    void disconnectConsole();
    void askForText (const juce::String& title, const juce::String& message, bool password,
                     std::function<void (juce::String)> onOk);

    RackProcessor& rack;
    juce::AudioDeviceManager& deviceManager;
    juce::PropertiesFile& settings;
    LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    PluginWindows pluginWindows;

    juce::Label title, status;
    juce::Image logo;
    juce::Rectangle<int> logoArea;
    juce::TextButton audioButton { "Audio..." }, pluginsButton { "Plugins..." }, scenesButton { "Scenes" },
        lockButton { "Lock" };
    juce::TextButton busViewButton { "Bus" }, wingViewButton { "WING Mix" };

    BusPanel busPanel;
    MasterPanel master;

    // The WING mix (the panel is declared after the client so it goes first).
    std::unique_ptr<wing::WingSimulator> demoConsole;
    wing::WingClient wingClient;
    ConsolePanel consolePanel;

    bool locked = false, wingView = false;
    int statusCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace sr::ui
