#include "ConsolePanel.h"

namespace sr::ui
{
namespace
{
    // WING channel colours 1-18.
    juce::Colour wingColour (int index)
    {
        static const juce::uint32 table[] = { 0xff3b6fd8, 0xff2f9bd6, 0xff5b4fd0, 0xff2ab3a6, 0xff3fae4f, 0xff9ccc3a,
                                              0xffe5c92e, 0xff9b6b3d, 0xffd9413a, 0xffec8a78, 0xffc93fa8, 0xff8e4fc9,
                                              0xffe89a2c, 0xff7cc4ef, 0xffe8602c, 0xff7fd6b0, 0xff8c8c8c, 0xffe8e8e8 };
        return index >= 1 && index <= 18 ? juce::Colour (table[index - 1]) : colours::outline;
    }

    juce::String formatDb (float db)
    {
        if (db <= -90.0f)
            return "-oo";
        return (db > 0.05f ? "+" : "") + juce::String (db, 1);
    }

    constexpr float talkingDb = -45.0f; // input above this counts as someone talking or playing

    // Follows peaks up at once and falls back slowly, like a meter's ballistics.
    float ballistics (float current, float target) { return target > current ? target : juce::jmax (target, current - 1.2f); }

    void drawMeterBar (juce::Graphics& g, juce::Rectangle<float> r, float db, bool active)
    {
        g.setColour (colours::background);
        g.fillRoundedRectangle (r, 2.0f);
        const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (proportion <= 0.0f)
            return;
        const auto fill = r.withTop (r.getBottom() - r.getHeight() * proportion);
        auto colour = db > -3.0f ? colours::red : (db > -12.0f ? colours::amber : colours::green);
        g.setColour (active ? colour : colour.withAlpha (0.35f));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    void drawBadge (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour fill, juce::Colour ink)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (ink);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawFittedText (text, r, juce::Justification::centred, 1);
    }
} // namespace

//==============================================================================
void SourceTile::update (const wing::SourceState& s, int sourceIndex)
{
    source = s;
    index = sourceIndex;
    meterDb = ballistics (meterDb, s.inputDb());
    repaint();
}

void SourceTile::paint (juce::Graphics& g)
{
    const bool inMix = source.sendOn && source.sendLevel > -90.0f;
    const bool feeds = source.feedsTarget();
    const bool talking = meterDb > talkingDb;
    const bool warning = talking && inMix && ! feeds;

    auto r = getLocalBounds().reduced (3).toFloat();
    g.setColour (feeds ? colours::panelRaised : colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (wingColour (source.colour));
    g.fillRoundedRectangle (r.withHeight (4.0f), 2.0f);

    // Outline: amber when someone is heard on the console but not on the stream, green when live.
    if (warning)
    {
        g.setColour (colours::amber);
        g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
    }
    else if (feeds && talking)
    {
        g.setColour (colours::green);
        g.drawRoundedRectangle (r.reduced (0.75f), 6.0f, 1.5f);
    }

    auto content = r.reduced (8.0f, 0.0f).withTrimmedTop (10.0f).withTrimmedBottom (8.0f).toNearestInt();
    drawMeterBar (g, content.removeFromLeft (8).toFloat(), meterDb, feeds);
    content.removeFromLeft (8);

    const auto name = source.name.isNotEmpty() ? source.name
                                               : (index < wing::numChannels ? "Ch " + juce::String (index + 1)
                                                                            : "Aux " + juce::String (index - wing::numChannels + 1));
    g.setColour (feeds ? colours::text : colours::dim);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawFittedText (name, content.removeFromTop (18), juce::Justification::centredLeft, 1);

    g.setFont (juce::FontOptions (11.5f));
    g.setColour (colours::dim);
    auto sendRow = content.removeFromTop (17);
    g.drawText ("Send " + formatDb (source.sendLevel), sendRow, juce::Justification::centredLeft);
    if (source.sendMode.isNotEmpty())
    {
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (source.sendMode, sendRow, juce::Justification::centredRight);
        g.setFont (juce::FontOptions (11.5f));
    }
    g.drawText ("Fader " + formatDb (source.fader), content.removeFromTop (17), juce::Justification::centredLeft);

    auto badge = content.removeFromBottom (18);
    if (source.mute && inMix)
        drawBadge (g, badge, warning ? "MUTED - LIVE" : "MUTED", colours::red, colours::text);
    else if (! inMix)
        drawBadge (g, badge, "NOT ON STREAM", colours::background, colours::dim);
    else if (! feeds)
        drawBadge (g, badge, "FADER DOWN", colours::amber, colours::onAccent);
    else if (talking)
        drawBadge (g, badge, "ON STREAM", colours::green, colours::onAccent);
}

//==============================================================================
void MixTile::update (const wing::MixState& m, const wing::Target& t, bool metersFlowing)
{
    mix = m;
    target = t;
    meters = metersFlowing;
    meterL = ballistics (meterL, m.outL);
    meterR = ballistics (meterR, m.outR);
    repaint();
}

void MixTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (3).toFloat();
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (r, 6.0f);
    if (mix.mute)
    {
        g.setColour (colours::red);
        g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
    }

    auto content = r.reduced (10.0f, 8.0f).toNearestInt();
    const bool across = getWidth() > getHeight() * 2; // a strip along the top of a narrow window
    auto labels = across ? content.removeFromLeft (juce::jmin (180, content.getWidth() / 3)) : content.removeFromTop (36);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("STREAM " + target.label().toUpperCase(), labels.removeFromTop (14), juce::Justification::centredLeft);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawFittedText (mix.name.isNotEmpty() ? mix.name : target.label(), labels.removeFromTop (22),
                      juce::Justification::centredLeft, 1);

    auto badge = across ? content.removeFromRight (90).withSizeKeepingCentre (90, 20) : content.removeFromBottom (20);
    if (mix.mute)
        drawBadge (g, badge, "BUS MUTED", colours::red, colours::text);
    else
        drawBadge (g, badge, "LIVE", colours::green.withAlpha (0.25f), colours::green);

    if (across)
    {
        // Horizontal meters with the fader readout under them.
        content.reduce (12, 0);
        auto text = content.removeFromBottom (16);
        auto meterRow = content.withSizeKeepingCentre (content.getWidth(), 18);
        for (float db : { meterL, meterR })
        {
            auto bar = meterRow.removeFromTop (8).toFloat();
            meterRow.removeFromTop (2);
            g.setColour (colours::background);
            g.fillRoundedRectangle (bar, 2.0f);
            const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour ((db > -3.0f ? colours::red : (db > -12.0f ? colours::amber : colours::green)).withAlpha (mix.mute ? 0.35f : 1.0f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proportion), 2.0f);
        }
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("Fader " + formatDb (mix.fader) + (meters ? "" : "   (no meters yet)"), text, juce::Justification::centredLeft);
        return;
    }

    auto meterArea = content.reduced (0, 4);
    auto left = meterArea.removeFromLeft (10);
    meterArea.removeFromLeft (3);
    auto right = meterArea.removeFromLeft (10);
    drawMeterBar (g, left.toFloat(), meterL, ! mix.mute);
    drawMeterBar (g, right.toFloat(), meterR, ! mix.mute);
    meterArea.removeFromLeft (10);

    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Fader " + formatDb (mix.fader), meterArea.removeFromTop (18), juce::Justification::centredLeft);
    g.drawFittedText (meters ? "Bus output" : "No meters yet", meterArea.removeFromTop (18), juce::Justification::centredLeft, 1);
}

//==============================================================================
ConsolePanel::ConsolePanel (wing::WingClient& c, juce::PropertiesFile& s) : client (c), settings (s)
{
    connectButton.onClick = [this]
    {
        if (onConnectClicked)
            onConnectClicked();
    };
    addAndMakeVisible (connectButton);

    targetBox.setTooltip ("The WING bus (or main) that feeds the stream");
    targetBox.onChange = [this] { targetChosen(); };
    addAndMakeVisible (targetBox);

    allToggle.setTooltip ("Show every named channel, not just the ones sent to the stream bus");
    allToggle.setToggleState (settings.getBoolValue ("wingShowAll", false), juce::dontSendNotification);
    allToggle.onClick = [this]
    {
        settings.setValue ("wingShowAll", allToggle.getToggleState());
        layoutTiles();
    };
    addAndMakeVisible (allToggle);

    addAndMakeVisible (mixTile);
    for (int i = 0; i < wing::numSources; ++i)
        tileContainer.addChildComponent (tiles.add (new SourceTile()));
    shown.assign ((size_t) wing::numSources, false);
    viewport.setViewedComponent (&tileContainer, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    client.setTarget (wing::Target::fromString (settings.getValue ("wingTarget", "bus1")));
    refreshTargets();
    startTimerHz (20);
}

ConsolePanel::~ConsolePanel()
{
    stopTimer();
}

void ConsolePanel::setLocked (bool shouldLock)
{
    locked = shouldLock;
    connectButton.setEnabled (! locked);
    targetBox.setEnabled (! locked);
}

bool ConsolePanel::showsSource (int i) const
{
    const auto& s = state.sources[(size_t) i];
    if (! s.known)
        return false;
    if (allToggle.getToggleState())
        return s.name.isNotEmpty() || (s.sendOn && s.sendLevel > -90.0f);
    return s.sendOn && s.sendLevel > -90.0f;
}

void ConsolePanel::refreshTargets()
{
    juce::StringArray names;
    for (const auto& b : state.buses)
        names.add (b.name);
    for (const auto& m : state.mains)
        names.add (m.name);
    if (names == targetNames && targetBox.getNumItems() > 0)
        return;
    targetNames = names;

    targetBox.clear (juce::dontSendNotification);
    for (int b = 1; b <= wing::numBuses; ++b)
    {
        const auto& n = state.buses[(size_t) (b - 1)].name;
        targetBox.addItem ("Bus " + juce::String (b) + (n.isNotEmpty() ? ":  " + n : juce::String()), b);
    }
    for (int m = 1; m <= wing::numMains; ++m)
    {
        const auto& n = state.mains[(size_t) (m - 1)].name;
        targetBox.addItem ("Main " + juce::String (m) + (n.isNotEmpty() ? ":  " + n : juce::String()), 100 + m);
    }

    // First time on this console: pick the bus named like a stream bus.
    if (settings.getValue ("wingTarget").isEmpty())
        for (int b = 0; b < wing::numBuses; ++b)
            if (state.buses[(size_t) b].name.containsIgnoreCase ("stream"))
            {
                settings.setValue ("wingTarget", wing::Target { wing::Target::Kind::Bus, b + 1 }.toString());
                client.setTarget ({ wing::Target::Kind::Bus, b + 1 });
                break;
            }

    const auto t = wing::Target::fromString (settings.getValue ("wingTarget", "bus1"));
    targetBox.setSelectedId (t.kind == wing::Target::Kind::Bus ? t.index : 100 + t.index, juce::dontSendNotification);
}

void ConsolePanel::targetChosen()
{
    const int id = targetBox.getSelectedId();
    if (id <= 0)
        return;
    const wing::Target t { id > 100 ? wing::Target::Kind::Main : wing::Target::Kind::Bus, id > 100 ? id - 100 : id };
    settings.setValue ("wingTarget", t.toString());
    client.setTarget (t);
}

void ConsolePanel::timerCallback()
{
    state = client.getState();
    refreshTargets();

    bool changed = false;
    for (int i = 0; i < wing::numSources; ++i)
    {
        const bool show = showsSource (i);
        changed = changed || show != shown[(size_t) i];
        shown[(size_t) i] = show;
        if (show)
            tiles[i]->update (state.sources[(size_t) i], i);
    }
    if (changed || state.status != lastStatus)
        resized();
    lastStatus = state.status;

    mixTile.update (state.targetMix(), state.target, state.metersFlowing);
    mixTile.setVisible (state.status == wing::ConsoleState::Status::Connected);
    repaint (statusArea);
    repaint (hintArea);
}

juce::String ConsolePanel::statusText() const
{
    using Status = wing::ConsoleState::Status;
    switch (state.status)
    {
        case Status::Off:
            return "Not connected";
        case Status::Connecting:
            return "Connecting to " + state.host + "...";
        case Status::NoReply:
            return "No answer from " + state.host;
        case Status::Connected:
        {
            auto text = (state.consoleName.isNotEmpty() ? state.consoleName : juce::String ("WING")) + "  (" + state.host + ")";
            if (! state.metersFlowing)
                text << "  -  no meters yet";
            return text;
        }
    }
    return {};
}

void ConsolePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds().withBottom (statusArea.getBottom() + 6));

    using Status = wing::ConsoleState::Status;
    const auto dotColour = state.status == Status::Connected ? (state.metersFlowing ? colours::green : colours::amber)
                                                             : (state.status == Status::NoReply ? colours::red : colours::dim);
    auto s = statusArea;
    g.setColour (dotColour);
    g.fillEllipse (s.removeFromLeft (12).withSizeKeepingCentre (9, 9).toFloat());
    s.removeFromLeft (6);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (statusText(), s, juce::Justification::centredLeft, 1);

    if (hintArea.isEmpty())
        return;

    juce::String hint;
    if (state.status == Status::Off)
        hint = "Click Connect... to find the WING on the network. The console and this Mac need to be on the same "
               "network (an Ethernet cable to the WING or its switch). StreamRack only watches the console; it "
               "never changes it.";
    else if (state.status == Status::NoReply)
        hint = "Check the network cable and the WING's IP address (on the WING: Setup > Network). If macOS asked "
               "whether StreamRack may find devices on your local network, it needs Allow (System Settings > "
               "Privacy & Security > Local Network).";
    else if (state.status == Status::Connected)
    {
        const bool any = std::any_of (shown.begin(), shown.end(), [] (bool b) { return b; });
        if (! any)
            hint = "No channels are sent to " + state.target.label()
                   + " yet. Pick the bus that feeds the stream above, or turn on All channels.";
    }
    if (hint.isNotEmpty())
    {
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (hint, hintArea.reduced (8), juce::Justification::topLeft, 4);
    }
}

void ConsolePanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    // Header: status, then the controls (on their own row when the window is narrow).
    auto header = r.removeFromTop (28);
    const bool twoRows = getWidth() < 640;
    connectButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (6);
    if (twoRows)
    {
        statusArea = header;
        r.removeFromTop (6);
        header = r.removeFromTop (28);
    }
    allToggle.setBounds (header.removeFromRight (110));
    header.removeFromRight (6);
    targetBox.setBounds (header.removeFromRight (juce::jmin (220, header.getWidth() / 2)));
    if (! twoRows)
    {
        header.removeFromRight (8);
        statusArea = header;
    }
    r.removeFromTop (12);

    const bool connected = state.status == wing::ConsoleState::Status::Connected;
    hintArea = connected ? r.removeFromBottom (0) : r.removeFromTop (90);
    if (connected && std::none_of (shown.begin(), shown.end(), [] (bool b) { return b; }))
        hintArea = r.removeFromTop (60);

    // The stream bus tile on the left (along the top when narrow), the channels feeding it in a
    // wrapping grid.
    if (twoRows)
    {
        mixTile.setBounds (r.removeFromTop (70));
        r.removeFromTop (6);
    }
    else
    {
        mixTile.setBounds (r.removeFromLeft (150).withHeight (juce::jmin (r.getHeight(), 2 * SourceTile::height)));
        r.removeFromLeft (6);
    }
    viewport.setBounds (r);
    layoutTiles();
}

void ConsolePanel::layoutTiles()
{
    const int width = juce::jmax (SourceTile::width, viewport.getMaximumVisibleWidth());
    const int columns = juce::jmax (1, width / SourceTile::width);
    int n = 0;
    for (int i = 0; i < wing::numSources; ++i)
    {
        const bool show = showsSource (i);
        tiles[i]->setVisible (show);
        if (show)
        {
            tiles[i]->setBounds ((n % columns) * SourceTile::width, (n / columns) * SourceTile::height, SourceTile::width,
                                 SourceTile::height);
            ++n;
        }
    }
    const int rows = (n + columns - 1) / columns;
    tileContainer.setSize (width, juce::jmax (1, rows * SourceTile::height));
}

//==============================================================================
ConnectPanel::ConnectPanel (const juce::String& currentHost, bool connected)
{
    searchButton.onClick = [this] { search(); };
    connectButton.onClick = [this]
    {
        if (address.getText().trim().isNotEmpty() && onConnect)
            onConnect (address.getText().trim());
        close();
    };
    demoButton.setTooltip ("A pretend WING on this computer, for learning the screen away from the booth");
    demoButton.onClick = [this]
    {
        if (onDemo)
            onDemo();
        close();
    };
    disconnectButton.onClick = [this]
    {
        if (onDisconnect)
            onDisconnect();
        close();
    };
    disconnectButton.setEnabled (connected);

    address.setText (currentHost == "demo" ? juce::String() : currentHost, false);
    address.setTextToShowWhenEmpty ("e.g. 192.168.1.62", colours::dim);
    address.setJustification (juce::Justification::centredLeft);
    address.onReturnKey = [this] { connectButton.triggerClick(); };

    for (auto* c : std::initializer_list<juce::Component*> { &searchButton, &connectButton, &demoButton, &disconnectButton, &address })
        addAndMakeVisible (c);
    setSize (440, 250);
}

ConnectPanel::~ConnectPanel()
{
    *alive = false;
}

void ConnectPanel::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

void ConnectPanel::search()
{
    searchButton.setEnabled (false);
    searchMessage = "Searching...";
    found.clear();
    repaint();

    juce::Component::SafePointer<ConnectPanel> safeThis (this);
    juce::Thread::launch ([safeThis]
    {
        auto consoles = wing::WingClient::discover (1500);
        juce::MessageManager::callAsync ([safeThis, consoles]
        {
            if (safeThis == nullptr)
                return;
            auto& self = *safeThis;
            self.searchButton.setEnabled (true);
            self.searchMessage = consoles.empty() ? "No WING found. Check the cable, or type its IP address below."
                                                  : juce::String ("Found:");
            for (const auto& c : consoles)
            {
                auto* b = self.found.add (new juce::TextButton (c.name + "   " + c.ip + "   (" + c.model + ", fw " + c.firmware + ")"));
                const auto ip = c.ip;
                b->onClick = [safe = juce::Component::SafePointer<ConnectPanel> (&self), ip]
                {
                    if (safe == nullptr)
                        return;
                    if (safe->onConnect)
                        safe->onConnect (ip);
                    safe->close();
                };
                self.addAndMakeVisible (b);
            }
            self.setSize (self.getWidth(), 250 + self.found.size() * 32);
            if (auto* dw = self.findParentComponentOfClass<juce::DialogWindow>())
                dw->setSize (dw->getWidth(), dw->getHeight() + self.found.size() * 32);
            self.resized();
            self.repaint();
        });
    });
}

void ConnectPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (13.0f));
    auto r = getLocalBounds().reduced (16);
    g.drawFittedText ("StreamRack reads the channel names, sends and meters from the WING over the network "
                      "(it never changes anything on the console).",
                      r.removeFromTop (36), juce::Justification::topLeft, 2);
    r.removeFromTop (44);
    g.drawText (searchMessage, r.removeFromTop (20), juce::Justification::centredLeft);
    r.removeFromTop (found.size() * 32);
    g.drawText ("Or type the WING's IP address (WING: Setup > Network):", r.removeFromTop (22), juce::Justification::centredLeft);
}

void ConnectPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (44);
    searchButton.setBounds (r.removeFromTop (32).removeFromLeft (180));
    r.removeFromTop (20 + 4);
    for (auto* b : found)
        b->setBounds (r.removeFromTop (32).reduced (0, 2));
    r.removeFromTop (22);
    auto row = r.removeFromTop (30);
    connectButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (6);
    address.setBounds (row);
    auto bottom = getLocalBounds().reduced (16).removeFromBottom (30);
    demoButton.setBounds (bottom.removeFromLeft (130));
    disconnectButton.setBounds (bottom.removeFromRight (110));
}
} // namespace sr::ui
