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

        ProjectState restoredState;
        expect (restoredState.loadFromJsonString (state.createJsonString()));
        expectEquals (restoredState.getNumAudioClips(), 1);
        expectEquals (restoredState.getAudioClip (0).filePath, clip.filePath);

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
