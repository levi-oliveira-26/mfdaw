#pragma once

#include <cmath>

namespace mf
{
// ---------------------------------------------------------------------------
// Clipes da Playlist (Vertical Slice 02) — vocabulário puro, sem JUCE.
//
// Um clipe ocupa um intervalo de compassos INTEIROS [startBar, startBar+lengthBars)
// dentro de UMA faixa. Clipes da mesma faixa nunca se sobrepõem.
// ---------------------------------------------------------------------------

/** Tamanho da Playlist no Slice 02: 16 compassos (a lane é ajustada à largura, sem rolagem horizontal). */
inline constexpr int kPlaylistBars   = 16;
inline constexpr int kMinClipBars    = 1;
inline constexpr int kDefaultClipBars = 2;

/** Resolução da grade: 16 passos por compasso. Clipes de áudio podem começar/terminar em qualquer passo. */
inline constexpr int kStepsPerBar   = 16;
inline constexpr int kPlaylistSteps = kPlaylistBars * kStepsPerBar;

/** Cópia imutável de um clipe (valor leve, seguro para guardar na UI). */
struct ClipData
{
    int id         { -1 };
    int startBar   { 0 };
    int lengthBars { 0 };
    int takeId { -1 }; // vínculo opcional a um TAKE, usado somente por faixas de Voz e Amostra
    int offsetMs { 0 }; // onde o clipe começa DENTRO do áudio (trim esquerdo / divisão); só clipes com takeId
    int leadSteps { 0 }; // 0..15: passos vazios no começo do 1º compasso (trim esquerdo fino); só clipes de áudio
    int tailSteps { 0 }; // 0..15: passos vazios no fim do último compasso (trim direito fino); só clipes de áudio

    [[nodiscard]] constexpr bool isValid() const noexcept { return id > 0; }
    [[nodiscard]] constexpr int  endBar() const noexcept  { return startBar + lengthBars; }
    /** Primeiro passo que realmente soa (compassos inteiros quando não há trim fino). */
    [[nodiscard]] constexpr int  startStep() const noexcept { return startBar * kStepsPerBar + leadSteps; }
    /** Passo final (exclusivo) do que soa. */
    [[nodiscard]] constexpr int  endStep() const noexcept   { return (startBar + lengthBars) * kStepsPerBar - tailSteps; }
};

namespace clip
{
[[nodiscard]] constexpr bool isValidRange (int startBar, int lengthBars) noexcept
{
    return startBar >= 0 && lengthBars >= kMinClipBars && startBar + lengthBars <= kPlaylistBars;
}

[[nodiscard]] constexpr bool overlaps (int startA, int lengthA, int startB, int lengthB) noexcept
{
    return startA < startB + lengthB && startB < startA + lengthA;
}

/** Leva o início para dentro do projeto para um clipe de `lengthBars` caber. */
[[nodiscard]] constexpr int clampStart (int startBar, int lengthBars) noexcept
{
    const int latest = kPlaylistBars - (lengthBars < kMinClipBars ? kMinClipBars : lengthBars);
    return startBar < 0 ? 0 : (startBar > latest ? latest : startBar);
}

/** Máscara de bits dos compassos [startBar, startBar+lengthBars); 0 se o intervalo é inválido. */
[[nodiscard]] constexpr int barMaskFor (int startBar, int lengthBars) noexcept
{
    return isValidRange (startBar, lengthBars) ? (((1 << lengthBars) - 1) << startBar) : 0;
}

/** Intervalos de passos [aStart,aEnd) e [bStart,bEnd) se sobrepõem? */
[[nodiscard]] constexpr bool overlapsSteps (int aStart, int aEnd, int bStart, int bEnd) noexcept
{
    return aStart < bEnd && bStart < aEnd;
}

/** Clipe descrito por passos: compassos ocupados + folgas no começo e no fim. */
struct StepLayout
{
    int startBar { 0 };
    int lengthBars { 0 };
    int leadSteps { 0 };
    int tailSteps { 0 };
};

/** Converte [startStep,endStep) (com endStep > startStep) no layout de compassos + folgas. */
[[nodiscard]] constexpr StepLayout layoutFromSteps (int startStep, int endStep) noexcept
{
    const int startBar = startStep / kStepsPerBar;
    const int endBar = (endStep + kStepsPerBar - 1) / kStepsPerBar;
    return { startBar, endBar - startBar, startStep % kStepsPerBar, endBar * kStepsPerBar - endStep };
}

/** Milissegundos que `steps` passos (1/16 de compasso 4/4) duram no BPM dado. Aceita valores negativos. */
[[nodiscard]] inline int stepsToMs (int steps, int bpm) noexcept
{
    return bpm < 1 ? 0 : static_cast<int> (std::lround (static_cast<double> (steps) * 15000.0 / static_cast<double> (bpm)));
}

/** Limite do trim: um áudio tem no máximo 1 hora (TakeModel). */
inline constexpr int kMaxClipOffsetMs = 3'600'000;

/** Duração de um compasso (4/4) em milissegundos arredondados, no BPM dado. */
[[nodiscard]] constexpr int barDurationMs (int bpm) noexcept
{
    return bpm < 1 ? 0 : (240000 + bpm / 2) / bpm;
}

/** Compasso seguinte, com loop ao fim da Playlist. */
[[nodiscard]] constexpr int nextBar (int bar) noexcept
{
    return (bar + 1) % kPlaylistBars;
}

/**
 * A bateria está audível neste compasso?
 *   barMask == 0  -> "modo rascunho": ainda não há clipe de bateria, o padrão toca sempre
 *                    (editar o ritmo antes de arranjar continua dando retorno sonoro);
 *   barMask != 0  -> arranjo: só soa dentro de clipes de faixas de bateria.
 * Função pura e sem alocação: é chamada pela audio thread.
 */
[[nodiscard]] constexpr bool isBarAudible (int barMask, int bar) noexcept
{
    return barMask == 0 || (bar >= 0 && bar < kPlaylistBars && ((barMask >> bar) & 1) != 0);
}
} // namespace clip

} // namespace mf
