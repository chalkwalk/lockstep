#include "StepGrid.h"

namespace lockstep
{
    StepGrid::StepGrid() = default;

    void StepGrid::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(getLocalBounds(), 1);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Step Grid 2x8 (M6)", getLocalBounds(), juce::Justification::centred);
    }
}
