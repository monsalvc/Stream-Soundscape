#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sr::ui
{
namespace colours
{
    const juce::Colour background { 0xff121418 };
    const juce::Colour panel { 0xff1b1e24 };
    const juce::Colour panelRaised { 0xff252932 };
    const juce::Colour outline { 0xff323743 };
    const juce::Colour text { 0xffe7e9ee };
    const juce::Colour dim { 0xff8b92a3 };
    const juce::Colour accent { 0xff4fa3ff };
    const juce::Colour green { 0xff3ccf6e };
    const juce::Colour amber { 0xffffb02e };
    const juce::Colour red { 0xffff4d4d };
    const juce::Colour mute { 0xffd9443c };
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
        : LookAndFeel_V4 ({ colours::background, colours::panel, colours::panelRaised, colours::outline,
                            colours::text, colours::accent, colours::text, colours::panelRaised, colours::text })
    {
        setColour (juce::Slider::thumbColourId, colours::text);
        setColour (juce::Slider::trackColourId, colours::accent);
        setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, colours::outline);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxTextColourId, colours::dim);
        setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, colours::accent);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, colours::panelRaised);
        setColour (juce::ComboBox::outlineColourId, colours::outline);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::TextEditor::backgroundColourId, colours::background);
        setColour (juce::TextEditor::outlineColourId, colours::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        setColour (juce::AlertWindow::backgroundColourId, colours::panel);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.55f), juce::Font::bold);
    }
};

// Draws a titled group box, used to separate sections in the panels.
inline void drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour (colours::panelRaised.withAlpha (0.5f));
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (title, area.removeFromTop (18).reduced (8, 0), juce::Justification::centredLeft);
}
} // namespace sr::ui
