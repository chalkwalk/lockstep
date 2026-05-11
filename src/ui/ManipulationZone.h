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
    class ManipulationZone : public juce::Component,
                             public juce::Timer
    {
    public:
        ManipulationZone(LockstepProcessor& processor, StepGrid& grid);
        ~ManipulationZone() override;

        void paint(juce::Graphics& g) override;
        void paintOverChildren(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;
        void mouseDown(const juce::MouseEvent& e) override;

        // Set the base slot offset within the 48-slot frame.
        // Slot i in the zone maps to absolute slot (slotOffset_ + i).
        void setSlotOffset(int offset);
        [[nodiscard]] int slotOffset() const { return slotOffset_; }

    private:
        static constexpr int kNumSlots = 4;

        void refreshSliders();
        void showMappingMenu(int slotIndex);

        LockstepProcessor& processor_;
        StepGrid& grid_;
        int slotOffset_ = 0;

        // Index of the slot column currently in "listening for CC" state, or -1.
        int learningSlotIndex_ = -1;

        std::array<juce::Slider,     kNumSlots> sliders_;
        std::array<juce::Label,      kNumSlots> labels_;
        std::array<juce::TextButton, kNumSlots> clearBtns_;
        bool updatingFromTimer_ = false;
    };
}
