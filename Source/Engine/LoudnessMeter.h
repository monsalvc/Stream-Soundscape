#pragma once

// ITU-R BS.1770-4 / EBU R128 loudness meter: momentary (400 ms), short-term (3 s)
// and gated integrated loudness. Runs on the audio thread without allocating;
// results are published through atomics for the UI.

#include "Dsp.h"
#include <array>
#include <atomic>

namespace sr
{
class LoudnessMeter
{
public:
    static constexpr float silence = -100.0f;

    void prepare (double fs)
    {
        // K-weighting coefficients for any sample rate (same derivation as libebur128).
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0);
            shelf.b1 = (float) (2.0 * (K * K - Vh) / a0);
            shelf.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
            shelf.a1 = (float) (2.0 * (K * K - 1.0) / a0);
            shelf.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highPass.b0 = 1.0f;
            highPass.b1 = -2.0f;
            highPass.b2 = 1.0f;
            highPass.a1 = (float) (2.0 * (K * K - 1.0) / a0);
            highPass.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }

        blockLength = juce::jmax (1, (int) std::round (fs * 0.1));
        resetAll();
    }

    // Safe to call from any thread; the reset happens on the next audio block.
    void requestReset() noexcept { resetRequested = true; }

    void process (const float* L, const float* R, int n) noexcept
    {
        if (resetRequested.exchange (false))
            resetAll();

        for (int i = 0; i < n; ++i)
        {
            const float l = hpL.process (shL.process (L[i]));
            const float r = hpR.process (shR.process (R[i]));
            blockSum += (double) l * l + (double) r * r;

            if (++blockCount == blockLength)
                finishBlock();
        }
    }

    std::atomic<float> momentary { silence }, shortTerm { silence }, integrated { silence };

private:
    static constexpr int numBins = 750; // -70 .. +5 LUFS in 0.1 LU steps
    static constexpr int ringSize = 30; // 3 s of 100 ms blocks

    static float toLufs (double energy) { return energy > 0 ? (float) (-0.691 + 10.0 * std::log10 (energy)) : silence; }

    void resetAll() noexcept
    {
        shL = { shelf };
        shR = { shelf };
        hpL = { highPass };
        hpR = { highPass };
        blockSum = 0;
        blockCount = 0;
        ring.fill (0);
        ringPos = ringFilled = 0;
        binEnergy.fill (0);
        binCount.fill (0);
        momentary = shortTerm = integrated = silence;
    }

    double meanOfLast (int count) const noexcept
    {
        double sum = 0;
        for (int k = 0; k < count; ++k)
            sum += ring[(size_t) ((ringPos - 1 - k + ringSize) % ringSize)];
        return sum / count;
    }

    void finishBlock() noexcept
    {
        ring[(size_t) ringPos] = blockSum / blockLength;
        ringPos = (ringPos + 1) % ringSize;
        ringFilled = juce::jmin (ringFilled + 1, ringSize);
        blockSum = 0;
        blockCount = 0;

        if (ringFilled >= 4)
        {
            // 400 ms gating blocks with 75 % overlap.
            const double e = meanOfLast (4);
            const float lufs = toLufs (e);
            momentary = lufs;

            if (lufs >= -70.0f)
            {
                const int bin = juce::jlimit (0, numBins - 1, (int) ((lufs + 70.0f) * 10.0f));
                binEnergy[(size_t) bin] += e;
                ++binCount[(size_t) bin];
                updateIntegrated();
            }
        }

        if (ringFilled >= ringSize)
            shortTerm = toLufs (meanOfLast (ringSize));
    }

    void updateIntegrated() noexcept
    {
        double sum = 0;
        int64_t count = 0;
        for (int b = 0; b < numBins; ++b)
        {
            sum += binEnergy[(size_t) b];
            count += binCount[(size_t) b];
        }

        if (count == 0)
            return;

        const float relativeGate = toLufs (sum / (double) count) - 10.0f;
        sum = 0;
        count = 0;
        for (int b = 0; b < numBins; ++b)
        {
            if (-70.0f + (float) b * 0.1f >= relativeGate - 0.05f)
            {
                sum += binEnergy[(size_t) b];
                count += binCount[(size_t) b];
            }
        }

        if (count > 0)
            integrated = toLufs (sum / (double) count);
    }

    BiquadCoeffs shelf, highPass;
    Biquad shL, shR, hpL, hpR;
    int blockLength = 4800, blockCount = 0, ringPos = 0, ringFilled = 0;
    double blockSum = 0;
    std::array<double, ringSize> ring {};
    std::array<double, numBins> binEnergy {};
    std::array<int64_t, numBins> binCount {};
    std::atomic<bool> resetRequested { false };
};
} // namespace sr
