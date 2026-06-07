#pragma once

#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class LockstepProcessor;
    class KeyboardArea;

    // Shows kMZSlots (8) parameter slots in a 4×2 grid for the active track.
    // Reads from and writes to the correct layer — Step Override when a step
    // is held, Track Base otherwise — via LockstepProcessor::writeParam.
    class ManipulationZone : public juce::Component,
                             public juce::Timer
    {
    public:
        ManipulationZone(LockstepProcessor& processor, KeyboardArea& area);
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

        // Switch the zone into a meta-section display mode (-1 = normal machine params).
        // Meta content (COND/TRACK/GLOBAL widgets) fills in at M6.5/M6.6.
        void setMetaSection(int metaSection);

        // 0 = none (show fader-blended value), 1 = preview A pole, 2 = preview B pole.
        // Set when the Morph+^/v qualifier is active so knobs show the raw endpoint.
        void setMorphQualifier(int q) { morphQualifier_ = q; }

        // Called when the user clicks "Manage pool..." from the sample picker menu.
        std::function<void()> onOpenPoolManager;

    public:
        // MHX §26.2: 8 encoders in a 4x2 staggered band.  Single constant so the
        // hardware-grow path (4 → 8) was a one-line change.
        static constexpr int kMZSlots = 8;

    private:
        static constexpr int kNumSlots = kMZSlots;

        void refreshSliders();
        void refreshCondSliders();
        void refreshTrigSliders();
        void refreshTrackSliders();
        void refreshGlobalSliders();
        void writeCondField(int field, float value);
        void writeTrigField(int field, float value);
        void writeTrackField(int field, float value);
        void writeGlobalField(int field, float value);
        void showMappingMenu(int slotIndex);
        void showSamplePicker(int absoluteSlot);

        LockstepProcessor& processor_;
        KeyboardArea& area_;
        int slotOffset_      = 0;
        int metaSection_     = -1;  // -1 = normal machine params; 0/1/2/5 = COND/TRIG/TRACK/GLOBAL
        int morphQualifier_  = 0;   // 0=blend, 1=A-pole preview, 2=B-pole preview

        // Index of the slot column currently in "listening for CC" state, or -1.
        int learningSlotIndex_ = -1;

        std::array<juce::Slider,     kNumSlots> sliders_;
        std::array<juce::Label,      kNumSlots> labels_;
        std::array<juce::Label,      kNumSlots> valueLabels_;
        std::array<juce::TextButton, kNumSlots> clearBtns_;
        juce::TextButton samplePickerBtn_;  // replaces sliders_[i] when a sample slot is in view
        bool updatingFromTimer_ = false;
    };
}
