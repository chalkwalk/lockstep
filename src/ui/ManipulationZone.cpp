#include "ManipulationZone.h"

namespace lockstep
{
    ManipulationZone::ManipulationZone() = default;

    void ManipulationZone::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(getLocalBounds(), 1);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Manipulation Zone (M6)", getLocalBounds(), juce::Justification::centred);
    }
}
