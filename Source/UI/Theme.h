#pragma once

#include <juce_graphics/juce_graphics.h>

namespace mf::theme
{
// ---------------------------------------------------------------------------
// Design Tokens (Relatório Técnico, seção 3.1) — Zero Skeuomorphism
// ---------------------------------------------------------------------------
inline constexpr juce::uint32 kBgDeep          = 0xff0E0E12;
inline constexpr juce::uint32 kSurfaceBase     = 0xff1A1A24;
inline constexpr juce::uint32 kSurfaceElevated = 0xff252533;
inline constexpr juce::uint32 kTextPrimary     = 0xffECECFC;
inline constexpr juce::uint32 kTextMuted       = 0xff8A8A9E;
inline constexpr juce::uint32 kBrandRhythm     = 0xffFF4C4C;
inline constexpr juce::uint32 kBrandHarmony    = 0xff3399FF;
inline constexpr juce::uint32 kBrandMelody     = 0xff00E676;
inline constexpr juce::uint32 kBrandVoice      = 0xffFFC400;
inline constexpr juce::uint32 kActionRecord    = 0xffFF2A2A;
inline constexpr juce::uint32 kBorderSubtle    = 0xff303040;

// ---------------------------------------------------------------------------
// Layout: grelha base 8 e área mínima de toque 44x44
// ---------------------------------------------------------------------------
inline constexpr int kGrid     = 8;
inline constexpr int kPad      = 16;
inline constexpr int kRowH     = 48;  // múltiplo de 8 e >= 44
inline constexpr int kMinTouch = 44;

[[nodiscard]] inline juce::Colour col (juce::uint32 argb) noexcept { return juce::Colour (argb); }

/** Cor semântica de cada passo do Wizard (1..5). O passo 5 não tem token próprio. */
[[nodiscard]] inline juce::Colour stepColour (int wizardStep) noexcept
{
    switch (wizardStep)
    {
        case 1:  return col (kBrandRhythm);
        case 2:  return col (kBrandHarmony);
        case 3:  return col (kBrandMelody);
        case 4:  return col (kBrandVoice);
        default: return col (kTextPrimary);
    }
}

/** Fonte compatível com JUCE 7 e 8 (FontOptions é novo no 8). */
[[nodiscard]] inline juce::Font makeFont (float height, bool bold = false)
{
   #if JUCE_MAJOR_VERSION >= 8
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
   #else
    return juce::Font (height, bold ? juce::Font::bold : juce::Font::plain);
   #endif
}

/** Converte literal UTF-8 (com acentos) em juce::String sem violar as asserções de ASCII. */
[[nodiscard]] inline juce::String txt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

} // namespace mf::theme
