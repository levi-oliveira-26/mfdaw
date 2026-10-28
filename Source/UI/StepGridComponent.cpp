#include "UI/StepGridComponent.h"

#include "UI/Theme.h"

namespace mf
{
StepGridComponent::StepGridComponent (ProjectState& stateToUse, SampleManager& sampleManagerToUse,
                                      double targetSampleRateToUse)
    : state (stateToUse),
      sampleManager (sampleManagerToUse),
      targetSampleRate (targetSampleRateToUse)
{
    swingSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    swingSlider.setRange (0.0, 100.0, 1.0);
    swingSlider.setValue (state.getSwingAmount() * 100.0,
                          juce::dontSendNotification);
    swingSlider.setTextValueSuffix ("%");
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
    swingSlider.addListener (this);
    addAndMakeVisible (swingSlider);

    for (int track = 0; track < kNumTracks; ++track)
    {
        auto* trackNameLabel = new juce::Label();
        trackNameLabel->setText (state.getTrackName (track), juce::dontSendNotification);
        trackNameLabel->setEditable (true, true);
        trackNameLabel->setJustificationType (juce::Justification::centredLeft);
        trackNameLabel->onTextChange = [this, track, trackNameLabel]
        {
            state.setTrackName (track, trackNameLabel->getText());
        };
        trackNameLabels.add (trackNameLabel);
        addAndMakeVisible (trackNameLabel);
    }

    state.getTree().addListener (this);
}

StepGridComponent::~StepGridComponent()
{
    swingSlider.removeListener (this);
    state.getTree().removeListener (this);
}

void StepGridComponent::resized()
{
    swingSlider.setBounds (getWidth() - 88, 4, 76, 44);

    const auto grid = getGridBounds();
    const float rowHeight = grid.getHeight() / static_cast<float> (kNumTracks);
    for (int track = 0; track < kNumTracks; ++track)
    {
        trackNameLabels[track]->setBounds (
            0, juce::roundToInt (grid.getY() + rowHeight * track),
            juce::roundToInt (kLabelWidth - 8.0f), juce::roundToInt (rowHeight));
    }
}

void StepGridComponent::sliderValueChanged (juce::Slider* slider)
{
    if (slider == &swingSlider)
        state.setSwingAmount (static_cast<float> (swingSlider.getValue() / 100.0));
}

void StepGridComponent::setPlayheadStep (int step)
{
    if (step != playheadStep)
    {
        playheadStep = step;
        repaint();
    }
}

void StepGridComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == IDs::name && tree.getParent().isValid()
        && tree.getParent().getType() == IDs::sequencer)
    {
        const auto sequencer = state.getTree().getChildWithName (IDs::sequencer);
        const int track = sequencer.indexOf (tree);
        if (track >= 0 && track < kNumTracks)
        {
            auto* label = trackNameLabels[track];
            const auto newName = state.getTrackName (track);
            if (label->getText() != newName)
                label->setText (newName, juce::dontSendNotification);
        }
    }

    repaint();
}

juce::Rectangle<float> StepGridComponent::getGridBounds() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (kLabelWidth)
        .withTrimmedTop (52.0f);
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

    const auto rhythm        = col (kBrandRhythm);
    const auto gridBackground = rhythm.withAlpha (0.08f);
    const auto groupA        = col (kSurfaceElevated);
    const auto groupB        = col (kSurfaceElevated).interpolatedWith (col (kSurfaceBase), 0.55f);
    const auto active        = rhythm.withAlpha (0.92f);
    const auto blockDivider  = rhythm.withAlpha (0.32f);
    const auto playhead      = col (kTextPrimary).withAlpha (0.16f);
    const auto grid          = getGridBounds();

    g.fillAll (col (kSurfaceBase));
    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (13.0f, true));
    g.drawText ("Swing", juce::Rectangle<float> (getWidth() - 88.0f, 0.0f, 76.0f, 16.0f),
                juce::Justification::centred);
    g.setColour (gridBackground);
    g.fillRect (grid);

    g.setFont (makeFont (14.0f, true));

    for (int track = 0; track < kNumTracks; ++track)
    {
        for (int step = 0; step < kNumSteps; ++step)
        {
            const auto cell = getCellBounds (track, step).reduced (2.0f);
            const bool firstStepInBlock = (step % 4) == 0;

            g.setColour (state.isStepActive (track, step) ? active
                                                          : (firstStepInBlock ? groupA.withAlpha (0.82f)
                                                                              : groupB.withAlpha (0.82f)));
            g.fillRect (cell);

            if (state.isStepActive (track, step))
            {
                const float velocity = static_cast<float> (state.getStepVelocity (track, step)) / 127.0f;
                const float barWidth = juce::jmax (1.0f, cell.getWidth() * 0.12f);
                const float barHeight = juce::jlimit (0.0f, cell.getHeight(), cell.getHeight() * velocity);

                g.setColour (col (kTextPrimary).withAlpha (0.72f));
                g.fillRect (cell.getRight() - barWidth - 1.0f,
                            cell.getBottom() - barHeight,
                            barWidth,
                            barHeight);
            }

            if (step == playheadStep)
            {
                g.setColour (playhead);
                g.fillRect (cell);
            }
        }
    }

    const float cellWidth = grid.getWidth() / static_cast<float> (kNumSteps);
    for (int block = 1; block < 4; ++block)
    {
        const float x = grid.getX() + cellWidth * static_cast<float> (block * 4);
        g.setColour (blockDivider);
        g.fillRect (x - 0.75f, grid.getY(), 1.5f, grid.getHeight());
    }
}

void StepGridComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto grid = getGridBounds();

    if (! grid.contains (e.position))
        return;

    const float cellW = grid.getWidth()  / static_cast<float> (kNumSteps);
    const float cellH = grid.getHeight() / static_cast<float> (kNumTracks);

    const int step  = juce::jlimit (0, kNumSteps - 1, static_cast<int> ((e.position.x - grid.getX()) / cellW));
    const int track = juce::jlimit (0, kNumTracks - 1, static_cast<int> ((e.position.y - grid.getY()) / cellH));

    if (e.mods.isShiftDown())
    {
        velocityTrack = track;
        velocityStep = step;

        const auto cell = getCellBounds (track, step);
        const float normalized = juce::jlimit (0.0f, 1.0f,
                                               (cell.getBottom() - e.position.y) / cell.getHeight());
        state.setStepVelocity (track, step, static_cast<int> (normalized * 127.0f));
        return;
    }

    state.toggleStep (track, step);
    repaint();
}

void StepGridComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (velocityTrack < 0 || velocityStep < 0 || ! e.mods.isShiftDown())
        return;

    const auto cell = getCellBounds (velocityTrack, velocityStep);
    const float normalized = juce::jlimit (0.0f, 1.0f,
                                           (cell.getBottom() - e.position.y) / cell.getHeight());
    const int velocity = juce::jlimit (0, 127,
                                       static_cast<int> (normalized * 127.0f));

    if (state.getStepVelocity (velocityTrack, velocityStep) != velocity)
        state.setStepVelocity (velocityTrack, velocityStep, velocity);
}

void StepGridComponent::mouseUp (const juce::MouseEvent&)
{
    velocityTrack = -1;
    velocityStep = -1;
}

} // namespace mf
