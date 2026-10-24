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
} // namespace

MainComponent::MainComponent (RackProcessor& r, juce::AudioDeviceManager& dm, juce::PropertiesFile& s)
    : rack (r), deviceManager (dm), settings (s), busPanel (r), master (r, pluginWindows), consolePanel (wingClient, settings)
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

    // Recording: the finished stream mix (and optionally the raw bus) to WAV files.
    recordButton.setTooltip ("Record the stream mix to a WAV file");
    recordButton.onClick = [this] { recordClicked(); };
    recordMenuButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")));
    recordMenuButton.setTooltip ("Recording options");
    recordMenuButton.onClick = [this] { showRecordMenu(); };
    recordButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    recordMenuButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (recordMenuButton);
    updateRecordButton();

    audioButton.onClick = [this] { showAudioSettings(); };
    pluginsButton.onClick = [this] { showPluginScanner(); };
    scenesButton.onClick = [this] { showScenesMenu(); };
    lockButton.onClick = [this] { lockClicked(); };
    for (auto* b : { &audioButton, &pluginsButton, &scenesButton, &lockButton })
        addAndMakeVisible (b);

    // Mode switch: Bus (the stereo stream bus) or Advanced (individual channels).
    // Two views: Bus (color the stream bus coming in over Dante) and WING Mix (the console
    // channels feeding that bus, read and adjusted over the network).
    busViewButton.setTooltip ("Color the stream bus coming in over Dante");
    wingViewButton.setTooltip ("The WING channels feeding the stream bus: who's talking, sends, mutes");
    for (auto* b : { &busViewButton, &wingViewButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setConnectedEdges (b == &busViewButton ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
        addAndMakeVisible (b);
    }
    busViewButton.onClick = [this] { setView (false); };
    wingViewButton.onClick = [this]
    {
        setView (true);
        if (! wingClient.isActive() && ! locked)
            showConnectDialog();
    };

    addChildComponent (busPanel);
    addAndMakeVisible (master);
    consolePanel.onConnectClicked = [this] { showConnectDialog(); };
    addChildComponent (consolePanel);

    rack.onSlotAboutToChange = [this] (PluginSlot& slot) { pluginWindows.close (slot); };
    rack.state.state.addListener (this);

    master.setBusMode (true);
    setView (settings.getBoolValue ("wingView", false));
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
    if (busViewButton.isVisible())
    {
        busViewButton.setBounds (bar.removeFromLeft (narrowBar ? 50 : 64));
        wingViewButton.setBounds (bar.removeFromLeft (narrowBar ? 78 : 92));
        bar.removeFromLeft (12);
    }
    for (auto* b : { &lockButton, &scenesButton, &pluginsButton, &audioButton })
    {
        if (b->isVisible())
        {
            const int w = b == &lockButton ? (narrowBar ? 56 : 80) : (narrowBar ? 66 : 96);
            b->setBounds (bar.removeFromRight (w));
            bar.removeFromRight (narrowBar ? 4 : 6);
        }
    }
    bar.removeFromRight (narrowBar ? 2 : 6);
    if (recordMenuButton.isVisible())
        recordMenuButton.setBounds (bar.removeFromRight (22));
    recordButton.setBounds (bar.removeFromRight (narrowBar ? 74 : 104));
    bar.removeFromRight (narrowBar ? 4 : 10);
    status.setVisible (bar.getWidth() >= 80);
    status.setBounds (bar);

    master.setAlongBottom (masterAlongBottom);
    if (masterAlongBottom)
        master.setBounds (r.removeFromBottom (juce::jmin (MasterPanel::preferredHeight, r.getHeight() / 2)));
    else
        master.setBounds (r.removeFromRight (MasterPanel::preferredWidth));

    busPanel.setBounds (r);
    consolePanel.setBounds (r);
}

//==============================================================================
void MainComponent::timerCallback()
{
    // The engine always processes the stream bus. Scenes saved with the old Advanced mode
    // would switch it to channel strips, so switch it back.
    if (rack.state.getRawParameterValue (masterId ("busMode"))->load() < 0.5f)
        rack.state.getParameter (masterId ("busMode"))->setValueNotifyingHost (1.0f);

    if (! wingView)
        busPanel.tick();
    master.tick();

    if (--statusCountdown <= 0)
    {
        statusCountdown = 15;
        updateStatus();
        updateRecordButton();
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

    if (rack.recorder.isRecording())
    {
        const auto file = rack.recorder.getStreamFile();
        const auto freeGb = (double) file.getBytesFreeOnVolume() / 1.0e9;
        juce::String text = "Recording " + file.getFileName() + "   " + juce::File::descriptionOfSizeInBytes (file.getSize());
        if (rack.recorder.getDroppedBlocks() > 0)
            text << "   the disk is too slow: some audio was skipped";
        else if (freeGb < 2.0)
            text << "   only " << juce::String (freeGb, 1) << " GB free";
        status.setText (text, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, freeGb < 2.0 || rack.recorder.getDroppedBlocks() > 0 ? colours::amber : colours::text);
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
    master.refresh();
}

void MainComponent::setView (bool wingMix)
{
    wingView = wingMix;
    settings.setValue ("wingView", wingView);
    busViewButton.setToggleState (! wingView, juce::dontSendNotification);
    wingViewButton.setToggleState (wingView, juce::dontSendNotification);
    busPanel.setVisible (! wingView);
    consolePanel.setVisible (wingView);
    resized();
    repaint();
}

void MainComponent::setLocked (bool shouldLock)
{
    locked = shouldLock;
    settings.setValue ("locked", locked);

    if (locked)
        pluginWindows.closeAll();

    // Volunteers can still switch views; both are view-only when locked.
    master.setLocked (locked);
    busPanel.setLocked (locked);
    consolePanel.setLocked (locked);

    audioButton.setVisible (! locked);
    pluginsButton.setVisible (! locked);
    recordMenuButton.setVisible (! locked); // volunteers can still start and stop recording
    lockButton.setButtonText (locked ? "Unlock" : "Lock");
    resized();
    repaint();
}

//==============================================================================
void MainComponent::showConnectDialog()
{
    const auto host = settings.getValue ("wingHost");
    auto* panel = new ConnectPanel (host, wingClient.isActive());
    panel->onConnect = [this] (juce::String h) { connectConsole (h); };
    panel->onDemo = [this] { connectConsole ("demo"); };
    panel->onDisconnect = [this] { disconnectConsole(); };
    panel->getDiagnostics = [this] { return wingClient.getDiagnostics(); };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel);
    options.dialogTitle = "Connect to the console";
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
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
void MainComponent::updateRecordButton()
{
    if (rack.recorder.isRecording())
    {
        const auto secs = (int) rack.recorder.getSecondsRecorded();
        recordButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 ")) // stop square
                                    + juce::String::formatted ("%d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60));
        recordButton.setColour (juce::TextButton::buttonColourId, colours::red);
        recordButton.setColour (juce::TextButton::textColourOffId, colours::text);
        recordButton.setTooltip ("Recording. Click to stop and finish the file.");
    }
    else
    {
        recordButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f Record"))); // dot
        recordButton.removeColour (juce::TextButton::buttonColourId);
        recordButton.setColour (juce::TextButton::textColourOffId, colours::red);
        recordButton.setTooltip ("Record the stream mix to a WAV file");
    }
}

void MainComponent::recordClicked()
{
    if (! rack.recorder.isRecording())
    {
        const juce::File folder (settings.getValue ("recordFolder", Recorder::defaultFolder().getFullPathName()));
        juce::String error;
        if (! rack.recorder.start (folder, settings.getBoolValue ("recordRaw", false), error))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Record", error);
        updateRecordButton();
        updateStatus();
        return;
    }

    // Stopping mid-service by accident would be costly, so ask.
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Stop recording?",
                                        "The file will be finished and saved in " + rack.recorder.getStreamFile().getParentDirectory().getFullPathName() + ".",
                                        "Stop", "Keep recording", this,
                                        juce::ModalCallbackFunction::create ([safeThis] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;
        safeThis->rack.recorder.stop();
        safeThis->updateRecordButton();
        safeThis->updateStatus();
    }));
}

void MainComponent::showRecordMenu()
{
    const juce::File folder (settings.getValue ("recordFolder", Recorder::defaultFolder().getFullPathName()));
    const bool raw = settings.getBoolValue ("recordRaw", false);

    juce::PopupMenu menu;
    menu.addItem (1, "Open recordings folder");
    menu.addItem (2, "Choose folder...  (" + folder.getFileName() + ")", ! rack.recorder.isRecording());
    menu.addSeparator();
    menu.addItem (3, "Also record the raw bus (before color)", ! rack.recorder.isRecording(), raw);
    menu.addSeparator();
    menu.addItem (4, "WAV, 24-bit: about 1 GB per hour of stereo", false, false);

    juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton), [safeThis, folder, raw] (int result)
    {
        if (safeThis == nullptr)
            return;
        auto& self = *safeThis;
        if (result == 1)
        {
            folder.createDirectory();
            folder.startAsProcess(); // opens it in Finder
        }
        else if (result == 2)
        {
            self.folderChooser = std::make_unique<juce::FileChooser> ("Where should recordings go?", folder);
            self.folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                             [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis != nullptr && chooser.getResult() != juce::File())
                    safeThis->settings.setValue ("recordFolder", chooser.getResult().getFullPathName());
            });
        }
        else if (result == 3)
        {
            self.settings.setValue ("recordRaw", ! raw);
        }
    });
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
