#include "StripComponent.h"

namespace sr::ui
{
StripComponent::StripComponent (RackProcessor& r, int i, std::function<void (int)> onEditClicked)
    : rack (r), index (i)
{
    auto& state = rack.state;

    name.setJustificationType (juce::Justification::centred);
    name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this] { rack.setStripName (index, name.getText()); };
    addAndMakeVisible (name);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (stripId (index, "src"))))
        input.addItemList (p->choices, 1);
    input.setTooltip ("Dante input channel");
    inputAttachment = std::make_unique<Apvts::ComboBoxAttachment> (state, stripId (index, "src"), input);
    addAndMakeVisible (input);

    stereo.setTooltip ("Stereo: use this input and the next one as L/R");
    stereo.attach (state, stripId (index, "stereo"));
    addAndMakeVisible (stereo);

    edit.setClickingTogglesState (false);
    edit.onClick = [this, onEditClicked] { onEditClicked (index); };
    addAndMakeVisible (edit);

    pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    pan.setTooltip ("Pan");
    panAttachment = std::make_unique<Apvts::SliderAttachment> (state, stripId (index, "pan"), pan);
    pan.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (pan);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, preferredWidth - 8, 18);
    fader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    faderAttachment = std::make_unique<Apvts::SliderAttachment> (state, stripId (index, "fader"), fader);
    fader.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fader);

    mute.attach (state, stripId (index, "mute"));
    addAndMakeVisible (mute);

    addAndMakeVisible (meter);
    refreshName();
    setLocked (false);
}

void StripComponent::setLocked (bool shouldLock)
{
    locked = shouldLock;
    name.setEditable (false, ! locked, false);
    for (auto* c : std::initializer_list<juce::Component*> { &input, &stereo, &edit, &pan })
        c->setVisible (! locked);
    resized();
    repaint();
}

void StripComponent::setSelected (bool shouldSelect)
{
    selected = shouldSelect;
    edit.setToggleState (selected, juce::dontSendNotification);
    edit.setColour (juce::TextButton::buttonColourId, selected ? colours::accent : colours::panelRaised);
    repaint();
}

void StripComponent::refreshName()
{
    name.setText (rack.getStripName (index), juce::dontSendNotification);
}

void StripComponent::tick()
{
    auto& strip = rack.getStrip (index);
    meter.setLevel (strip.peak.exchange (0.0f));

    const float gr = strip.compReductionDb.load();
    const bool open = strip.gateOpen.load();
    if (std::abs (gr - compReduction) > 0.2f || open != gateOpen)
    {
        compReduction = gr;
        gateOpen = open;
        repaint (grArea);
    }
}

void StripComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    g.setColour (selected ? colours::panelRaised.brighter (0.08f) : colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    if (selected)
    {
        g.setColour (colours::accent);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.5f);
    }

    if (! grArea.isEmpty())
    {
        // Gain reduction bar (amber) plus a gate indicator dot.
        auto bar = grArea.toFloat();
        auto dot = bar.removeFromRight (10.0f).withSizeKeepingCentre (6.0f, 6.0f);
        g.setColour (colours::background);
        g.fillRect (bar);
        g.setColour (colours::amber);
        g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, -compReduction / 20.0f)));
        g.setColour (gateOpen ? colours::green : colours::background.brighter (0.2f));
        g.fillEllipse (dot);
    }
}

void StripComponent::resized()
{
    auto r = getLocalBounds().reduced (6, 6);

    name.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    if (! locked)
    {
        input.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto row = r.removeFromTop (22);
        stereo.setBounds (row.removeFromLeft (28));
        row.removeFromLeft (4);
        edit.setBounds (row);
        r.removeFromTop (6);
        grArea = r.removeFromTop (6);
        r.removeFromTop (4);
        pan.setBounds (r.removeFromTop (40));
    }
    else
    {
        grArea = r.removeFromTop (6);
    }

    mute.setBounds (r.removeFromBottom (26));
    r.removeFromBottom (4);

    auto meterArea = r.removeFromRight (10);
    meter.setBounds (meterArea.withTrimmedBottom (22));
    fader.setBounds (r);
}
} // namespace sr::ui
