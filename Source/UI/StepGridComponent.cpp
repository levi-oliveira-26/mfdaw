#include "UI/StepGridComponent.h"

#include "UI/Theme.h"

namespace mf
{
StepGridComponent::StepGridComponent (ProjectState& stateToUse)
    : state (stateToUse)
{
    state.getTree().addListener (this);
}

StepGridComponent::~StepGridComponent()
{
    state.getTree().removeListener (this);
}

void StepGridComponent::setPlayheadStep (int step)
{
    if (step != playheadStep)
    {
        playheadStep = step;
        repaint();
    }
}

void StepGridComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    repaint();
}

juce::Rectangle<float> StepGridComponent::getGridBounds() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (kLabelWidth);
}

juce::Rectangle<float> StepGridComponent::getCellBounds (int track, int step) const
{
    const auto grid  = getGridBounds();
    const float cellW = grid.getWidth()  / static_cast<float> (kNumSteps);
    const float cellH = grid.getHeight() / static_cast<float> (kNumTracks);

    return { grid.getX() + cellW * static_cast<float> (step),
             grid.getY() + cellH * static_cast<float> (track),
             cellW, cellH };
}

void StepGridComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto active    = col (kBrandRhythm);
    const auto groupA    = col (kSurfaceElevated);
    const auto groupB    = col (kSurfaceElevated).interpolatedWith (col (kSurfaceBase), 0.55f);
    const auto playhead  = col (kTextPrimary).withAlpha (0.16f);

    g.setFont (makeFont (14.0f, true));

    for (int track = 0; track < kNumTracks; ++track)
    {
        // Rótulo da pista
        const auto firstCell = getCellBounds (track, 0);
        g.setColour (col (kTextMuted));
        g.drawText (state.getTrackName (track),
                    juce::Rectangle<float> (0.0f, firstCell.getY(), kLabelWidth - 8.0f, firstCell.getHeight()),
                    juce::Justification::centredLeft);

        // Células
        for (int step = 0; step < kNumSteps; ++step)
        {
            const auto cell = getCellBounds (track, step).reduced (2.0f);

            // Agrupa visualmente de 4 em 4 passos (uma batida).
            g.setColour (state.isStepActive (track, step) ? active
                                                          : ((step / 4) % 2 == 0 ? groupA : groupB));
            g.fillRoundedRectangle (cell, 4.0f);

            if (step == playheadStep)
            {
                g.setColour (playhead);
                g.fillRoundedRectangle (cell, 4.0f);
            }
        }
    }
}

void StepGridComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto grid = getGridBounds();

    if (! grid.contains (e.position))
        return;

    const float cellW = grid.getWidth()  / static_cast<float> (kNumSteps);
    const float cellH = grid.getHeight() / static_cast<float> (kNumTracks);

    const int step  = juce::jlimit (0, kNumSteps - 1,  static_cast<int> ((e.position.x - grid.getX()) / cellW));
    const int track = juce::jlimit (0, kNumTracks - 1, static_cast<int> ((e.position.y - grid.getY()) / cellH));

    state.toggleStep (track, step);
}

} // namespace mf
