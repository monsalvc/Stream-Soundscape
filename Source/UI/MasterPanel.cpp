#include "MasterPanel.h"

namespace sr::ui
{
LoudnessDisplay::LoudnessDisplay (LoudnessMeter& m) : meter (m)
{
    reset.setTooltip ("Restart the integrated (whole-service) measurement");
    reset.onClick = [this] { meter.requestReset(); };
    addAndMakeVisible (reset);
}

void LoudnessDisplay::tick()
{
    momentary = meter.momentary.load();
    shortTerm = meter.shortTerm.load();
    integrated = meter.integrated.load();
    repaint();
}

void LoudnessDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colours::panelRaised.withAlpha (0.5f));
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    r.reduce (10, 6);

    const auto fmt = [] (float v) { return v <= -70.0f ? juce::String ("--.-") : juce::String (v, 1); };
    const auto colourFor = [] (float v)
    {
        if (v <= -70.0f)
            return colours::dim;
        const float diff = v - loudnessTarget;
        return diff > 2.0f ? colours::red : (diff < -4.0f ? colours::amber : colours::green);
    };

    auto header = r.removeFromTop (16);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SHORT-TERM LOUDNESS", header, juce::Justification::centredLeft);
    g.drawText ("target " + juce::String (loudnessTarget, 0) + " LUFS", header, juce::Justification::centredRight);

    auto big = r.removeFromTop (52);
    g.setColour (colourFor (shortTerm));
    g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
    g.drawText (fmt (shortTerm), big.removeFromLeft (120), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (colours::dim);
    g.drawText ("LUFS", big, juce::Justification::centredLeft);

    // Horizontal scale -36 .. -6 LUFS with the target marked.
    auto scale = r.removeFromTop (10).toFloat();
    const auto xFor = [&] (float v) { return scale.getX() + scale.getWidth() * juce::jlimit (0.0f, 1.0f, (v + 36.0f) / 30.0f); };
    g.setColour (colours::background);
    g.fillRoundedRectangle (scale, 3.0f);
    g.setColour (colourFor (shortTerm));
    g.fillRoundedRectangle (scale.withRight (xFor (shortTerm)), 3.0f);
    g.setColour (colours::text);
    g.fillRect (xFor (loudnessTarget) - 1.0f, scale.getY() - 3.0f, 2.0f, scale.getHeight() + 6.0f);

    r.removeFromTop (6);
    auto row = r.removeFromTop (20);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (colours::dim);
    g.drawText ("Momentary", row.removeFromLeft (76), juce::Justification::centredLeft);
    g.setColour (colourFor (momentary));
    g.drawText (fmt (momentary), row.removeFromLeft (50), juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.drawText ("Integrated", row.removeFromLeft (70), juce::Justification::centredLeft);
    g.setColour (colourFor (integrated));
    g.drawText (fmt (integrated), row.removeFromLeft (44), juce::Justification::centredLeft);
}

void LoudnessDisplay::resized()
{
    reset.setBounds (getLocalBounds().reduced (10, 6).removeFromBottom (20).removeFromRight (56));
}

//==============================================================================
MasterPanel::MasterPanel (RackProcessor& r, PluginWindows& windows)
    : rack (r), loudness (r.loudness), slot1 (r, windows), slot2 (r, windows), verbSlot (r, windows)
{
    auto& state = rack.state;

    addAndMakeVisible (loudness);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    fader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    faderAttachment = std::make_unique<Apvts::SliderAttachment> (state, masterId ("fader"), fader);
    fader.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fader);

    compOn.attach (state, masterId ("compOn"));
    compThr.attach (state, masterId ("compThr"));
    compRatio.attach (state, masterId ("compRatio"));
    compAtk.attach (state, masterId ("compAtk"));
    compRel.attach (state, masterId ("compRel"));
    compMakeup.attach (state, masterId ("compMakeup"));
    verbSize.attach (state, masterId ("verbSize"));
    verbDamp.attach (state, masterId ("verbDamp"));
    verbReturn.attach (state, masterId ("verbReturn"));
    limOn.attach (state, masterId ("limOn"));
    ceiling.attach (state, masterId ("ceiling"));
    limRel.attach (state, masterId ("limRel"));

    slot1.setSlot (&rack.getMasterSlot (0));
    slot2.setSlot (&rack.getMasterSlot (1));
    verbSlot.setSlot (&rack.getReverbSlot());
    verbSlot.setEmptyText ("Built-in reverb (click to load a plugin)");
    verbSlot.setTooltip ("Load a reverb plugin (set it to 100% wet) to replace the built-in reverb");

    for (auto* c : std::initializer_list<juce::Component*> { &compOn, &compThr, &compRatio, &compAtk, &compRel,
                                                             &compMakeup, &verbSize, &verbDamp, &verbReturn, &limOn,
                                                             &ceiling, &limRel, &slot1, &slot2, &verbSlot })
        addAndMakeVisible (c);
}

void MasterPanel::setLocked (bool shouldLock)
{
    locked = shouldLock;
    for (auto* c : std::initializer_list<juce::Component*> { &compOn, &compThr, &compRatio, &compAtk, &compRel,
                                                             &compMakeup, &verbSize, &verbDamp, &verbReturn, &limOn,
                                                             &ceiling, &limRel, &slot1, &slot2, &verbSlot })
        c->setVisible (! locked);
    resized();
    repaint();
}

void MasterPanel::refresh()
{
    slot1.refresh();
    slot2.refresh();
    verbSlot.refresh();
}

void MasterPanel::tick()
{
    loudness.tick();
    meterL.setLevel (rack.masterPeakL.exchange (0.0f));
    meterR.setLevel (rack.masterPeakR.exchange (0.0f));

    const float lim = rack.limiterReductionDb.load(), bus = rack.busCompReductionDb.load();
    if (std::abs (lim - limiterReduction) > 0.1f || std::abs (bus - busReduction) > 0.1f)
    {
        limiterReduction = lim;
        busReduction = bus;
        repaint (grArea);
    }
}

void MasterPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    if (! locked)
    {
        drawSection (g, compArea, "");
        drawSection (g, verbArea, "REVERB BUS");
        drawSection (g, limArea, "");
        drawSection (g, insertArea, "MASTER INSERTS");
    }

    // Gain reduction readouts under the meters.
    g.setFont (juce::FontOptions (11.0f));
    auto r = grArea;
    g.setColour (colours::dim);
    g.drawText ("LIM", r.removeFromLeft (28), juce::Justification::centredLeft);
    g.setColour (limiterReduction < -0.1f ? colours::amber : colours::dim);
    g.drawText (juce::String (limiterReduction, 1), r.removeFromLeft (40), juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.drawText ("BUS", r.removeFromLeft (28), juce::Justification::centredLeft);
    g.setColour (busReduction < -0.1f ? colours::amber : colours::dim);
    g.drawText (juce::String (busReduction, 1), r, juce::Justification::centredLeft);
}

void MasterPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    loudness.setBounds (r.removeFromTop (128));
    r.removeFromTop (8);

    grArea = r.removeFromBottom (18);

    // Master fader and stereo meter on the left.
    auto left = locked ? r.withSizeKeepingCentre (140, r.getHeight()) : r.removeFromLeft (104);
    auto meters = left.removeFromRight (26);
    meterL.setBounds (meters.removeFromLeft (12).withTrimmedBottom (22));
    meters.removeFromLeft (2);
    meterR.setBounds (meters.removeFromLeft (12).withTrimmedBottom (22));
    fader.setBounds (left);

    if (locked)
        return;

    r.removeFromLeft (8);
    constexpr int knob = 66, rowH = 80;

    compArea = r.removeFromTop (20 + 2 * rowH + 8);
    {
        auto a = compArea.reduced (6, 4);
        auto header = a.removeFromTop (22);
        compOn.setBounds (header.removeFromLeft (90));
        a.removeFromTop (2);
        layoutRow (a.removeFromTop (rowH), { &compThr, &compRatio, &compMakeup }, knob);
        layoutRow (a.removeFromTop (rowH), { &compAtk, &compRel }, knob);
    }
    r.removeFromTop (6);

    verbArea = r.removeFromTop (20 + rowH + 34);
    {
        auto a = verbArea.reduced (6, 0).withTrimmedTop (20);
        layoutRow (a.removeFromTop (rowH), { &verbSize, &verbDamp, &verbReturn }, knob);
        verbSlot.setBounds (a.removeFromTop (30).reduced (2, 3));
    }
    r.removeFromTop (6);

    limArea = r.removeFromTop (24 + rowH + 6);
    {
        auto a = limArea.reduced (6, 4);
        limOn.setBounds (a.removeFromTop (22).removeFromLeft (90));
        a.removeFromTop (2);
        layoutRow (a.removeFromTop (rowH), { &ceiling, &limRel }, knob);
    }
    r.removeFromTop (6);

    insertArea = r.removeFromTop (20 + 2 * 30 + 8);
    {
        auto a = insertArea.reduced (8, 0).withTrimmedTop (20);
        slot1.setBounds (a.removeFromTop (30).reduced (0, 2));
        slot2.setBounds (a.removeFromTop (30).reduced (0, 2));
    }
}
} // namespace sr::ui
