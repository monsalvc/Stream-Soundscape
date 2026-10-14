#pragma once

#include "Engine/RackProcessor.h"
#include "Widgets.h"

namespace sr::ui
{
// Frequency-response view of a channel's high-pass filter and 4-band EQ.
// Drag a point to change its frequency and gain, use the scroll wheel over a mid band
// to change its Q, and double-click a point to reset its gain to 0 dB.
class EqCurve : public juce::Component
{
public:
    explicit EqCurve (RackProcessor&);

    void setStrip (int index);
    void tick(); // repaints when any EQ parameter has changed

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Band
    {
        const char* label;
        const char* freq;
        const char* gain; // nullptr for the high-pass
        const char* q;    // nullptr when the band has no Q control
        juce::Colour colour;
    };

    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f, dbRange = 18.0f;

    juce::Rectangle<float> plotArea() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    juce::Point<float> nodePosition (int band) const;
    int nodeAt (juce::Point<float>) const;

    juce::RangedAudioParameter* param (const char* name) const;
    float value (const char* name) const;
    void setValue (const char* name, float v);
    float responseDb (float freq) const;

    RackProcessor& rack;
    int strip = -1, dragging = -1, hover = -1;
    std::array<Band, 5> bands;
    std::vector<float> lastKey;
};
} // namespace sr::ui
