#include "DetailPanel.h"

namespace sr::ui
{
namespace
{
    constexpr int knobWidth = 64;
    constexpr int toggleWidth = 54;
    constexpr int slotWidth = 150;
    constexpr int gap = 8;
} // namespace

DetailPanel::DetailPanel (RackProcessor& r, PluginWindows& windows)
    : rack (r), curve (r), slot1 (r, windows), slot2 (r, windows)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    sections = {
        { "INPUT", { &trim, &hpfOn, &hpfFreq }, {} },
        { "PITCH", { &tuneOn, &tuneAmount, &tuneSpeed }, {} },
        { "GATE", { &gateOn, &gateThr }, {} },
        { "EQ", { &eqOn, &lowGain, &lowFreq, &m1Gain, &m1Freq, &m1Q, &m2Gain, &m2Freq, &m2Q, &highGain, &highFreq }, {} },
        { "COMPRESSOR", { &compOn, &compThr, &compRatio, &compAtk, &compRel, &compMakeup }, {} },
        { "SEND", { &send }, {} },
        { "INSERTS", { &slot1 }, {} },
    };

    for (auto& s : sections)
        for (auto* c : s.items)
            addAndMakeVisible (c);
    addAndMakeVisible (slot2);

    tuneOn.onClick = [this] { repaint (tuneReadout); };
    tuneOn.setTooltip ("Pitch correction for a single voice: nudges each sung note toward the nearest semitone");
    tuneAmount.slider.setTooltip ("How far toward the note (100% = fully in tune; 50-80% sounds most natural)");
    tuneSpeed.slider.setTooltip ("How quickly it corrects (slower keeps natural slides and vibrato)");

    curveButton.setClickingTogglesState (true);
    curveButton.setTooltip ("Show the EQ as a graph you can drag");
    curveButton.onClick = [this] { setCurveShown (curveButton.getToggleState()); };
    addAndMakeVisible (curveButton);
    addChildComponent (curve);
}

void DetailPanel::setCurveShown (bool shouldShow)
{
    showCurve = shouldShow;
    curveButton.setToggleState (showCurve, juce::dontSendNotification);
    curve.setVisible (showCurve);
    for (auto* k : std::initializer_list<juce::Component*> { &lowGain, &lowFreq, &m1Gain, &m1Freq, &m1Q, &m2Gain,
                                                             &m2Freq, &m2Q, &highGain, &highFreq })
        k->setVisible (! showCurve);
    resized();
    if (onLayoutChanged)
        onLayoutChanged();
}

void DetailPanel::setStrip (int index)
{
    strip = index;
    if (strip < 0)
        return;

    auto& state = rack.state;
    auto id = [this] (const char* n) { return stripId (strip, n); };

    trim.attach (state, id ("trim"));
    tuneOn.attach (state, id ("tuneOn"));
    tuneAmount.attach (state, id ("tuneAmount"));
    tuneSpeed.attach (state, id ("tuneSpeed"));
    hpfOn.attach (state, id ("hpfOn"));
    hpfFreq.attach (state, id ("hpfFreq"));
    gateOn.attach (state, id ("gateOn"));
    gateThr.attach (state, id ("gateThr"));
    eqOn.attach (state, id ("eqOn"));
    lowGain.attach (state, id ("lowGain"));
    lowFreq.attach (state, id ("lowFreq"));
    m1Gain.attach (state, id ("m1Gain"));
    m1Freq.attach (state, id ("m1Freq"));
    m1Q.attach (state, id ("m1Q"));
    m2Gain.attach (state, id ("m2Gain"));
    m2Freq.attach (state, id ("m2Freq"));
    m2Q.attach (state, id ("m2Q"));
    highGain.attach (state, id ("highGain"));
    highFreq.attach (state, id ("highFreq"));
    compOn.attach (state, id ("compOn"));
    compThr.attach (state, id ("compThr"));
    compRatio.attach (state, id ("compRatio"));
    compAtk.attach (state, id ("compAtk"));
    compRel.attach (state, id ("compRel"));
    compMakeup.attach (state, id ("compMakeup"));
    send.attach (state, id ("send"));

    curve.setStrip (strip);
    slot1.setSlot (&rack.getStrip (strip).getSlot (0));
    slot2.setSlot (&rack.getStrip (strip).getSlot (1));
    refresh();
}

void DetailPanel::refresh()
{
    if (strip < 0)
        return;
    title.setText (juce::String (strip + 1) + "  " + rack.getStripName (strip), juce::dontSendNotification);
    slot1.refresh();
    slot2.refresh();
}

void DetailPanel::tick()
{
    if (strip < 0)
        return;
    curve.tick();

    const auto& tuner = rack.getStrip (strip).getTuner();
    const float midi = tuner.detectedMidi.load(), cents = tuner.correctionCents.load();
    if (std::abs (midi - tuneMidi) > 0.02f || std::abs (cents - tuneCents) > 0.5f)
    {
        tuneMidi = midi;
        tuneCents = cents;
        repaint (tuneReadout);
    }
    const float gr = rack.getStrip (strip).compReductionDb.load();
    if (std::abs (gr - compReduction) > 0.2f)
    {
        compReduction = gr;
        repaint (grArea);
    }
}

int DetailPanel::getPreferredWidth() const
{
    int w = 160 + gap;
    for (auto& s : sections)
    {
        int sw = 2 * gap;
        for (auto* c : s.items)
            sw += dynamic_cast<const Toggle*> (c) != nullptr ? toggleWidth
                  : dynamic_cast<const SlotButton*> (c) != nullptr ? slotWidth
                                                                   : knobWidth;
        w += sw + gap;
    }
    return w;
}

void DetailPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    for (auto& s : sections)
        drawSection (g, s.area, s.title);

    // Pitch readout in the PITCH header: the note being sung and the correction applied.
    if (! tuneReadout.isEmpty() && tuneOn.getToggleState())
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        juce::String text ("--");
        if (tuneMidi > 0.0f)
        {
            const int note = juce::roundToInt (tuneMidi);
            const int off = juce::roundToInt ((tuneMidi - (float) note) * 100.0f);
            text = juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1) + " "
                   + (off >= 0 ? "+" : "") + juce::String (off) + "c";
        }
        const bool correcting = std::abs (tuneCents) > 3.0f;
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (correcting ? colours::amber : colours::green);
        g.drawText (text, tuneReadout, juce::Justification::centredRight);
    }

    if (! grArea.isEmpty())
    {
        g.setColour (colours::background);
        g.fillRect (grArea);
        g.setColour (colours::amber);
        const float frac = juce::jlimit (0.0f, 1.0f, -compReduction / 20.0f);
        g.fillRect (grArea.toFloat().withTrimmedTop ((float) grArea.getHeight() * (1.0f - frac)));
    }
}

void DetailPanel::resized()
{
    auto r = getLocalBounds().reduced (gap);
    title.setBounds (r.removeFromLeft (160));
    r.removeFromLeft (gap);

    for (auto& s : sections)
    {
        int sw = 2 * gap;
        for (auto* c : s.items)
            sw += dynamic_cast<Toggle*> (c) != nullptr ? toggleWidth
                  : dynamic_cast<SlotButton*> (c) != nullptr ? slotWidth
                                                             : knobWidth;
        s.area = r.removeFromLeft (sw);
        r.removeFromLeft (gap);

        auto inner = s.area.reduced (gap, 0).withTrimmedTop (20).withTrimmedBottom (4);

        if (&s == &sections[3])
        {
            // EQ: on/off and CURVE stacked in the first column, then knobs or the curve.
            auto column = inner.removeFromLeft (toggleWidth).withHeight (100);
            eqOn.setBounds (column.withSizeKeepingCentre (toggleWidth - 6, 26).translated (0, -16));
            curveButton.setBounds (column.withSizeKeepingCentre (toggleWidth - 6, 22).translated (0, 16));
            if (showCurve)
            {
                curve.setBounds (inner.reduced (2, 0));
                continue;
            }
        }

        inner = inner.withHeight (juce::jmin (inner.getHeight(), 104));
        for (auto* c : s.items)
        {
            if (c == &eqOn)
                continue;
            if (dynamic_cast<Toggle*> (c) != nullptr)
                c->setBounds (inner.removeFromLeft (toggleWidth).withSizeKeepingCentre (toggleWidth - 6, 26));
            else if (c == &slot1)
            {
                // Both insert slots share one column.
                auto column = inner.removeFromLeft (slotWidth).reduced (4, 4);
                slot1.setBounds (column.removeFromTop (column.getHeight() / 2).reduced (0, 4));
                slot2.setBounds (column.reduced (0, 4));
            }
            else
                c->setBounds (inner.removeFromLeft (knobWidth));
        }
    }

    tuneReadout = sections[1].area.withHeight (18).reduced (8, 0).withTrimmedLeft (40);

    // Gain reduction meter sits at the right edge of the compressor section.
    const auto& comp = sections[4].area;
    grArea = juce::Rectangle<int> (comp.getRight() - gap - 6, comp.getY() + 22, 6, juce::jmin (comp.getHeight() - 30, 94));
}
} // namespace sr::ui
