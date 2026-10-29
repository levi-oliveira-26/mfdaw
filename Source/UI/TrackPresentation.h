#pragma once

#include <juce_graphics/juce_graphics.h>

#include "Core/TrackType.h"
#include "UI/Theme.h"

namespace mf::presentation
{
/**
 * Camada de apresentação das faixas (Hexagonal: o domínio diz O QUE a faixa é;
 * aqui a UI decide cor e rótulo). Centraliza os tokens semânticos para que
 * cabeçalhos, menus e lanes nunca divirjam.
 */
[[nodiscard]] inline juce::Colour colourFor (TrackType type) noexcept
{
    using namespace theme;

    switch (type)
    {
        case TrackType::Drum:    return col (kBrandRhythm);
        case TrackType::Harmony: return col (kBrandHarmony);
        case TrackType::Melody:  return col (kBrandMelody);
        case TrackType::Record:  return col (kBrandVoice);
        case TrackType::Sample:  return col (kTextPrimary);   // sem token próprio (como o antigo passo 5)
    }

    return col (kTextMuted);
}

/** Nome amigável exibido no menu "Adicionar faixa" (sem jargão). */
[[nodiscard]] inline juce::String typeLabel (TrackType type)
{
    switch (type)
    {
        case TrackType::Drum:    return theme::txt ("Bateria (ritmo)");
        case TrackType::Harmony: return theme::txt ("Harmonia (acordes)");
        case TrackType::Melody:  return theme::txt ("Melodia (notas)");
        case TrackType::Record:  return theme::txt ("Voz (gravação)");
        case TrackType::Sample:  return theme::txt ("Amostra (áudio)");
    }

    return {};
}

/** Texto do botão de ação do cabeçalho. */
[[nodiscard]] inline juce::String actionLabel (TrackAction action)
{
    switch (action)
    {
        case TrackAction::OpenStepSequencer: return theme::txt ("Editar ritmo");
        case TrackAction::OpenSmartChords:   return theme::txt ("Acordes");
        case TrackAction::OpenPianoRoll:     return theme::txt ("Escrever notas");
        case TrackAction::ToggleRecording:   return theme::txt ("Gravar");
        case TrackAction::OpenSampleEditor:  return theme::txt ("Editar áudio");
    }

    return {};
}

} // namespace mf::presentation
