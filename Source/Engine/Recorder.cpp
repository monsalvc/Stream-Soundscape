#include "Recorder.h"

namespace sr
{
Recorder::Recorder()
{
    thread.startThread();
}

Recorder::~Recorder()
{
    stop();
    thread.stopThread (5000);
}

juce::File Recorder::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("StreamRack Recordings");
}

void Recorder::prepare (double newSampleRate)
{
    if (! juce::approximatelyEqual (newSampleRate, sampleRate))
        stop();
    sampleRate = newSampleRate > 0 ? newSampleRate : 48000.0;
}

std::unique_ptr<Recorder::Writer> Recorder::createWriter (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (out.get())->failedToOpen())
    {
        error = "Couldn't create " + file.getFullPathName();
        return nullptr;
    }

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                .withSampleRate (sampleRate)
                                                .withNumChannels (2)
                                                .withBitsPerSample (24));
    if (writer == nullptr)
    {
        error = "Couldn't start a WAV file at " + juce::String (sampleRate, 0) + " Hz";
        return nullptr;
    }

    // About 10 s of buffer, and the header rewritten every 5 s.
    auto threaded = std::make_unique<Writer> (writer.release(), thread, (int) (sampleRate * 10.0));
    threaded->setFlushInterval ((int) (sampleRate * 5.0));
    return threaded;
}

bool Recorder::start (const juce::File& folder, bool includeRaw, juce::String& error)
{
    stop();
    if (! folder.createDirectory())
    {
        error = "Couldn't create the folder " + folder.getFullPathName();
        return false;
    }

    const auto stamp = "StreamRack " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M");
    const auto unique = [&] (const juce::String& suffix)
    {
        auto f = folder.getChildFile (stamp + " " + suffix + ".wav");
        for (int n = 2; f.exists(); ++n)
            f = folder.getChildFile (stamp + " " + suffix + " " + juce::String (n) + ".wav");
        return f;
    };

    streamFile = unique ("stream");
    auto stream = createWriter (streamFile, error);
    if (stream == nullptr)
        return false;

    std::unique_ptr<Writer> raw;
    if (includeRaw)
    {
        rawFile = unique ("raw");
        raw = createWriter (rawFile, error);
        if (raw == nullptr)
            return false;
    }

    samplesWritten = 0;
    dropped = 0;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        streamOwner = std::move (stream);
        rawOwner = std::move (raw);
        streamWriter = streamOwner.get();
        rawWriter = rawOwner.get();
    }
    recording = true;
    return true;
}

void Recorder::stop()
{
    std::unique_ptr<Writer> stream, raw;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        streamWriter = nullptr;
        rawWriter = nullptr;
        stream = std::move (streamOwner);
        raw = std::move (rawOwner);
    }
    recording = false;
    // Deleting the writers writes what's left in the buffer and finishes the files.
}

double Recorder::getSecondsRecorded() const
{
    return (double) samplesWritten.load() / sampleRate;
}

void Recorder::writeStream (const float* left, const float* right, int numSamples) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || streamWriter == nullptr)
        return;
    const float* channels[] = { left, right };
    if (streamWriter->write (channels, numSamples))
        samplesWritten += numSamples;
    else
        ++dropped; // the disk fell more than 10 s behind
}

void Recorder::writeRaw (const float* left, const float* right, int numSamples) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || rawWriter == nullptr)
        return;
    const float* channels[] = { left, right };
    if (! rawWriter->write (channels, numSamples))
        ++dropped;
}
} // namespace sr
