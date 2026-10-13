#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace sr::ui
{
using Apvts = juce::AudioProcessorValueTreeState;

// Vertical peak meter with a short peak hold. Feed it a linear peak every UI frame.
class Meter : public juce::Component
{
public:
    void setLevel (float linearPeak)
    {
        const float db = juce::Decibels::gainToDecibels (linearPeak, -100.0f);
        level = db > level ? db : juce::jmax (db, level - 1.5f);

        if (db >= holdDb)
        {
            holdDb = db;
            holdFrames = 45;
        }
        else if (--holdFrames <= 0)
        {
            holdDb = juce::jmax (-100.0f, holdDb - 1.0f);
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::background);
        g.fillRoundedRectangle (r, 2.0f);

        const auto yFor = [&] (float db) { return r.getBottom() - r.getHeight() * proportion (db); };

        // Coloured gradient clipped to the current level.
        juce::ColourGradient grad (colours::green, 0, r.getBottom(), colours::red, 0, r.getY(), false);
        grad.addColour (proportion (-18.0f), colours::green);
        grad.addColour (proportion (-6.0f), colours::amber);
        g.setGradientFill (grad);
        const float top = yFor (level);
        g.fillRect (r.withTop (top));

        if (holdDb > minDb)
        {
            g.setColour (holdDb > -0.5f ? colours::red : colours::text);
            g.fillRect (r.getX(), yFor (holdDb) - 1.0f, r.getWidth(), 2.0f);
        }
    }

    static float proportion (float db) { return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); }

private:
    static constexpr float minDb = -60.0f, maxDb = 6.0f;
    float level = -100.0f, holdDb = -100.0f;
    int holdFrames = 0;
};

// Rotary knob with a caption, attached to a parameter.
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        addAndMakeVisible (slider);

        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (11.0f));
        label.setColour (juce::Label::textColourId, colours::dim);
        addAndMakeVisible (label);
    }

    void attach (Apvts& state, const juce::String& paramId)
    {
        attachment.reset();
        attachment = std::make_unique<Apvts::SliderAttachment> (state, paramId, slider);
        slider.setDoubleClickReturnValue (true, state.getParameter (paramId)->convertFrom0to1 (
                                                    state.getParameter (paramId)->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<Apvts::SliderAttachment> attachment;
};

// Latching button attached to a bool parameter.
class Toggle : public juce::TextButton
{
public:
    explicit Toggle (const juce::String& buttonText, juce::Colour onColour = colours::accent) : TextButton (buttonText)
    {
        setClickingTogglesState (true);
        setColour (juce::TextButton::buttonOnColourId, onColour);
    }

    void attach (Apvts& state, const juce::String& paramId)
    {
        attachment.reset();
        attachment = std::make_unique<Apvts::ButtonAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<Apvts::ButtonAttachment> attachment;
};

// Lays knobs out left to right inside an area.
inline void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int itemWidth)
{
    for (auto* c : items)
        c->setBounds (area.removeFromLeft (itemWidth));
}
} // namespace sr::ui
