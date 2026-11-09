#include <array>
#include <memory>

#include <juce_core/juce_core.h>

#include "Core/ClipModel.h"
#include "Engine/TakePlayer.h"

namespace mf::tests
{
class TakePlayerTests final : public juce::UnitTest
{
public:
    TakePlayerTests() : juce::UnitTest ("Take Player Tests", "Takes") {}

    void runTest() override
    {
        constexpr double rate = 48000.0;
        std::array<float, TakePlayer::kSlots> gains {};
        gains.fill (1.0f);

        auto makeBuffer = [] (std::size_t n, float value, double sampleRate = 48000.0)
        {
            auto b = std::make_unique<TakeBuffer>();
            b->samples.assign (n, value);
            b->sampleRate = sampleRate;
            return b;
        };
        auto render = [&] (TakePlayer& player, int count)
        {
            float peak = 0.0f;
            for (int i = 0; i < count; ++i)
                peak = juce::jmax (peak, std::abs (player.process (rate, gains)));
            return peak;
        };

        beginTest ("A region starts its selected take on the exact start step");
        {
            TakePlayer player;
            auto a = makeBuffer (200000, 0.5f);
            player.publishTake (0, 0, a.get());
            player.setClipRegion (0, 0, 16, 48, 0);
            player.beginBlock();
            player.triggerStep (0, 0);
            expect (! player.isPlaying (0));
            player.triggerStep (1, 0);
            expect (player.isPlaying (0));
            expectWithinAbsoluteError (render (player, 2000), 0.5f, 1e-4f);
        }

        beginTest ("A take that finishes loading after the region starts can begin on a later step");
        {
            TakePlayer player;
            auto a = makeBuffer (200000, 0.45f);
            player.setClipRegion (0, 0, 0, 16, 0);
            player.triggerStep (0, 0); // buffer ainda não publicado
            expect (! player.isPlaying (0));
            player.publishTake (0, 0, a.get());
            player.triggerStep (0, 1);
            expect (player.isPlaying (0));
            expectWithinAbsoluteError (render (player, 2000), 0.45f, 1e-4f);
        }

        beginTest ("Multiple non-overlapping regions in one slot select different takes");
        {
            TakePlayer player;
            auto a = makeBuffer (200000, 0.25f);
            auto b = makeBuffer (200000, 0.75f);
            player.publishTake (0, 0, a.get());
            player.publishTake (0, 1, b.get());
            player.setClipRegion (0, 0, 0, 16, 0);
            player.setClipRegion (0, 1, 32, 48, 1);
            player.triggerStep (0, 0);
            expect (player.isPlaying (0));
            expectWithinAbsoluteError (render (player, 2000), 0.25f, 1e-4f);
            player.triggerStep (1, 0); // gap: fade the first region out
            render (player, 1000);
            expect (! player.isPlaying (0));
            player.triggerStep (2, 0);
            expect (player.isPlaying (0));
            expectWithinAbsoluteError (render (player, 2000), 0.75f, 1e-4f);
        }

        beginTest ("Moving a region moves its trigger step");
        {
            TakePlayer player;
            auto a = makeBuffer (200000, 0.4f);
            player.publishTake (0, 0, a.get());
            player.setClipRegion (0, 0, 16, 32, 0);
            player.triggerStep (0, 0);
            expect (! player.isPlaying (0));
            player.setClipRegion (0, 0, 48, 64, 0);
            player.triggerStep (1, 0);
            expect (! player.isPlaying (0));
            player.triggerStep (3, 0);
            expect (player.isPlaying (0));
        }

        beginTest ("Changing take within a slot announces the new buffer and resets playback");
        {
            TakePlayer player;
            auto a = makeBuffer (200000, 0.2f);
            auto b = makeBuffer (200000, 0.6f);
            player.publishTake (2, 0, a.get());
            player.publishTake (2, 1, b.get());
            player.setClipRegion (2, 0, 0, 16, 0);
            player.setClipRegion (2, 1, 16, 32, 1);
            player.triggerStep (0, 0);
            render (player, 2000);
            player.triggerStep (1, 0);
            expect (player.isPlaying (2));
            auto slotGains = gains;
            expectWithinAbsoluteError (render (player, 2000), 0.6f, 1e-4f);
            expect (! player.isRetired (b.get()));
            (void) slotGains;
        }

        beginTest ("Hazard pointer protects a playing buffer after it is unpublished");
        {
            TakePlayer player;
            auto a = makeBuffer (200000, 0.3f);
            player.publishTake (0, 0, a.get());
            player.setClipRegion (0, 0, 0, 16, 0);
            expect (! player.isRetired (a.get()));
            player.triggerStep (0, 0);
            player.publishTake (0, 0, nullptr);
            expect (! player.isRetired (a.get()));
            render (player, 3000);
            player.triggerStep (1, 0);
            render (player, 1000);
            expect (player.isRetired (a.get()));
        }

        beginTest ("Removed regions release audio and invalid regions are ignored");
        {
            TakePlayer player;
            auto a = makeBuffer (200000, 0.5f);
            player.publishTake (0, 0, a.get());
            player.setClipRegion (0, 0, 0, 16, 0);
            player.triggerStep (0, 0);
            render (player, 2000);
            player.setClipRegion (0, 0, -1, -1, -1);
            player.triggerStep (1, 0);
            render (player, 1000);
            expect (! player.isPlaying (0));
            player.setClipRegion (0, 1, 0, 16, 999);
            player.triggerStep (0, 0);
            expect (! player.isPlaying (0));
        }

        beginTest ("Per-slot gain and sample-rate conversion are preserved");
        {
            TakePlayer player;
            auto a = makeBuffer (1000, 1.0f, 24000.0);
            player.publishTake (3, 0, a.get());
            player.setClipRegion (3, 0, 0, 16, 0);
            player.triggerStep (0, 0);
            auto scaled = gains;
            scaled[3] = 0.25f;
            float last = 0.0f;
            for (int i = 0; i < 1000; ++i)
                last = player.process (rate, scaled);
            expectWithinAbsoluteError (last, 0.25f, 1e-4f);
        }

        // Rampa 0..1 em 4 s (192000 amostras): o valor revela a posição de leitura.
        auto makeRamp = [&]
        {
            auto b = std::make_unique<TakeBuffer>();
            b->sampleRate = rate;
            for (int i = 0; i < 192000; ++i)
                b->samples.push_back (static_cast<float> (i) / 192000.0f);
            return b;
        };
        auto lastAfter = [&] (TakePlayer& player, int count)
        {
            float v = 0.0f;
            for (int i = 0; i < count; ++i)
                v = player.process (rate, gains);
            return v;
        };

        beginTest ("Left trim: a region starts reading inside the audio at its offset");
        {
            TakePlayer player;
            auto ramp = makeRamp();
            player.publishTake (0, 0, ramp.get());
            player.setClipRegion (0, 0, 0, 16, 0, 1000);   // começa 1 s dentro do áudio
            player.triggerStep (0, 0, 0.125);
            expectWithinAbsoluteError (lastAfter (player, 1000), (48000.0f + 999.0f) / 192000.0f, 1e-4f);
        }

        beginTest ("Entering a clip mid-way (late load) stays aligned with the audio, offset included");
        {
            TakePlayer player;
            auto ramp = makeRamp();
            player.setClipRegion (0, 0, 0, 16, 0, 1000);
            player.triggerStep (0, 0, 0.125);                 // ainda não carregado
            expect (! player.isPlaying (0));
            player.publishTake (0, 0, ramp.get());
            player.triggerStep (0, 4, 0.125);                 // 4 passos = 0,5 s depois
            expectWithinAbsoluteError (lastAfter (player, 1000), (72000.0f + 999.0f) / 192000.0f, 1e-4f);
        }

        beginTest ("Resuming after pause re-enters the running clip at the right place");
        {
            TakePlayer player;
            auto ramp = makeRamp();
            player.publishTake (0, 0, ramp.get());
            player.setClipRegion (0, 0, 0, 16, 0);
            player.triggerStep (0, 0, 0.125);
            lastAfter (player, 500);
            player.releaseAll();
            lastAfter (player, 1000);                         // fade-out termina
            expect (! player.isPlaying (0));
            player.triggerStep (0, 8, 0.125);                 // retomou no passo 8 = 1 s
            expectWithinAbsoluteError (lastAfter (player, 1000), (48000.0f + 999.0f) / 192000.0f, 1e-4f);
        }

        beginTest ("Back-to-back clips hand off without a click: the old voice fades out while the new fades in");
        {
            TakePlayer player;
            auto a = makeBuffer (400000, 1.0f);
            auto b = makeBuffer (400000, 1.0f);
            player.publishTake (0, 0, a.get());
            player.publishTake (0, 1, b.get());
            player.setClipRegion (0, 0, 0, 16, 0);
            player.setClipRegion (0, 1, 16, 32, 1);

            player.triggerStep (0, 0, 0.125);
            lastAfter (player, 2000);                       // regime: saída = 1.0
            player.triggerStep (1, 0, 0.125);               // o 2º clipe começa colado no 1º
            float lowest = 1.0f;
            for (int i = 0; i < 1000; ++i)
                lowest = juce::jmin (lowest, player.process (rate, gains));
            expect (lowest > 0.98f);                        // sem emenda ficaria em 0 (estalo)
            expectWithinAbsoluteError (lastAfter (player, 100), 1.0f, 1e-3f);
        }

        beginTest ("The outgoing buffer stays protected while its tail fades, then can be retired");
        {
            TakePlayer player;
            auto a = makeBuffer (400000, 1.0f);
            auto b = makeBuffer (400000, 1.0f);
            player.publishTake (0, 0, a.get());
            player.publishTake (0, 1, b.get());
            player.setClipRegion (0, 0, 0, 16, 0);
            player.setClipRegion (0, 1, 16, 32, 1);
            player.triggerStep (0, 0, 0.125);
            lastAfter (player, 2000);
            player.triggerStep (1, 0, 0.125);

            player.publishTake (0, 0, nullptr);             // a mensagem "tira" A enquanto a cauda ainda o lê
            expect (! player.isRetired (a.get()));
            lastAfter (player, 1000);                       // o fade de 5 ms termina
            expect (player.isRetired (a.get()));
            expect (! player.isRetired (b.get()));
        }

        beginTest ("Negative offsets are ignored");
        {
            TakePlayer player;
            auto ramp = makeRamp();
            player.publishTake (0, 0, ramp.get());
            player.setClipRegion (0, 0, 0, 16, 0, -5000);
            player.triggerStep (0, 0, 0.125);
            expectWithinAbsoluteError (lastAfter (player, 1000), 999.0f / 192000.0f, 1e-4f);
        }
    }
};

static TakePlayerTests takePlayerTests;
} // namespace mf::tests
