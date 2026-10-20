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

// The scrollable list of master-chain controls (leveler, bus compressor, reverb bus,
// limiter, inserts). The reverb section only applies in Advanced mode.
class MasterControls : public juce::Component
{
public:
    MasterControls (RackProcessor&, PluginWindows&);

    void setBusMode (bool);
    void refresh();
    void tick();

    // Stacked layout (right-hand column): lays out for a width, returns the height needed.
    int layout (int width);
    // Row layout (strip along the bottom of a narrow window): lays out for a height,
    // returns the width needed.
    int layoutAsRow (int height);
    void setRowLayout (bool shouldUseRows) { rows = shouldUseRows; }

    void paint (juce::Graphics&) override;
    void resized() override
    {
        if (rows)
            layoutAsRow (getHeight());
        else
            layout (getWidth());
    }

private:
    RackProcessor& rack;
    bool busMode = true, rows = false;
    float levelerGain = 0.0f;

    Toggle levOn { "LEVELER" }, compOn { "BUS COMP" }, limOn { "LIMITER" };
    Knob levTarget { "Target" }, levRange { "Range" }, levSpeed { "Speed" };
    Knob compThr { "Threshold" }, compRatio { "Ratio" }, compAtk { "Attack" }, compRel { "Release" },
        compMakeup { "Makeup" };
    Knob verbSize { "Size" }, verbDamp { "Damping" }, verbReturn { "Return" };
    Knob ceiling { "Ceiling" }, limRel { "Release" };
    SlotButton verbSlot;
    juce::OwnedArray<SlotButton> inserts;

    juce::Rectangle<int> levArea, levMeterArea, compArea, verbArea, limArea, insertArea;
};

// Right-hand column: loudness, master meters/fader, and the master-chain controls.
class MasterPanel : public juce::Component
{
public:
    static constexpr int preferredWidth = 370;       // as a right-hand column
    static constexpr int minimumColumnHeight = 800; // below this it goes along the bottom
    static constexpr int preferredHeight = 170; // as a strip along the bottom

    MasterPanel (RackProcessor&, PluginWindows&);

    void setLocked (bool);
    void setBusMode (bool);
    void setAlongBottom (bool); // true: a horizontal strip under the mixer (narrow windows)
    void refresh(); // insert buttons
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RackProcessor& rack;
    bool locked = false, alongBottom = false;
    float limiterReduction = 0.0f, busReduction = 0.0f, levelerGain = 0.0f;

    LoudnessDisplay loudness;
    Meter meterL, meterR;
    juce::Slider fader;
    std::unique_ptr<Apvts::SliderAttachment> faderAttachment;

    juce::Viewport viewport;
    MasterControls controls;
    juce::Rectangle<int> grArea;
};
} // namespace sr::ui
