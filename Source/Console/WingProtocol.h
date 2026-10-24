#pragma once

#include "Osc.h"
#include <array>
#include <optional>

// Behringer WING remote protocol: what StreamRack needs to watch the channels feeding the
// stream bus. Two separate connections:
//  - Control (OSC over UDP 2223): names, colours, faders, mutes and send levels. GET by sending
//    the bare address; "/*s" subscribes to change notifications for 10 s (renewed every 4 s).
//  - Meters (binary over TCP 2222, channel 3): the client tells the console a UDP port, a report
//    id (resent every 3 s as the keepalive; the console stops after 5 s) and the meter groups,
//    and the console streams frames of big-endian int16 words (dB * 256) about 20 times a second.
// Discovery: "WING?" broadcast to UDP 2222, consoles reply "WING,<ip>,<name>,<model>,<serial>,<fw>".
namespace sr::wing
{
constexpr int oscPort = 2223, meterPort = 2222, discoveryPort = 2222;
constexpr int numChannels = 40, numAux = 8, numBuses = 16, numMains = 4;
constexpr int numSources = numChannels + numAux; // channels then aux inputs
constexpr float minusInf = -144.0f;

//==============================================================================
struct Discovered
{
    juce::String ip, name, model, serial, firmware;
};

inline std::optional<Discovered> parseDiscoveryReply (const juce::String& reply)
{
    auto parts = juce::StringArray::fromTokens (reply.trim(), ",", {});
    if (parts.size() < 3 || parts[0] != "WING")
        return std::nullopt;
    while (parts.size() < 6)
        parts.add ({});
    return Discovered { parts[1], parts[2], parts[3], parts[4], parts[5] };
}

//==============================================================================
// Where the stream mix is built on the console: one of the 16 buses or 4 mains.
struct Target
{
    enum class Kind { Bus, Main };
    Kind kind = Kind::Bus;
    int index = 1; // 1-based, as on the console

    bool operator== (const Target& o) const { return kind == o.kind && index == o.index; }
    bool operator!= (const Target& o) const { return ! (*this == o); }

    juce::String label() const { return (kind == Kind::Bus ? "Bus " : "Main ") + juce::String (index); }
    juce::String path() const { return (kind == Kind::Bus ? "/bus/" : "/main/") + juce::String (index); }
    // Send node on a source for this target, e.g. "send/3" or "main/1".
    juce::String sendNode() const { return (kind == Kind::Bus ? "send/" : "main/") + juce::String (index); }

    juce::String toString() const { return (kind == Kind::Bus ? "bus" : "main") + juce::String (index); }
    static Target fromString (const juce::String& s)
    {
        Target t;
        t.kind = s.startsWith ("main") ? Kind::Main : Kind::Bus;
        t.index = juce::jlimit (1, t.kind == Kind::Bus ? numBuses : numMains, s.retainCharacters ("0123456789").getIntValue());
        return t;
    }
};

// OSC path of a source: channels are /ch/1../ch/40, then aux inputs /aux/1../aux/8.
inline juce::String sourcePath (int source)
{
    return source < numChannels ? "/ch/" + juce::String (source + 1) : "/aux/" + juce::String (source - numChannels + 1);
}

//==============================================================================
struct SourceState
{
    juce::String name;
    int colour = 0; // WING colour index 1-18, 0 = unknown
    float fader = minusInf;
    bool mute = false;
    bool known = false; // the console has answered for this source

    // Send to the stream target.
    float sendLevel = minusInf;
    bool sendOn = false;
    juce::String sendMode; // "PRE", "POST" or empty when unknown

    // Meters, dB.
    float inL = minusInf, inR = minusInf, outL = minusInf, outR = minusInf;

    float inputDb() const { return juce::jmax (inL, inR); }
    // Whether this source can be heard on the stream target right now.
    bool feedsTarget() const { return sendOn && ! mute && sendLevel > -90.0f && (sendMode == "PRE" || fader > -90.0f); }
};

struct MixState
{
    juce::String name;
    float fader = minusInf;
    bool mute = false;
    float outL = minusInf, outR = minusInf;
};

struct ConsoleState
{
    enum class Status { Off, Connecting, Connected, NoReply };
    Status status = Status::Off;
    juce::String host, consoleName, model, firmware;
    bool metersFlowing = false;
    Target target;
    std::array<SourceState, numSources> sources {};
    std::array<MixState, numBuses> buses {};
    std::array<MixState, numMains> mains {};

    const MixState& targetMix() const
    {
        return target.kind == Target::Kind::Bus ? buses[(size_t) (target.index - 1)] : mains[(size_t) (target.index - 1)];
    }

    MixState& targetMix()
    {
        return target.kind == Target::Kind::Bus ? buses[(size_t) (target.index - 1)] : mains[(size_t) (target.index - 1)];
    }
};

//==============================================================================
// Control messages: the reply to a GET (and a "/*s" notification) carries the value as
// (text, 0..1, value) for numbers or (text) for strings and enums. Notifications arrive on the
// "$" shadow address (e.g. /ch/1/$fdr), which means the same parameter.
inline float numericValue (const osc::Message& m)
{
    if (m.args.size() >= 3)
        return m.args[2].asFloat();
    return m.args.empty() ? 0.0f : m.args[0].asFloat();
}

inline juce::String textValue (const osc::Message& m)
{
    return m.args.empty() ? juce::String() : m.args[0].asString();
}

// Applies one control message to the state. Returns false if it wasn't something we track.
inline bool apply (ConsoleState& state, const osc::Message& m)
{
    if (m.args.empty())
        return false;

    auto parts = juce::StringArray::fromTokens (m.address.replace ("/$", "/"), "/", {});
    parts.removeEmptyStrings();
    if (parts.size() < 3)
        return false;

    const auto& kind = parts[0];
    const int index = parts[1].getIntValue();
    const auto& field = parts[2];

    if (kind == "ch" || kind == "aux")
    {
        const int source = kind == "ch" ? index - 1 : numChannels + index - 1;
        const int limit = kind == "ch" ? numChannels : numAux;
        if (index < 1 || index > limit)
            return false;

        auto& s = state.sources[(size_t) source];
        s.known = true;
        if (parts.size() == 3)
        {
            if (field == "name")
                s.name = textValue (m);
            else if (field == "col")
                s.colour = textValue (m).getIntValue();
            else if (field == "fdr")
                s.fader = numericValue (m);
            else if (field == "mute")
                s.mute = numericValue (m) > 0.5f;
            else
                return false;
            return true;
        }

        // Send to the stream target: send/<bus>/(lvl|on|mode) or main/<n>/(lvl|on|pre).
        if (parts.size() == 5 && field + "/" + parts[3] == state.target.sendNode())
        {
            const auto& leaf = parts[4];
            if (leaf == "lvl")
                s.sendLevel = numericValue (m);
            else if (leaf == "on")
                s.sendOn = numericValue (m) > 0.5f;
            else if (leaf == "mode")
                s.sendMode = textValue (m).toUpperCase();
            else if (leaf == "pre")
                s.sendMode = numericValue (m) > 0.5f ? "PRE" : "POST";
            else
                return false;
            return true;
        }
        return false;
    }

    if ((kind == "bus" || kind == "main") && parts.size() == 3)
    {
        const bool bus = kind == "bus";
        if (index < 1 || index > (bus ? numBuses : numMains))
            return false;
        auto& mix = bus ? state.buses[(size_t) (index - 1)] : state.mains[(size_t) (index - 1)];
        if (field == "name")
            mix.name = textValue (m);
        else if (field == "fdr")
            mix.fader = numericValue (m);
        else if (field == "mute")
            mix.mute = numericValue (m) > 0.5f;
        else
            return false;
        return true;
    }
    return false;
}

inline juce::StringArray sendAddresses (int source, const Target& target)
{
    const auto base = sourcePath (source) + "/" + target.sendNode();
    return { base + "/lvl", base + "/on", base + (target.kind == Target::Kind::Bus ? "/mode" : "/pre") };
}

// Every address StreamRack reads, so it can ask for them all after connecting.
inline juce::StringArray sourceAddresses (const Target& target)
{
    juce::StringArray a;
    for (int s = 0; s < numSources; ++s)
    {
        const auto p = sourcePath (s);
        for (const auto* f : { "/name", "/col", "/fdr", "/mute" })
            a.add (p + f);
        a.addArray (sendAddresses (s, target));
    }
    return a;
}

inline juce::StringArray mixAddresses()
{
    juce::StringArray a;
    for (int b = 1; b <= numBuses; ++b)
        for (const auto* f : { "/name", "/fdr", "/mute" })
            a.add ("/bus/" + juce::String (b) + f);
    for (int m = 1; m <= numMains; ++m)
        for (const auto* f : { "/name", "/fdr", "/mute" })
            a.add ("/main/" + juce::String (m) + f);
    return a;
}

//==============================================================================
// Meter connection framing. 0xdf is the escape byte: 0xdf 0xd0+n selects channel n, and a
// literal 0xdf in the data is sent as 0xdf 0xde.
namespace meter
{
    constexpr uint8_t escape = 0xdf, selectBase = 0xd0, meterChannel = 3;
    constexpr uint8_t udpPortToken = 0xd3, reportIdToken = 0xd4, collectionStart = 0xdc, collectionEnd = 0xde;
    constexpr uint8_t channelToken = 0xa0, auxToken = 0xa1, busToken = 0xa2, mainToken = 0xa3;
    constexpr int wordsPerGroup = 8; // inL, inR, outL, outR, gate key, gate gain, dyn key, dyn gain
    constexpr int numGroups = numChannels + numAux + numBuses + numMains;
    constexpr int frameBytes = 4 + numGroups * wordsPerGroup * 2;

    inline void appendEscaped (juce::MemoryOutputStream& out, std::initializer_list<uint8_t> bytes)
    {
        for (auto b : bytes)
        {
            out.writeByte ((char) b);
            if (b == escape)
                out.writeByte ((char) collectionEnd);
        }
    }

    inline void appendReportId (juce::MemoryOutputStream& out, uint32_t id)
    {
        appendEscaped (out, { reportIdToken, (uint8_t) (id >> 24), (uint8_t) (id >> 16), (uint8_t) (id >> 8), (uint8_t) id });
    }

    // Selects the meter channel, then: UDP port, report id, and the groups we want: every
    // channel, aux, bus and main, each as (token, 0-based index).
    inline juce::MemoryBlock setup (int udpPort, uint32_t reportId)
    {
        juce::MemoryOutputStream out;
        out.writeByte ((char) escape);
        out.writeByte ((char) (selectBase + meterChannel));
        appendEscaped (out, { udpPortToken, (uint8_t) (udpPort >> 8), (uint8_t) udpPort });
        appendReportId (out, reportId);
        appendEscaped (out, { collectionStart });
        const auto group = [&] (uint8_t token, int count)
        {
            for (int i = 0; i < count; ++i)
                appendEscaped (out, { token, (uint8_t) i });
        };
        group (channelToken, numChannels);
        group (auxToken, numAux);
        group (busToken, numBuses);
        group (mainToken, numMains);
        appendEscaped (out, { collectionEnd });
        return out.getMemoryBlock();
    }

    inline juce::MemoryBlock keepAlive (uint32_t reportId)
    {
        juce::MemoryOutputStream out;
        appendReportId (out, reportId);
        return out.getMemoryBlock();
    }

    // Applies one UDP meter frame. Returns false if it isn't ours or is too short.
    inline bool applyFrame (ConsoleState& state, const void* raw, size_t size, uint32_t reportId)
    {
        const auto* d = static_cast<const uint8_t*> (raw);
        if (size < (size_t) frameBytes || osc::detail::readBE32 (d) != reportId)
            return false;

        size_t pos = 4;
        const auto word = [&]
        {
            const auto v = (int16_t) (uint16_t) ((d[pos] << 8) | d[pos + 1]);
            pos += 2;
            return (float) v / 256.0f;
        };

        for (auto& s : state.sources)
        {
            s.inL = word();
            s.inR = word();
            s.outL = word();
            s.outR = word();
            pos += 8; // gate and dynamics words
        }
        const auto mixes = [&] (auto& array)
        {
            for (auto& m : array)
            {
                pos += 4; // inputs
                m.outL = word();
                m.outR = word();
                pos += 8;
            }
        };
        mixes (state.buses);
        mixes (state.mains);
        return true;
    }

    // Undoes the escape framing of bytes received on the meter connection (used by the
    // simulator). Feeds each data byte with its channel.
    struct Demux
    {
        int channel = 0;
        bool pendingEscape = false;

        template <typename Fn>
        void feed (const uint8_t* data, size_t size, Fn&& onByte)
        {
            for (size_t i = 0; i < size; ++i)
            {
                const auto b = data[i];
                if (pendingEscape)
                {
                    pendingEscape = false;
                    if (b == collectionEnd)
                        onByte (channel, escape);
                    else
                        channel = b - selectBase;
                }
                else if (b == escape)
                    pendingEscape = true;
                else
                    onByte (channel, b);
            }
        }
    };
} // namespace meter
} // namespace sr::wing
