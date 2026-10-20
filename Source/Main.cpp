#include "Engine/RackProcessor.h"
#include "UI/MainComponent.h"

namespace sr
{
juce::File getDataFolder()
{
#if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/StreamRack");
#else
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("StreamRack");
#endif
    dir.createDirectory();
    return dir;
}

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (RackProcessor& rack, juce::AudioDeviceManager& dm, juce::PropertiesFile& settings)
        : DocumentWindow ("StreamRack", ui::colours::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ui::MainComponent (rack, dm, settings), true);
        setResizable (true, false);
        setResizeLimits (560, 440, 10000, 10000);

        if (! restoreWindowStateFromString (settings.getValue ("window")))
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class StreamRackApplication : public juce::JUCEApplication,
                              private juce::ChangeListener,
                              private juce::Timer
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        const auto dataFolder = getDataFolder();

        juce::PropertiesFile::Options options;
        options.applicationName = "StreamRack";
        options.filenameSuffix = ".settings";
        options.folderName = dataFolder.getFullPathName();
        options.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (dataFolder.getChildFile ("StreamRack.settings"), options);

        host = std::make_unique<PluginHost> (dataFolder);
        rack = std::make_unique<RackProcessor> (*host);

        // Restore the last session (including insert plugins) before audio starts.
        if (auto xml = juce::XmlDocument::parse (getAutosaveFile()))
            rack->restoreStateTree (juce::ValueTree::fromXml (*xml));

        const auto savedDevice = settings->getXmlValue ("audioDevice");
        const auto error = deviceManager.initialise (maxInputs, 2, savedDevice.get(), true);
        if (error.isNotEmpty())
        {
            DBG ("Audio device error: " << error);
        }
        deviceManager.addChangeListener (this);

        player.setProcessor (rack.get());
        deviceManager.addAudioCallback (&player);

        window = std::make_unique<MainWindow> (*rack, deviceManager, *settings);
        startTimer (30000);
    }

    void shutdown() override
    {
        stopTimer();
        autosave();

        if (window != nullptr)
            settings->setValue ("window", window->getWindowStateAsString());
        window.reset();

        deviceManager.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        deviceManager.removeChangeListener (this);
        deviceManager.closeAudioDevice();

        rack.reset();
        host.reset();
        settings->saveIfNeeded();
        settings.reset();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    juce::File getAutosaveFile() const { return getDataFolder().getChildFile ("autosave.streamrack"); }

    void autosave()
    {
        if (rack == nullptr)
            return;
        if (auto xml = rack->createStateTree().createXml())
            xml->writeTo (getAutosaveFile());
    }

    void timerCallback() override { autosave(); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (auto xml = deviceManager.createStateXml())
            settings->setValue ("audioDevice", xml.get());
        settings->saveIfNeeded();
    }

    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<PluginHost> host;
    std::unique_ptr<RackProcessor> rack;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<MainWindow> window;
};
} // namespace sr

START_JUCE_APPLICATION (sr::StreamRackApplication)
