#pragma once

#include "WingClient.h"
#include <map>

namespace sr::wing
{
// A pretend WING on this computer, speaking the same network protocol as the real console:
// control (OSC: GET, SET, "/*s" subscriptions), meters (TCP setup + UDP frames) and discovery.
// The tests use it to check the client end to end, and the app offers it as a demo console so
// volunteers can learn the screen away from the booth.
class WingSimulator : private juce::Thread
{
public:
    // demo: a church-like set of names and sends, with moving meters.
    explicit WingSimulator (bool demo);
    ~WingSimulator() override;

    // Binds free ports on this computer and starts answering. Returns false if it couldn't.
    bool start();
    void stop();
    Ports getPorts() const { return ports; }

    // Changes a value as if someone did it on the console (subscribers are notified).
    void setFloat (const juce::String& address, float value);
    void setInt (const juce::String& address, int value);
    void setText (const juce::String& address, const juce::String& value);

    // Input level of a source for the meters (only used when not animating).
    void setInputLevel (int source, float db);

    // Current values, for checking what a client changed.
    float getFloat (const juce::String& address) const;
    int getInt (const juce::String& address) const;

    int getMeterFramesSent() const { return framesSent.load(); }
    bool hasSubscriber() const;

private:
    struct Param
    {
        enum class Kind { Float, Int, Enum, Text };
        Kind kind = Kind::Float;
        float f = 0.0f;
        int i = 0, min = 0;
        juce::String s;
    };

    void run() override;
    void addDefaults();
    void handleControl (const osc::Message&, const juce::String& senderIp, int senderPort);
    osc::Message valueMessage (const juce::String& address, const Param&) const;
    void notify (const juce::String& address);
    void handleMeterBytes (const uint8_t* data, size_t size);
    void sendMeterFrame();
    float levelFor (int source) const;

    const bool demo;
    Ports ports;
    juce::DatagramSocket control, discovery;
    juce::StreamingSocket listener;
    std::unique_ptr<juce::StreamingSocket> meterClient;
    juce::DatagramSocket meterOut;

    mutable juce::CriticalSection lock;
    std::map<juce::String, Param> params;
    std::array<float, numSources> inputLevels {};
    juce::String subscriberIp;
    int subscriberPort = 0;
    int64_t subscribedAt = 0;

    meter::Demux demux;
    std::vector<uint8_t> meterCommand;
    std::vector<std::pair<uint8_t, int>> meterGroups;
    int meterUdpPort = 0;
    uint32_t meterReportId = 0;
    int64_t meterKeepAliveAt = 0, lastFrameAt = 0;
    std::atomic<int> framesSent { 0 };
    double phase = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WingSimulator)
};
} // namespace sr::wing
