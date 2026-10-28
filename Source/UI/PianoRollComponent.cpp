#include "UI/PianoRollComponent.h"

#include "UI/Theme.h"

namespace mf
{
namespace
{
constexpr int kTopOctaveNote = 72;
constexpr int kGridLeft = 48;
constexpr int kHeaderHeight = 48;

juce::Rectangle<int> gridBounds (const juce::Component& component)
{
    auto bounds = component.getLocalBounds().reduced (theme::kPad);
    bounds.removeFromTop (kHeaderHeight);
    bounds.removeFromLeft (kGridLeft);
    return bounds;
}
} // namespace

PianoRollComponent::PianoRollComponent (ProjectState& projectState, MainProcessor& processor)
    : state (projectState), processor (processor)
{
    notes[4][0] = true;
    notes[3][4] = true;
    notes[1][8] = true;
    notes[0][12] = true;
    state.getTree().addListener (this);
}

PianoRollComponent::~PianoRollComponent()
{
    state.getTree().removeListener (this);
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::col (theme::kSurfaceBase));

    g.setColour (theme::col (theme::kTextPrimary));
    g.setFont (theme::makeFont (22.0f, true));
    g.drawText ("Piano Roll — Scale Lock", getLocalBounds().removeFromTop (48),
                juce::Justification::centredLeft);

    const auto grid = gridBounds (*this);
    const float cellWidth = static_cast<float> (grid.getWidth()) / numSteps;
    const float cellHeight = static_cast<float> (grid.getHeight()) / numRows;

    for (int row = 0; row < numRows; ++row)
    {
        const int midiNote = kTopOctaveNote - row;
        const bool allowed = ScaleLock::isNoteInScale (midiNote, state);

        const auto rowBounds = juce::Rectangle<float> (static_cast<float> (grid.getX()),
                                                       static_cast<float> (grid.getY()) + row * cellHeight,
                                                       static_cast<float> (grid.getWidth()), cellHeight);
        g.setColour (allowed ? theme::col (theme::kSurfaceElevated)
                             : theme::col (theme::kBgDeep));
        g.fillRect (rowBounds);

        g.setColour (allowed ? theme::col (theme::kBrandMelody).withAlpha (0.12f)
                             : theme::col (theme::kTextMuted).withAlpha (0.10f));
        g.drawHorizontalLine (juce::roundToInt (rowBounds.getBottom()),
                              static_cast<float> (grid.getX()),
                              static_cast<float> (grid.getRight()));

        if (allowed)
        {
            g.setColour (theme::col (theme::kBrandMelody).withAlpha (0.55f));
            g.drawText (juce::String (midiNote),
                        juce::Rectangle<int> (grid.getX() - kGridLeft, juce::roundToInt (rowBounds.getY()),
                                              kGridLeft - 4, juce::roundToInt (cellHeight)),
                        juce::Justification::centredRight);
        }
    }

    for (int step = 0; step <= numSteps; ++step)
    {
        const float x = grid.getX() + step * cellWidth;
        g.setColour (theme::col (theme::kBorderSubtle));
        g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (grid.getY()),
                            static_cast<float> (grid.getBottom()));
    }

    g.setColour (theme::col (theme::kBrandMelody));
    for (int row = 0; row < numRows; ++row)
    {
        if (! ScaleLock::isNoteInScale (kTopOctaveNote - row, state))
            continue;

        for (int step = 0; step < numSteps; ++step)
        {
            if (! notes[static_cast<size_t> (row)][static_cast<size_t> (step)])
                continue;

            const auto noteBounds = juce::Rectangle<float> (grid.getX() + step * cellWidth + 2.0f,
                                                            grid.getY() + row * cellHeight + 2.0f,
                                                            cellWidth - 4.0f, cellHeight - 4.0f);
            g.fillRoundedRectangle (noteBounds, 3.0f);
        }
    }
}

void PianoRollComponent::resized()
{
}

int PianoRollComponent::noteAt (const juce::Point<int>& position, int* rowOut, int* stepOut) const
{
    const auto grid = gridBounds (*this);
    if (! grid.contains (position))
        return -1;

    const float cellWidth = static_cast<float> (grid.getWidth()) / numSteps;
    const float cellHeight = static_cast<float> (grid.getHeight()) / numRows;
    const int step = juce::jlimit (0, numSteps - 1,
                                   static_cast<int> ((position.x - grid.getX()) / cellWidth));
    const int row = juce::jlimit (0, numRows - 1,
                                  static_cast<int> ((position.y - grid.getY()) / cellHeight));
    const int midiNote = kTopOctaveNote - row;

    if (rowOut != nullptr)
        *rowOut = row;
    if (stepOut != nullptr)
        *stepOut = step;

    return ScaleLock::isNoteInScale (midiNote, state) ? midiNote : -1;
}

int PianoRollComponent::toggleNote (const juce::Point<int>& position)
{
    int row = 0;
    int step = 0;
    const int midiNote = noteAt (position, &row, &step);
    if (midiNote < 0)
        return -1;

    auto& active = notes[static_cast<size_t> (row)][static_cast<size_t> (step)];
    active = ! active;
    repaint (gridBounds (*this));
    return midiNote;
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown())
        return;

    if (previewNote >= 0)
        processor.requestNotePreview (previewNote, false);

    previewNote = toggleNote (event.getPosition());

    if (previewNote >= 0)
    {
        juce::ignoreUnused (noteAt (event.getPosition(), &previewRow, &previewStep));
        processor.requestNotePreview (previewNote, true);
    }
    else
    {
        previewRow = -1;
        previewStep = -1;
    }
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown())
        return;

    int row = 0;
    int step = 0;
    const int nextNote = noteAt (event.getPosition(), &row, &step);

    if (row == previewRow && step == previewStep)
        return;

    if (previewNote >= 0 && nextNote != previewNote)
        processor.requestNotePreview (previewNote, false);

    previewRow = row;
    previewStep = step;

    if (nextNote >= 0)
    {
        notes[static_cast<size_t> (row)][static_cast<size_t> (step)] = true;
        repaint (gridBounds (*this));

        if (nextNote != previewNote)
            processor.requestNotePreview (nextNote, true);
    }

    previewNote = nextNote;
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

void PianoRollComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) {}
void PianoRollComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) {}
} // namespace mf
