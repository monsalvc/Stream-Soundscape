#pragma once

#include "Console/WingClient.h"
#include "Widgets.h"

namespace sr::ui
{
// One console channel as a condensed fader strip: scribble strip, the send to the stream bus
// drawn as a fader, the channel's input meter, the send's ON button and the channel's mute light.
// When editing is allowed, the fader and ON button change the send (the stream only).
class SourceTile : public juce::Component
{
public:
    static constexpr int width = 56, minHeight = 200;

    void update (const wing::SourceState&, int index);
    void setEditable (bool);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void (float db)> onSendLevel;
    std::function<void (bool on)> onSendOn;

private:
    wing::SourceState source;
    int index = 0;
    float meterDb = -100.0f;
    bool editable = false, dragging = false;
    float dragStartDb = 0.0f;
    juce::Rectangle<int> faderArea, onArea;
};

// The stream bus itself as a master strip: its fader, stereo meter and mute. When editing is
// allowed, the fader and MUTE change the stream bus (which only the stream hears).
class MixTile : public juce::Component
{
public:
    void update (const wing::MixState&, const wing::Target&, bool metersFlowing);
    void setEditable (bool);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void (float db)> onFader;
    std::function<void (bool mute)> onMute;

private:
    wing::MixState mix;
    wing::Target target;
    bool meters = false, editable = false, dragging = false;
    float meterL = -100.0f, meterR = -100.0f, dragStartDb = 0.0f;
    juce::Rectangle<int> faderArea, muteArea;
};

// The WING mix for the stream: the console channels that feed the stream bus, read from the
// WING over the network (who is talking, what is on the stream, what is muted or turned down).
// With Edit sends on, the sends and the stream bus fader and mute can be changed from here.
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
    void updateEditable();
    juce::String statusText() const;
    bool showsSource (int index) const;

    wing::WingClient& client;
    juce::PropertiesFile& settings;
    wing::ConsoleState state;
    wing::ConsoleState::Status lastStatus = wing::ConsoleState::Status::Off;
    bool locked = false;

    juce::TextButton connectButton { "Connect..." };
    juce::ComboBox targetBox;
    Toggle allToggle { "All channels" }, editToggle { "Edit sends" };
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
    std::function<juce::String()> getDiagnostics;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void search();
    void close();

    juce::TextButton searchButton { "Search the network" }, connectButton { "Connect" },
        demoButton { "Demo console" }, disconnectButton { "Disconnect" }, diagnosticsButton { "Copy diagnostics" };
    juce::TextEditor address;
    juce::OwnedArray<juce::TextButton> found;
    juce::String searchMessage;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace sr::ui
