#include "BusPanel.h"

namespace sr::ui
{
namespace
{
    juce::String pairLabel (int pair) { return juce::String (pair * 2 + 1) + "-" + juce::String (pair * 2 + 2); }
} // namespace

//==============================================================================
void PairButton::setActivity (float peakDb)
{
    const float db = peakDb > activityDb ? peakDb : juce::jmax (peakDb, activityDb - 1.0f);
    const auto bucket = [] (float v) { return v > -3.0f ? 2 : (v > -50.0f ? 1 : 0); };
    const bool changed = bucket (db) != bucket (activityDb);
    activityDb = db;
    if (changed)
        repaint();
}

void PairButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = getToggleState();
    auto fill = selected ? colours::accent : colours::panelRaised;
    if (! selected && (highlighted || down))
        fill = fill.brighter (0.15f);
    if (! isEnabled())
        fill = fill.withMultipliedAlpha (0.6f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);

    // Signal dot: green with signal, red when hot, an empty ring when silent.
    auto dot = r.removeFromLeft (24.0f).withSizeKeepingCentre (9.0f, 9.0f);
    if (activityDb > -50.0f)
    {
        g.setColour (activityDb > -3.0f ? colours::red : (selected ? colours::onAccent : colours::green));
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour ((selected ? colours::onAccent : colours::dim).withAlpha (0.7f));
        g.drawEllipse (dot, 1.2f);
    }

    g.setColour (selected ? colours::onAccent : colours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (getButtonText(), r.toNearestInt().withTrimmedRight (8), juce::Justification::centredLeft, 1);
}

//==============================================================================
BusPanel::BusPanel (RackProcessor& r) : rack (r)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (rack.state.getParameter (masterId ("busSrc"))))
        for (int i = 0; i < p->choices.size(); ++i)
            input.addItem ("Dante " + p->choices[i], i + 1);
    input.setTooltip ("The two Dante channels carrying the stream bus (left, right)");
    inputAttachment = std::make_unique<Apvts::ComboBoxAttachment> (rack.state, masterId ("busSrc"), input);
    addAndMakeVisible (input);

    channelsBox.addItemList ({ "Stereo", "Left only (to both)", "Right only (to both)", "Mono sum" }, 1);
    channelsBox.setTooltip ("Use Left only or Right only when the bus arrives on one side (a mono bus, or one "
                            "Dante channel routed); Mono sum folds both sides together");
    channelsAttachment = std::make_unique<Apvts::ComboBoxAttachment> (rack.state, masterId ("busChannels"), channelsBox);
    addAndMakeVisible (channelsBox);
    fixButton.setTooltip ("Only one side of the bus has signal: copy it to both sides");
    fixButton.onClick = [this]
    {
        if (auto* p = rack.state.getParameter (masterId ("busChannels")))
            p->setValueNotifyingHost (p->convertTo0to1 (silentSide == 1 ? 1.0f : 2.0f));
        oneSideFrames = 0;
    };
    addChildComponent (fixButton);

    renameButton.setTooltip ("Name the selected Dante pair (e.g. \"Stream bus\") to get a one-click button for it");
    renameButton.onClick = [this] { renameCurrentPair(); };
    addAndMakeVisible (renameButton);
    refreshPairNames();

    trim.attach (rack.state, masterId ("busTrim"));
    addAndMakeVisible (trim);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);
}

void BusPanel::setLocked (bool shouldLock)
{
    locked = shouldLock;
    input.setEnabled (! locked);
    for (auto* b : picks)
        b->setEnabled (! locked);
    resized();
    repaint();
}

int BusPanel::selectedPair() const
{
    return (int) rack.state.getRawParameterValue (masterId ("busSrc"))->load();
}

void BusPanel::refreshPairNames()
{
    juce::StringArray names;
    for (int pair = 0; pair < maxInputs / 2; ++pair)
        names.add (rack.getPairName (pair));
    if (names == pairNames)
        return;
    pairNames = names;

    picks.clear();
    for (int pair = 0; pair < pairNames.size(); ++pair)
    {
        const auto& name = pairNames[pair];
        input.changeItemText (pair + 1, "Dante " + pairLabel (pair) + (name.isNotEmpty() ? ":  " + name : juce::String()));
        if (name.isEmpty())
            continue;

        auto* b = picks.add (new PairButton (pair));
        b->setButtonText (name);
        b->setTooltip ("Dante " + pairLabel (pair));
        b->setEnabled (! locked);
        b->onClick = [this, pair]
        {
            if (auto* p = rack.state.getParameter (masterId ("busSrc")))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) pair));
                p->endChangeGesture();
            }
        };
        addChildComponent (b);
    }

    // A ComboBox forgets its selection when the selected item's text changes.
    input.setSelectedId (selectedPair() + 1, juce::dontSendNotification);
    resized();
    repaint();
}

void BusPanel::renameCurrentPair()
{
    const int pair = selectedPair();

    auto* window = new juce::AlertWindow ("Name Dante " + pairLabel (pair),
                                          "What does the WING send on this pair? For example \"Stream bus\" or "
                                          "\"Youth bus\". Leave it empty to remove the name.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("text", rack.getPairName (pair));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
    juce::Component::SafePointer<BusPanel> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeWindow, safeThis, pair] (int result)
    {
        if (result == 1 && safeWindow != nullptr && safeThis != nullptr)
        {
            safeThis->rack.setPairName (pair, safeWindow->getTextEditorContents ("text"));
            safeThis->refreshPairNames();
        }
    }), true);
}

void BusPanel::tick()
{
    refreshPairNames(); // a scene recall can change the names

    const int current = selectedPair();
    for (auto* b : picks)
    {
        const float peak = juce::jmax (rack.inputPeaks[(size_t) (b->pair * 2)].exchange (0.0f),
                                       rack.inputPeaks[(size_t) (b->pair * 2 + 1)].exchange (0.0f));
        b->setActivity (juce::Decibels::gainToDecibels (peak, -100.0f));
        b->setToggleState (b->pair == current, juce::dontSendNotification);
    }

    // Only one side of the pair has signal for a couple of seconds: offer to copy it to both.
    {
        const float rawL = juce::Decibels::gainToDecibels (rack.busRawPeakL.exchange (0.0f), -100.0f);
        const float rawR = juce::Decibels::gainToDecibels (rack.busRawPeakR.exchange (0.0f), -100.0f);
        const int side = rawL > -50.0f && rawR < -80.0f ? 1 : (rawR > -50.0f && rawL < -80.0f ? 0 : -1);
        if (side >= 0 && (side == silentSide || oneSideFrames == 0))
        {
            silentSide = side;
            oneSideFrames = juce::jmin (oneSideFrames + 1, 1000);
        }
        else if (rawL > -50.0f && rawR > -50.0f)
        {
            oneSideFrames = 0;
        }
        const bool stereo = channelsBox.getSelectedId() == 1;
        const bool show = stereo && oneSideFrames > 60 && ! locked;
        if (show != fixButton.isVisible())
        {
            fixButton.setButtonText (silentSide == 1 ? "Use left on both" : "Use right on both");
            fixButton.setVisible (show);
            repaint (inputLabelArea);
        }
    }

    const float l = rack.busInPeakL.exchange (0.0f), r = rack.busInPeakR.exchange (0.0f);
    meterL.setLevel (l);
    meterR.setLevel (r);

    const float peakDb = juce::Decibels::gainToDecibels (juce::jmax (l, r), -100.0f);
    inPeakDb = peakDb > inPeakDb ? peakDb : juce::jmax (peakDb, inPeakDb - 1.0f);

    levelerGain = rack.levelerGainDb.load();
    repaint (inputLabelArea);
    repaint (levelerArea);
}

void BusPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    drawSection (g, inputArea, "STREAM BUS IN");
    drawSection (g, levelerArea, "AUTO LEVELER");
    if (! flowArea.isEmpty())
        drawSection (g, flowArea, "SIGNAL FLOW");

    // Input level readout.
    {
        auto a = inputLabelArea;
        const bool roomy = a.getHeight() >= 68;
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        if (roomy)
            g.drawText ("Peak", a.removeFromTop (18), juce::Justification::centredLeft);
        const bool hot = inPeakDb > -3.0f, silent = inPeakDb < -60.0f;
        g.setColour (hot ? colours::red : (silent ? colours::dim : colours::text));
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.drawText (silent ? juce::String ("--") : juce::String (inPeakDb, 1), a.removeFromTop (34),
                    juce::Justification::centredLeft);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        if (fixButton.isVisible())
            g.setColour (colours::amber);
        g.drawFittedText (fixButton.isVisible() ? (silentSide == 1 ? "Only the left side has signal" : "Only the right side has signal")
                          : silent ? "No signal: check Dante routing" : (hot ? "Very hot: lower the trim" : "dBFS peak"),
                          a.removeFromTop (18), juce::Justification::centredLeft, 1);
    }

    // Until a pair is named there are no quick buttons; say how to get them.
    if (picks.isEmpty() && ! picksArea.isEmpty())
    {
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText ("Several buses on Dante? Click Name... to label each pair and get a one-click button for it.",
                          picksArea, juce::Justification::centredLeft, 2);
    }

    // Leveler: big readout plus a bar from the centre (right = boosting, left = cutting).
    {
        auto a = levelerArea.reduced (16, 0).withTrimmedTop (24);
        const bool on = rack.state.getRawParameterValue (masterId ("levOn"))->load() > 0.5f;
        const float range = juce::jmax (1.0f, rack.state.getRawParameterValue (masterId ("levRange"))->load());

        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.setColour (! on ? colours::dim : (levelerGain >= 0 ? colours::green : colours::amber));
        g.drawText (on ? (levelerGain > 0.05f ? "+" : "") + juce::String (levelerGain, 1) + " dB" : juce::String ("Off"),
                    a.removeFromTop (38), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (colours::dim);
        if (levelerArea.getHeight() >= 130)
            g.drawFittedText (! on ? "Turn it on in the LEVELER section"
                             : (levelerGain > 0.5f ? "Turning the stream up (the bus is quiet)"
                                                   : (levelerGain < -0.5f ? "Turning the stream down (the bus is loud)"
                                                                          : "On target")),
                        a.removeFromTop (18), juce::Justification::centredLeft, 1);

        auto bar = a.removeFromTop (22).withTrimmedTop (6).toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (bar, 4.0f);
        const float frac = juce::jlimit (-1.0f, 1.0f, levelerGain / range), cx = bar.getCentreX();
        g.setColour (levelerGain >= 0 ? colours::green : colours::amber);
        if (on)
            g.fillRect (frac >= 0 ? juce::Rectangle<float> (cx, bar.getY(), frac * bar.getWidth() * 0.5f, bar.getHeight())
                                  : juce::Rectangle<float> (cx + frac * bar.getWidth() * 0.5f, bar.getY(),
                                                            -frac * bar.getWidth() * 0.5f, bar.getHeight()));
        g.setColour (colours::text.withAlpha (0.6f));
        g.fillRect (cx - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        auto labels = a.removeFromTop (16);
        g.drawText ("-" + juce::String (range, 0) + " dB", labels, juce::Justification::centredLeft);
        g.drawText ("+" + juce::String (range, 0) + " dB", labels, juce::Justification::centredRight);
    }

    // Signal flow chips.
    if (! flowArea.isEmpty())
    {
        const juce::StringArray steps { "Dante in", "Trim", "Leveler", "Bus Comp", "Inserts", "Fader", "Limiter",
                                        "Dante out" };
        auto a = flowArea.reduced (12, 0).withTrimmedTop (26).withHeight (30);
        const float arrow = 14.0f;
        const float chipW = ((float) a.getWidth() - arrow * (float) (steps.size() - 1)) / (float) steps.size();
        float x = (float) a.getX();
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        for (int i = 0; i < steps.size(); ++i)
        {
            juce::Rectangle<float> chip (x, (float) a.getY(), chipW, (float) a.getHeight());
            const bool endPoint = i == 0 || i == steps.size() - 1;
            g.setColour (endPoint ? colours::accent.darker (0.5f) : colours::panelRaised);
            g.fillRoundedRectangle (chip, 5.0f);
            g.setColour (colours::text);
            g.drawFittedText (steps[i], chip.toNearestInt(), juce::Justification::centred, 1);
            x += chipW;
            if (i < steps.size() - 1)
            {
                juce::Path p;
                const float cy = chip.getCentreY();
                p.addTriangle (x + 4.0f, cy - 4.0f, x + 4.0f, cy + 4.0f, x + arrow - 3.0f, cy);
                g.setColour (colours::dim);
                g.fillPath (p);
                x += arrow;
            }
        }
    }
}

void BusPanel::resized()
{
    // Tall: input, leveler and signal flow stacked. Shorter: no signal-flow row.
    // Very short: input and leveler side by side.
    const bool tight = getHeight() < 420;
    auto r = getLocalBounds().reduced (tight ? 10 : 24);
    card = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 820), juce::jmin (r.getHeight(), 520));
    auto c = card;
    constexpr int gap = 12;

    flowArea = {};
    if (c.getHeight() >= 400)
    {
        flowArea = c.removeFromBottom (70);
        c.removeFromBottom (gap);
    }

    if (c.getHeight() < 260 && c.getWidth() >= 480)
    {
        levelerArea = c.removeFromRight (c.getWidth() / 2 - gap / 2);
        c.removeFromRight (gap);
        inputArea = c;
    }
    else
    {
        levelerArea = c.removeFromBottom (juce::jlimit (110, 150, c.getHeight() / 2 - gap));
        c.removeFromBottom (gap);
        inputArea = c;
    }

    auto a = inputArea.reduced (16, 0).withTrimmedTop (26).withTrimmedBottom (10);
    auto meters = a.removeFromLeft (34);
    meterL.setBounds (meters.removeFromLeft (15));
    meters.removeFromLeft (4);
    meterR.setBounds (meters.removeFromLeft (15));
    a.removeFromLeft (16);

    // Quick-pick row along the bottom of the input box, when there's room for it.
    const bool showPicks = a.getHeight() >= 28 + 6 + 24 + 8 + 52 + 8 + 30 && (! picks.isEmpty() || ! locked);
    picksArea = showPicks ? a.removeFromBottom (30) : juce::Rectangle<int>();
    if (showPicks)
        a.removeFromBottom (8);
    layoutPicks();

    // The trim knob drops out of a very narrow input box rather than squashing the selector.
    const bool roomForTrim = a.getWidth() >= 300;
    trim.setVisible (! locked && roomForTrim);
    auto col = a.removeFromLeft (juce::jmin (300, roomForTrim ? a.getWidth() - 96 : a.getWidth()));
    auto row = col.removeFromTop (28);
    renameButton.setVisible (! locked);
    if (! locked)
    {
        renameButton.setBounds (row.removeFromRight (70));
        row.removeFromRight (6);
    }
    input.setBounds (row);
    col.removeFromTop (6);
    auto channelsRow = col.removeFromTop (24);
    channelsBox.setEnabled (! locked);
    channelsBox.setBounds (channelsRow.removeFromLeft (juce::jmin (170, channelsRow.getWidth() / 2 + 20)));
    channelsRow.removeFromLeft (6);
    fixButton.setBounds (channelsRow.removeFromLeft (juce::jmin (130, channelsRow.getWidth())));
    col.removeFromTop (8);
    inputLabelArea = col.removeFromTop (juce::jmin (72, col.getHeight()));
    if (roomForTrim)
    {
        a.removeFromLeft (16);
        trim.setBounds (a.removeFromLeft (80).withHeight (juce::jmin (90, a.getHeight())));
    }
}

void BusPanel::layoutPicks()
{
    // As many buttons as fit at a readable width; the dropdown always has every pair.
    constexpr int gap = 6, minW = 96, maxW = 170;
    const int fit = picksArea.isEmpty() ? 0 : juce::jmax (1, (picksArea.getWidth() + gap) / (minW + gap));
    const int shown = juce::jmin (fit, picks.size());
    const int w = shown > 0 ? juce::jmin (maxW, (picksArea.getWidth() + gap) / shown - gap) : 0;

    auto r = picksArea;
    for (int i = 0; i < picks.size(); ++i)
    {
        picks[i]->setVisible (i < shown);
        if (i < shown)
        {
            picks[i]->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (gap);
        }
    }
}
} // namespace sr::ui
