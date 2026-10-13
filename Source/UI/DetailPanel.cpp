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
    : rack (r), slot1 (r, windows), slot2 (r, windows)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    sections = {
        { "INPUT", { &trim, &hpfOn, &hpfFreq }, {} },
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
}

void DetailPanel::setStrip (int index)
{
    strip = index;
    if (strip < 0)
        return;

    auto& state = rack.state;
    auto id = [this] (const char* n) { return stripId (strip, n); };

    trim.attach (state, id ("trim"));
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
        for (auto* c : s.items)
        {
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

    // Gain reduction meter sits at the right edge of the compressor section.
    const auto& comp = sections[3].area;
    grArea = juce::Rectangle<int> (comp.getRight() - gap - 6, comp.getY() + 22, 6, comp.getHeight() - 30);
}
} // namespace sr::ui
