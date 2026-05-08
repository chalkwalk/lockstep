#include "PageBar.h"

namespace lockstep
{
    PageBar::PageBar() = default;

    void PageBar::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(getLocalBounds(), 1);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Pages 1..12 (M6)", getLocalBounds(), juce::Justification::centred);
    }
}
