#pragma once

#include "Params.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>

// StreamRack hears whatever the Mac's selected audio device delivers. These helpers tell a network
// audio driver (Dante Virtual Soundcard, SoundGrid, AES67) from a local interface such as a USB
// box, and switch to Dante Virtual Soundcard when it's installed.
namespace sr::devices
{
inline bool isDante (const juce::String& deviceName)
{
    return deviceName.containsIgnoreCase ("Dante");
}

inline bool isNetworkAudio (const juce::String& deviceName)
{
    for (const auto* word : { "Dante", "SoundGrid", "AES67", "RAVENNA", "Merging" })
        if (deviceName.containsIgnoreCase (word))
            return true;
    return false;
}

struct Found
{
    juce::String type, name;
};

// Dante Virtual Soundcard as an input device, if it's installed (Dante Via is a different product
// and isn't the console's network channels).
inline std::optional<Found> findDanteVirtualSoundcard (juce::AudioDeviceManager& dm)
{
    for (auto* type : dm.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        for (const auto& name : type->getDeviceNames (true))
            if (isDante (name) && ! name.containsIgnoreCase ("Via"))
                return Found { type->getTypeName(), name };
    }
    return std::nullopt;
}

// Switches input (and output, if it offers one) to Dante Virtual Soundcard with every input
// channel open. Returns an error message, or an empty string when it worked.
inline juce::String useDanteVirtualSoundcard (juce::AudioDeviceManager& dm)
{
    const auto found = findDanteVirtualSoundcard (dm);
    if (! found)
        return "Dante Virtual Soundcard isn't installed on this Mac (or isn't running).";

    dm.setCurrentAudioDeviceType (found->type, true);
    auto setup = dm.getAudioDeviceSetup();
    setup.inputDeviceName = found->name;
    if (auto* type = dm.getCurrentDeviceTypeObject(); type != nullptr && type->getDeviceNames (false).contains (found->name))
        setup.outputDeviceName = found->name;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, maxInputs, true);
    setup.useDefaultOutputChannels = true;
    return dm.setAudioDeviceSetup (setup, true);
}
} // namespace sr::devices
