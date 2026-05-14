#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../state/UiState.h"
#include "GridDisplayMode.h"

namespace lockstep
{
    class LockstepProcessor;

    // Displays the Q-row keys (Q W E R T Y U I) with primary and Shift
    // function labels. Keys highlight when physically held.
    class FunctionBar : public juce::Component, private juce::Timer
    {
    public:
        FunctionBar(LockstepProcessor& processor, UiState& uiState);

        void setDisplayMode(GridDisplayMode mode);

        void paint(juce::Graphics& g) override;

        static constexpr int kNumKeys = 8;

    private:
        LockstepProcessor& processor_;
        UiState&           uiState_;
        GridDisplayMode    mode_ = GridDisplayMode::Ortholinear;

        void timerCallback() override { repaint(); }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FunctionBar)
    };
}
