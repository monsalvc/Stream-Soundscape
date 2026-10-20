#include "RackProcessor.h"

namespace sr
{
namespace
{
    const juce::Identifier slotsTag ("SLOTS");
    const juce::Identifier slotTag ("SLOT");
    const juce::Identifier idProp ("id");
    const juce::Identifier bypassProp ("bypass");
    const juce::Identifier pluginStateProp ("pluginState");
    const juce::Identifier descTag ("PLUGIN");

    juce::Identifier nameProp (int strip) { return "name" + juce::String (strip); }
    juce::Identifier pairNameProp (int pair) { return "pairName" + juce::String (pair); }

    float peakOf (const float* data, int n) noexcept
    {
        return juce::jmax (juce::FloatVectorOperations::findMaximum (data, n),
                           -juce::FloatVectorOperations::findMinimum (data, n));
    }
} // namespace

RackProcessor::RackProcessor (PluginHost& h)
    : AudioProcessor (BusesProperties()
                          .withInput ("Dante", juce::AudioChannelSet::discreteChannels (maxInputs), true)
                          .withOutput ("Stream", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "STREAMRACK", createParameterLayout()),
      host (h)
{
    for (int i = 0; i < numStrips; ++i)
        strips[(size_t) i].bind (state, i);

    for (int i = 0; i < numMasterSlots; ++i)
        masterSlots[(size_t) i] = std::make_unique<PluginSlot> (masterId ("slot") + juce::String (i));
    reverbSlot = std::make_unique<PluginSlot> (masterId ("verbSlot"));

    auto get = [this] (const char* name) { return state.getRawParameterValue (masterId (name)); };
    m = { get ("compOn"),   get ("compThr"),  get ("compRatio"),  get ("compAtk"), get ("compRel"),
          get ("compMakeup"), get ("verbSize"), get ("verbDamp"), get ("verbReturn"), get ("limOn"),
          get ("ceiling"),  get ("limRel"),   get ("fader"),    get ("busMode"), get ("busSrc"),
          get ("busTrim"),  get ("levOn"),    get ("levTarget"), get ("levRange"), get ("levSpeed") };
}

RackProcessor::~RackProcessor()
{
    // Plugins must be released before the host's format manager goes away.
    forEachSlot ([] (PluginSlot& s) { s.setPlugin (nullptr); });
}

void RackProcessor::forEachSlot (const std::function<void (PluginSlot&)>& fn)
{
    for (auto& s : strips)
        for (int i = 0; i < slotsPerStrip; ++i)
            fn (s.getSlot (i));
    for (auto& s : masterSlots)
        fn (*s);
    fn (*reverbSlot);
}

bool RackProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const int ins = layout.getMainInputChannels();
    const int outs = layout.getMainOutputChannels();
    return ins <= maxInputs && outs >= 1 && outs <= maxInputs;
}

void RackProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (32, maximumExpectedSamplesPerBlock);

    for (auto& s : strips)
        s.prepare (sampleRate, maxBlock);
    for (auto& s : masterSlots)
        s->prepare (sampleRate, maxBlock);
    reverbSlot->prepare (sampleRate, maxBlock);

    mix.setSize (2, maxBlock);
    send.setSize (2, maxBlock);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    busComp.prepare (sampleRate);
    limiter.prepare (sampleRate);
    leveler.prepare (sampleRate);
    busTrim.reset (sampleRate, 0.05);
    fadeLength = juce::jmax (1, (int) (sampleRate * 0.05));
    fadeFromPair = -1;
    loudness.prepare (sampleRate);
    masterGain.reset (sampleRate, 0.05);
    verbReturn.reset (sampleRate, 0.05);
}

void RackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numIn = juce::jmin (getTotalNumInputChannels(), maxInputs, buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
    const int total = buffer.getNumSamples();

    // Inputs and outputs share the buffer, so every chunk reads all of its inputs
    // into the mix before anything is written back.
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        for (int ch = 0; ch < numIn; ++ch)
            inputPtrs[(size_t) ch] = buffer.getReadPointer (ch, start);

        processChunk (inputPtrs.data(), numIn, mix.getWritePointer (0), mix.getWritePointer (1), n);

        if (numOut >= 2)
        {
            // The stream mix goes to every output pair (e.g. 1-2 to Wirecast via BlackHole,
            // 3-4 to the operator's headphones in the same aggregate device).
            for (int ch = 0; ch + 1 < numOut; ch += 2)
            {
                buffer.copyFrom (ch, start, mix, 0, 0, n);
                buffer.copyFrom (ch + 1, start, mix, 1, 0, n);
            }
            for (int ch = numOut - numOut % 2; ch < buffer.getNumChannels(); ++ch)
                buffer.clear (ch, start, n);
        }
        else if (numOut == 1)
        {
            buffer.copyFrom (0, start, mix.getReadPointer (0), n, 0.5f);
            buffer.addFrom (0, start, mix.getReadPointer (1), n, 0.5f);
            for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
                buffer.clear (ch, start, n);
        }
    }
}

void RackProcessor::processChunk (const float* const* inputs, int numInputs, float* outL, float* outR, int n) noexcept
{
    for (int ch = 0; ch < numInputs; ++ch)
        if (const float p = peakOf (inputs[ch], n); p > inputPeaks[(size_t) ch].load())
            inputPeaks[(size_t) ch] = p;

    auto* sendL = send.getWritePointer (0);
    auto* sendR = send.getWritePointer (1);
    juce::FloatVectorOperations::clear (outL, n);
    juce::FloatVectorOperations::clear (outR, n);
    juce::FloatVectorOperations::clear (sendL, n);
    juce::FloatVectorOperations::clear (sendR, n);

    if (m.busMode->load() > 0.5f)
    {
        // Bus mode: the selected input pair is the mix.
        const auto copyPair = [&] (int pair, float* l, float* r)
        {
            const int first = pair * 2;
            if (first >= numInputs)
                return false;
            juce::FloatVectorOperations::copy (l, inputs[first], n);
            juce::FloatVectorOperations::copy (r, inputs[first + 1 < numInputs ? first + 1 : first], n);
            return true;
        };

        const int pair = (int) m.busSrc->load();
        if (activePair < 0)
            activePair = pair;
        if (pair != activePair && fadeFromPair < 0)
        {
            fadeFromPair = activePair;
            activePair = pair;
            fadePos = 0;
        }
        copyPair (activePair, outL, outR);

        if (fadeFromPair >= 0)
        {
            // Equal-power crossfade: the two buses are unrelated signals. The send buffer is
            // free in bus mode, so it holds the old pair while fading.
            if (! copyPair (fadeFromPair, sendL, sendR))
            {
                juce::FloatVectorOperations::clear (sendL, n);
                juce::FloatVectorOperations::clear (sendR, n);
            }
            for (int i = 0; i < n; ++i)
            {
                const float t = juce::jmin (1.0f, (float) (fadePos + i) / (float) fadeLength);
                const float in = std::sin (t * juce::MathConstants<float>::halfPi);
                const float out = std::cos (t * juce::MathConstants<float>::halfPi);
                outL[i] = outL[i] * in + sendL[i] * out;
                outR[i] = outR[i] * in + sendR[i] * out;
            }
            fadePos += n;
            if (fadePos >= fadeLength)
                fadeFromPair = -1;
        }

        busTrim.setTargetValue (juce::Decibels::decibelsToGain (m.busTrim->load()));
        for (int i = 0; i < n; ++i)
        {
            const float g = busTrim.getNextValue();
            outL[i] *= g;
            outR[i] *= g;
        }

        const float pl = peakOf (outL, n), pr = peakOf (outR, n);
        if (pl > busInPeakL.load())
            busInPeakL = pl;
        if (pr > busInPeakR.load())
            busInPeakR = pr;
    }
    else
    {
        int compensation = 0;
        for (auto& s : strips)
            compensation = juce::jmax (compensation, s.getLatencySamples());

        for (auto& s : strips)
            s.process (inputs, numInputs, n, compensation, outL, outR, sendL, sendR);

        // Reverb bus (wet only), returned into the mix.
        juce::Reverb::Parameters rp;
        rp.roomSize = m.verbSize->load();
        rp.damping = m.verbDamp->load();
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        if (reverbSlot->hasPlugin())
        {
            // Wet-only bus: if the plugin is bypassed (or mid-swap) return nothing rather than
            // the dry send, which would double the channels.
            if (! reverbSlot->process (sendL, sendR, n))
            {
                juce::FloatVectorOperations::clear (sendL, n);
                juce::FloatVectorOperations::clear (sendR, n);
            }
        }
        else
        {
            reverb.setParameters (rp);
            reverb.processStereo (sendL, sendR, n);
        }
        verbReturn.setTargetValue (dbToGainWithOff (m.verbReturn->load()));
        for (int i = 0; i < n; ++i)
        {
            const float g = verbReturn.getNextValue();
            outL[i] += sendL[i] * g;
            outR[i] += sendR[i] * g;
        }
    }

    leveler.process (outL, outR, n, m.levOn->load() > 0.5f, m.levTarget->load(), m.levRange->load(),
                     m.levSpeed->load());
    levelerGainDb = leveler.getGainDb();

    // Master bus.
    if (m.compOn->load() > 0.5f)
    {
        busComp.process (outL, outR, n, m.compThr->load(), m.compRatio->load(), m.compAtk->load(),
                         m.compRel->load(), m.compMakeup->load());
        busCompReductionDb = busComp.getReductionDb();
    }
    else
    {
        busCompReductionDb = 0.0f;
    }

    for (auto& s : masterSlots)
        s->process (outL, outR, n);

    masterGain.setTargetValue (dbToGainWithOff (m.fader->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue();
        outL[i] *= g;
        outR[i] *= g;
    }

    if (m.limOn->load() > 0.5f)
    {
        limiter.process (outL, outR, n, m.ceiling->load(), m.limRel->load());
        limiterReductionDb = limiter.getReductionDb();
    }
    else
    {
        limiterReductionDb = 0.0f;
    }

    loudness.process (outL, outR, n);

    const float pl = peakOf (outL, n), pr = peakOf (outR, n);
    if (pl > masterPeakL.load())
        masterPeakL = pl;
    if (pr > masterPeakR.load())
        masterPeakR = pr;
}

//==============================================================================
juce::String RackProcessor::getStripName (int i) const
{
    const auto name = state.state.getProperty (nameProp (i)).toString();
    return name.isNotEmpty() ? name : "Ch " + juce::String (i + 1);
}

void RackProcessor::setStripName (int i, const juce::String& name)
{
    state.state.setProperty (nameProp (i), name.trim(), nullptr);
}

juce::String RackProcessor::getPairName (int pair) const
{
    return state.state.getProperty (pairNameProp (pair)).toString();
}

void RackProcessor::setPairName (int pair, const juce::String& name)
{
    state.state.setProperty (pairNameProp (pair), name.trim(), nullptr);
}

PluginSlot* RackProcessor::findSlot (const juce::String& id)
{
    PluginSlot* found = nullptr;
    forEachSlot ([&] (PluginSlot& s) { if (s.getId() == id) found = &s; });
    return found;
}

bool RackProcessor::loadPlugin (PluginSlot& slot, const juce::PluginDescription* desc, juce::String& error)
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    if (desc != nullptr)
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        instance = host.createInstance (*desc, sr, maxBlock, error);
        if (instance == nullptr)
            return false;
    }

    if (onSlotAboutToChange)
        onSlotAboutToChange (slot);

    slot.bypassed = false;
    auto old = slot.setPlugin (std::move (instance));
    old.reset();
    return true;
}

//==============================================================================
juce::ValueTree RackProcessor::createStateTree()
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName (slotsTag), nullptr);

    juce::ValueTree slotsTree (slotsTag);
    auto addSlot = [&] (PluginSlot& slot)
    {
        auto* plugin = slot.getPlugin();
        if (plugin == nullptr)
            return;

        juce::MemoryBlock data;
        plugin->getStateInformation (data);

        juce::ValueTree s (slotTag);
        s.setProperty (idProp, slot.getId(), nullptr);
        s.setProperty (bypassProp, slot.bypassed.load(), nullptr);
        s.setProperty (pluginStateProp, data.toBase64Encoding(), nullptr);
        if (auto xml = plugin->getPluginDescription().createXml())
            s.appendChild (juce::ValueTree::fromXml (*xml), nullptr);
        slotsTree.appendChild (s, nullptr);
    };

    forEachSlot (addSlot);

    tree.appendChild (slotsTree, nullptr);
    return tree;
}

void RackProcessor::restoreStateTree (const juce::ValueTree& source)
{
    if (! source.hasType (state.state.getType()))
        return;

    auto tree = source.createCopy();

    // Pair names describe the Dante routing rather than a scene, so a scene saved before
    // they were set keeps the current ones.
    for (int pair = 0; pair < maxInputs / 2; ++pair)
        if (! tree.hasProperty (pairNameProp (pair)) && state.state.hasProperty (pairNameProp (pair)))
            tree.setProperty (pairNameProp (pair), state.state.getProperty (pairNameProp (pair)), nullptr);

    const auto slotsTree = tree.getChildWithName (slotsTag);
    tree.removeChild (slotsTree, nullptr);
    state.replaceState (tree);

    // Clear every insert, then load the ones saved in this state.
    juce::String error;
    forEachSlot ([&] (PluginSlot& s) { loadPlugin (s, nullptr, error); });

    for (const auto& s : slotsTree)
    {
        auto* slot = findSlot (s.getProperty (idProp).toString());
        const auto descTree = s.getChildWithName (descTag);
        if (slot == nullptr || ! descTree.isValid())
            continue;

        juce::PluginDescription desc;
        if (auto xml = descTree.createXml(); xml == nullptr || ! desc.loadFromXml (*xml))
            continue;

        if (! loadPlugin (*slot, &desc, error))
        {
            DBG ("Could not load " << desc.name << ": " << error);
            continue;
        }

        juce::MemoryBlock data;
        if (data.fromBase64Encoding (s.getProperty (pluginStateProp).toString()) && data.getSize() > 0)
            slot->getPlugin()->setStateInformation (data.getData(), (int) data.getSize());
        slot->bypassed = (bool) s.getProperty (bypassProp, false);
    }
}

void RackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, dest);
}

void RackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        restoreStateTree (juce::ValueTree::fromXml (*xml));
}
} // namespace sr
