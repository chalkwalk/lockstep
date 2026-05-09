#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class LockstepProcessor;
    class StepGrid;

    // Shows the first 4 parameter slots (page 0) for the active track.
    // Reads from and writes to the correct layer — Step Override when a step
    // is held, Track Base otherwise — via LockstepProcessor::writeParam.
    class ManipulationZone : public juce::Component, public juce::Timer
    {
    public:
        ManipulationZone(LockstepProcessor& processor, StepGrid& grid);
        ~ManipulationZone() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;

    private:
        static constexpr int kNumSlots = 4;

        void refreshSliders();

        LockstepProcessor& processor_;
        StepGrid& grid_;

        std::array<juce::Slider, kNumSlots> sliders_;
        std::array<juce::Label,  kNumSlots> labels_;
        bool updatingFromTimer_ = false;
    };
}
