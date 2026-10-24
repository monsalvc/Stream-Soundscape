#pragma once

#include "WingProtocol.h"

namespace sr::wing
{
// Network ports of a console (the simulator uses free ones on this computer).
struct Ports
{
    int osc = oscPort, meter = meterPort, discovery = discoveryPort;
};

// Watches a WING over the network on a background thread: reads names, faders, mutes and the
// sends to the stream target, follows changes as they happen on the console, and streams the
// meters. The only things it can change are stream-only: each source's send to the stream
// target, and the stream target's own fader and mute. Channel faders and mutes (which the
// house hears) are never touched.
// The UI polls getState() (a copy) on its timer.
class WingClient : private juce::Thread
{
public:
    WingClient();
    ~WingClient() override;

    void connect (const juce::String& host, Ports ports = {});
    void disconnect();
    bool isActive() const { return isThreadRunning(); }

    void setTarget (Target);
    ConsoleState getState() const;

    // Stream-only changes, sent to the console in the background. The state updates at once.
    void setSendLevel (int source, float db);
    void setSendOn (int source, bool on);
    void setTargetFader (float db);
    void setTargetMute (bool mute);

    // Broadcasts "WING?" and collects replies for timeoutMs. Blocks; call it off the message thread.
    static std::vector<Discovered> discover (int timeoutMs = 1500, int port = discoveryPort,
                                             const juce::String& broadcastAddress = "255.255.255.255");

private:
    void run() override;
    void queueChange (const juce::String& address, osc::Arg value);

    mutable juce::CriticalSection lock;
    ConsoleState state;
    juce::String host;
    Ports ports;
    std::atomic<bool> targetChanged { false };
    std::vector<osc::Message> outbox; // changes waiting to be sent (guarded by lock)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WingClient)
};
} // namespace sr::wing
