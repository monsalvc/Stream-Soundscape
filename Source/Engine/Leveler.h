#pragma once

#include "LoudnessMeter.h"

namespace sr
{
// Slow automatic gain rider. It measures K-weighted loudness over a few seconds and
// turns the mix up or down toward a target, within +/- range dB.
// It holds (instead of pumping up room noise) when a fast 400 ms detector says the input
// is below the gate, or has just dropped well below the slow average: that is the start
// of a pause, not a quieter passage. While holding, the slow average is frozen too, so
// when the music comes back the leveler carries on from where it was instead of
// treating the pause as a very quiet passage and surging.
class Leveler
{
public:
    static constexpr float gateLufs = -45.0f;
    static constexpr float dropDb = 6.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        BiquadCoeffs shelf, highPass;
        kWeighting (fs, shelf, highPass);
        shL = { shelf };
        shR = { shelf };
        hpL = { highPass };
        hpR = { highPass };
        slowCoef = timeCoef (3000.0f, fs); // ~3 s, like short-term loudness
        fastCoef = timeCoef (400.0f, fs);  // ~400 ms, like momentary loudness
        slowEnergy = fastEnergy = 0;
        holding = true;
        gainDb = desiredDb = 0;
        speedSec = -1;
        counter = 0;
    }

    void process (float* L, float* R, int n, bool enabled, float targetLufs, float rangeDb, float speed) noexcept
    {
        if (! juce::exactlyEqual (speed, speedSec))
        {
            speedSec = speed;
            gainCoef = timeCoef (speed * 1000.0f, fs);
        }

        for (int i = 0; i < n; ++i)
        {
            const float l = hpL.process (shL.process (L[i]));
            const float r = hpR.process (shR.process (R[i]));
            const double e = (double) l * l + (double) r * r;
            fastEnergy = fastCoef * fastEnergy + (1.0 - fastCoef) * e;
            if (! holding)
                slowEnergy = slowCoef * slowEnergy + (1.0 - slowCoef) * e;

            if ((++counter & 63) == 0)
            {
                const float fast = toLufs (fastEnergy);

                // First signal after start-up: seed the slow average instead of rising from silence.
                if (slowEnergy <= 0.0 && fast > gateLufs)
                    slowEnergy = fastEnergy;

                const float slow = toLufs (slowEnergy);
                holding = ! (fast > gateLufs && fast > slow - dropDb);

                if (! enabled)
                    desiredDb = 0.0f;
                else if (! holding)
                    desiredDb = juce::jlimit (-rangeDb, rangeDb, targetLufs - slow);
                else
                    desiredDb = juce::jlimit (-rangeDb, rangeDb, desiredDb);
            }

            gainDb = desiredDb + gainCoef * (gainDb - desiredDb);
            const float g = dbToGainFast (gainDb);
            L[i] *= g;
            R[i] *= g;
        }

        currentGainDb = gainDb;
    }

    float getGainDb() const noexcept { return currentGainDb; }

private:
    static float toLufs (double energy) noexcept { return (float) (-0.691 + 10.0 * std::log10 (energy + 1.0e-12)); }

    double fs = 48000.0, slowEnergy = 0.0, fastEnergy = 0.0;
    float slowCoef = 0, fastCoef = 0, gainCoef = 0, speedSec = -1, gainDb = 0, desiredDb = 0, currentGainDb = 0;
    Biquad shL, shR, hpL, hpR;
    unsigned int counter = 0;
    bool holding = true;
};
} // namespace sr
