#pragma once

#include "EqCurve.h"
#include "PluginUi.h"
#include "Widgets.h"

namespace sr::ui
{
// Full processing controls for the selected channel (engineer mode only).
class DetailPanel : public juce::Component
{
public:
    int getPreferredHeight() const { return showCurve ? 260 : 140; }

    // Called when the panel needs a different height (the EQ curve was shown or hidden).
    std::function<void()> onLayoutChanged;

    bool isCurveShown() const noexcept { return showCurve; }
    void setCurveShown (bool);

    DetailPanel (RackProcessor&, PluginWindows&);

    void setStrip (int index);
    int getStrip() const noexcept { return strip; }
    void refresh(); // names and insert buttons
    void tick();

    int getPreferredWidth() const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        std::vector<juce::Component*> items;
        juce::Rectangle<int> area;
    };

    RackProcessor& rack;
    int strip = -1;
    float compReduction = 0.0f;

    juce::Label title;
    Knob trim { "Trim" }, hpfFreq { "HPF" }, gateThr { "Threshold" };
    Knob lowGain { "Low" }, lowFreq { "Low Hz" }, m1Gain { "Lo-Mid" }, m1Freq { "Lo-Mid Hz" }, m1Q { "Q" };
    Knob m2Gain { "Hi-Mid" }, m2Freq { "Hi-Mid Hz" }, m2Q { "Q" }, highGain { "High" }, highFreq { "High Hz" };
    Knob compThr { "Threshold" }, compRatio { "Ratio" }, compAtk { "Attack" }, compRel { "Release" },
        compMakeup { "Makeup" };
    Knob send { "Reverb" };
    Toggle hpfOn { "HPF" }, gateOn { "GATE" }, eqOn { "EQ" }, compOn { "COMP" };
    juce::TextButton curveButton { "CURVE" };
    EqCurve curve;
    bool showCurve = false;
    SlotButton slot1, slot2;
    juce::Rectangle<int> grArea;
    std::vector<Section> sections;
};
} // namespace sr::ui
