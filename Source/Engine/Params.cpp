#include "Params.h"

namespace sr
{
juce::String stripId (int strip, const char* name) { return "s" + juce::String (strip) + "_" + name; }
juce::String masterId (const char* name) { return juce::String ("m_") + name; }

namespace
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String formatHz (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

juce::String formatDb (float v, int)
{
    return v <= offDb ? juce::String ("-inf") : juce::String (v, 1) + " dB";
}

void addFloat (Layout& layout, const juce::String& id, const juce::String& name,
               juce::NormalisableRange<float> range, float def,
               std::function<juce::String (float, int)> toText = {})
{
    auto attrs = juce::AudioParameterFloatAttributes();
    if (toText)
        attrs = attrs.withStringFromValueFunction (std::move (toText));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attrs));
}

void addDb (Layout& layout, const juce::String& id, const juce::String& name, float lo, float hi, float def)
{
    addFloat (layout, id, name, { lo, hi, 0.1f }, def, formatDb);
}

void addHz (Layout& layout, const juce::String& id, const juce::String& name, float lo, float hi, float def)
{
    juce::NormalisableRange<float> range (lo, hi, 1.0f);
    range.setSkewForCentre (std::sqrt (lo * hi));
    addFloat (layout, id, name, range, def, formatHz);
}

void addMs (Layout& layout, const juce::String& id, const juce::String& name, float lo, float hi, float def)
{
    juce::NormalisableRange<float> range (lo, hi, 0.1f);
    range.setSkewForCentre (std::sqrt (lo * hi));
    addFloat (layout, id, name, range, def, [] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; });
}

void addBool (Layout& layout, const juce::String& id, const juce::String& name, bool def)
{
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}

void addFader (Layout& layout, const juce::String& id, const juce::String& name, float def)
{
    juce::NormalisableRange<float> range (offDb, 10.0f, 0.1f);
    range.setSkewForCentre (-12.0f);
    addFloat (layout, id, name, range, def, formatDb);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Layout layout;

    juce::StringArray inputChoices { "--" };
    for (int i = 1; i <= maxInputs; ++i)
        inputChoices.add (juce::String (i));

    for (int s = 0; s < numStrips; ++s)
    {
        const auto n = "Ch " + juce::String (s + 1) + " ";
        auto id = [s] (const char* name) { return stripId (s, name); };

        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("src"), 1 }, n + "Input",
                                                                  inputChoices, s + 1));
        addBool (layout, id ("stereo"), n + "Stereo", false);
        addDb (layout, id ("trim"), n + "Trim", -24.0f, 24.0f, 0.0f);

        addBool (layout, id ("hpfOn"), n + "HPF On", true);
        addHz (layout, id ("hpfFreq"), n + "HPF Freq", 20.0f, 400.0f, 80.0f);

        addBool (layout, id ("gateOn"), n + "Gate On", false);
        addDb (layout, id ("gateThr"), n + "Gate Threshold", -80.0f, 0.0f, -50.0f);

        addBool (layout, id ("eqOn"), n + "EQ On", true);
        addDb (layout, id ("lowGain"), n + "Low Gain", -15.0f, 15.0f, 0.0f);
        addHz (layout, id ("lowFreq"), n + "Low Freq", 40.0f, 400.0f, 100.0f);
        addDb (layout, id ("m1Gain"), n + "Lo-Mid Gain", -15.0f, 15.0f, 0.0f);
        addHz (layout, id ("m1Freq"), n + "Lo-Mid Freq", 100.0f, 2000.0f, 400.0f);
        addFloat (layout, id ("m1Q"), n + "Lo-Mid Q", { 0.3f, 6.0f, 0.01f, 0.5f }, 1.0f);
        addDb (layout, id ("m2Gain"), n + "Hi-Mid Gain", -15.0f, 15.0f, 0.0f);
        addHz (layout, id ("m2Freq"), n + "Hi-Mid Freq", 800.0f, 10000.0f, 3000.0f);
        addFloat (layout, id ("m2Q"), n + "Hi-Mid Q", { 0.3f, 6.0f, 0.01f, 0.5f }, 1.0f);
        addDb (layout, id ("highGain"), n + "High Gain", -15.0f, 15.0f, 0.0f);
        addHz (layout, id ("highFreq"), n + "High Freq", 2000.0f, 16000.0f, 10000.0f);

        addBool (layout, id ("compOn"), n + "Comp On", false);
        addDb (layout, id ("compThr"), n + "Comp Threshold", -50.0f, 0.0f, -18.0f);
        addFloat (layout, id ("compRatio"), n + "Comp Ratio", { 1.0f, 20.0f, 0.1f, 0.4f }, 3.0f,
                  [] (float v, int) { return juce::String (v, 1) + ":1"; });
        addMs (layout, id ("compAtk"), n + "Comp Attack", 0.5f, 100.0f, 10.0f);
        addMs (layout, id ("compRel"), n + "Comp Release", 10.0f, 1000.0f, 120.0f);
        addDb (layout, id ("compMakeup"), n + "Comp Makeup", 0.0f, 24.0f, 0.0f);

        addDb (layout, id ("send"), n + "Reverb Send", offDb, 0.0f, offDb);
        addFloat (layout, id ("pan"), n + "Pan", { -1.0f, 1.0f, 0.01f }, 0.0f);
        addFader (layout, id ("fader"), n + "Fader", offDb);
        addBool (layout, id ("mute"), n + "Mute", false);
    }

    addBool (layout, masterId ("compOn"), "Bus Comp On", false);
    addDb (layout, masterId ("compThr"), "Bus Comp Threshold", -40.0f, 0.0f, -12.0f);
    addFloat (layout, masterId ("compRatio"), "Bus Comp Ratio", { 1.0f, 10.0f, 0.1f, 0.5f }, 2.0f,
              [] (float v, int) { return juce::String (v, 1) + ":1"; });
    addMs (layout, masterId ("compAtk"), "Bus Comp Attack", 0.5f, 100.0f, 20.0f);
    addMs (layout, masterId ("compRel"), "Bus Comp Release", 20.0f, 1000.0f, 200.0f);
    addDb (layout, masterId ("compMakeup"), "Bus Comp Makeup", 0.0f, 12.0f, 0.0f);

    addFloat (layout, masterId ("verbSize"), "Reverb Size", { 0.0f, 1.0f, 0.01f }, 0.5f);
    addFloat (layout, masterId ("verbDamp"), "Reverb Damping", { 0.0f, 1.0f, 0.01f }, 0.5f);
    addDb (layout, masterId ("verbReturn"), "Reverb Return", offDb, 6.0f, 0.0f);

    addBool (layout, masterId ("limOn"), "Limiter On", true);
    addDb (layout, masterId ("ceiling"), "Limiter Ceiling", -6.0f, 0.0f, -1.0f);
    addMs (layout, masterId ("limRel"), "Limiter Release", 10.0f, 500.0f, 80.0f);

    addFader (layout, masterId ("fader"), "Master Fader", 0.0f);

    return layout;
}
} // namespace sr
