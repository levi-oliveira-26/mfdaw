#include <array>
#include <memory>

#include <juce_core/juce_core.h>

#include "Core/ClipModel.h"
#include "Engine/TakePlayer.h"

namespace mf::tests
{
/** Vertical Slice 07: reprodução dos takes dentro dos clipes (audio thread, sem JUCE). */
class TakePlayerTests final : public juce::UnitTest
{
public:
    TakePlayerTests() : juce::UnitTest ("Take Player Tests", "Takes") {}

    void runTest() override
    {
        constexpr double kRate = 48000.0;
        std::array<float, TakePlayer::kSlots> unity {};
        unity.fill (1.0f);

        auto makeBuffer = [] (std::size_t n, float value, double rate)
        {
            auto b = std::make_unique<TakeBuffer>();
            b->samples.assign (n, value);
            b->sampleRate = rate;
            return b;
        };

        auto render = [&] (TakePlayer& p, int n)
        {
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
                peak = juce::jmax (peak, std::abs (p.process (kRate, unity)));
            return peak;
        };

        beginTest ("Silent without a clip (mask 0), plays from the exact start step inside a clip");
        {
            TakePlayer player;
            auto buf = makeBuffer (200000, 0.5f, kRate);
            player.publish (0, buf.get());
            player.setStartStep (0, 16);                // compasso 1, passo 0
            player.beginBlock();

            player.triggerStep (1, 0);                  // sem clipe: máscara 0 = mudo
            expect (! player.isPlaying (0));
            expectEquals (render (player, 500), 0.0f);

            player.setBars (0, 0b110);                  // clipe cobre compassos 1 e 2
            player.triggerStep (0, 0);
            expect (! player.isPlaying (0));            // antes do início
            player.triggerStep (1, 1);
            expect (! player.isPlaying (0));            // não é o passo de início
            player.triggerStep (1, 0);
            expect (player.isPlaying (0));
            expectWithinAbsoluteError (render (player, 2000), 0.5f, 1e-4f);   // fade-in terminou
        }

        beginTest ("Leaving the clip releases with a fade (no click) and stops");
        {
            TakePlayer player;
            auto buf = makeBuffer (400000, 0.5f, kRate);
            player.publish (0, buf.get());
            player.setStartStep (0, 0);
            player.setBars (0, 0b1);                    // só o compasso 0
            player.beginBlock();
            player.triggerStep (0, 0);
            render (player, 1000);

            player.triggerStep (1, 0);                  // compasso 1 está fora do clipe
            float previous = 0.5f;
            float maxJump = 0.0f;
            for (int i = 0; i < 1000; ++i)
            {
                const float v = player.process (kRate, unity);
                maxJump = juce::jmax (maxJump, std::abs (v - previous));
                previous = v;
            }
            expect (maxJump < 0.01f);
            expect (! player.isPlaying (0));
            expectEquals (previous, 0.0f);
        }

        beginTest ("Resamples by the file rate and stops at the end of the buffer");
        {
            TakePlayer player;
            auto buf = std::make_unique<TakeBuffer>();
            buf->sampleRate = 24000.0;                  // metade do host: dura o dobro em amostras
            buf->samples.assign (1000, 1.0f);
            player.publish (0, buf.get());
            player.setStartStep (0, 0);
            player.setBars (0, 1);
            player.beginBlock();
            player.triggerStep (0, 0);

            int audibleSamples = 0;
            for (int i = 0; i < 3000; ++i)
                if (player.process (kRate, unity) != 0.0f)
                    ++audibleSamples;

            expect (audibleSamples > 1900 && audibleSamples <= 2000);
            expect (! player.isPlaying (0));
        }

        beginTest ("Per-slot gain scales the voice; other slots are untouched");
        {
            TakePlayer player;
            auto buf = makeBuffer (100000, 1.0f, kRate);
            player.publish (3, buf.get());
            player.setStartStep (3, 0);
            player.setBars (3, 1);
            player.beginBlock();
            player.triggerStep (0, 0);

            auto gains = unity;
            gains[3] = 0.25f;
            float last = 0.0f;
            for (int i = 0; i < 2000; ++i)
                last = player.process (kRate, gains);
            expectWithinAbsoluteError (last, 0.25f, 1e-4f);
        }

        beginTest ("Hazard pointers: a buffer in use is never retired; replaced buffers are, once unused");
        {
            TakePlayer player;
            auto a = makeBuffer (100, 0.1f, kRate);
            auto b = makeBuffer (100, 0.2f, kRate);

            expect (player.isRetired (a.get()));         // nunca publicado
            player.publish (0, a.get());
            expect (! player.isRetired (a.get()));       // publicado

            player.beginBlock();                         // a audio thread passa a usar A
            player.publish (0, b.get());                 // troca
            expect (! player.isRetired (a.get()));       // ainda anunciado: NÃO pode ser destruído
            expect (! player.isRetired (b.get()));

            player.beginBlock();                         // próximo bloco já usa B
            expect (player.isRetired (a.get()));         // agora A pode ser liberado
            expect (! player.isRetired (b.get()));
        }

        beginTest ("Swapping the take resets the voice; invalid start/slot are ignored");
        {
            TakePlayer player;
            auto a = makeBuffer (100000, 0.5f, kRate);
            auto b = makeBuffer (100000, 0.5f, kRate);
            player.publish (0, a.get());
            player.setStartStep (0, 0);
            player.setBars (0, 1);
            player.beginBlock();
            player.triggerStep (0, 0);
            expect (player.isPlaying (0));

            player.publish (0, b.get());
            player.beginBlock();
            expect (! player.isPlaying (0));

            player.setStartStep (-5, 0);
            player.setStartStep (0, kPlaylistSteps);     // inválido => sem início
            player.triggerStep (0, 0);
            expect (! player.isPlaying (0));
            expect (! player.isPlaying (99));
        }

        beginTest ("releaseAll fades every voice out");
        {
            TakePlayer player;
            auto a = makeBuffer (100000, 0.5f, kRate);
            player.publish (0, a.get());
            player.setStartStep (0, 0);
            player.setBars (0, 1);
            player.beginBlock();
            player.triggerStep (0, 0);
            render (player, 2000);
            player.releaseAll();
            render (player, 1000);
            expect (! player.isPlaying (0));
        }
    }
};

static TakePlayerTests takePlayerTests;

} // namespace mf::tests
