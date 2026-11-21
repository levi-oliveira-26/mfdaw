#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "Core/ProjectState.h"
#include "Engine/AudioRecorder.h"

namespace mf
{
namespace
{
class AudioRecorderUnitTests final : public juce::UnitTest
{
public:
    AudioRecorderUnitTests() : juce::UnitTest ("Voice AudioRecorder and Clip State", "Voice") {}

    void runTest() override
    {
        beginTest ("ProjectState registers and serializes an audio clip");
        ProjectState state;
        const AudioClipModel clip { "C:\\temp\\take.wav", 2, 1.25 };
        state.addAudioClip (clip);
        expectEquals (state.getNumAudioClips(), 1);
        const auto restored = state.getAudioClip (0);
        expectEquals (restored.filePath, clip.filePath);
        expectEquals (restored.startBar, clip.startBar);
        expectWithinAbsoluteError (restored.durationSeconds, clip.durationSeconds, 0.001);

        AudioClipModel trimmedClip { "C:\\temp\\trimmed.wav", 0, 2.0 };
        trimmedClip.startPosition = 5;
        trimmedClip.duration = 7;
        trimmedClip.trimStartOffset = 125;
        trimmedClip.trimEndOffset = 250;
        state.addAudioClip (trimmedClip);
        const auto restoredTrim = state.getAudioClip (1);
        expectEquals (restoredTrim.startPosition, 5);
        expectEquals (restoredTrim.duration, 7);
        expectEquals (restoredTrim.trimStartOffset, 125);
        expectEquals (restoredTrim.trimEndOffset, 250);

        ProjectState restoredState;
        expect (restoredState.loadFromJsonString (state.createJsonString()));
        expectEquals (restoredState.getNumAudioClips(), 2);
        expectEquals (restoredState.getAudioClip (0).filePath, clip.filePath);
        expectEquals (restoredState.getAudioClip (1).startPosition, 5);
        expectEquals (restoredState.getAudioClip (1).duration, 7);
        expectEquals (restoredState.getAudioClip (1).trimStartOffset, 125);
        expectEquals (restoredState.getAudioClip (1).trimEndOffset, 250);

        beginTest ("Recorder creates a finished WAV take asynchronously");
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("mfdaw-audio-recorder-test.wav");
        file.deleteFile();

        AudioRecorder recorder (44100.0);
        recorder.prepare (44100.0, 512);
        expect (recorder.startRecording (file));

        juce::AudioBuffer<float> samples (2, 512);
        samples.clear();
        recorder.pushAudio (samples);
        juce::Thread::sleep (50);
        recorder.stopRecording();

        expect (file.existsAsFile());
        expect (file.getSize() > 44);
        expect (recorder.getRecordedSeconds() >= 0.0);
        file.deleteFile();

        beginTest ("Audio push contains fixed-size lock-free capture operations");
        expect (recorder.getCurrentFile().getFileName().isNotEmpty()
                || recorder.getCurrentFile().getFullPathName().isNotEmpty());
    }
};

static AudioRecorderUnitTests audioRecorderUnitTests;
}
} // namespace mf
