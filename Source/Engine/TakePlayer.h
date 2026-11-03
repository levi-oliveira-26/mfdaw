#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

#include "Core/ClipModel.h"
#include "Core/TakeModel.h"
#include "Core/TrackType.h"

namespace mf
{
struct TakeBuffer
{
    std::vector<float> samples;
    double sampleRate { 44100.0 };
};

/** Reproduz takes por regiões de clipe sem alocação, bloqueio ou I/O na audio thread. */
class TakePlayer
{
public:
    static constexpr int kSlots = kMaxPlaylistTracks;
    static constexpr int kMaxRegionsPerSlot = kPlaylistBars;
    static constexpr double kFadeSeconds = 0.005;

    struct Region { int startStep { -1 }; int endStep { -1 }; int takeIndex { -1 }; };

    // Message thread
    void publishTake (int slot, int takeIndex, const TakeBuffer* buffer) noexcept
    {
        if (validSlot (slot) && validTake (takeIndex))
            published[index (slot)][index (takeIndex)].store (buffer, std::memory_order_seq_cst);
    }

    [[nodiscard]] bool isRetired (const TakeBuffer* buffer) const noexcept
    {
        if (buffer == nullptr) return true;
        for (int s = 0; s < kSlots; ++s)
        {
            for (int t = 0; t < kMaxTakesPerTrack; ++t)
                if (published[index (s)][index (t)].load (std::memory_order_seq_cst) == buffer)
                    return false;
            if (hazard[index (s)].load (std::memory_order_seq_cst) == buffer)
                return false;
        }
        return true;
    }

    // Audio thread: endStep é exclusivo; takeIndex é o índice na tabela da faixa.
    void setClipRegion (int slot, int regionIndex, int startStep, int endStep, int takeIndex) noexcept
    {
        if (! validSlot (slot) || regionIndex < 0 || regionIndex >= kMaxRegionsPerSlot)
            return;
        auto& r = regions[index (slot)][static_cast<std::size_t> (regionIndex)];
        if (startStep < 0 || endStep <= startStep || endStep > kPlaylistSteps || ! validTake (takeIndex))
            r = Region {};
        else
            r = { startStep, endStep, takeIndex };
    }

    void clearRegions (int slot) noexcept
    {
        if (! validSlot (slot)) return;
        for (auto& r : regions[index (slot)]) r = Region {};
    }

    void beginBlock() noexcept {}

    void triggerStep (int bar, int step) noexcept
    {
        if (step < 0 || step >= kStepsPerBar || bar < 0 || bar >= kPlaylistBars) return;
        const int absoluteStep = bar * kStepsPerBar + step;

        for (int s = 0; s < kSlots; ++s)
        {
            auto& voice = voices[index (s)];
            const Region* match = nullptr;
            for (const auto& r : regions[index (s)])
                if (r.startStep >= 0 && absoluteStep >= r.startStep && absoluteStep < r.endStep)
                { match = &r; break; }

            if (match == nullptr) { voice.target = false; voice.lastStartedRegionStartStep = -1; continue; }
            if (absoluteStep != match->startStep && voice.lastStartedRegionStartStep == match->startStep) continue;

            const TakeBuffer* buffer = acquireBuffer (s, match->takeIndex);
            if (buffer == nullptr || buffer->samples.size() < 2 || buffer->sampleRate <= 0.0)
            {
                voice.target = false;
                voice.active = false;
                voice.buffer = nullptr;
                hazard[index (s)].store (nullptr, std::memory_order_seq_cst);
                continue;
            }

            voice.buffer = buffer;
            voice.lastStartedRegionStartStep = match->startStep;
            voice.pos = 0.0;
            voice.env = 0.0f;
            voice.target = true;
            voice.active = true;
        }
    }

    void releaseAll() noexcept { for (auto& v : voices) v.target = false; }

    [[nodiscard]] float process (double hostSampleRate, const std::array<float, kSlots>& slotGains) noexcept
    {
        if (hostSampleRate <= 0.0) return 0.0f;
        float sum = 0.0f;
        for (int s = 0; s < kSlots; ++s)
        {
            auto& v = voices[index (s)];
            if (! v.active) continue;
            sum += processVoice (v, hostSampleRate) * slotGains[index (s)];
            if (! v.active)
            {
                v.buffer = nullptr;
                hazard[index (s)].store (nullptr, std::memory_order_seq_cst);
            }
        }
        return sum;
    }

    [[nodiscard]] bool isPlaying (int slot) const noexcept
    {
        return validSlot (slot) && voices[index (slot)].active;
    }

private:
    struct Voice { const TakeBuffer* buffer { nullptr }; double pos { 0.0 }; float env { 0.0f }; int lastStartedRegionStartStep { -1 }; bool target { false }; bool active { false }; };

    [[nodiscard]] const TakeBuffer* acquireBuffer (int slot, int takeIndex) noexcept
    {
        auto& h = hazard[index (slot)];
        auto& p = published[index (slot)][index (takeIndex)];
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const TakeBuffer* candidate = p.load (std::memory_order_seq_cst);
            if (candidate == nullptr) { h.store (nullptr, std::memory_order_seq_cst); return nullptr; }
            h.store (candidate, std::memory_order_seq_cst);
            if (p.load (std::memory_order_seq_cst) == candidate) return candidate;
        }
        h.store (nullptr, std::memory_order_seq_cst);
        return nullptr;
    }

    [[nodiscard]] static float processVoice (Voice& v, double rate) noexcept
    {
        if (v.buffer == nullptr || v.buffer->samples.size() < 2) { v.active = false; v.env = 0.0f; return 0.0f; }
        const float ramp = static_cast<float> (1.0 / (kFadeSeconds * rate));
        v.env = v.target ? (v.env + ramp > 1.0f ? 1.0f : v.env + ramp)
                         : (v.env - ramp < 0.0f ? 0.0f : v.env - ramp);
        const auto& samples = v.buffer->samples;
        const auto i = static_cast<std::size_t> (v.pos);
        if ((! v.target && v.env <= 0.0f) || i + 1 >= samples.size())
        { v.active = false; v.env = 0.0f; return 0.0f; }
        const float frac = static_cast<float> (v.pos - static_cast<double> (i));
        const float sample = samples[i] + (samples[i + 1] - samples[i]) * frac;
        v.pos += v.buffer->sampleRate / rate;
        return sample * v.env;
    }

    [[nodiscard]] static constexpr bool validSlot (int s) noexcept { return s >= 0 && s < kSlots; }
    [[nodiscard]] static constexpr bool validTake (int t) noexcept { return t >= 0 && t < kMaxTakesPerTrack; }
    [[nodiscard]] static constexpr std::size_t index (int n) noexcept { return static_cast<std::size_t> (n); }

    std::array<std::array<std::atomic<const TakeBuffer*>, kMaxTakesPerTrack>, kSlots> published {};
    std::array<std::atomic<const TakeBuffer*>, kSlots> hazard {};
    std::array<std::array<Region, kMaxRegionsPerSlot>, kSlots> regions {};
    std::array<Voice, kSlots> voices {};
};
} // namespace mf
