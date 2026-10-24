#include "MainComponent.h"
#include <BinaryData.h>

namespace sr::ui
{
namespace
{
    constexpr int topBarHeight = 44;
    const char* sceneExtension = ".streamrack";

    juce::String hashPin (const juce::String& pin)
    {
        return juce::SHA256 (("streamrack:" + pin).toUTF8()).toHexString();
    }

    // A separate window, so it can be stacked next to Wirecast and StreamRack.
    class ConsoleWindow : public juce::DocumentWindow
    {
    public:
        ConsoleWindow (ConsolePanel* panel, juce::PropertiesFile& s)
            : DocumentWindow ("StreamRack - Console", colours::background, DocumentWindow::allButtons), settings (s)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (panel, false);
            setResizable (true, false);
            setResizeLimits (420, 300, 10000, 10000);
            if (! restoreWindowStateFromString (settings.getValue ("consoleWindow")))
                centreWithSize (900, 520);
        }

        ~ConsoleWindow() override { settings.setValue ("consoleWindow", getWindowStateAsString()); }

        void closeButtonPressed() override
        {
            settings.setValue ("consoleWindow", getWindowStateAsString());
            setVisible (false);
        }

    private:
        juce::PropertiesFile& settings;
    };
} // namespace

MainComponent::MainComponent (RackProcessor& r, juce::AudioDeviceManager& dm, juce::PropertiesFile& s)
    : rack (r), deviceManager (dm), settings (s), detail (r, pluginWindows), busPanel (r), master (r, pluginWindows)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel); // dialogs and plugin windows too
    setLookAndFeel (&lookAndFeel);

    logo = juce::ImageCache::getFromMemory (BinaryData::icon_png, BinaryData::icon_pngSize);
    title.setText ("StreamRack", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    addAndMakeVisible (title);

    status.setColour (juce::Label::textColourId, colours::dim);
    status.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (status);

    consoleButton.setTooltip ("Watch the WING channels feeding the stream bus (names, sends, mutes, meters)");
    consoleButton.onClick = [this] { showConsole(); };
    addAndMakeVisible (consoleButton);
    audioButton.onClick = [this] { showAudioSettings(); };
    pluginsButton.onClick = [this] { showPluginScanner(); };
    scenesButton.onClick = [this] { showScenesMenu(); };
    lockButton.onClick = [this] { lockClicked(); };
    for (auto* b : { &audioButton, &pluginsButton, &scenesButton, &lockButton })
        addAndMakeVisible (b);

    // Mode switch: Bus (the stereo stream bus) or Advanced (individual channels).
    busModeButton.setTooltip ("Bus mode: color the console's stereo stream bus");
    advancedModeButton.setTooltip ("Advanced mode: mix and color individual channels");
    for (auto* b : { &busModeButton, &advancedModeButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setConnectedEdges (b == &busModeButton ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
        addAndMakeVisible (b);
    }
    busModeButton.onClick = [this] { requestMode (true); };
    advancedModeButton.onClick = [this] { requestMode (false); };

    for (int i = 0; i < numStrips; ++i)
        stripContainer.addAndMakeVisible (strips.add (new StripComponent (rack, i, [this] (int idx) { selectStrip (idx); })));

    stripViewport.setViewedComponent (&stripContainer, false);
    stripViewport.setScrollBarsShown (true, true); // vertical only appears if the window is very short
    addAndMakeVisible (stripViewport);

    detailViewport.setViewedComponent (&detail, false);
    detailViewport.setScrollBarsShown (false, true);
    addChildComponent (detailViewport);
    detail.setCurveShown (settings.getBoolValue ("eqCurve", false));
    detail.onLayoutChanged = [this]
    {
        settings.setValue ("eqCurve", detail.isCurveShown());
        resized();
    };

    addChildComponent (busPanel);
    addAndMakeVisible (master);

    rack.onSlotAboutToChange = [this] (PluginSlot& slot) { pluginWindows.close (slot); };
    rack.state.state.addListener (this);

    setBusMode (rack.state.getRawParameterValue (masterId ("busMode"))->load() > 0.5f);
    setLocked (settings.getBoolValue ("locked", false));
    updateStatus();
    startTimerHz (30);
    setSize (1440, 900);

    // Reconnect to the console used last time.
    if (const auto host = settings.getValue ("wingHost"); host.isNotEmpty())
        connectConsole (host);
}

MainComponent::~MainComponent()
{
    stopTimer();
    consoleWindow.reset();
    wingClient.disconnect();
    demoConsole.reset();
    rack.state.state.removeListener (this);
    rack.onSlotAboutToChange = nullptr;
    pluginWindows.closeAll();
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (topBarHeight));

    if (logo.isValid())
        g.drawImage (logo, logoArea.toFloat(), juce::RectanglePlacement::centred);

    if (locked)
    {
        g.setColour (colours::amber);
        g.fillRect (getLocalBounds().removeFromTop (topBarHeight).removeFromBottom (2));
    }
}

void MainComponent::resized()
{
    // The stream computer usually has several windows stacked, so this window has to work
    // small: narrow windows move the master section to a strip along the bottom, and short
    // windows shrink the faders (and tuck away the pan knobs). The right-hand column also
    // needs height, so a short window uses the bottom strip too rather than a squashed column.
    const bool narrowBar = getWidth() < 900;
    const bool masterAlongBottom = getWidth() < 1100 || getHeight() - topBarHeight < MasterPanel::minimumColumnHeight;

    auto r = getLocalBounds();
    auto bar = r.removeFromTop (topBarHeight).reduced (10, 7);

    logoArea = bar.removeFromLeft (30).withSizeKeepingCentre (30, 30);
    bar.removeFromLeft (6);
    title.setVisible (! narrowBar);
    if (! narrowBar)
        title.setBounds (bar.removeFromLeft (114));
    if (busModeButton.isVisible())
    {
        busModeButton.setBounds (bar.removeFromLeft (narrowBar ? 56 : 70));
        advancedModeButton.setBounds (bar.removeFromLeft (narrowBar ? 78 : 90));
        bar.removeFromLeft (12);
    }
    for (auto* b : { &lockButton, &scenesButton, &pluginsButton, &audioButton, &consoleButton })
    {
        if (b->isVisible())
        {
            const int w = b == &lockButton ? (narrowBar ? 56 : 80) : (narrowBar ? 66 : 96);
            b->setBounds (bar.removeFromRight (w));
            bar.removeFromRight (narrowBar ? 4 : 6);
        }
    }
    status.setVisible (bar.getWidth() >= 80);
    status.setBounds (bar);

    master.setAlongBottom (masterAlongBottom);
    if (masterAlongBottom)
        master.setBounds (r.removeFromBottom (juce::jmin (MasterPanel::preferredHeight, r.getHeight() / 2)));
    else
        master.setBounds (r.removeFromRight (MasterPanel::preferredWidth));

    if (busMode)
    {
        busPanel.setBounds (r);
        return;
    }

    const bool showDetail = ! locked && ! busMode && selected >= 0;
    detailViewport.setVisible (showDetail);
    if (showDetail)
    {
        detailViewport.setBounds (r.removeFromBottom (juce::jmin (detail.getPreferredHeight() + 8, r.getHeight() / 2)));
        detail.setSize (juce::jmax (detailViewport.getWidth(), detail.getPreferredWidth()), detail.getPreferredHeight());
    }

    stripViewport.setBounds (r);
    const int contentWidth = numStrips * StripComponent::preferredWidth;
    const int scrollBar = r.getWidth() < contentWidth ? stripViewport.getScrollBarThickness() : 0;
    stripContainer.setSize (contentWidth, juce::jmax (StripComponent::minimumHeight, r.getHeight() - scrollBar));
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * StripComponent::preferredWidth, 4, StripComponent::preferredWidth,
                              stripContainer.getHeight() - 8);
}

//==============================================================================
void MainComponent::timerCallback()
{
    // The mode is a parameter, so a scene recall can change it; follow it here.
    const bool paramBusMode = rack.state.getRawParameterValue (masterId ("busMode"))->load() > 0.5f;
    if (paramBusMode != busMode)
        setBusMode (paramBusMode);

    if (busMode)
    {
        busPanel.tick();
    }
    else
    {
        for (auto* s : strips)
            s->tick();
        detail.tick();
    }
    master.tick();

    if (--statusCountdown <= 0)
    {
        statusCountdown = 15;
        updateStatus();
    }
}

void MainComponent::updateStatus()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        status.setText ("No audio device. Click Audio... to choose Dante Virtual Soundcard.", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, colours::red);
        return;
    }

    const int ins = device->getActiveInputChannels().countNumberOfSetBits();
    const int outs = device->getActiveOutputChannels().countNumberOfSetBits();
    status.setText (device->getName() + "   " + juce::String (ins) + " in / " + juce::String (outs) + " out   "
                        + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz   "
                        + juce::String (device->getCurrentBufferSizeSamples()) + " samples   CPU "
                        + juce::String (juce::roundToInt (deviceManager.getCpuUsage() * 100.0)) + "%",
                    juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, colours::dim);
}

void MainComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == rack.state.state && property.toString().startsWith ("name"))
        refreshAll();
}

void MainComponent::refreshAll()
{
    for (auto* s : strips)
        s->refreshName();
    detail.refresh();
    master.refresh();
}

void MainComponent::selectStrip (int index)
{
    selected = (index == selected) ? -1 : index;
    for (auto* s : strips)
        s->setSelected (s == strips[selected]);
    if (selected >= 0)
        detail.setStrip (selected);
    resized();
}

void MainComponent::requestMode (bool shouldBeBusMode)
{
    auto* p = rack.state.getParameter (masterId ("busMode"));
    p->setValueNotifyingHost (shouldBeBusMode ? 1.0f : 0.0f);
    setBusMode (shouldBeBusMode);
}

void MainComponent::setBusMode (bool shouldBeBusMode)
{
    busMode = shouldBeBusMode;
    busModeButton.setToggleState (busMode, juce::dontSendNotification);
    advancedModeButton.setToggleState (! busMode, juce::dontSendNotification);
    busPanel.setVisible (busMode);
    stripViewport.setVisible (! busMode);
    master.setBusMode (busMode);
    resized();
    repaint();
}

void MainComponent::setLocked (bool shouldLock)
{
    locked = shouldLock;
    settings.setValue ("locked", locked);

    if (locked)
    {
        pluginWindows.closeAll();
        selected = -1;
        for (auto* s : strips)
            s->setSelected (false);
    }

    for (auto* s : strips)
        s->setLocked (locked);
    master.setLocked (locked);
    busPanel.setLocked (locked);
    if (consolePanel != nullptr)
        consolePanel->setLocked (locked);
    busModeButton.setVisible (! locked);
    advancedModeButton.setVisible (! locked);

    audioButton.setVisible (! locked);
    pluginsButton.setVisible (! locked);
    lockButton.setButtonText (locked ? "Unlock" : "Lock");
    resized();
    repaint();
}

//==============================================================================
void MainComponent::showConsole()
{
    if (consoleWindow == nullptr)
    {
        consolePanel = new ConsolePanel (wingClient, settings);
        consolePanel->setLocked (locked);
        consolePanel->onConnectClicked = [this] { showConnectDialog(); };
        consoleWindow = std::make_unique<ConsoleWindow> (consolePanel, settings);
    }
    consoleWindow->setVisible (true);
    consoleWindow->toFront (true);

    if (! wingClient.isActive())
        showConnectDialog();
}

void MainComponent::showConnectDialog()
{
    const auto host = settings.getValue ("wingHost");
    auto* panel = new ConnectPanel (host, wingClient.isActive());
    panel->onConnect = [this] (juce::String h) { connectConsole (h); };
    panel->onDemo = [this] { connectConsole ("demo"); };
    panel->onDisconnect = [this] { disconnectConsole(); };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel);
    options.dialogTitle = "Connect to the console";
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = consoleWindow != nullptr && consoleWindow->isVisible()
                                          ? static_cast<juce::Component*> (consoleWindow.get())
                                          : static_cast<juce::Component*> (this);
    options.launchAsync();
}

void MainComponent::connectConsole (const juce::String& host)
{
    settings.setValue ("wingHost", host);
    if (host == "demo")
    {
        // A pretend WING on this computer.
        wingClient.disconnect();
        demoConsole = std::make_unique<wing::WingSimulator> (true);
        if (demoConsole->start())
            wingClient.connect ("127.0.0.1", demoConsole->getPorts());
        return;
    }
    demoConsole.reset();
    wingClient.connect (host);
}

void MainComponent::disconnectConsole()
{
    settings.setValue ("wingHost", juce::String());
    wingClient.disconnect();
    demoConsole.reset();
}

//==============================================================================
void MainComponent::askForText (const juce::String& heading, const juce::String& message, bool password,
                                std::function<void (juce::String)> onOk)
{
    auto* window = new juce::AlertWindow (heading, message, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("text", {}, {}, password);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeWindow, onOk] (int result)
    {
        if (result == 1 && safeWindow != nullptr)
            onOk (safeWindow->getTextEditorContents ("text"));
    }), true);
}

void MainComponent::lockClicked()
{
    juce::Component::SafePointer<MainComponent> safeThis (this);

    if (locked)
    {
        askForText ("Unlock", "Enter the engineer PIN", true, [safeThis] (juce::String pin)
        {
            if (safeThis == nullptr)
                return;
            if (hashPin (pin) == safeThis->settings.getValue ("pinHash"))
                safeThis->setLocked (false);
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Unlock", "Wrong PIN.");
        });
        return;
    }

    if (settings.getValue ("pinHash").isEmpty())
    {
        askForText ("Set engineer PIN", "Choose a PIN. Volunteers will need it to leave lock mode.", true,
                    [safeThis] (juce::String pin)
        {
            if (safeThis == nullptr || pin.trim().isEmpty())
                return;
            safeThis->settings.setValue ("pinHash", hashPin (pin.trim()));
            safeThis->setLocked (true);
        });
        return;
    }

    setLocked (true);
}

//==============================================================================
juce::File MainComponent::getScenesFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("StreamRack Scenes");
    dir.createDirectory();
    return dir;
}

void MainComponent::saveScene (const juce::String& name)
{
    const auto file = getScenesFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + sceneExtension);
    if (auto xml = rack.createStateTree().createXml(); xml == nullptr || ! xml->writeTo (file))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save scene",
                                                "Couldn't write " + file.getFullPathName());
}

void MainComponent::loadScene (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load scene",
                                                "Couldn't read " + file.getFileName());
        return;
    }

    pluginWindows.closeAll();
    rack.restoreStateTree (juce::ValueTree::fromXml (*xml));
    settings.setValue ("lastScene", file.getFileNameWithoutExtension());
    refreshAll();
}

void MainComponent::showScenesMenu()
{
    auto files = getScenesFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + sceneExtension);
    files.sort();

    const auto current = settings.getValue ("lastScene");
    juce::PopupMenu menu, deleteMenu;
    menu.addSectionHeader ("Recall scene");
    for (int i = 0; i < files.size(); ++i)
    {
        const auto name = files[i].getFileNameWithoutExtension();
        menu.addItem (100 + i, name, true, name == current);
        deleteMenu.addItem (1000 + i, name);
    }
    if (files.isEmpty())
        menu.addItem (-1, "(no scenes saved yet)", false);

    if (! locked)
    {
        menu.addSeparator();
        menu.addItem (1, "Save current as...");
        menu.addSubMenu ("Delete scene", deleteMenu, ! files.isEmpty());
        menu.addItem (2, "Show scenes folder");
    }

    juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scenesButton), [safeThis, files] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (result == 1)
        {
            safeThis->askForText ("Save scene", "Name for this scene (e.g. Sabbath Worship, Sabbath School)", false,
                                  [safeThis] (juce::String name)
            {
                if (safeThis != nullptr && name.trim().isNotEmpty())
                {
                    safeThis->saveScene (name);
                    safeThis->settings.setValue ("lastScene", juce::File::createLegalFileName (name.trim()));
                }
            });
        }
        else if (result == 2)
        {
            getScenesFolder().revealToUser();
        }
        else if (result >= 1000 && result - 1000 < files.size())
        {
            files[result - 1000].moveToTrash();
        }
        else if (result >= 100 && result - 100 < files.size())
        {
            safeThis->loadScene (files[result - 100]);
        }
    });
}

//==============================================================================
void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, maxInputs, 1, maxInputs,
                                                             false, false, false, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio Device";
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void MainComponent::showPluginScanner()
{
    auto& host = rack.getHost();
    auto* list = new juce::PluginListComponent (host.formats, host.knownPlugins, host.getDeadMansPedalFile(),
                                                &settings, false);
    list->setSize (720, 520);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (list);
    options.dialogTitle = "Plugins (click Options... > Scan for new or updated VST3/AU plug-ins)";
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}
} // namespace sr::ui
