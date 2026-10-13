#pragma once

#include "PluginUi.h"
#include "Widgets.h"

namespace sr::ui
{
// Big loudness readout against the stream target, with a reset for integrated loudness.
class LoudnessDisplay : public juce::Component
{
public:
    explicit LoudnessDisplay (LoudnessMeter&);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LoudnessMeter& meter;
    float momentary = LoudnessMeter::silence, shortTerm = LoudnessMeter::silence,
          integrated = LoudnessMeter::silence;
    juce::TextButton reset { "RESET" };
};

// Right-hand column: loudness, master meters/fader, bus processing and master inserts.
class MasterPanel : public juce::Component
{
public:
    static constexpr int preferredWidth = 330;

    MasterPanel (RackProcessor&, PluginWindows&);

    void setLocked (bool);
    void refresh(); // insert buttons
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RackProcessor& rack;
    bool locked = false;
    float limiterReduction = 0.0f, busReduction = 0.0f;

    LoudnessDisplay loudness;
    Meter meterL, meterR;
    juce::Slider fader;
    std::unique_ptr<Apvts::SliderAttachment> faderAttachment;

    Toggle compOn { "BUS COMP" }, limOn { "LIMITER" };
    Knob compThr { "Threshold" }, compRatio { "Ratio" }, compAtk { "Attack" }, compRel { "Release" },
        compMakeup { "Makeup" };
    Knob verbSize { "Size" }, verbDamp { "Damping" }, verbReturn { "Return" };
    Knob ceiling { "Ceiling" }, limRel { "Release" };
    SlotButton slot1, slot2;

    juce::Rectangle<int> compArea, verbArea, limArea, insertArea, grArea;
};
} // namespace sr::ui
