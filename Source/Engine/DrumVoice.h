#pragma once

#include <cmath>
#include <cstdint>

namespace mf
{
/**
 * Voz de bateria sintética mínima (placeholder do instrumento do Passo 1).
 *
 * 100% segura para a audio thread: apenas aritmética sobre membros, sem
 * alocação, sem I/O, sem locks. Ruído gerado por xorshift32 (sem juce::Random,
 * que não é necessário aqui).
 *
 * Uso: setKind() uma vez, prepare(sampleRate) em prepareToPlay(),
 * trigger() a cada passo ativo e process() uma vez por amostra.
 */
class DrumVoice
{
public:
    enum class Kind { Kick, Snare, HiHat, Perc };

    void setKind (Kind k) noexcept
    {
        kind = k;
        noiseState = 0x9E3779B9u + static_cast<std::uint32_t> (k) * 7919u;
    }

    /** Recalcula os coeficientes de decaimento para a taxa de amostragem atual. */
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;

        // Decaimento exponencial atingindo -60 dB (~ 6.9078 em ln) em "seconds".
        const auto coeff = [newSampleRate] (double seconds)
        {
            return static_cast<float> (std::exp (-6.9078 / (seconds * newSampleRate)));
        };

        switch (kind)
        {
            case Kind::Kick:  ampCoeff = coeff (0.45); pitchCoeff = coeff (0.04); break;
            case Kind::Snare: ampCoeff = coeff (0.20); pitchCoeff = coeff (0.05); break;
            case Kind::HiHat: ampCoeff = coeff (0.06); pitchCoeff = coeff (0.05); break;
            case Kind::Perc:  ampCoeff = coeff (0.15); pitchCoeff = coeff (0.05); break;
        }
    }

    void trigger() noexcept
    {
        env      = 1.0f;
        pitchEnv = 1.0f;
        phase    = 0.0;
    }

    /** Gera uma amostra. */
    [[nodiscard]] float process() noexcept
    {
        if (env <= 0.0f)
            return 0.0f;

        float out = 0.0f;

        switch (kind)
        {
            case Kind::Kick:
                out = 0.9f * sine (45.0 + 130.0 * static_cast<double> (pitchEnv));
                break;

            case Kind::Snare:
                out = 0.35f * sine (190.0) + 0.65f * noise() * 0.6f;
                break;

            case Kind::HiHat:
            {
                // Passa-altas de 1ª ordem simples (diferença) => "chiado" de prato.
                const float n = noise();
                out = (n - lastNoise) * 0.35f;
                lastNoise = n;
                break;
            }

            case Kind::Perc:
                out = 0.5f * sine (900.0);
                break;
        }

        out *= env;
        env *= ampCoeff;
        pitchEnv *= pitchCoeff;

        if (env < 1.0e-4f)
            env = 0.0f;

        return out;
    }

private:
    static constexpr double kTwoPi = 6.283185307179586;

    [[nodiscard]] float sine (double frequency) noexcept
    {
        phase += frequency / sampleRate;
        phase -= std::floor (phase);
        return static_cast<float> (std::sin (kTwoPi * phase));
    }

    /** Ruído branco em [-1, 1) via xorshift32. */
    [[nodiscard]] float noise() noexcept
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return static_cast<float> (static_cast<std::int32_t> (noiseState)) * (1.0f / 2147483648.0f);
    }

    Kind kind { Kind::Kick };
    double sampleRate { 44100.0 };
    double phase { 0.0 };
    float env { 0.0f };
    float pitchEnv { 0.0f };
    float ampCoeff { 0.999f };
    float pitchCoeff { 0.999f };
    float lastNoise { 0.0f };
    std::uint32_t noiseState { 0x12345678u };
};

} // namespace mf
