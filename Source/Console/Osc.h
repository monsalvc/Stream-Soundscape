#pragma once

#include <juce_core/juce_core.h>

namespace sr::osc
{
// Just enough OSC for talking to a console: single messages (no bundles) with int, float
// and string arguments, big-endian, padded to 4 bytes.
struct Arg
{
    enum class Type { Int, Float, String };

    Type type = Type::Int;
    int i = 0;
    float f = 0.0f;
    juce::String s;

    static Arg ofInt (int v) { Arg a; a.type = Type::Int; a.i = v; return a; }
    static Arg ofFloat (float v) { Arg a; a.type = Type::Float; a.f = v; return a; }
    static Arg ofString (const juce::String& v) { Arg a; a.type = Type::String; a.s = v; return a; }

    // A number from whichever kind the argument is ("-oo" reads as -144).
    float asFloat() const
    {
        switch (type)
        {
            case Type::Int: return (float) i;
            case Type::Float: return f;
            case Type::String: return s.trim() == "-oo" ? -144.0f : s.getFloatValue();
        }
        return 0.0f;
    }

    juce::String asString() const
    {
        switch (type)
        {
            case Type::Int: return juce::String (i);
            case Type::Float: return juce::String (f);
            case Type::String: return s;
        }
        return {};
    }
};

struct Message
{
    juce::String address;
    std::vector<Arg> args;
};

namespace detail
{
    inline void pad (juce::MemoryOutputStream& out)
    {
        while (out.getDataSize() % 4 != 0)
            out.writeByte (0);
    }

    inline void writeString (juce::MemoryOutputStream& out, const juce::String& s)
    {
        out.write (s.toRawUTF8(), s.getNumBytesAsUTF8());
        out.writeByte (0);
        pad (out);
    }

    inline bool readString (const uint8_t* data, size_t size, size_t& pos, juce::String& result)
    {
        size_t end = pos;
        while (end < size && data[end] != 0)
            ++end;
        if (end >= size)
            return false;
        result = juce::String::fromUTF8 (reinterpret_cast<const char*> (data + pos), (int) (end - pos));
        pos = (end + 4) & ~(size_t) 3;
        return pos <= size;
    }

    inline uint32_t readBE32 (const uint8_t* p)
    {
        return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
    }
} // namespace detail

inline juce::MemoryBlock encode (const Message& m)
{
    juce::MemoryOutputStream out;
    detail::writeString (out, m.address);

    juce::String tags (",");
    for (const auto& a : m.args)
        tags << (a.type == Arg::Type::Int ? "i" : (a.type == Arg::Type::Float ? "f" : "s"));
    detail::writeString (out, tags);

    for (const auto& a : m.args)
    {
        if (a.type == Arg::Type::Int)
            out.writeIntBigEndian (a.i);
        else if (a.type == Arg::Type::Float)
            out.writeFloatBigEndian (a.f);
        else
            detail::writeString (out, a.s);
    }
    return out.getMemoryBlock();
}

// Returns false for anything that isn't a well-formed message (bundles included).
inline bool decode (const void* raw, size_t size, Message& m)
{
    const auto* data = static_cast<const uint8_t*> (raw);
    size_t pos = 0;
    m = {};
    if (size < 4 || data[0] != '/' || ! detail::readString (data, size, pos, m.address))
        return false;

    juce::String tags;
    if (pos >= size)
        return true; // no type tag string: no arguments
    if (! detail::readString (data, size, pos, tags) || ! tags.startsWith (","))
        return false;

    for (int t = 1; t < tags.length(); ++t)
    {
        const auto tag = tags[t];
        if (tag == 'i' || tag == 'f')
        {
            if (pos + 4 > size)
                return false;
            const auto bits = detail::readBE32 (data + pos);
            pos += 4;
            if (tag == 'i')
                m.args.push_back (Arg::ofInt ((int) bits));
            else
            {
                float f;
                std::memcpy (&f, &bits, sizeof (f));
                m.args.push_back (Arg::ofFloat (f));
            }
        }
        else if (tag == 's')
        {
            juce::String s;
            if (! detail::readString (data, size, pos, s))
                return false;
            m.args.push_back (Arg::ofString (s));
        }
        else if (tag == 'b')
        {
            if (pos + 4 > size)
                return false;
            const auto len = (size_t) detail::readBE32 (data + pos);
            pos = (pos + 4 + len + 3) & ~(size_t) 3;
            if (pos > size)
                return false;
        }
        else
        {
            return false; // a type we don't use
        }
    }
    return true;
}
} // namespace sr::osc
