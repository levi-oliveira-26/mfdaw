#include "UI/PianoRollComponent.h"

#include <cmath>

#include "Core/ScaleLock.h"
#include "UI/Theme.h"

namespace mf
{
namespace
{
constexpr int kGridLeft     = 64;   // calha dos rótulos ("C#4")
constexpr int kHeaderHeight = 48;
constexpr int kScrollBarW   = 6;
constexpr int kInitialTopNote = 79; // ao abrir, a janela mostra ~G5 para baixo (inclui C4..C5)

juce::Rectangle<int> gridBounds (const juce::Component& component)
{
    auto bounds = component.getLocalBounds().reduced (theme::kPad);
    bounds.removeFromTop (kHeaderHeight);
    bounds.removeFromLeft (kGridLeft);
    bounds.removeFromRight (kScrollBarW + 2);
    return bounds;
}

bool isBlackKey (int midiNote) noexcept
{
    switch (((midiNote % 12) + 12) % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}
} // namespace

// ---------------------------------------------------------------------------
PianoRollComponent::PianoRollComponent (ProjectState& projectState, MainProcessor& processorToUse)
    : state (projectState), processor (processorToUse)
{
    state.getTree().addListener (this);
}

PianoRollComponent::~PianoRollComponent()
{
    state.getTree().removeListener (this);
}

juce::String PianoRollComponent::noteName (int midiNote)
{
    static constexpr const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    if (midiNote < 0 || midiNote > 127)
        return {};

    // Convenção MIDI 60 = C4.
    return juce::String (kNames[midiNote % 12]) + juce::String (midiNote / 12 - 1);
}

void PianoRollComponent::setTrackId (int newTrackId)
{
    if (newTrackId == trackId)
        return;

    trackId = newTrackId;
    reloadNotes();
}

void PianoRollComponent::reloadNotes()
{
    for (auto& row : notes)
        row.fill (false);

    if (trackId >= 0)
    {
        for (const auto& note : state.getMelodyNotes (trackId))
            notes[static_cast<std::size_t> (kHighestNote - note.pitch)][static_cast<std::size_t> (note.step)] = true;
    }

    repaint();
}

bool PianoRollComponent::commitNote (int row, int step, bool on)
{
    // O domínio valida faixa/tipo/limite; o cache só muda via listener (reloadNotes).
    return trackId >= 0 && state.setMelodyNote (trackId, step, kHighestNote - row, on);
}

// ---------------------------------------------------------------------------
// Rolagem vertical
// ---------------------------------------------------------------------------
void PianoRollComponent::clampScroll()
{
    const float contentHeight = static_cast<float> (numRows * kRowHeight);
    const float maxScroll = juce::jmax (0.0f, contentHeight - static_cast<float> (gridBounds (*this).getHeight()));
    scrollY = juce::jlimit (0.0f, maxScroll, scrollY);
}

void PianoRollComponent::scrollToNote (int midiNote)
{
    const int row = juce::jlimit (0, numRows - 1, kHighestNote - midiNote);
    scrollY = static_cast<float> (row * kRowHeight);
    clampScroll();
}

void PianoRollComponent::resized()
{
    // A altura só é conhecida aqui: posiciona a janela inicial uma única vez.
    if (! initialScrollDone && gridBounds (*this).getHeight() > 0)
    {
        initialScrollDone = true;
        scrollToNote (kInitialTopNote);
    }

    clampScroll();
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // deltaY ~ 1.0 por "clique" da roda: 3 linhas por clique; trackpads enviam frações.
    const float delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
    scrollY -= delta * 3.0f * static_cast<float> (kRowHeight);
    clampScroll();
    repaint();
}

// ---------------------------------------------------------------------------
// Pintura
// ---------------------------------------------------------------------------
void PianoRollComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    g.fillAll (col (kSurfaceBase));

    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (22.0f, true));
    g.drawText (txt ("Piano Roll · Scale Lock"), getLocalBounds().removeFromTop (kHeaderHeight),
                juce::Justification::centredLeft);

    const auto grid = gridBounds (*this);
    if (grid.isEmpty())
        return;

    const float cellWidth = static_cast<float> (grid.getWidth()) / numSteps;
    const int firstRow = juce::jmax (0, static_cast<int> (scrollY) / kRowHeight);
    const int lastRow  = juce::jmin (numRows - 1,
                                     (static_cast<int> (scrollY) + grid.getHeight()) / kRowHeight);

    // Tudo abaixo desenha apenas o que está visível e nunca vaza da grade.
    juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (juce::Rectangle<int> (grid.getX() - kGridLeft, grid.getY(),
                                              grid.getWidth() + kGridLeft, grid.getHeight()));

    g.setFont (makeFont (13.0f));

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int midiNote = kHighestNote - row;
        const bool allowed = ScaleLock::isNoteInScale (midiNote, state);
        const float y = static_cast<float> (grid.getY()) + static_cast<float> (row * kRowHeight) - scrollY;

        const auto rowBounds = juce::Rectangle<float> (static_cast<float> (grid.getX()), y,
                                                       static_cast<float> (grid.getWidth()),
                                                       static_cast<float> (kRowHeight));

        // Linhas fora do tom: escurecidas e sem rótulo (Scale Lock).
        g.setColour (! allowed ? col (kBgDeep)
                               : (isBlackKey (midiNote) ? col (kSurfaceBase) : col (kSurfaceElevated)));
        g.fillRect (rowBounds);

        // Separador de oitava (a cada C) para orientar na rolagem.
        const bool isC = (midiNote % 12) == 0;
        g.setColour (isC ? col (kBrandMelody).withAlpha (0.45f) : col (kBorderSubtle).withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (rowBounds.getBottom()) - 1,
                              static_cast<float> (grid.getX()), static_cast<float> (grid.getRight()));

        if (allowed)
        {
            g.setColour (isC ? col (kBrandMelody) : col (kTextMuted));
            g.drawText (noteName (midiNote),
                        juce::Rectangle<int> (grid.getX() - kGridLeft, juce::roundToInt (y),
                                              kGridLeft - 8, kRowHeight),
                        juce::Justification::centredRight);
        }
    }

    // Divisões de passo: o início de cada tempo (a cada 4 passos) é mais forte.
    for (int step = 0; step <= numSteps; ++step)
    {
        g.setColour (step % 4 == 0 ? col (kTextMuted).withAlpha (0.45f) : col (kBorderSubtle));
        g.drawVerticalLine (juce::roundToInt (static_cast<float> (grid.getX()) + step * cellWidth),
                            static_cast<float> (grid.getY()), static_cast<float> (grid.getBottom()));
    }

    // Notas visíveis. Se a tonalidade mudou depois de escrever, as notas que ficaram fora
    // do tom continuam soando: aparecem apagadas em vez de sumirem.
    for (int row = firstRow; row <= lastRow; ++row)
    {
        const bool inScale = ScaleLock::isNoteInScale (kHighestNote - row, state);
        g.setColour (inScale ? col (kBrandMelody) : col (kTextMuted).withAlpha (0.55f));

        const float y = static_cast<float> (grid.getY()) + static_cast<float> (row * kRowHeight) - scrollY;

        for (int step = 0; step < numSteps; ++step)
        {
            if (! notes[static_cast<std::size_t> (row)][static_cast<std::size_t> (step)])
                continue;

            g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (grid.getX()) + step * cellWidth + 2.0f,
                                                            y + 2.0f, cellWidth - 4.0f,
                                                            static_cast<float> (kRowHeight) - 4.0f),
                                    3.0f);
        }
    }

    // Indicador de rolagem (plano, sem sombra).
    const float contentHeight = static_cast<float> (numRows * kRowHeight);
    if (contentHeight > static_cast<float> (grid.getHeight()))
    {
        const float trackH = static_cast<float> (grid.getHeight());
        const float thumbH = juce::jmax (24.0f, trackH * trackH / contentHeight);
        const float maxScroll = contentHeight - trackH;
        const float thumbY = static_cast<float> (grid.getY()) + (trackH - thumbH) * (scrollY / maxScroll);

        g.setColour (col (kBorderSubtle));
        g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (grid.getRight() + 2),
                                                        static_cast<float> (grid.getY()),
                                                        static_cast<float> (kScrollBarW), trackH), 3.0f);
        g.setColour (col (kTextMuted));
        g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (grid.getRight() + 2), thumbY,
                                                        static_cast<float> (kScrollBarW), thumbH), 3.0f);
    }
}

// ---------------------------------------------------------------------------
// Interação
// ---------------------------------------------------------------------------
int PianoRollComponent::noteAt (const juce::Point<int>& position, int* rowOut, int* stepOut) const
{
    if (rowOut != nullptr)
        *rowOut = -1;
    if (stepOut != nullptr)
        *stepOut = -1;

    const auto grid = gridBounds (*this);
    if (! grid.contains (position))
        return -1;

    const float cellWidth = static_cast<float> (grid.getWidth()) / numSteps;
    const int step = juce::jlimit (0, numSteps - 1, static_cast<int> ((position.x - grid.getX()) / cellWidth));
    const int row  = juce::jlimit (0, numRows - 1,
                                   static_cast<int> ((static_cast<float> (position.y - grid.getY()) + scrollY) / kRowHeight));
    const int midiNote = kHighestNote - row;

    if (rowOut != nullptr)
        *rowOut = row;
    if (stepOut != nullptr)
        *stepOut = step;

    return ScaleLock::isNoteInScale (midiNote, state) ? midiNote : -1;
}

int PianoRollComponent::toggleNote (const juce::Point<int>& position)
{
    int row = -1;
    int step = -1;
    const int midiNote = noteAt (position, &row, &step);
    if (midiNote < 0)
        return -1;

    const bool turnOn = ! notes[static_cast<std::size_t> (row)][static_cast<std::size_t> (step)];
    if (! commitNote (row, step, turnOn))
        return -1;                       // sem faixa, faixa errada ou limite de notas

    return turnOn ? midiNote : -1;       // apagar uma nota não toca pré-escuta
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown())
        return;

    if (previewNote >= 0)
        processor.requestNotePreview (previewNote, false);

    state.getUndoManager().beginNewTransaction();   // o gesto inteiro (clique + arrasto) = 1 undo
    previewNote = toggleNote (event.getPosition());

    // A célula do clique já foi tratada (ligada OU apagada): o arrasto só age ao entrar em outra.
    juce::ignoreUnused (noteAt (event.getPosition(), &previewRow, &previewStep));

    if (previewNote >= 0)
        processor.requestNotePreview (previewNote, true);
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown())
        return;

    int row = -1;
    int step = -1;
    const int nextNote = noteAt (event.getPosition(), &row, &step);

    if (row == previewRow && step == previewStep)
        return;

    if (previewNote >= 0 && nextNote != previewNote)
        processor.requestNotePreview (previewNote, false);

    previewRow = row;
    previewStep = step;

    int audibleNote = nextNote;
    if (nextNote >= 0 && ! commitNote (row, step, true))
        audibleNote = -1;

    if (audibleNote >= 0 && audibleNote != previewNote)
        processor.requestNotePreview (audibleNote, true);

    previewNote = audibleNote;
}

void PianoRollComponent::mouseUp (const juce::MouseEvent&)
{
    if (previewNote >= 0)
        processor.requestNotePreview (previewNote, false);

    previewNote = -1;
    previewRow = -1;
    previewStep = -1;
}

void PianoRollComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == IDs::keyRoot || property == IDs::scale)
        repaint();
}

// Nota criada/apagada (inclui undo/redo) ou estrutura da Playlist trocada (carregar projeto,
// remover a faixa editada): o cache é relido do domínio.
void PianoRollComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (child.hasType (IDs::note) || parent.hasType (IDs::playlist) || child.hasType (IDs::playlist))
        reloadNotes();
}

void PianoRollComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (child.hasType (IDs::note) || parent.hasType (IDs::playlist) || child.hasType (IDs::playlist))
        reloadNotes();
}
} // namespace mf
