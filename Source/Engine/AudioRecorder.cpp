#include "Engine/AudioRecorder.h"

#include <cmath>

namespace mf
{
AudioRecorder::AudioRecorder (double initialSampleRate)
    : juce::Thread ("MFDaw Audio Recorder I/O"), sampleRate (initialSampleRate)
{
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    stopThread (2000);
}

void AudioRecorder::prepare (double newSampleRate, int maximumBlockSize) noexcept
{
    if (newSampleRate > 0.0)
        sampleRate = newSampleRate;
    preparedBlockSize = juce::jmax (0, maximumBlockSize);
}

bool AudioRecorder::startRecording (juce::File destination)
{
    if (recording.load (std::memory_order_acquire))
        return false;

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    formatManager.registerBasicFormats();
    auto* wavFormat = formatManager.findFormatForFileExtension ("wav");
    if (wavFormat == nullptr)
        return false;

    std::unique_ptr<juce::FileOutputStream> outputStream (destination.createOutputStream());
    if (outputStream == nullptr)
        return false;

    writer.reset (wavFormat->createWriterFor (outputStream.get(),
                                              sampleRate,
                                              numChannels,
                                              24,
                                              juce::StringPairArray(),
                                              0));
    if (writer == nullptr)
        return false;

    juce::ignoreUnused (outputStream.release());

    currentFile = destination;
    writtenSamples = 0;
    recordedSeconds.store (0.0, std::memory_order_release);
    stopRequested.store (false, std::memory_order_release);
    fifo.reset();
    recording.store (true, std::memory_order_release);

    if (! isThreadRunning())
        startThread (juce::Thread::Priority::normal);
    ioWakeup.signal();
    return true;
}

void AudioRecorder::stopRecording()
{
    if (! recording.exchange (false, std::memory_order_acq_rel))
    {
        if (isThreadRunning())
            stopThread (5000);
        return;
    }

    stopped.reset();
    stopRequested.store (true, std::memory_order_release);
    ioWakeup.signal();
    stopped.wait (5000);
    stopThread (5000);
}

void AudioRecorder::pushAudio (const juce::AudioBuffer<float>& buffer) noexcept
{
    pushAudio (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void AudioRecorder::pushAudio (const float* const* channelData, int numChannelsIn, int numSamples) noexcept
{
    if (! recording.load (std::memory_order_acquire) || numSamples <= 0)
        return;

    const int channels = juce::jlimit (1, numChannels, numChannelsIn);
    float peak = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            peak = juce::jmax (peak, std::abs (channelData[channel][sample]));
    inputLevel.store (peak, std::memory_order_relaxed);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    const int writable = size1 + size2;

    if (writable <= 0)
        return;

    const int stored = juce::jmin (numSamples, writable);
    if (stored < numSamples)
        return;

    for (int channel = 0; channel < channels; ++channel)
        ringBuffer.copyFrom (channel, start1, channelData[channel], size1);

    if (channels == 1)
        ringBuffer.copyFrom (1, start1, channelData[0], size1);

    if (size2 > 0)
    {
        for (int channel = 0; channel < channels; ++channel)
            ringBuffer.copyFrom (channel, start2, channelData[channel] + size1, size2);

        if (channels == 1)
            ringBuffer.copyFrom (1, start2, channelData[0] + size1, size2);
    }

    if (channels < numChannels)
    {
        ringBuffer.clear (channels, start1, size1);
        if (size2 > 0)
            ringBuffer.clear (channels, start2, size2);
    }

    fifo.finishedWrite (stored);
}

void AudioRecorder::run()
{
    while (! threadShouldExit())
    {
        writePendingAudio();

        if (stopRequested.load (std::memory_order_acquire)
            && fifo.getNumReady() == 0)
        {
            writer.reset();
            stopRequested.store (false, std::memory_order_release);
            stopped.signal();
        }

        ioWakeup.wait (10);
    }
}

void AudioRecorder::writePendingAudio() noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (writeBuffer.getNumSamples(), start1, size1, start2, size2);

    const int readable = size1 + size2;
    if (readable <= 0 || writer == nullptr)
        return;

    for (int channel = 0; channel < numChannels; ++channel)
        writeBuffer.copyFrom (channel, 0, ringBuffer, channel, start1, size1);

    if (size2 > 0)
    {
        for (int channel = 0; channel < numChannels; ++channel)
            writeBuffer.copyFrom (channel, size1, ringBuffer, channel, start2, size2);
    }

    writer->writeFromAudioSampleBuffer (writeBuffer, 0, readable);
    fifo.finishedRead (readable);
    writtenSamples += static_cast<std::uint64_t> (readable);
    recordedSeconds.store (writtenSamples / juce::jmax (1.0, sampleRate), std::memory_order_relaxed);
}
} // namespace mf
