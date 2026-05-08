#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class PageBar : public juce::Component
    {
    public:
        PageBar();
        void paint(juce::Graphics& g) override;
    };
}
