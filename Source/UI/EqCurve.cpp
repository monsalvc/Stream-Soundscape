#include "EqCurve.h"
#include <complex>

namespace sr::ui
{
namespace
{
    // Magnitude response of one biquad, in dB, at the given frequency.
    float magnitudeDb (const BiquadCoeffs& c, double freq, double fs)
    {
        const double w = juce::MathConstants<double>::twoPi * freq / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto num = (double) c.b0 + (double) c.b1 * z1 + (double) c.b2 * z2;
        const auto den = 1.0 + (double) c.a1 * z1 + (double) c.a2 * z2;
        return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (num) / std::abs (den))));
    }
} // namespace

EqCurve::EqCurve (RackProcessor& r)
    : rack (r),
      bands { { { "High-pass", "hpfFreq", nullptr, nullptr, colours::dim },
                { "Low", "lowFreq", "lowGain", nullptr, colours::peach },
                { "Lo-Mid", "m1Freq", "m1Gain", "m1Q", colours::accent },
                { "Hi-Mid", "m2Freq", "m2Gain", "m2Q", colours::green },
                { "High", "highFreq", "highGain", nullptr, colours::text } } }
{
    setRepaintsOnMouseActivity (false);
}

void EqCurve::setStrip (int index)
{
    strip = index;
    lastKey.clear();
    repaint();
}

juce::RangedAudioParameter* EqCurve::param (const char* name) const
{
    return rack.state.getParameter (stripId (strip, name));
}

float EqCurve::value (const char* name) const
{
    return rack.state.getRawParameterValue (stripId (strip, name))->load();
}

void EqCurve::setValue (const char* name, float v)
{
    if (auto* p = param (name))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void EqCurve::tick()
{
    if (strip < 0 || ! isShowing())
        return;

    std::vector<float> key;
    for (const auto* name : { "hpfOn", "eqOn", "hpfFreq", "lowFreq", "lowGain", "m1Freq", "m1Gain", "m1Q", "m2Freq",
                              "m2Gain", "m2Q", "highFreq", "highGain" })
        key.push_back (value (name));

    if (key != lastKey)
    {
        lastKey = std::move (key);
        repaint();
    }
}

//==============================================================================
juce::Rectangle<float> EqCurve::plotArea() const
{
    return getLocalBounds().toFloat().reduced (4.0f).withTrimmedLeft (26.0f).withTrimmedBottom (14.0f);
}

float EqCurve::xForFreq (float f) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (f / minFreq) / std::log (maxFreq / minFreq);
}

float EqCurve::freqForX (float x) const
{
    const auto a = plotArea();
    return minFreq * std::pow (maxFreq / minFreq, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float EqCurve::yForDb (float db) const
{
    const auto a = plotArea();
    return a.getCentreY() - (db / dbRange) * a.getHeight() * 0.5f;
}

float EqCurve::dbForY (float y) const
{
    const auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * dbRange;
}

juce::Point<float> EqCurve::nodePosition (int band) const
{
    const auto& b = bands[(size_t) band];
    // The high-pass point sits at its -3 dB corner.
    const float db = b.gain != nullptr ? value (b.gain) : -3.0f;
    return { xForFreq (value (b.freq)), yForDb (db) };
}

int EqCurve::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 14.0f;
    for (int i = 0; i < (int) bands.size(); ++i)
        if (const float d = nodePosition (i).getDistanceFrom (p); d < bestDist)
        {
            best = i;
            bestDist = d;
        }
    return best;
}

float EqCurve::responseDb (float freq) const
{
    double fs = rack.getSampleRate();
    if (fs <= 0)
        fs = 48000.0;

    float db = 0.0f;
    if (value ("hpfOn") > 0.5f)
    {
        db += magnitudeDb (rbj::highPass (fs, value ("hpfFreq"), 0.5412), freq, fs);
        db += magnitudeDb (rbj::highPass (fs, value ("hpfFreq"), 1.3066), freq, fs);
    }
    if (value ("eqOn") > 0.5f)
    {
        db += magnitudeDb (rbj::lowShelf (fs, value ("lowFreq"), value ("lowGain")), freq, fs);
        db += magnitudeDb (rbj::peak (fs, value ("m1Freq"), value ("m1Q"), value ("m1Gain")), freq, fs);
        db += magnitudeDb (rbj::peak (fs, value ("m2Freq"), value ("m2Q"), value ("m2Gain")), freq, fs);
        db += magnitudeDb (rbj::highShelf (fs, value ("highFreq"), value ("highGain")), freq, fs);
    }
    return db;
}

//==============================================================================
void EqCurve::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    g.setColour (colours::background);
    g.fillRoundedRectangle (area, 4.0f);

    if (strip < 0)
        return;

    // Grid.
    g.setFont (juce::FontOptions (10.0f));
    for (const float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (colours::dim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 16.0f, area.getBottom() + 1.0f, 32.0f, 12.0f),
                    juce::Justification::centred);
    }
    for (const float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = yForDb (db);
        g.setColour (juce::exactlyEqual (db, 0.0f) ? colours::dim.withAlpha (0.7f) : colours::outline.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        g.setColour (colours::dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, 26.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    // Combined response curve.
    const bool active = value ("eqOn") > 0.5f || value ("hpfOn") > 0.5f;
    juce::Path curve;
    const int steps = juce::jmax (64, (int) area.getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) steps;
        const float y = juce::jlimit (area.getY(), area.getBottom(), yForDb (responseDb (freqForX (x))));
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    juce::Path fill (curve);
    fill.lineTo (area.getRight(), yForDb (0.0f));
    fill.lineTo (area.getX(), yForDb (0.0f));
    fill.closeSubPath();

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setColour (colours::peach.withAlpha (active ? 0.18f : 0.06f));
    g.fillPath (fill);
    g.setColour (active ? colours::accent : colours::dim);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
    g.restoreState();

    // Band points.
    for (int i = 0; i < (int) bands.size(); ++i)
    {
        const auto& b = bands[(size_t) i];
        const bool enabled = (b.gain == nullptr ? value ("hpfOn") : value ("eqOn")) > 0.5f;
        const auto p = nodePosition (i);
        const float radius = (i == hover || i == dragging) ? 7.0f : 5.5f;
        g.setColour (enabled ? b.colour : colours::dim.withAlpha (0.5f));
        g.fillEllipse (p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (colours::background);
        g.drawEllipse (p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);
    }

    // Readout for the point under the mouse.
    if (const int i = dragging >= 0 ? dragging : hover; i >= 0)
    {
        const auto& b = bands[(size_t) i];
        juce::String text = juce::String (b.label) + "   " + param (b.freq)->getCurrentValueAsText();
        if (b.gain != nullptr)
            text << "   " << param (b.gain)->getCurrentValueAsText();
        if (b.q != nullptr)
            text << "   Q " << juce::String (value (b.q), 2) << "  (scroll)";
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (text, area.reduced (8.0f, 4.0f).removeFromTop (16.0f), juce::Justification::topLeft);
    }
}

//==============================================================================
void EqCurve::mouseMove (const juce::MouseEvent& e)
{
    if (const int n = nodeAt (e.position); n != hover)
    {
        hover = n;
        setMouseCursor (hover >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqCurve::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void EqCurve::mouseDown (const juce::MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (dragging < 0)
        return;

    const auto& b = bands[(size_t) dragging];
    param (b.freq)->beginChangeGesture();
    if (b.gain != nullptr)
        param (b.gain)->beginChangeGesture();
}

void EqCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;

    const auto& b = bands[(size_t) dragging];
    setValue (b.freq, freqForX (e.position.x));
    if (b.gain != nullptr)
        setValue (b.gain, dbForY (e.position.y));
    repaint();
}

void EqCurve::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;

    const auto& b = bands[(size_t) dragging];
    param (b.freq)->endChangeGesture();
    if (b.gain != nullptr)
        param (b.gain)->endChangeGesture();
    dragging = -1;
    repaint();
}

void EqCurve::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int n = nodeAt (e.position); n >= 0 && bands[(size_t) n].gain != nullptr)
        setValue (bands[(size_t) n].gain, 0.0f);
}

void EqCurve::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int n = nodeAt (e.position);
    if (n < 0 || bands[(size_t) n].q == nullptr)
    {
        Component::mouseWheelMove (e, wheel); // let the panel scroll
        return;
    }

    const char* q = bands[(size_t) n].q;
    setValue (q, value (q) * std::exp (wheel.deltaY * 1.5f));
}
} // namespace sr::ui
