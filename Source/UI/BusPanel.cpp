#include "BusPanel.h"

namespace sr::ui
{
BusPanel::BusPanel (RackProcessor& r) : rack (r)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (rack.state.getParameter (masterId ("busSrc"))))
        for (int i = 0; i < p->choices.size(); ++i)
            input.addItem ("Dante " + p->choices[i], i + 1);
    input.setTooltip ("The two Dante channels carrying the stream bus (left, right)");
    inputAttachment = std::make_unique<Apvts::ComboBoxAttachment> (rack.state, masterId ("busSrc"), input);
    addAndMakeVisible (input);

    trim.attach (rack.state, masterId ("busTrim"));
    addAndMakeVisible (trim);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);
}

void BusPanel::setLocked (bool shouldLock)
{
    locked = shouldLock;
    input.setEnabled (! locked);
    trim.setVisible (! locked);
    resized();
    repaint();
}

void BusPanel::tick()
{
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
    drawSection (g, flowArea, "SIGNAL FLOW");

    // Input level readout.
    {
        auto a = inputLabelArea;
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Peak", a.removeFromTop (18), juce::Justification::centredLeft);
        const bool hot = inPeakDb > -3.0f, silent = inPeakDb < -60.0f;
        g.setColour (hot ? colours::red : (silent ? colours::dim : colours::text));
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.drawText (silent ? juce::String ("--") : juce::String (inPeakDb, 1), a.removeFromTop (34),
                    juce::Justification::centredLeft);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (silent ? "No signal: check Dante routing" : (hot ? "Very hot: lower the trim" : "dBFS"),
                    a.removeFromTop (18), juce::Justification::centredLeft);
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
        g.drawText (! on ? "Turn it on in the LEVELER section on the right"
                         : (levelerGain > 0.5f ? "Turning the stream up (the bus is quiet)"
                                               : (levelerGain < -0.5f ? "Turning the stream down (the bus is loud)"
                                                                      : "On target")),
                    a.removeFromTop (18), juce::Justification::centredLeft);

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
    auto r = getLocalBounds().reduced (24);
    card = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 820), juce::jmin (r.getHeight(), 520));
    auto c = card;

    inputArea = c.removeFromTop (200);
    c.removeFromTop (12);
    levelerArea = c.removeFromTop (150);
    c.removeFromTop (12);
    flowArea = c.removeFromTop (70);

    auto a = inputArea.reduced (16, 0).withTrimmedTop (26).withTrimmedBottom (12);
    auto meters = a.removeFromLeft (34);
    meterL.setBounds (meters.removeFromLeft (15));
    meters.removeFromLeft (4);
    meterR.setBounds (meters.removeFromLeft (15));
    a.removeFromLeft (20);

    auto col = a.removeFromLeft (220);
    input.setBounds (col.removeFromTop (28));
    col.removeFromTop (14);
    inputLabelArea = col.removeFromTop (72);
    a.removeFromLeft (20);
    trim.setBounds (a.removeFromLeft (80).withHeight (90));
}
} // namespace sr::ui
