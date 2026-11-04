#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Core/SampleEdit.h"

namespace mf::sampleproc
{
inline constexpr float kSilenceThresholdDb = -50.0f;   // abaixo disso é "silêncio"
inline constexpr float kTrimMarginSeconds  = 0.010f;   // folga mantida em volta do som
inline constexpr float kNormalizeTargetDb  = -1.0f;

[[nodiscard]] inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

struct Range { std::size_t begin { 0 }; std::size_t end { 0 }; [[nodiscard]] std::size_t size() const noexcept { return end - begin; } };

/** Trecho que contém som (acima do limiar), com uma pequena folga. Tudo silêncio -> intervalo vazio. */
[[nodiscard]] inline Range findAudibleRange (const std::vector<float>& s, double sampleRate) noexcept
{
    const float threshold = dbToGain (kSilenceThresholdDb);
    std::size_t first = 0;
    while (first < s.size() && std::abs (s[first]) < threshold)
        ++first;

    if (first == s.size())
        return {};

    std::size_t last = s.size();
    while (last > first && std::abs (s[last - 1]) < threshold)
        --last;

    const auto margin = static_cast<std::size_t> (kTrimMarginSeconds * static_cast<float> (sampleRate));
    return { first > margin ? first - margin : 0, std::min (s.size(), last + margin) };
}

[[nodiscard]] inline float peakOf (const std::vector<float>& s) noexcept
{
    float peak = 0.0f;
    for (const float v : s)
        peak = std::max (peak, std::abs (v));
    return peak;
}

/** Sobe/desce o volume para o pico ficar em kNormalizeTargetDb. Áudio mudo não muda. */
inline void normalizePeak (std::vector<float>& s) noexcept
{
    const float peak = peakOf (s);
    if (peak < 1.0e-6f)
        return;

    const float gain = dbToGain (kNormalizeTargetDb) / peak;
    for (auto& v : s)
        v *= gain;
}

/** Rampa linear de entrada e de saída (limitadas a metade do áudio cada, sem se sobrepor). */
inline void applyFades (std::vector<float>& s, double sampleRate, int fadeInMs, int fadeOutMs) noexcept
{
    const std::size_t half = s.size() / 2;
    const auto toSamples = [sampleRate, half] (int ms)
    {
        return std::min (half, static_cast<std::size_t> (static_cast<double> (ms) * 0.001 * sampleRate));
    };

    const std::size_t in = toSamples (fadeInMs);
    const std::size_t out = toSamples (fadeOutMs);

    for (std::size_t i = 0; i < in; ++i)
        s[i] *= static_cast<float> (i) / static_cast<float> (in);

    for (std::size_t i = 0; i < out; ++i)
        s[s.size() - 1 - i] *= static_cast<float> (i) / static_cast<float> (out);
}

/** Pipeline completo: corte de silêncio -> normalizar -> fades. Áudio todo mudo vira vazio se cortar. */
[[nodiscard]] inline std::vector<float> process (const std::vector<float>& input, double sampleRate, const SampleEdit& rawEdit)
{
    const auto edit = rawEdit.clamped();
    std::vector<float> out;

    if (edit.trimSilence)
    {
        const auto range = findAudibleRange (input, sampleRate);
        out.assign (input.begin() + static_cast<std::ptrdiff_t> (range.begin),
                    input.begin() + static_cast<std::ptrdiff_t> (range.end));
    }
    else
    {
        out = input;
    }

    if (edit.normalize)
        normalizePeak (out);

    applyFades (out, sampleRate, edit.fadeInMs, edit.fadeOutMs);
    return out;
}
} // namespace mf::sampleproc
