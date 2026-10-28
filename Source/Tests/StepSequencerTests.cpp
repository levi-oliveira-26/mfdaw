#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/SpscQueue.h"
#include "Engine/EngineEvent.h"
#include "Engine/MainProcessor.h"
#include "Engine/SampleManager.h"

namespace mf::tests
{
class StepSequencerVelocityTests final : public juce::UnitTest
{
public:
    StepSequencerVelocityTests() : juce::UnitTest ("Step Sequencer Velocity Headless Tests", "Rhythm") {}

    void runTest() override
    {
        beginTest ("Velocity maps linearly from MIDI 0 to 127");
        expectWithinAbsoluteError (velocityToGain (0), 0.0f, 0.0001f);
        expectWithinAbsoluteError (velocityToGain (64), 64.0f / 127.0f, 0.0001f);
        expectWithinAbsoluteError (velocityToGain (127), 1.0f, 0.0001f);

        beginTest ("Velocity event is transported through the lock-free queue");
        SpscQueue<EngineEvent, 4> queue;
        expect (queue.push (EngineEvent::setStepVelocity (3, 9, 64)));

        EngineEvent event;
        expect (queue.pop (event));
        expectEquals (event.a, 3);
        expectEquals (event.b, 9);
        expectEquals (event.c, 64);

        beginTest ("Velocity 64 produces approximately half the peak of velocity 127");
        const float fullPeak = measureKickPeak (127);
        const float halfPeak = measureKickPeak (64);

        expect (fullPeak > 0.0f);
        expectWithinAbsoluteError (halfPeak * 127.0f / 64.0f,
                                   fullPeak,
                                   fullPeak * 0.01f);

        beginTest ("Custom sample atomic swaps remain race-free under writer stress");
        expect (stressSampleSwaps ());

        beginTest ("Swing maps percentages to bounded step offsets");
        expectEquals (swingOffsetSamples (44100.0, 120, 0.0f), 0);
        expectEquals (swingOffsetSamples (44100.0, 120, 0.5f), 1378);
        expectEquals (swingOffsetSamples (44100.0, 120, 1.0f), 2756);
        expectEquals (swingOffsetSamples (48000.0, 120, 1.0f), 3000);
        expectEquals (swingOffsetSamples (44100.0, 120, -0.25f), 0);
        expectEquals (swingOffsetSamples (44100.0, 120, 1.25f), 2756);

        beginTest ("Swing atomic updates remain bounded and lock-free");
        std::atomic<float> amount { 0.0f };
        amount.store (0.5f, std::memory_order_release);
        expectWithinAbsoluteError (amount.load (std::memory_order_acquire), 0.5f, 0.0001f);
        amount.store (1.0f, std::memory_order_release);
        expect (std::atomic<float>::is_always_lock_free);

        beginTest ("Swing E2E delays the offbeat MIDI event at 120 BPM");
        const auto offsets = measureSwingMidiOffsets (44100.0, 0.5f);
        expectEquals (static_cast<std::size_t> (offsets.size()), static_cast<std::size_t> (2u));

        if (offsets.size() == 2u)
        {
            const int rigidOffbeat = static_cast<int> (std::round (44100.0 * 60.0 / (120.0 * 4.0)));
            const int expectedDelay = swingOffsetSamples (44100.0, 120, 0.5f);
            expectWithinAbsoluteError (offsets[1] - offsets[0],
                                       rigidOffbeat + expectedDelay,
                                       2);
        }
    }

private:
    static float velocityToGain (int velocity) noexcept
    {
        return juce::jlimit (0, 127, velocity) / 127.0f;
    }

    static bool stressSampleSwaps()
    {
        SampleManager manager;
        std::atomic<bool> writerFinished { false };
        std::atomic<bool> readerFailed { false };

        std::thread writer ([&]
        {
            for (int iteration = 0; iteration < 4096; ++iteration)
            {
                const int track = iteration % kNumTracks;
                juce::AudioBuffer<float> buffer (1, 64);
                buffer.clear();
                buffer.setSample (0, iteration % 64, static_cast<float> (iteration % 127) / 127.0f);

                manager.setSample (track,
                    std::make_shared<const AudioSample> (std::move (buffer)));

                if ((iteration % 257) == 0)
                    manager.clearSample (track);
            }

            writerFinished.store (true, std::memory_order_release);
        });

        std::thread reader ([&]
        {
            while (! writerFinished.load (std::memory_order_acquire))
            {
                for (int track = 0; track < kNumTracks; ++track)
                {
                    const auto sample = manager.getSample (track);
                    if (sample != nullptr)
                    {
                        if (sample->buffer.getNumSamples() != 64
                            || sample->buffer.getNumChannels() != 1)
                        {
                            readerFailed.store (true, std::memory_order_release);
                            return;
                        }

                        volatile float value = sample->buffer.getSample (0, 0);
                        juce::ignoreUnused (value);
                    }
                }
            }
        });

        writer.join();
        reader.join();

        return ! readerFailed.load (std::memory_order_acquire);
    }

    static int swingOffsetSamples (double sampleRate, int bpm, float percentage) noexcept
    {
        const float amount = juce::jlimit (0.0f, 1.0f, percentage);
        const double samplesPerStep = sampleRate * 60.0 / (static_cast<double> (bpm) * 4.0);
        const double maximumOffset = samplesPerStep * 0.5;
        const int maximumSamples = static_cast<int> (std::floor (maximumOffset));
        return juce::jlimit (0, maximumSamples,
                             static_cast<int> (std::round (maximumOffset * amount)));
    }

    static std::vector<int> measureSwingMidiOffsets (double sampleRate, float swing)
    {
        MainProcessor processor;
        processor.prepareToPlay (sampleRate, 512);
        auto& state = processor.getProjectState();
        state.setBpm (120);
        state.setSwingAmount (swing);

        if (! state.isStepActive (0, 0))
            state.toggleStep (0, 0);
        if (! state.isStepActive (0, 1))
            state.toggleStep (0, 1);

        state.setPlaying (true);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        std::vector<int> noteOffsets;
        int absoluteSample = 0;

        for (int block = 0; block < 24 && noteOffsets.size() < 2u; ++block)
        {
            buffer.clear();
            midi.clear();
            processor.processBlock (buffer, midi);

            for (const auto metadata : midi)
            {
                if (metadata.numBytes > 0)
                {
                    const auto message = metadata.getMessage();

                    if (message.isNoteOn() && message.getNoteNumber() == 36)
                        noteOffsets.push_back (absoluteSample + metadata.samplePosition);
                }
            }

            absoluteSample += buffer.getNumSamples();
        }

        return noteOffsets;
    }

    static float measureKickPeak (int velocity)
    {
        MainProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        processor.getProjectState().setStepVelocity (0, 0, velocity);
        if (! processor.getProjectState().isStepActive (0, 0))
            processor.getProjectState().toggleStep (0, 0);
        processor.getProjectState().setPlaying (true);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        float peak = 0.0f;

        for (int block = 0; block < 16; ++block)
        {
            buffer.clear();
            midi.clear();
            processor.processBlock (buffer, midi);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                peak = juce::jmax (peak, std::abs (buffer.getSample (0, sample)));
        }

        return peak;
    }
};

static StepSequencerVelocityTests stepSequencerVelocityTests;
} // namespace mf::tests
