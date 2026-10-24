#pragma once

#include "Console/WingClient.h"
#include "Widgets.h"

namespace sr::ui
{
// One console channel in the console view: name, input meter, send to the stream and warnings.
class SourceTile : public juce::Component
{
public:
    static constexpr int width = 116, height = 112;

    void update (const wing::SourceState&, int index);
    void paint (juce::Graphics&) override;

private:
    wing::SourceState source;
    int index = 0;
    float meterDb = -100.0f;
};

// The stream bus itself: its meter, fader and mute.
class MixTile : public juce::Component
{
public:
    void update (const wing::MixState&, const wing::Target&, bool metersFlowing);
    void paint (juce::Graphics&) override;

private:
    wing::MixState mix;
    wing::Target target;
    bool meters = false;
    float meterL = -100.0f, meterR = -100.0f;
};

// Live view of the console channels that feed the stream bus, read from the WING over the
// network: who is talking, what is on the stream, and what is muted or turned down.
// Read-only: nothing here changes the console.
class ConsolePanel : public juce::Component, private juce::Timer
{
public:
    ConsolePanel (wing::WingClient&, juce::PropertiesFile&);
    ~ConsolePanel() override;

    std::function<void()> onConnectClicked;
    void setLocked (bool);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshTargets();
    void targetChosen();
    void layoutTiles();
    juce::String statusText() const;
    bool showsSource (int index) const;

    wing::WingClient& client;
    juce::PropertiesFile& settings;
    wing::ConsoleState state;
    wing::ConsoleState::Status lastStatus = wing::ConsoleState::Status::Off;
    bool locked = false;

    juce::TextButton connectButton { "Connect..." };
    juce::ComboBox targetBox;
    Toggle allToggle { "All channels" };
    MixTile mixTile;
    juce::OwnedArray<SourceTile> tiles;
    juce::Component tileContainer;
    juce::Viewport viewport;
    juce::StringArray targetNames;
    std::vector<bool> shown;

    juce::Rectangle<int> statusArea, hintArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsolePanel)
};

// Find the console on the network, type its address, or start the demo console.
class ConnectPanel : public juce::Component
{
public:
    ConnectPanel (const juce::String& currentHost, bool connected);
    ~ConnectPanel() override;

    std::function<void (juce::String host)> onConnect;
    std::function<void()> onDemo, onDisconnect;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void search();
    void close();

    juce::TextButton searchButton { "Search the network" }, connectButton { "Connect" },
        demoButton { "Demo console" }, disconnectButton { "Disconnect" };
    juce::TextEditor address;
    juce::OwnedArray<juce::TextButton> found;
    juce::String searchMessage;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace sr::ui
