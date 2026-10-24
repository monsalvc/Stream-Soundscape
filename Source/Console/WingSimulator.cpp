#include "WingSimulator.h"

namespace sr::wing
{
namespace
{
    int64_t nowMs() { return (int64_t) juce::Time::getMillisecondCounterHiRes(); }

    juce::String formatDb (float db) { return db <= minusInf ? juce::String ("-oo") : juce::String (db, 1); }
} // namespace

WingSimulator::WingSimulator (bool isDemo) : juce::Thread ("WING simulator"), demo (isDemo)
{
    inputLevels.fill (minusInf);
    addDefaults();
}

WingSimulator::~WingSimulator()
{
    stop();
}

bool WingSimulator::start()
{
    if (! control.bindToPort (0, "127.0.0.1") || ! discovery.bindToPort (0, "127.0.0.1")
        || ! listener.createListener (0, "127.0.0.1"))
        return false;

    ports.osc = control.getBoundPort();
    ports.discovery = discovery.getBoundPort();
    ports.meter = listener.getBoundPort();
    meterOut.bindToPort (0);
    startThread();
    return true;
}

void WingSimulator::stop()
{
    stopThread (3000);
    meterClient.reset();
    listener.close();
    control.shutdown();
    discovery.shutdown();
}

//==============================================================================
void WingSimulator::addDefaults()
{
    const auto addFloat = [this] (const juce::String& a, float v) { params[a] = { Param::Kind::Float, v, 0, 0, {} }; };
    const auto addInt = [this] (const juce::String& a, int v, int min = 0) { params[a] = { Param::Kind::Int, 0.0f, v, min, {} }; };
    const auto addEnum = [this] (const juce::String& a, const juce::String& v) { params[a] = { Param::Kind::Enum, 0.0f, 0, 0, v }; };
    const auto addText = [this] (const juce::String& a, const juce::String& v) { params[a] = { Param::Kind::Text, 0.0f, 0, 0, v }; };

    for (int s = 0; s < numSources; ++s)
    {
        const auto p = sourcePath (s);
        addText (p + "/name", {});
        addInt (p + "/col", 1, 1);
        addFloat (p + "/fdr", 0.0f);
        addInt (p + "/mute", 0);
        for (int b = 1; b <= numBuses; ++b)
        {
            const auto send = p + "/send/" + juce::String (b);
            addFloat (send + "/lvl", minusInf);
            addInt (send + "/on", 0);
            addEnum (send + "/mode", "POST");
        }
        for (int m = 1; m <= numMains; ++m)
        {
            const auto send = p + "/main/" + juce::String (m);
            addFloat (send + "/lvl", 0.0f);
            addInt (send + "/on", m == 1 ? 1 : 0);
            addInt (send + "/pre", 0);
        }
    }
    for (int b = 1; b <= numBuses; ++b)
    {
        addText ("/bus/" + juce::String (b) + "/name", {});
        addFloat ("/bus/" + juce::String (b) + "/fdr", 0.0f);
        addInt ("/bus/" + juce::String (b) + "/mute", 0);
    }
    for (int m = 1; m <= numMains; ++m)
    {
        addText ("/main/" + juce::String (m) + "/name", m == 1 ? "MAIN" : "");
        addFloat ("/main/" + juce::String (m) + "/fdr", 0.0f);
        addInt ("/main/" + juce::String (m) + "/mute", 0);
    }

    if (! demo)
        return;

    // A Sabbath service: the stream bus is Bus 14, fed post-fader except the choir.
    struct Source { int index; const char* name; int colour; float send; bool on; bool mute; const char* mode; };
    const Source sources[] = {
        { 0, "Pastor", 9, 0.0f, true, false, "POST" },     { 1, "Elder", 9, -3.0f, true, true, "POST" },
        { 2, "Lead Vox", 5, -2.0f, true, false, "POST" },  { 3, "Vox 2", 5, -5.0f, true, false, "POST" },
        { 4, "Vox 3", 5, -5.0f, false, false, "POST" },    { 5, "Choir L", 4, -4.0f, true, false, "PRE" },
        { 6, "Choir R", 4, -4.0f, true, false, "PRE" },    { 7, "Ac Gtr", 13, -8.0f, true, false, "POST" },
        { 8, "Keys", 3, -6.0f, true, false, "POST" },      { 9, "Organ", 3, -7.0f, true, false, "POST" },
        { 10, "Bass", 12, -9.0f, true, false, "POST" },    { 11, "Kick", 1, -12.0f, true, false, "POST" },
        { 12, "Snare", 1, -14.0f, true, false, "POST" },   { 13, "Overheads", 1, -15.0f, true, false, "POST" },
        { 14, "Announce", 14, minusInf, true, false, "POST" }, { 15, "Wireless 1", 9, -4.0f, true, true, "POST" },
        { numChannels, "Tracks", 11, -10.0f, true, false, "POST" }, { numChannels + 1, "Video", 11, -6.0f, true, true, "POST" },
    };
    for (const auto& s : sources)
    {
        const auto p = sourcePath (s.index);
        params[p + "/name"].s = s.name;
        params[p + "/col"].i = s.colour;
        params[p + "/mute"].i = s.mute ? 1 : 0;
        params[p + "/fdr"].f = s.index == 1 ? -10.0f : 0.0f;
        params[p + "/send/14/lvl"].f = s.send;
        params[p + "/send/14/on"].i = s.on ? 1 : 0;
        params[p + "/send/14/mode"].s = s.mode;
    }
    const char* busNames[] = { "Wedge 1", "Wedge 2", "Wedge 3", "IEM Band", "IEM Vox", "", "", "", "", "",
                               "Reverb", "Delay", "Lobby", "STREAM", "Hearing", "Record" };
    for (int b = 0; b < numBuses; ++b)
        params["/bus/" + juce::String (b + 1) + "/name"].s = busNames[b];
}

//==============================================================================
void WingSimulator::setFloat (const juce::String& address, float value)
{
    {
        const juce::ScopedLock sl (lock);
        params[address].kind = Param::Kind::Float;
        params[address].f = value;
    }
    notify (address);
}

void WingSimulator::setInt (const juce::String& address, int value)
{
    {
        const juce::ScopedLock sl (lock);
        params[address].kind = Param::Kind::Int;
        params[address].i = value;
    }
    notify (address);
}

void WingSimulator::setText (const juce::String& address, const juce::String& value)
{
    {
        const juce::ScopedLock sl (lock);
        auto& p = params[address];
        if (p.kind != Param::Kind::Enum)
            p.kind = Param::Kind::Text;
        p.s = value;
    }
    notify (address);
}

void WingSimulator::setInputLevel (int source, float db)
{
    const juce::ScopedLock sl (lock);
    inputLevels[(size_t) source] = db;
}

bool WingSimulator::hasSubscriber() const
{
    const juce::ScopedLock sl (lock);
    return subscriberPort != 0 && nowMs() - subscribedAt < 10000;
}

osc::Message WingSimulator::valueMessage (const juce::String& address, const Param& p) const
{
    // Like the console: numbers come as (text, 0..1, value), with integers as an offset from
    // their minimum; strings and enums as (text).
    switch (p.kind)
    {
        case Param::Kind::Float:
            return { address, { osc::Arg::ofString (formatDb (p.f)), osc::Arg::ofFloat (juce::jmap (p.f, minusInf, 10.0f, 0.0f, 1.0f)),
                                osc::Arg::ofFloat (p.f) } };
        case Param::Kind::Int:
            return { address, { osc::Arg::ofString (juce::String (p.i)), osc::Arg::ofFloat ((float) p.i), osc::Arg::ofInt (p.i - p.min) } };
        case Param::Kind::Enum:
        case Param::Kind::Text:
            return { address, { osc::Arg::ofString (p.s) } };
    }
    return { address, {} };
}

void WingSimulator::notify (const juce::String& address)
{
    juce::String ip;
    int port = 0;
    osc::Message m;
    {
        const juce::ScopedLock sl (lock);
        const auto it = params.find (address);
        if (it == params.end() || subscriberPort == 0 || nowMs() - subscribedAt >= 10000)
            return;
        ip = subscriberIp;
        port = subscriberPort;
        // Notifications use the "$" shadow address: /ch/1/fdr -> /ch/1/$fdr.
        const int slash = address.lastIndexOfChar ('/');
        m = valueMessage (address.substring (0, slash + 1) + "$" + address.substring (slash + 1), it->second);
    }
    const auto data = osc::encode (m);
    control.write (ip, port, data.getData(), (int) data.getSize());
}

void WingSimulator::handleControl (const osc::Message& m, const juce::String& ip, int port)
{
    if (m.address == "/*s" || m.address == "/*S")
    {
        const juce::ScopedLock sl (lock);
        subscriberIp = ip;
        subscriberPort = port;
        subscribedAt = nowMs();
        return;
    }

    if (m.args.size() == 1)
    {
        // SET.
        const auto& a = m.args[0];
        bool known = false;
        {
            const juce::ScopedLock sl (lock);
            if (auto it = params.find (m.address); it != params.end())
            {
                known = true;
                auto& p = it->second;
                if (p.kind == Param::Kind::Float)
                    p.f = a.asFloat();
                else if (p.kind == Param::Kind::Int)
                    p.i = a.type == osc::Arg::Type::Int && a.i == -1 ? 1 - p.i : (int) a.asFloat();
                else
                    p.s = a.asString();
            }
        }
        if (known)
            notify (m.address);
        return;
    }

    // GET: no reply for an unknown address, like the console.
    osc::Message reply;
    {
        const juce::ScopedLock sl (lock);
        const auto it = params.find (m.address);
        if (it == params.end())
            return;
        reply = valueMessage (m.address, it->second);
    }
    const auto data = osc::encode (reply);
    control.write (ip, port, data.getData(), (int) data.getSize());
}

//==============================================================================
void WingSimulator::handleMeterBytes (const uint8_t* data, size_t size)
{
    demux.feed (data, size, [this] (int channel, uint8_t b)
    {
        if (channel == meter::meterChannel)
            meterCommand.push_back (b);
    });

    size_t i = 0;
    while (i < meterCommand.size())
    {
        const auto token = meterCommand[i];
        const size_t left = meterCommand.size() - i;
        if (token == meter::udpPortToken)
        {
            if (left < 3)
                break;
            meterUdpPort = (meterCommand[i + 1] << 8) | meterCommand[i + 2];
            i += 3;
        }
        else if (token == meter::reportIdToken)
        {
            if (left < 5)
                break;
            meterReportId = osc::detail::readBE32 (meterCommand.data() + i + 1);
            meterKeepAliveAt = nowMs();
            i += 5;
        }
        else if (token == meter::collectionStart)
        {
            const auto end = std::find (meterCommand.begin() + (long) i, meterCommand.end(), meter::collectionEnd);
            if (end == meterCommand.end())
                break;
            meterGroups.clear();
            for (auto it = meterCommand.begin() + (long) i + 1; it < end; ++it)
            {
                const auto t = *it;
                const bool indexed = t != 0xa9 && t != 0xaa;
                if (indexed && it + 1 < end)
                    meterGroups.emplace_back (t, *++it);
                else
                    meterGroups.emplace_back (t, 0);
            }
            i = (size_t) (end - meterCommand.begin()) + 1;
        }
        else
        {
            ++i;
        }
    }
    meterCommand.erase (meterCommand.begin(), meterCommand.begin() + (long) i);
}

float WingSimulator::levelFor (int source) const
{
    if (! demo)
        return inputLevels[(size_t) source];

    // Pastor talking in phrases; the band and choir underneath; unnamed channels silent.
    const auto noise = [] (double x) { return (float) (std::sin (x * 1.7) * 0.5 + std::sin (x * 3.1 + 1.0) * 0.3 + std::sin (x * 7.3) * 0.2); };
    const double t = phase;
    switch (source)
    {
        case 0:
        {
            const bool phrase = std::fmod (t, 7.0) < 5.2;
            return phrase ? -20.0f + 8.0f * noise (t * 3.0) : -62.0f;
        }
        case 1: return -55.0f + 3.0f * noise (t);
        case 2: case 3: case 4: return -26.0f + 7.0f * noise (t * 2.0 + source);
        case 5: case 6: return -30.0f + 5.0f * noise (t * 1.5 + source);
        case 7: case 8: case 9: case 10: return -24.0f + 6.0f * noise (t * 2.5 + source);
        case 11: case 12: return -16.0f + 9.0f * noise (t * 6.0 + source);
        case 13: return -28.0f + 4.0f * noise (t * 4.0);
        case 15: return -12.0f + 6.0f * noise (t * 3.3); // a wireless mic left on, muted
        case numChannels: return -22.0f + 4.0f * noise (t);
        default: return minusInf;
    }
}

void WingSimulator::sendMeterFrame()
{
    juce::MemoryOutputStream out;
    out.writeIntBigEndian ((int) meterReportId);
    const auto word = [&out] (float db) { out.writeShortBigEndian ((short) juce::jlimit (-32768.0f, 32767.0f, db * 256.0f)); };

    float streamLevel = minusInf;
    {
        const juce::ScopedLock sl (lock);
        for (int s = 0; s < numSources; ++s)
        {
            const auto p = sourcePath (s);
            if (params[p + "/mute"].i == 0 && params[p + "/send/14/on"].i != 0)
                streamLevel = juce::jmax (streamLevel, levelFor (s) + params[p + "/send/14/lvl"].f);
        }
    }

    for (const auto& [token, index] : meterGroups)
    {
        int source = -1;
        if (token == meter::channelToken)
            source = index;
        else if (token == meter::auxToken)
            source = numChannels + index;

        if (source >= 0 && source < numSources)
        {
            const float in = levelFor (source);
            float fader, muted;
            {
                const juce::ScopedLock sl (lock);
                fader = params[sourcePath (source) + "/fdr"].f;
                muted = (float) params[sourcePath (source) + "/mute"].i;
            }
            const float outDb = muted > 0.5f ? minusInf : in + fader;
            for (float v : { in, in, outDb, outDb, minusInf, 0.0f, minusInf, 0.0f })
                word (v);
        }
        else
        {
            const bool stream = demo && token == meter::busToken && index == 13;
            const float level = stream ? streamLevel - 4.0f : minusInf;
            for (float v : { level, level, level, level, minusInf, 0.0f, minusInf, 0.0f })
                word (v);
        }
    }

    const auto host = meterClient->getHostName();
    meterOut.write (host.isEmpty() ? juce::String ("127.0.0.1") : host, meterUdpPort, out.getData(), (int) out.getDataSize());
    ++framesSent;
}

//==============================================================================
void WingSimulator::run()
{
    juce::HeapBlock<uint8_t> buffer (65536);
    while (! threadShouldExit())
    {
        // Control.
        if (control.waitUntilReady (true, 5) == 1)
        {
            for (int n = 0; n < 512 && control.waitUntilReady (true, 0) == 1; ++n)
            {
                juce::String ip;
                int port = 0;
                const int size = control.read (buffer.get(), 65536, false, ip, port);
                if (size <= 0)
                    break;
                const auto text = juce::String::fromUTF8 ((const char*) buffer.get(), juce::jmin (size, 5));
                osc::Message m;
                if (text == "WING?")
                {
                    const juce::String reply ("WING,127.0.0.1,StreamRack demo,ngc-full,SIM-0001,3.1.0");
                    control.write (ip, port, reply.toRawUTF8(), (int) reply.getNumBytesAsUTF8());
                }
                else if (osc::decode (buffer.get(), (size_t) size, m))
                    handleControl (m, ip, port);
            }
        }

        // Discovery.
        if (discovery.waitUntilReady (true, 0) == 1)
        {
            juce::String ip;
            int port = 0;
            const int size = discovery.read (buffer.get(), 65536, false, ip, port);
            if (size >= 5 && juce::String::fromUTF8 ((const char*) buffer.get(), 5) == "WING?")
            {
                const juce::String reply ("WING,127.0.0.1,StreamRack demo,ngc-full,SIM-0001,3.1.0");
                discovery.write (ip, port, reply.toRawUTF8(), (int) reply.getNumBytesAsUTF8());
            }
        }

        // Meter connection.
        if (meterClient == nullptr && listener.waitUntilReady (true, 0) == 1)
        {
            meterClient.reset (listener.waitForNextConnection());
            demux = {};
            meterCommand.clear();
            meterGroups.clear();
            meterUdpPort = 0;
        }
        if (meterClient != nullptr && meterClient->waitUntilReady (true, 0) == 1)
        {
            const int size = meterClient->read (buffer.get(), 4096, false);
            if (size <= 0)
                meterClient.reset();
            else
                handleMeterBytes (buffer.get(), (size_t) size);
        }

        const auto now = nowMs();
        if (meterClient != nullptr && meterUdpPort != 0 && ! meterGroups.empty() && now - meterKeepAliveAt < 5000
            && now - lastFrameAt >= 50)
        {
            phase += (double) (now - lastFrameAt) / 1000.0;
            lastFrameAt = now;
            sendMeterFrame();
        }
    }
}
} // namespace sr::wing
