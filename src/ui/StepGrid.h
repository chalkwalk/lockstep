#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class StepGrid : public juce::Component
    {
    public:
        StepGrid();
        void paint(juce::Graphics& g) override;
    };
}
