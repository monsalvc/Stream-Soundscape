#include "WingClient.h"
#include <deque>

namespace sr::wing
{
namespace
{
    int64_t nowMs() { return (int64_t) juce::Time::getMillisecondCounterHiRes(); }

    void clearSends (ConsoleState& s)
    {
        for (auto& src : s.sources)
        {
            src.sendLevel = minusInf;
            src.sendOn = false;
            src.sendMode = {};
        }
    }

    void clearMeters (ConsoleState& s)
    {
        for (auto& src : s.sources)
            src.inL = src.inR = src.outL = src.outR = minusInf;
        for (auto& m : s.buses)
            m.outL = m.outR = minusInf;
        for (auto& m : s.mains)
            m.outL = m.outR = minusInf;
    }

    void send (juce::DatagramSocket& socket, const juce::String& host, int port, const osc::Message& m)
    {
        const auto data = osc::encode (m);
        socket.write (host, port, data.getData(), (int) data.getSize());
    }
} // namespace

WingClient::WingClient() : juce::Thread ("WING client") {}

WingClient::~WingClient()
{
    disconnect();
}

void WingClient::connect (const juce::String& newHost, Ports newPorts)
{
    disconnect();
    {
        const juce::ScopedLock sl (lock);
        host = newHost.trim();
        ports = newPorts;
        const auto target = state.target;
        state = {};
        state.target = target;
        state.host = host;
        state.status = ConsoleState::Status::Connecting;
    }
    startThread();
}

void WingClient::disconnect()
{
    stopThread (3000);
    const juce::ScopedLock sl (lock);
    state.status = ConsoleState::Status::Off;
    state.metersFlowing = false;
    clearMeters (state);
}

void WingClient::setTarget (Target t)
{
    const juce::ScopedLock sl (lock);
    if (state.target == t)
        return;
    state.target = t;
    clearSends (state);
    targetChanged = true;
}

ConsoleState WingClient::getState() const
{
    const juce::ScopedLock sl (lock);
    return state;
}

void WingClient::run()
{
    juce::DatagramSocket control, meters;
    control.bindToPort (0);
    meters.bindToPort (0);
    std::unique_ptr<juce::StreamingSocket> meterLink;

    const auto reportId = (uint32_t) juce::Random::getSystemRandom().nextInt() | 1u;
    std::deque<juce::String> pending;
    const auto queue = [&pending] (const juce::StringArray& addresses)
    {
        for (const auto& a : addresses)
            pending.push_back (a);
    };

    Target target;
    {
        const juce::ScopedLock sl (lock);
        target = state.target;
    }
    queue (mixAddresses());
    queue (sourceAddresses (target));

    const auto started = nowMs();
    int64_t lastSubscribe = 0, lastReply = 0, lastFrame = 0, lastKeepAlive = 0, nextMeterAttempt = 0;
    int64_t lastRefresh = started;
    bool wasConnected = false, everConnected = false, metersWereFlowing = false;

    // Ask the console directly for its name and model (the discovery reply).
    control.write (host, ports.discovery, "WING?", 5);
    juce::HeapBlock<uint8_t> buffer (65536);

    while (! threadShouldExit())
    {
        const auto now = nowMs();

        if (targetChanged.exchange (false))
        {
            const juce::ScopedLock sl (lock);
            target = state.target;
            for (int s = 0; s < numSources; ++s)
                queue (sendAddresses (s, target));
        }

        // Keep the change subscription alive (it lapses after 10 s), and ask for one value so a
        // quiet console still shows as connected.
        if (now - lastSubscribe >= 4000)
        {
            send (control, host, ports.osc, { "/*s", {} });
            send (control, host, ports.osc, { target.path() + "/fdr", {} });
            lastSubscribe = now;
        }

        // Re-read everything now and then in case a notification was lost (it's UDP).
        if (now - lastRefresh >= 30000)
        {
            queue (mixAddresses());
            queue (sourceAddresses (target));
            lastRefresh = now;
        }

        // Pace the requests so the console's receive buffer isn't flooded.
        for (int n = 0; n < 8 && ! pending.empty(); ++n)
        {
            send (control, host, ports.osc, { pending.front(), {} });
            pending.pop_front();
        }

        // Meter connection: set up, then keep it alive (the console stops after 5 s).
        if (meterLink == nullptr && now >= nextMeterAttempt)
        {
            meterLink = std::make_unique<juce::StreamingSocket>();
            if (meterLink->connect (host, ports.meter, 1500))
            {
                const auto setup = meter::setup (meters.getBoundPort(), reportId);
                meterLink->write (setup.getData(), (int) setup.getSize());
                lastKeepAlive = now;
            }
            else
            {
                meterLink.reset();
                nextMeterAttempt = now + 3000;
            }
        }
        if (meterLink != nullptr)
        {
            bool lost = false;
            if (now - lastKeepAlive >= 3000)
            {
                const auto ping = meter::keepAlive (reportId);
                lost = meterLink->write (ping.getData(), (int) ping.getSize()) != (int) ping.getSize();
                lastKeepAlive = now;
            }
            if (! lost && meterLink->waitUntilReady (true, 0) == 1)
            {
                char discard[512];
                lost = meterLink->read (discard, (int) sizeof (discard), false) <= 0;
            }
            if (lost)
            {
                meterLink.reset();
                nextMeterAttempt = now + 1000;
            }
        }

        // Incoming control replies and change notifications.
        if (control.waitUntilReady (true, 10) == 1)
        {
            const juce::ScopedLock sl (lock);
            for (int n = 0; n < 512 && control.waitUntilReady (true, 0) == 1; ++n)
            {
                const int size = control.read (buffer.get(), 65536, false);
                osc::Message m;
                if (size <= 0)
                    break;
                if (const auto reply = parseDiscoveryReply (juce::String::fromUTF8 ((const char*) buffer.get(), size)))
                {
                    state.consoleName = reply->name;
                    state.model = reply->model;
                    state.firmware = reply->firmware;
                }
                else if (osc::decode (buffer.get(), (size_t) size, m))
                {
                    apply (state, m);
                    lastReply = now;
                }
            }
        }

        // Incoming meter frames.
        for (int n = 0; n < 64 && meters.waitUntilReady (true, 0) == 1; ++n)
        {
            const int size = meters.read (buffer.get(), 65536, false);
            if (size <= 0)
                break;
            const juce::ScopedLock sl (lock);
            if (meter::applyFrame (state, buffer.get(), (size_t) size, reportId))
                lastFrame = now;
        }

        const bool connected = lastReply > 0 && now - lastReply < 12000;
        const bool metersFlowing = lastFrame > 0 && now - lastFrame < 1500;
        if (connected && ! wasConnected && everConnected)
        {
            // Back after a dropout: read everything again.
            queue (mixAddresses());
            queue (sourceAddresses (target));
        }
        wasConnected = connected;
        everConnected = everConnected || connected;

        const juce::ScopedLock sl (lock);
        state.status = connected ? ConsoleState::Status::Connected
                                 : (lastReply == 0 && now - started < 5000 ? ConsoleState::Status::Connecting
                                                                          : ConsoleState::Status::NoReply);
        state.metersFlowing = metersFlowing;
        if (metersWereFlowing && ! metersFlowing)
            clearMeters (state);
        metersWereFlowing = metersFlowing;
    }
}

std::vector<Discovered> WingClient::discover (int timeoutMs, int port, const juce::String& broadcastAddress)
{
    std::vector<Discovered> found;
    juce::DatagramSocket socket (true);
    if (! socket.bindToPort (0))
        return found;

    // Ask on every network interface: a Mac on Wi-Fi and the console's Ethernet would otherwise
    // only send the general broadcast out of one of them.
    const char query[] = "WING?";
    juce::StringArray targets { broadcastAddress };
    if (broadcastAddress == "255.255.255.255")
        for (const auto& address : juce::IPAddress::getAllAddresses (false))
            if (! address.isNull() && address.toString() != "127.0.0.1")
                targets.addIfNotAlreadyThere (juce::IPAddress::getInterfaceBroadcastAddress (address).toString());
    for (const auto& t : targets)
        if (t.isNotEmpty() && t != "0.0.0.0")
            socket.write (t, port, query, 5);

    const auto deadline = nowMs() + timeoutMs;
    char reply[1024];
    while (nowMs() < deadline)
    {
        if (socket.waitUntilReady (true, 50) != 1)
            continue;
        juce::String senderIp;
        int senderPort = 0;
        const int size = socket.read (reply, (int) sizeof (reply) - 1, false, senderIp, senderPort);
        if (size <= 0)
            continue;
        auto parsed = parseDiscoveryReply (juce::String::fromUTF8 (reply, size));
        if (! parsed)
            continue;
        if (parsed->ip.isEmpty())
            parsed->ip = senderIp;
        const bool duplicate = std::any_of (found.begin(), found.end(), [&] (const auto& d) { return d.ip == parsed->ip; });
        if (! duplicate)
            found.push_back (*parsed);
    }
    return found;
}
} // namespace sr::wing
