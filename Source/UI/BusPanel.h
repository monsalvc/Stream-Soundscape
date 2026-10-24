#pragma once

#include "Engine/RackProcessor.h"
#include "Widgets.h"

namespace sr::ui
{
// One-click source button for a named Dante pair, with a dot that lights when it has signal.
class PairButton : public juce::Button
{
public:
    explicit PairButton (int pairIndex) : juce::Button ({}), pair (pairIndex) {}

    void setActivity (float peakDb);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    const int pair;

private:
    float activityDb = -100.0f;
};

// Bus mode main screen: pick the Dante pair carrying the stream bus, trim it, watch it
// come in, and see what the master chain is doing to it. Naming the pairs that carry
// buses gives one-click buttons for switching between them.
class BusPanel : public juce::Component
{
public:
    explicit BusPanel (RackProcessor&);

    void setLocked (bool);
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int selectedPair() const;
    void refreshPairNames();
    void renameCurrentPair();
    void layoutPicks();

    RackProcessor& rack;
    bool locked = false;
    float levelerGain = 0.0f, inPeakDb = -100.0f;

    juce::ComboBox input;
    juce::TextButton renameButton { "Name..." }, fixButton;
    juce::ComboBox channelsBox;
    std::unique_ptr<Apvts::ComboBoxAttachment> channelsAttachment;
    int oneSideFrames = 0, silentSide = -1; // which side has been silent while the other has signal
    juce::OwnedArray<PairButton> picks;
    juce::StringArray pairNames;
    Knob trim { "Trim" };
    Meter meterL, meterR;
    std::unique_ptr<Apvts::ComboBoxAttachment> inputAttachment;

    juce::Rectangle<int> card, inputArea, inputLabelArea, levelerArea, flowArea, picksArea;
};
} // namespace sr::ui
