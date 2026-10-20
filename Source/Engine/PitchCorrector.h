#pragma once

// Vocal pitch correction for one channel: a YIN pitch detector decides which semitone the
// singer is aiming for, and a pitch-synchronous time-domain shifter moves the voice toward it.
// Chromatic only (every semitone is a valid target), so there is no key to set during a service.

#include "Dsp.h"
#include <atomic>

namespace sr
{
// Monophonic pitch detector (YIN, de Cheveigne & Kawahara 2002) running on a ~24 kHz
// decimated copy of the input. Results are in MIDI note numbers (69 = A4 = 440 Hz).
class PitchDetector
{
public:
    static constexpr float minFreq = 70.0f, maxFreq = 1000.0f;

    void prepare (double sampleRate)
    {
        decimation = juce::jmax (1, (int) std::round (sampleRate / 24000.0));
        anaRate = sampleRate / decimation;
        minLag = (int) std::floor (anaRate / maxFreq);
        maxLag = (int) std::ceil (anaRate / minFreq);
        window = (int) std::round (anaRate * 0.02); // 20 ms integration window
        hop = juce::jmax (1, (int) std::round (anaRate * 0.005));
        ring.assign ((size_t) ringSize, 0.0f);
        frame.assign ((size_t) (window + maxLag + 2), 0.0f);
        diff.assign ((size_t) (maxLag + 2), 0.0f);
        antiAlias[0].c = antiAlias[1].c = rbj::lowPass (sampleRate, 0.4 * anaRate * 0.5, 0.7071);
        antiAlias[0].reset();
        antiAlias[1].reset();
        pos = decimCount = hopCount = 0;
        midi = -1.0f;
    }

    // Feed one input sample. Returns true when a new estimate is available.
    bool push (float x) noexcept
    {
        x = antiAlias[1].process (antiAlias[0].process (x));
        if (++decimCount < decimation)
            return false;
        decimCount = 0;

        ring[(size_t) pos] = x;
        pos = (pos + 1) & (ringSize - 1);

        if (++hopCount < hop)
            return false;
        hopCount = 0;
        midi = analyse();
        return true;
    }

    // Latest estimate in MIDI notes, or -1 when there is no clear pitch (consonants, breaths, silence).
    float getMidi() const noexcept { return midi; }

    static float freqToMidi (float f) { return 69.0f + 12.0f * std::log2 (f / 440.0f); }

private:
    static constexpr int ringSize = 2048;
    static constexpr float threshold = 0.15f, silenceRms = 0.003f; // about -50 dBFS

    float analyse() noexcept
    {
        const int len = window + maxLag;
        for (int i = 0; i < len; ++i)
            frame[(size_t) i] = ring[(size_t) ((pos - len + i + ringSize) & (ringSize - 1))];

        double energy = 0;
        for (int i = 0; i < window; ++i)
            energy += (double) frame[(size_t) i] * frame[(size_t) i];
        if (std::sqrt (energy / window) < silenceRms)
            return -1.0f;

        // Difference function and cumulative mean normalised difference.
        double running = 0;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0;
            for (int j = 0; j < window; ++j)
            {
                const float delta = frame[(size_t) j] - frame[(size_t) (j + tau)];
                d += (double) delta * delta;
            }
            running += d;
            diff[(size_t) tau] = running > 0 ? (float) (d * tau / running) : 1.0f;
        }

        // First dip below the threshold, then walk down to its minimum.
        int tau = minLag;
        while (tau < maxLag && diff[(size_t) tau] >= threshold)
            ++tau;
        if (tau >= maxLag)
            return -1.0f;
        while (tau + 1 < maxLag && diff[(size_t) (tau + 1)] < diff[(size_t) tau])
            ++tau;

        // Parabolic interpolation for sub-sample accuracy.
        float lag = (float) tau;
        if (tau > 1 && tau < maxLag)
        {
            const float a = diff[(size_t) (tau - 1)], b = diff[(size_t) tau], c = diff[(size_t) (tau + 1)];
            const float denom = a - 2.0f * b + c;
            if (std::abs (denom) > 1.0e-9f)
                lag += 0.5f * (a - c) / denom;
        }

        const float f = (float) anaRate / lag;
        return (f >= minFreq && f <= maxFreq) ? freqToMidi (f) : -1.0f;
    }

    double anaRate = 24000.0;
    int decimation = 2, minLag = 24, maxLag = 343, window = 480, hop = 120;
    int pos = 0, decimCount = 0, hopCount = 0;
    std::vector<float> ring, frame, diff;
    std::array<Biquad, 2> antiAlias {};
    float midi = -1.0f;
};

//==============================================================================
// Time-domain pitch shifter for small corrections. Two read heads move through a delay line
// at the shifted rate, so the output frequency is exactly input x ratio. The heads are kept
// a whole number of the singer's periods apart, so their crossfades stay in phase instead of
// wobbling. With no shift requested it fades to a plain delay of the same length, so a
// singer who is in tune (or a consonant, a breath) passes through untouched.
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size = 1;
        while (size < (int) (fs * 0.25))
            size <<= 1;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        write = 0;
        latency = (int) std::ceil (fs * 0.028);
        halfWindowMin = fs * 0.012;
        lastPeriod = fs / 200.0;
        engaged = false;
        mix = 0.0f;
        fadeStep = 1.0f / (float) (0.03 * fs);
    }

    int getLatencySamples() const noexcept { return latency; }

    // Clears the delay line without allocating (audio thread), e.g. when the tuner is switched on.
    void reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        engaged = false;
        mix = 0.0f;
    }

    // ratio: output/input frequency. period: the singer's period in samples (<= 0 if unknown).
    void process (float* L, float* R, int n, double ratio, double period) noexcept
    {
        if (period > 0.0)
            lastPeriod = period;
        const bool wanted = std::abs (ratio - 1.0) > 1.0e-5;
        const double rate = ratio - 1.0;

        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) write] = L[i];
            bufR[(size_t) write] = R[i];

            const int cleanPos = (write - latency + size) & (size - 1);
            float outL = bufL[(size_t) cleanPos], outR = bufR[(size_t) cleanPos];

            if (wanted && ! engaged)
                engage();

            if (engaged)
            {
                float sumL = 0, sumR = 0, sumG = 0;
                for (size_t h = 0; h < 2; ++h)
                {
                    auto& head = heads[h];
                    const float g = gainOf (head);
                    sumL += g * readAt (bufL, head.delay);
                    sumR += g * readAt (bufR, head.delay);
                    sumG += g;
                }
                const float norm = sumG > 0.05f ? 1.0f / sumG : 0.0f;
                const float shiftedL = sumL * norm, shiftedR = sumR * norm;

                mix = wanted ? 1.0f : juce::jmax (0.0f, mix - fadeStep);
                outL = outL + mix * (shiftedL - outL);
                outR = outR + mix * (shiftedR - outR);
                if (! wanted && mix <= 0.0f)
                    engaged = false;

                advanceHeads (wanted ? rate : 0.0);
            }

            L[i] = outL;
            R[i] = outR;
            write = (write + 1) & (size - 1);
        }
    }

private:
    struct Head
    {
        double delay = 0, span = 1;
    };

    double halfWindow() const
    {
        const double m = std::ceil (halfWindowMin / lastPeriod);
        return juce::jmin (m * lastPeriod, (double) latency - 4.0);
    }

    void engage()
    {
        // Head 0 sits exactly on the plain-delay tap at full gain, so switching on is seamless.
        const double half = halfWindow();
        floorDelay = (double) latency - half;
        heads[0] = { (double) latency, 2.0 * half };
        heads[1] = { (double) latency + half, 2.0 * half };
        engaged = true;
        mix = 1.0f;
    }

    float gainOf (const Head& h) const noexcept
    {
        const double x = juce::jlimit (0.0, 1.0, (h.delay - floorDelay) / h.span);
        const double s = std::sin (juce::MathConstants<double>::pi * x);
        return (float) (s * s);
    }

    void advanceHeads (double rate) noexcept
    {
        for (size_t h = 0; h < 2; ++h)
        {
            auto& head = heads[h];
            const auto& other = heads[1 - h];
            head.delay -= rate;

            // When a head runs off its window (where its gain is zero) it jumps a whole
            // number of periods away from the other head and starts again.
            if (rate > 0 && head.delay <= floorDelay)
            {
                const double half = halfWindow();
                head = { other.delay + half, 2.0 * half };
            }
            else if (rate < 0 && head.delay >= floorDelay + head.span)
            {
                const double half = halfWindow();
                head = { juce::jmax (floorDelay, other.delay - half), 2.0 * half };
            }
        }
    }

    // 4-point Hermite interpolation at a fractional delay behind the write position.
    float readAt (const std::vector<float>& buf, double delay) const noexcept
    {
        const double pos = (double) write - delay;
        const int i = (int) std::floor (pos);
        const float t = (float) (pos - i);
        auto at = [&] (int k) { return buf[(size_t) ((k % size + size) & (size - 1))]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    double fs = 48000.0, halfWindowMin = 576.0, lastPeriod = 240.0, floorDelay = 0.0;
    int size = 16384, write = 0, latency = 1344;
    std::vector<float> bufL, bufR;
    std::array<Head, 2> heads {};
    bool engaged = false;
    float mix = 0.0f, fadeStep = 0.001f;
};

//==============================================================================
class PitchCorrector
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        detector.prepare (fs);
        shifter.prepare (fs);
        juce::ignoreUnused (maxBlockSize);

        currentCents = 0.0f;
        targetNote = -1;
        unvoicedSamples = 0;
        period = 0.0;
        detectedMidi = -1.0f;
        correctionCents = 0.0f;
    }

    int getLatencySamples() const noexcept { return shifter.getLatencySamples(); }

    // Audio thread: forget the previous phrase (used when the tuner is switched on).
    void reset() noexcept
    {
        shifter.reset();
        currentCents = 0.0f;
        targetNote = -1;
        detectedMidi = -1.0f;
        correctionCents = 0.0f;
    }

    // amount: 0..1 (how far toward the note), speedMs: how quickly the correction moves.
    void process (float* L, float* R, int n, float amount, float speedMs) noexcept
    {
        for (int start = 0; start < n; start += chunk)
        {
            const int len = juce::jmin (chunk, n - start);

            for (int i = 0; i < len; ++i)
                detector.push (0.5f * (L[start + i] + R[start + i]));
            updateCorrection (detector.getMidi(), len, amount, speedMs);

            shifter.process (L + start, R + start, len, std::pow (2.0, currentCents / 1200.0), period);
        }
    }

    // For the UI: the detected note (MIDI, -1 when none) and the correction being applied.
    std::atomic<float> detectedMidi { -1.0f }, correctionCents { 0.0f };

private:
    static constexpr int chunk = 64;            // how often the correction is updated
    static constexpr float switchMargin = 0.6f; // semitones before jumping to a new target note

    void updateCorrection (float midi, int len, float amount, float speedMs) noexcept
    {
        float desired = 0.0f;
        if (midi > 0.0f)
        {
            unvoicedSamples = 0;
            period = fs / (440.0 * std::pow (2.0, (midi - 69.0) / 12.0));
            // Hysteresis: only move to another note when the singer is clearly closer to it.
            if (targetNote < 0 || std::abs (midi - (float) targetNote) > switchMargin)
                targetNote = (int) std::round (midi);
            desired = ((float) targetNote - midi) * 100.0f * juce::jlimit (0.0f, 1.0f, amount);
        }
        else
        {
            period = 0.0;
            if ((unvoicedSamples += len) > (int) (fs * 0.15))
                targetNote = -1; // a real gap, not just a consonant
        }

        const float coef = std::exp (-(float) len / juce::jmax (1.0f, speedMs * 0.001f * (float) fs));
        currentCents = desired + coef * (currentCents - desired);
        if (std::abs (currentCents) < 0.05f && juce::exactlyEqual (desired, 0.0f))
            currentCents = 0.0f; // let the shifter fall back to its clean path

        detectedMidi = midi;
        correctionCents = currentCents;
    }

    double fs = 48000.0, period = 0.0;
    PitchDetector detector;
    PitchShifter shifter;
    float currentCents = 0.0f;
    int targetNote = -1, unvoicedSamples = 0;
};
} // namespace sr
