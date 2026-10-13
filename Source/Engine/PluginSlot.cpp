#include "PluginSlot.h"

namespace sr
{
void PluginSlot::configure (juce::AudioPluginInstance& p)
{
    p.enableAllBuses();

    // Prefer stereo in/out; fall back to whatever the plugin defaults to.
    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    if (p.getBusCount (true) > 1 || p.getBusCount (false) > 1 || ! p.setBusesLayout (stereo))
    {
        auto current = p.getBusesLayout();
        if (current.inputBuses.size() > 0)
            current.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (current.outputBuses.size() > 0)
            current.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        p.setBusesLayout (current);
    }

    p.setNonRealtime (false);
    p.prepareToPlay (fs, maxBlock);
}

void PluginSlot::prepare (double sampleRate, int maxBlockSize)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    fs = sampleRate;
    maxBlock = maxBlockSize;

    if (plugin != nullptr)
    {
        plugin->releaseResources();
        configure (*plugin);
        buffer.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), maxBlock);
        latency = plugin->getLatencySamples();
    }
}

std::unique_ptr<juce::AudioPluginInstance> PluginSlot::setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin)
{
    juce::AudioBuffer<float> newBuffer;
    if (newPlugin != nullptr)
    {
        configure (*newPlugin);
        newBuffer.setSize (juce::jmax (2, newPlugin->getTotalNumInputChannels(), newPlugin->getTotalNumOutputChannels()),
                           maxBlock);
    }

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (plugin, newPlugin);
        std::swap (buffer, newBuffer);
        latency = plugin != nullptr ? plugin->getLatencySamples() : 0;
        loaded = plugin != nullptr;
    }

    if (newPlugin != nullptr)
        newPlugin->releaseResources();

    return newPlugin;
}

bool PluginSlot::process (float* L, float* R, int n) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || plugin == nullptr || n > buffer.getNumSamples())
        return false;

    latency = plugin->getLatencySamples();

    if (bypassed)
        return false;

    const int numIn = plugin->getMainBusNumInputChannels();
    const int numOut = plugin->getMainBusNumOutputChannels();

    buffer.clear();
    if (numIn >= 2)
    {
        buffer.copyFrom (0, 0, L, n);
        buffer.copyFrom (1, 0, R, n);
    }
    else if (numIn == 1)
    {
        buffer.copyFrom (0, 0, L, n, 0.5f);
        buffer.addFrom (0, 0, R, n, 0.5f);
    }

    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);
    midi.clear();
    plugin->processBlock (view, midi);

    if (numOut >= 2)
    {
        juce::FloatVectorOperations::copy (L, buffer.getReadPointer (0), n);
        juce::FloatVectorOperations::copy (R, buffer.getReadPointer (1), n);
    }
    else if (numOut == 1)
    {
        juce::FloatVectorOperations::copy (L, buffer.getReadPointer (0), n);
        juce::FloatVectorOperations::copy (R, buffer.getReadPointer (0), n);
    }

    return true;
}
} // namespace sr
