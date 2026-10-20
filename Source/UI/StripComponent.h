#pragma once

#include "Engine/RackProcessor.h"
#include "Widgets.h"

namespace sr::ui
{
// One vertical channel strip on the main screen: name, input, pan, fader, meter, mute.
class StripComponent : public juce::Component
{
public:
    static constexpr int preferredWidth = 84;
    static constexpr int minimumHeight = 200; // below this the mixer area scrolls vertically

    StripComponent (RackProcessor&, int index, std::function<void (int)> onEditClicked);

    void setLocked (bool);
    void setSelected (bool);
    void refreshName();
    void tick(); // called by the UI timer to update meters

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RackProcessor& rack;
    const int index;
    bool locked = false, selected = false;
    float compReduction = 0.0f;
    bool gateOpen = true;

    juce::Label name;
    juce::ComboBox input;
    Toggle stereo { "ST" };
    juce::TextButton edit { "EDIT" };
    juce::Slider pan, fader;
    Toggle mute { "MUTE", colours::mute };
    Meter meter;
    juce::Rectangle<int> grArea;

    std::unique_ptr<Apvts::ComboBoxAttachment> inputAttachment;
    std::unique_ptr<Apvts::SliderAttachment> panAttachment, faderAttachment;
};
} // namespace sr::ui
