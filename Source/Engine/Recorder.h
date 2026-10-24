#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace sr
{
// Records the finished stream mix (and optionally the raw bus as it arrived) to 24-bit WAV
// files. The audio thread only copies into a buffer; a background thread writes to disk, and
// the file header is updated every few seconds so a crash still leaves a playable file.
class Recorder
{
public:
    Recorder();
    ~Recorder();

    // Audio setup changed (sample rate): any recording in progress is finished first.
    void prepare (double sampleRate);

    // Message thread. Files are named by date and time in the folder, e.g.
    // "StreamRack 2026-10-25 09.31 stream.wav" (and "... raw.wav" when includeRaw).
    bool start (const juce::File& folder, bool includeRaw, juce::String& error);
    void stop();
    bool isRecording() const { return recording.load(); }
    double getSecondsRecorded() const;
    juce::File getStreamFile() const { return streamFile; }
    int getDroppedBlocks() const { return dropped.load(); }

    // Audio thread.
    void writeStream (const float* left, const float* right, int numSamples) noexcept;
    void writeRaw (const float* left, const float* right, int numSamples) noexcept;

    static juce::File defaultFolder();

private:
    using Writer = juce::AudioFormatWriter::ThreadedWriter;
    std::unique_ptr<Writer> createWriter (const juce::File&, juce::String& error);

    juce::TimeSliceThread thread { "StreamRack recorder" };
    juce::SpinLock lock; // guards the two pointers below against the audio thread
    Writer* streamWriter = nullptr;
    Writer* rawWriter = nullptr;
    std::unique_ptr<Writer> streamOwner, rawOwner;

    std::atomic<bool> recording { false };
    std::atomic<int64_t> samplesWritten { 0 };
    std::atomic<int> dropped { 0 };
    double sampleRate = 48000.0;
    juce::File streamFile, rawFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};
} // namespace sr
