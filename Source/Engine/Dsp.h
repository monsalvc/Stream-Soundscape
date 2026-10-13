#pragma once

// Small, allocation-free DSP building blocks used on the audio thread.

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace sr
{
inline float timeCoef (float ms, double fs)
{
    return ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * fs));
}

inline float gainToDbFast (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }
inline float dbToGainFast (float db) { return std::pow (10.0f, db * 0.05f); }

//==============================================================================
struct BiquadCoeffs
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// RBJ "Audio EQ Cookbook" designs.
namespace rbj
{
    inline BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
    }

    inline double clampFreq (double f, double fs) { return juce::jlimit (10.0, fs * 0.45, f); }

    inline BiquadCoeffs highPass (double fs, double f, double q)
    {
        const double w = juce::MathConstants<double>::twoPi * clampFreq (f, fs) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    inline BiquadCoeffs peak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = juce::MathConstants<double>::twoPi * clampFreq (f, fs) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
    }

    inline BiquadCoeffs lowShelf (double fs, double f, double gainDb, double q = 0.7071)
    {
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        const double w = juce::MathConstants<double>::twoPi * clampFreq (f, fs) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise (A * ((A + 1) - (A - 1) * c + 2 * sA * alpha),
                          2 * A * ((A - 1) - (A + 1) * c),
                          A * ((A + 1) - (A - 1) * c - 2 * sA * alpha),
                          (A + 1) + (A - 1) * c + 2 * sA * alpha,
                          -2 * ((A - 1) + (A + 1) * c),
                          (A + 1) + (A - 1) * c - 2 * sA * alpha);
    }

    inline BiquadCoeffs highShelf (double fs, double f, double gainDb, double q = 0.7071)
    {
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        const double w = juce::MathConstants<double>::twoPi * clampFreq (f, fs) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise (A * ((A + 1) + (A - 1) * c + 2 * sA * alpha),
                          -2 * A * ((A - 1) + (A + 1) * c),
                          A * ((A + 1) + (A - 1) * c - 2 * sA * alpha),
                          (A + 1) - (A - 1) * c + 2 * sA * alpha,
                          2 * ((A - 1) - (A + 1) * c),
                          (A + 1) - (A - 1) * c - 2 * sA * alpha);
    }
} // namespace rbj

// Transposed direct form II biquad.
struct Biquad
{
    BiquadCoeffs c;
    float z1 = 0, z2 = 0;

    inline float process (float x) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void processBlock (float* data, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            data[i] = process (data[i]);
    }

    void reset() noexcept { z1 = z2 = 0; }
};

//==============================================================================
// Stereo-linked feed-forward compressor with a 6 dB soft knee.
class Compressor
{
public:
    void prepare (double sampleRate) { fs = sampleRate; envDb = 0; atkMs = relMs = -1; }

    void process (float* L, float* R, int n, float thresholdDb, float ratio, float attackMs, float releaseMs,
                  float makeupDb) noexcept
    {
        if (! juce::exactlyEqual (attackMs, atkMs) || ! juce::exactlyEqual (releaseMs, relMs))
        {
            atkMs = attackMs;
            relMs = releaseMs;
            atk = timeCoef (attackMs, fs);
            rel = timeCoef (releaseMs, fs);
        }

        const float slope = 1.0f / juce::jmax (1.0f, ratio) - 1.0f;
        float maxReduction = 0;

        for (int i = 0; i < n; ++i)
        {
            const float xDb = gainToDbFast (juce::jmax (std::abs (L[i]), std::abs (R[i])));
            const float over = xDb - thresholdDb;
            float target;

            if (2.0f * over < -knee)
                target = 0;
            else if (2.0f * std::abs (over) <= knee)
                target = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            else
                target = slope * over;

            const float a = target < envDb ? atk : rel;
            envDb = target + a * (envDb - target);
            maxReduction = juce::jmin (maxReduction, envDb);

            const float g = dbToGainFast (envDb + makeupDb);
            L[i] *= g;
            R[i] *= g;
        }

        lastReductionDb = maxReduction;
    }

    float getReductionDb() const noexcept { return lastReductionDb; }

private:
    static constexpr float knee = 6.0f;
    double fs = 48000;
    float envDb = 0, atk = 0, rel = 0, atkMs = -1, relMs = -1, lastReductionDb = 0;
};

//==============================================================================
// Stereo-linked noise gate with fixed attack/hold/release, closing down to -60 dB.
class Gate
{
public:
    void prepare (double fs)
    {
        atk = timeCoef (1.0f, fs);
        rel = timeCoef (150.0f, fs);
        detRel = timeCoef (10.0f, fs);
        holdSamples = (int) (0.05 * fs);
        env = 0;
        gain = 1;
        hold = 0;
    }

    void process (float* L, float* R, int n, float thresholdDb) noexcept
    {
        const float thr = juce::Decibels::decibelsToGain (thresholdDb);
        constexpr float closedGain = 0.001f;

        for (int i = 0; i < n; ++i)
        {
            const float x = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            env = x > env ? x : env * detRel;

            float target = closedGain;
            if (env > thr)
            {
                hold = holdSamples;
                target = 1.0f;
            }
            else if (hold > 0)
            {
                --hold;
                target = 1.0f;
            }

            gain = target + (target > gain ? atk : rel) * (gain - target);
            L[i] *= gain;
            R[i] *= gain;
        }
    }

    bool isOpen() const noexcept { return gain > 0.5f; }

private:
    float atk = 0, rel = 0, detRel = 0, env = 0, gain = 1;
    int holdSamples = 0, hold = 0;
};

//==============================================================================
// Look-ahead brickwall limiter. The gain curve is a min-hold over the look-ahead
// window followed by a box filter of the same length, so the gain is already at
// (or below) the required level when each peak leaves the delay line.
class Limiter
{
public:
    void prepare (double sampleRate, float lookaheadMs = 1.5f)
    {
        fs = sampleRate;
        length = juce::jmax (2, (int) std::round (lookaheadMs * 0.001 * fs));
        required.assign ((size_t) length, 1.0f);
        smoothed.assign ((size_t) length, 1.0f);
        delayL.assign ((size_t) length, 0.0f);
        delayR.assign ((size_t) length, 0.0f);
        pos = 0;
        boxSum = length;
        released = 1.0f;
        relMs = -1;
        samplesSinceResum = 0;
    }

    int getLatencySamples() const noexcept { return length - 1; }

    void process (float* L, float* R, int n, float ceilingDb, float releaseMs) noexcept
    {
        if (! juce::exactlyEqual (releaseMs, relMs))
        {
            relMs = releaseMs;
            relCoef = timeCoef (releaseMs, fs);
        }

        const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        float minGain = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const float peak = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            required[(size_t) pos] = peak > ceiling ? ceiling / peak : 1.0f;

            float hold = 1.0f;
            for (float r : required)
                hold = juce::jmin (hold, r);

            released = hold < released ? hold : hold + relCoef * (released - hold);

            boxSum += released - smoothed[(size_t) pos];
            smoothed[(size_t) pos] = released;

            if (++samplesSinceResum >= 4096)
            {
                samplesSinceResum = 0;
                boxSum = 0;
                for (float s : smoothed)
                    boxSum += s;
            }

            const float g = juce::jmin (1.0f, (float) (boxSum / length));
            minGain = juce::jmin (minGain, g);

            // Read the sample written length - 1 samples ago, then store the new one.
            const int readPos = (pos + 1) % length;
            const float outL = delayL[(size_t) readPos] * g;
            const float outR = delayR[(size_t) readPos] * g;
            delayL[(size_t) pos] = L[i];
            delayR[(size_t) pos] = R[i];

            L[i] = juce::jlimit (-ceiling, ceiling, outL);
            R[i] = juce::jlimit (-ceiling, ceiling, outR);

            pos = readPos;
        }

        lastReductionDb = gainToDbFast (minGain);
    }

    float getReductionDb() const noexcept { return lastReductionDb; }

private:
    double fs = 48000;
    int length = 2, pos = 0, samplesSinceResum = 0;
    std::vector<float> required, smoothed, delayL, delayR;
    double boxSum = 2;
    float released = 1, relCoef = 0, relMs = -1, lastReductionDb = 0;
};

//==============================================================================
// Stereo delay used for plugin delay compensation between strips.
class CompensationDelay
{
public:
    static constexpr int maxDelay = 16384;

    void prepare()
    {
        bufL.assign ((size_t) maxDelay, 0.0f);
        bufR.assign ((size_t) maxDelay, 0.0f);
        pos = 0;
    }

    void process (float* L, float* R, int n, int delay) noexcept
    {
        delay = juce::jlimit (0, maxDelay - 1, delay);
        if (delay == 0 && lastDelay == 0)
            return;

        lastDelay = delay;
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) pos] = L[i];
            bufR[(size_t) pos] = R[i];
            const int read = (pos - delay + maxDelay) % maxDelay;
            L[i] = bufL[(size_t) read];
            R[i] = bufR[(size_t) read];
            pos = (pos + 1) % maxDelay;
        }
    }

private:
    std::vector<float> bufL, bufR;
    int pos = 0, lastDelay = 0;
};
} // namespace sr
