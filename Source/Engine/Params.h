#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sr
{
constexpr int numStrips = 24;
constexpr int maxInputs = 64;
constexpr int slotsPerStrip = 2;
constexpr int numMasterSlots = 2;

// Faders and sends at or below this level are treated as fully off.
constexpr float offDb = -60.0f;

// Loudness target shown on the master meter (YouTube normalises to about -14 LUFS).
constexpr float loudnessTarget = -14.0f;

// Parameter ID for a strip parameter, e.g. stripId (3, "fader") -> "s3_fader".
juce::String stripId (int strip, const char* name);

// Parameter ID for a master parameter, e.g. masterId ("ceiling") -> "m_ceiling".
juce::String masterId (const char* name);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

inline float dbToGainWithOff (float db)
{
    return db <= offDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}
} // namespace sr
