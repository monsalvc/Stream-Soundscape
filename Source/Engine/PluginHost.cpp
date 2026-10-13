#include "PluginHost.h"

namespace sr
{
PluginHost::PluginHost (juce::File dataDirectory)
    : dataDir (std::move (dataDirectory)), listFile (dataDir.getChildFile ("plugins.xml"))
{
    formats.addDefaultFormats();

    if (auto xml = juce::XmlDocument::parse (listFile))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);
}

PluginHost::~PluginHost()
{
    knownPlugins.removeChangeListener (this);
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
    {
        dataDir.createDirectory();
        xml->writeTo (listFile);
    }
}

juce::Array<juce::PluginDescription> PluginHost::getEffects() const
{
    juce::Array<juce::PluginDescription> result;
    for (const auto& d : knownPlugins.getTypes())
        if (! d.isInstrument)
            result.add (d);
    return result;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& desc,
                                                                       double sampleRate, int blockSize,
                                                                       juce::String& error)
{
    return formats.createPluginInstance (desc, sampleRate, blockSize, error);
}
} // namespace sr
