#include <cmath>
#include <vector>

#include <juce_core/juce_core.h>

#include "Core/ProjectState.h"
#include "Core/SampleEdit.h"
#include "Engine/SampleProcessing.h"

namespace mf::tests
{
/** Vertical Slice 09: faixa de Amostra (importar + cortar silêncio / normalizar / fades). */
class SampleTrackTests final : public juce::UnitTest
{
public:
    SampleTrackTests() : juce::UnitTest ("Sample Track Tests", "Samples") {}

    void runTest() override
    {
        runDspTests();
        runDomainTests();
    }

private:
    static std::vector<float> burst (double rate, double silenceBefore, double tone, double silenceAfter, float amp)
    {
        std::vector<float> s;
        s.assign (static_cast<std::size_t> (silenceBefore * rate), 0.0f);
        for (std::size_t i = 0; i < static_cast<std::size_t> (tone * rate); ++i)
            s.push_back (amp * std::sin (static_cast<float> (i) * 0.3f));
        s.insert (s.end(), static_cast<std::size_t> (silenceAfter * rate), 0.0f);
        return s;
    }

    void runDspTests()
    {
        constexpr double kRate = 48000.0;

        beginTest ("SampleEdit: defaults, clamping and a unique signature per edit");
        {
            expect (SampleEdit {}.isDefault());
            const auto wild = SampleEdit { true, false, 99999, -5 }.clamped();
            expectEquals (wild.fadeInMs, SampleEdit::kMaxFadeMs);
            expectEquals (wild.fadeOutMs, 0);
            expect (SampleEdit { true, false, 0, 0 }.signature() != SampleEdit { false, true, 0, 0 }.signature());
            expect (SampleEdit { false, false, 100, 0 }.signature() != SampleEdit { false, false, 0, 100 }.signature());
            expect (SampleEdit {}.signature() == 0);
        }

        beginTest ("findAudibleRange keeps the sound plus a small margin; pure silence -> empty");
        {
            const auto s = burst (kRate, 0.5, 0.2, 0.5, 0.5f);
            const auto r = sampleproc::findAudibleRange (s, kRate);
            expect (r.begin > 0.48 * kRate && r.begin <= 0.5 * kRate);
            expect (r.end >= 0.7 * kRate && r.end < 0.72 * kRate);

            expectEquals (static_cast<int> (sampleproc::findAudibleRange (std::vector<float> (1000, 0.0f), kRate).size()), 0);
            expectEquals (static_cast<int> (sampleproc::findAudibleRange ({}, kRate).size()), 0);
        }

        beginTest ("normalizePeak sets the peak to -1 dBFS (also for negative peaks); silence is untouched");
        {
            std::vector<float> a { 0.1f, -0.25f, 0.2f };
            sampleproc::normalizePeak (a);
            expectWithinAbsoluteError (sampleproc::peakOf (a), sampleproc::dbToGain (-1.0f), 1e-5f);
            expect (a[1] < 0.0f);   // sinal preservado

            std::vector<float> quiet (100, 0.0f);
            sampleproc::normalizePeak (quiet);
            expectEquals (sampleproc::peakOf (quiet), 0.0f);

            std::vector<float> loud { 2.0f, -1.0f };   // também reduz o que estoura
            sampleproc::normalizePeak (loud);
            expectWithinAbsoluteError (sampleproc::peakOf (loud), sampleproc::dbToGain (-1.0f), 1e-5f);
        }

        beginTest ("Fades ramp from/to silence and never overlap on tiny audio");
        {
            std::vector<float> s (1000, 1.0f);
            sampleproc::applyFades (s, 1000.0, 100, 100);   // 1 kHz: 100 ms = 100 amostras
            expectEquals (s.front(), 0.0f);
            expectEquals (s.back(), 0.0f);
            expectWithinAbsoluteError (s[50], 0.5f, 1e-5f);
            expectEquals (s[500], 1.0f);

            std::vector<float> tiny (10, 1.0f);
            sampleproc::applyFades (tiny, 1000.0, 2000, 2000);   // pedido enorme: limitado à metade
            for (const float v : tiny)
                expect (v >= 0.0f && v <= 1.0f);

            std::vector<float> none;
            sampleproc::applyFades (none, 48000.0, 100, 100);   // vazio não quebra
            expect (none.empty());
        }

        beginTest ("process: trim -> normalize -> fades; default edit is a faithful copy");
        {
            const auto s = burst (kRate, 0.5, 0.2, 0.5, 0.2f);

            const auto same = sampleproc::process (s, kRate, SampleEdit {});
            expect (same == s);

            const auto trimmed = sampleproc::process (s, kRate, SampleEdit { true, false, 0, 0 });
            expect (trimmed.size() < s.size() / 2);

            const auto all = sampleproc::process (s, kRate, SampleEdit { true, true, 20, 20 });
            expect (all.size() == trimmed.size());
            expectEquals (all.front(), 0.0f);
            expectEquals (all.back(), 0.0f);
            expect (sampleproc::peakOf (all) > sampleproc::peakOf (trimmed));   // ficou mais alto

            const auto silent = sampleproc::process (std::vector<float> (500, 0.0f), kRate, SampleEdit { true, true, 10, 10 });
            expect (silent.empty());
        }
    }

    void runDomainTests()
    {
        beginTest ("addSample works only on Sample tracks and validates input");
        {
            ProjectState state;
            state.setBpm (120);
            const int sample = state.addTrack (TrackType::Sample);
            const int voice = state.addTrack (TrackType::Record);

            expectEquals (state.addSample (voice, "a.wav", 1.0), -1);
            expectEquals (state.addSample (999, "a.wav", 1.0), -1);
            expectEquals (state.addSample (sample, "", 1.0), -1);
            expectEquals (state.addSample (sample, "a.wav", 0.0), -1);
            expectEquals (state.addSample (sample, "a.wav", 1.0, kPlaylistBars), -1);
            expect (state.getTakes (sample).empty() && state.getClips (sample).empty());
        }

        beginTest ("A sample creates audio + active + clip at the first free bar, as ONE undo step");
        {
            ProjectState state;
            state.setBpm (120);
            const int track = state.addTrack (TrackType::Sample);

            const int a = state.addSample (track, "a.wav", 3.0);   // 3 s a 120 BPM = 2 compassos
            expect (a > 0);
            expectEquals (state.getActiveTakeId (track), a);
            auto clips = state.getClips (track);
            expectEquals (static_cast<int> (clips.size()), 1);
            expectEquals (clips[0].startBar, 0);
            expectEquals (clips[0].lengthBars, 2);
            expectEquals (clips[0].takeId, a);

            const int b = state.addSample (track, "b.wav", 1.0);   // próximo livre: compasso 2
            clips = state.getClips (track);
            expectEquals (static_cast<int> (clips.size()), 2);
            expectEquals (clips[1].startBar, 2);
            expectEquals (clips[1].takeId, b);

            state.getUndoManager().undo();
            expectEquals (static_cast<int> (state.getTakes (track).size()), 1);
            expectEquals (static_cast<int> (state.getClips (track).size()), 1);
            expectEquals (state.getActiveTakeId (track), a);
        }

        beginTest ("Explicit start bar, clamping to free space, occupied start and a full lane");
        {
            ProjectState state;
            state.setBpm (120);
            const int track = state.addTrack (TrackType::Sample);

            expect (state.addSample (track, "a.wav", 1.0, 4) > 0);   // compasso 4
            expect (state.addSample (track, "b.wav", 60.0, 2) > 0);  // quer 30 compassos: cabem 2 (2..3)
            bool clamped = false;
            for (const auto& c : state.getClips (track))
                if (c.startBar == 2)
                    clamped = c.lengthBars == 2;
            expect (clamped);

            const int taken = state.addSample (track, "c.wav", 1.0, 4);   // início ocupado: áudio fica, sem clipe
            expect (taken > 0);
            expectEquals (static_cast<int> (state.getClips (track).size()), 2);

            ProjectState full;
            full.setBpm (120);
            const int t2 = full.addTrack (TrackType::Sample);
            expect (full.addSample (t2, "all.wav", 600.0) > 0);   // ocupa as 16 barras
            const int extra = full.addSample (t2, "x.wav", 1.0);
            expect (extra > 0);                                    // faixa cheia: áudio salvo, sem clipe novo
            expectEquals (static_cast<int> (full.getClips (t2).size()), 1);
        }

        beginTest ("Clips on Sample tracks must point to an audio; addClip uses the active one");
        {
            ProjectState state;
            state.setBpm (120);
            const int track = state.addTrack (TrackType::Sample);
            expectEquals (state.addClip (track, 5, 1), -1);        // sem áudio: recusado

            const int a = state.addSample (track, "a.wav", 1.0);
            const int clipId = state.addClip (track, 5, 1);
            expect (clipId > 0);
            bool linked = false;
            for (const auto& c : state.getClips (track))
                if (c.id == clipId)
                    linked = c.takeId == a;
            expect (linked);
        }

        beginTest ("Edits: set/get, clamped, no-op leaves no history, undoable, only on Sample tracks");
        {
            ProjectState state;
            state.setBpm (120);
            const int track = state.addTrack (TrackType::Sample);
            const int voice = state.addTrack (TrackType::Record);
            const int a = state.addSample (track, "a.wav", 1.0);
            const int v = state.addRecordedTake (voice, "v.wav", 0, 1.0);

            expect (state.getSampleEdit (track, a).isDefault());
            expect (state.setSampleEdit (track, a, SampleEdit { true, true, 120, 99999 }));
            auto e = state.getSampleEdit (track, a);
            expect (e.trimSilence && e.normalize);
            expectEquals (e.fadeInMs, 120);
            expectEquals (e.fadeOutMs, SampleEdit::kMaxFadeMs);

            state.getUndoManager().undo();
            expect (state.getSampleEdit (track, a).isDefault());
            state.getUndoManager().redo();
            expect (state.getSampleEdit (track, a) == e);

            // repetir o mesmo valor não cria passo de undo
            expect (state.setSampleEdit (track, a, e));
            state.getUndoManager().undo();
            expect (state.getSampleEdit (track, a).isDefault());   // desfez a edição, não um no-op

            expect (! state.setSampleEdit (track, 12345, e));
            expect (! state.setSampleEdit (voice, v, e));
            expect (! state.setSampleEdit (999, a, e));
        }

        beginTest ("Edits and audios survive save/load; corrupt values are sanitised");
        {
            ProjectState source;
            source.setBpm (120);
            const int track = source.addTrack (TrackType::Sample);
            const int voice = source.addTrack (TrackType::Record);
            const int a = source.addSample (track, "a.wav", 2.0);
            const int v = source.addRecordedTake (voice, "v.wav", 0, 1.0);
            source.setSampleEdit (track, a, SampleEdit { true, false, 250, 0 });

            auto playlist = source.getTree().getChildWithName (IDs::playlist);
            auto sampleNode = playlist.getChild (source.indexOfTrack (track));
            for (int c = 0; c < sampleNode.getNumChildren(); ++c)
                if (sampleNode.getChild (c).hasType (IDs::take))
                    sampleNode.getChild (c).setProperty (IDs::fadeOutMs, 123456, nullptr);   // corrompido

            auto voiceNode = playlist.getChild (source.indexOfTrack (voice));
            for (int c = 0; c < voiceNode.getNumChildren(); ++c)
                if (voiceNode.getChild (c).hasType (IDs::take))
                    voiceNode.getChild (c).setProperty (IDs::trimSilence, true, nullptr);    // edição em faixa errada

            ProjectState loaded;
            expect (loaded.loadFromJsonString (source.createJsonString()));

            const auto e = loaded.getSampleEdit (track, a);
            expect (e.trimSilence);
            expectEquals (e.fadeInMs, 250);
            expectEquals (e.fadeOutMs, SampleEdit::kMaxFadeMs);
            expect (loaded.getSampleEdit (voice, v).isDefault());
            expectEquals (static_cast<int> (loaded.getClips (track).size()), 1);
            expectEquals (loaded.getClips (track)[0].takeId, a);
        }

        beginTest ("Removing an audio removes its clips in the same undo step");
        {
            ProjectState state;
            state.setBpm (120);
            const int track = state.addTrack (TrackType::Sample);
            const int a = state.addSample (track, "a.wav", 1.0);
            const int b = state.addSample (track, "b.wav", 1.0);
            expectEquals (static_cast<int> (state.getClips (track).size()), 2);

            expect (state.removeTake (track, a));
            expectEquals (static_cast<int> (state.getClips (track).size()), 1);
            expectEquals (state.getClips (track)[0].takeId, b);

            state.getUndoManager().undo();
            expectEquals (static_cast<int> (state.getClips (track).size()), 2);
            expectEquals (static_cast<int> (state.getTakes (track).size()), 2);
        }
    }
};

static SampleTrackTests sampleTrackTests;

} // namespace mf::tests
