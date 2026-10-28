#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace mf
{
class AudioRecorder final : private juce::Thread
{
public:
    explicit AudioRecorder (double sampleRate = 44100.0);
    ~AudioRecorder() override;

    void prepare (double newSampleRate, int maximumBlockSize) noexcept;
    bool startRecording (juce::File destination);
    void stopRecording();

    /** Seguro para Audio Thread: não aloca nem bloqueia. */
    void pushAudio (const juce::AudioBuffer<float>& buffer) noexcept;
    void pushAudio (const float* const* channelData, int numChannels, int numSamples) noexcept;

    [[nodiscard]] bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }
    [[nodiscard]] juce::File getCurrentFile() const noexcept { return currentFile; }
    [[nodiscard]] float getInputLevel() const noexcept { return inputLevel.load (std::memory_order_relaxed); }
    [[nodiscard]] double getRecordedSeconds() const noexcept { return recordedSeconds.load (std::memory_order_relaxed); }

private:
    void run() override;
    void writePendingAudio() noexcept;

    static constexpr int numChannels = 2;
    static constexpr int ringCapacity = 44100 * 10;

    juce::WaitableEvent ioWakeup;
    juce::WaitableEvent stopped;
    juce::AbstractFifo fifo { ringCapacity };
    juce::AudioBuffer<float> ringBuffer { numChannels, ringCapacity };
    juce::AudioBuffer<float> writeBuffer { numChannels, 4096 };
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatWriter> writer;

    double sampleRate { 44100.0 };
    int preparedBlockSize { 0 };
    std::atomic<bool> recording { false };
    std::atomic<bool> stopRequested { false };
    juce::File currentFile;
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<double> recordedSeconds { 0.0 };
    std::uint64_t writtenSamples { 0 };
};
} // namespace mf
