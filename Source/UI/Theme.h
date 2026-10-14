#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sr::ui
{
namespace colours
{
    // "Poppy field" palette: deep forest greens, sage, mist and poppy orange.
    const juce::Colour background { 0xff0e1d17 };  // darker than forest, for the window
    const juce::Colour panel { 0xff142920 };       // forest
    const juce::Colour panelRaised { 0xff1f4035 }; // pine
    const juce::Colour outline { 0xff2f5648 };
    const juce::Colour text { 0xffcddbde };        // mist
    const juce::Colour dim { 0xff86a19c };         // sage, lifted for readability on dark green
    const juce::Colour accent { 0xfff5751c };      // poppy orange
    const juce::Colour onAccent { 0xff142920 };    // text on orange
    const juce::Colour sage { 0xff64817d };        // fader tracks
    const juce::Colour peach { 0xfffca26e };       // knob arcs
    const juce::Colour green { 0xff9cc9b4 };       // "good": light sage-green (on target, signal OK)
    const juce::Colour amber { 0xfffca26e };       // peach: getting hot / working
    const juce::Colour red { 0xffe8503a };         // warm red: too hot
    const juce::Colour mute { 0xffe8503a };
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
        : LookAndFeel_V4 ({ colours::background, colours::panel, colours::panelRaised, colours::outline,
                            colours::text, colours::accent, colours::text, colours::panelRaised, colours::text })
    {
        setColour (juce::Slider::thumbColourId, colours::text);
        setColour (juce::Slider::trackColourId, colours::sage);
        setColour (juce::Slider::rotarySliderFillColourId, colours::peach);
        setColour (juce::Slider::rotarySliderOutlineColourId, colours::outline);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxTextColourId, colours::dim);
        setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, colours::accent);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, colours::onAccent);
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
