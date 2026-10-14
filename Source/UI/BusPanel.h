#pragma once

#include "Engine/RackProcessor.h"
#include "Widgets.h"

namespace sr::ui
{
// Bus mode main screen: pick the Dante pair carrying the stream bus, trim it, watch it
// come in, and see what the master chain is doing to it.
class BusPanel : public juce::Component
{
public:
    explicit BusPanel (RackProcessor&);

    void setLocked (bool);
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RackProcessor& rack;
    bool locked = false;
    float levelerGain = 0.0f, inPeakDb = -100.0f;

    juce::ComboBox input;
    Knob trim { "Trim" };
    Meter meterL, meterR;
    std::unique_ptr<Apvts::ComboBoxAttachment> inputAttachment;

    juce::Rectangle<int> card, inputArea, inputLabelArea, levelerArea, flowArea;
};
} // namespace sr::ui
