#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Core/ProjectState.h"

namespace mf
{
/**
 * Immutable audio sample owned by the message/background-loading side and
 * exchanged atomically with the audio thread. Ownership operations never
 * require locks, and sample decoding remains outside this class.
 */
struct AudioSample
{
    AudioSample (juce::AudioBuffer<float>&& source) noexcept
        : buffer (std::move (source))
    {}

    juce::AudioBuffer<float> buffer;
};

class SampleManager final
{
public:
    using SamplePtr = std::shared_ptr<const AudioSample>;

    [[nodiscard]] SamplePtr getSample (int track) const noexcept
    {
        if (track < 0 || track >= kNumTracks)
            return {};

        return samples[static_cast<std::size_t> (track)].load (std::memory_order_acquire);
    }

    void setSample (int track, SamplePtr sample) noexcept
    {
        if (track >= 0 && track < kNumTracks)
            samples[static_cast<std::size_t> (track)].store (std::move (sample),
                                                              std::memory_order_release);
    }

    void clearSample (int track) noexcept { setSample (track, {}); }

private:
    std::array<std::atomic<SamplePtr>, static_cast<std::size_t> (kNumTracks)> samples;
};

} // namespace mf
