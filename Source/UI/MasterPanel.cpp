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
MasterControls::MasterControls (RackProcessor& r, PluginWindows& windows) : rack (r), verbSlot (r, windows)
{
    auto& state = rack.state;
    levOn.attach (state, masterId ("levOn"));
    levOn.setTooltip ("Automatically rides the overall level toward the target, within the range");
    levTarget.attach (state, masterId ("levTarget"));
    levRange.attach (state, masterId ("levRange"));
    levSpeed.attach (state, masterId ("levSpeed"));
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

    verbSlot.setSlot (&rack.getReverbSlot());
    verbSlot.setEmptyText ("Built-in reverb (click to load a plugin)");
    verbSlot.setTooltip ("Load a reverb plugin (set it to 100% wet) to replace the built-in reverb");

    for (int i = 0; i < numMasterSlots; ++i)
    {
        auto* b = inserts.add (new SlotButton (rack, windows));
        b->setSlot (&rack.getMasterSlot (i));
        b->setEmptyText ("+ insert " + juce::String (i + 1));
        addAndMakeVisible (b);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &levOn, &levTarget, &levRange, &levSpeed, &compOn,
                                                             &compThr, &compRatio, &compAtk, &compRel, &compMakeup,
                                                             &verbSize, &verbDamp, &verbReturn, &verbSlot, &limOn,
                                                             &ceiling, &limRel })
        addAndMakeVisible (c);
}

void MasterControls::setBusMode (bool shouldBeBusMode)
{
    busMode = shouldBeBusMode;
    for (auto* c : std::initializer_list<juce::Component*> { &verbSize, &verbDamp, &verbReturn, &verbSlot })
        c->setVisible (! busMode);
}

void MasterControls::refresh()
{
    verbSlot.refresh();
    for (auto* b : inserts)
        b->refresh();
}

void MasterControls::tick()
{
    const float gain = rack.levelerGainDb.load();
    if (std::abs (gain - levelerGain) > 0.05f)
    {
        levelerGain = gain;
        repaint (levMeterArea);
    }
}

int MasterControls::layout (int width)
{
    constexpr int knob = 64, rowH = 80, gap = 6;
    auto r = juce::Rectangle<int> (0, 0, width, 10000);

    auto section = [&] (int height)
    {
        auto a = r.removeFromTop (height);
        r.removeFromTop (gap);
        return a;
    };

    levArea = section (24 + rowH + 22);
    {
        auto a = levArea.reduced (6, 4);
        levOn.setBounds (a.removeFromTop (22).removeFromLeft (90));
        a.removeFromTop (2);
        layoutRow (a.removeFromTop (rowH), { &levTarget, &levRange, &levSpeed }, knob);
        levMeterArea = a.removeFromTop (16).reduced (2, 3);
    }

    compArea = section (24 + 2 * rowH + 6);
    {
        auto a = compArea.reduced (6, 4);
        compOn.setBounds (a.removeFromTop (22).removeFromLeft (90));
        a.removeFromTop (2);
        layoutRow (a.removeFromTop (rowH), { &compThr, &compRatio, &compMakeup }, knob);
        layoutRow (a.removeFromTop (rowH), { &compAtk, &compRel }, knob);
    }

    if (busMode)
    {
        verbArea = {};
    }
    else
    {
        verbArea = section (20 + rowH + 34);
        auto a = verbArea.reduced (6, 0).withTrimmedTop (20);
        layoutRow (a.removeFromTop (rowH), { &verbSize, &verbDamp, &verbReturn }, knob);
        verbSlot.setBounds (a.removeFromTop (30).reduced (2, 3));
    }

    limArea = section (24 + rowH + 6);
    {
        auto a = limArea.reduced (6, 4);
        limOn.setBounds (a.removeFromTop (22).removeFromLeft (90));
        a.removeFromTop (2);
        layoutRow (a.removeFromTop (rowH), { &ceiling, &limRel }, knob);
    }

    insertArea = section (20 + numMasterSlots * 30 + 8);
    {
        auto a = insertArea.reduced (8, 0).withTrimmedTop (20);
        for (auto* b : inserts)
            b->setBounds (a.removeFromTop (30).reduced (0, 2));
    }

    return r.getY();
}

void MasterControls::paint (juce::Graphics& g)
{
    drawSection (g, levArea, "");
    drawSection (g, compArea, "");
    if (! verbArea.isEmpty())
        drawSection (g, verbArea, "REVERB BUS");
    drawSection (g, limArea, "");
    drawSection (g, insertArea, "MASTER INSERTS (in order)");

    // Leveler activity: a bar from the centre, right = boosting, left = cutting.
    auto bar = levMeterArea.toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (bar, 3.0f);
    const float range = juce::jmax (1.0f, rack.state.getRawParameterValue (masterId ("levRange"))->load());
    const float frac = juce::jlimit (-1.0f, 1.0f, levelerGain / range);
    const float cx = bar.getCentreX();
    g.setColour (levelerGain >= 0 ? colours::green : colours::amber);
    g.fillRect (frac >= 0 ? juce::Rectangle<float> (cx, bar.getY(), frac * bar.getWidth() * 0.5f, bar.getHeight())
                          : juce::Rectangle<float> (cx + frac * bar.getWidth() * 0.5f, bar.getY(),
                                                    -frac * bar.getWidth() * 0.5f, bar.getHeight()));
    g.setColour (colours::text.withAlpha (0.6f));
    g.fillRect (cx - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);
}

//==============================================================================
MasterPanel::MasterPanel (RackProcessor& r, PluginWindows& windows)
    : rack (r), loudness (r.loudness), controls (r, windows)
{
    addAndMakeVisible (loudness);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    fader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    faderAttachment = std::make_unique<Apvts::SliderAttachment> (rack.state, masterId ("fader"), fader);
    fader.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fader);

    viewport.setViewedComponent (&controls, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void MasterPanel::setLocked (bool shouldLock)
{
    locked = shouldLock;
    viewport.setVisible (! locked);
    resized();
    repaint();
}

void MasterPanel::setBusMode (bool busMode)
{
    controls.setBusMode (busMode);
    resized();
    repaint();
}

void MasterPanel::refresh() { controls.refresh(); }

void MasterPanel::tick()
{
    loudness.tick();
    controls.tick();
    meterL.setLevel (rack.masterPeakL.exchange (0.0f));
    meterR.setLevel (rack.masterPeakR.exchange (0.0f));

    const float lim = rack.limiterReductionDb.load(), bus = rack.busCompReductionDb.load(),
                lev = rack.levelerGainDb.load();
    if (std::abs (lim - limiterReduction) > 0.1f || std::abs (bus - busReduction) > 0.1f
        || std::abs (lev - levelerGain) > 0.1f)
    {
        limiterReduction = lim;
        busReduction = bus;
        levelerGain = lev;
        repaint (grArea);
    }
}

void MasterPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    // Gain readouts under the meters: leveler (+/-), bus compressor and limiter reduction.
    g.setFont (juce::FontOptions (11.0f));
    auto r = grArea;
    const auto item = [&] (const juce::String& label, float value, bool active)
    {
        g.setColour (colours::dim);
        g.drawText (label, r.removeFromLeft (28), juce::Justification::centredLeft);
        g.setColour (active ? colours::amber : colours::dim);
        g.drawText ((value > 0.05f ? "+" : "") + juce::String (value, 1), r.removeFromLeft (44),
                    juce::Justification::centredLeft);
    };
    item ("LEV", levelerGain, std::abs (levelerGain) > 0.1f);
    item ("BUS", busReduction, busReduction < -0.1f);
    item ("LIM", limiterReduction, limiterReduction < -0.1f);
}

void MasterPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    loudness.setBounds (r.removeFromTop (128));
    r.removeFromTop (8);

    grArea = r.removeFromBottom (18);

    // Master fader and stereo meter on the left (centred when locked).
    auto left = locked ? r.withSizeKeepingCentre (140, r.getHeight()) : r.removeFromLeft (104);
    auto meters = left.removeFromRight (26);
    meterL.setBounds (meters.removeFromLeft (12).withTrimmedBottom (22));
    meters.removeFromLeft (2);
    meterR.setBounds (meters.removeFromLeft (12).withTrimmedBottom (22));
    fader.setBounds (left);

    if (locked)
        return;

    r.removeFromLeft (8);
    viewport.setBounds (r);
    const int needed = controls.layout (r.getWidth());
    const int width = needed > r.getHeight() ? r.getWidth() - viewport.getScrollBarThickness() : r.getWidth();
    controls.setSize (width, controls.layout (width));
}
} // namespace sr::ui
