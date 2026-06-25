#pragma once

#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MetaBand.h"
#include "MetaRotary.h"
#include "../state/UiState.h"

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

        // Set the section title for normal (band==None) mode — shown in the MZ header strip.
        // Call from PluginEditor whenever the active section or page changes.
        void setNormalTitle(const juce::String& title, int page, int pageCount);

        // Switch the zone to a MetaBand (resolveMetaBand result) with the given swing scope.
        // MetaBand::None = normal machine params; anything else renders the meta surface.
        void setBand(MetaBand band, int swingScope);

        // Provide the current UiState for meta-band operations that need it (MasterFx slot, Euclidean params).
        void setUiState(UiState* ui) { uiState_ = ui; }

        // 0 = none (show fader-blended value), 1 = preview A pole, 2 = preview B pole.
        // Set when the Morph+^/v qualifier is active so knobs show the raw endpoint.
        void setMorphQualifier(int q) { morphQualifier_ = q; }

        // True while the Morph modifier is held (or latched).
        // When set, mouse-drag writes the morph overlay at fader split (matches encoder).
        void setMorphHeld(bool b) { morphHeld_ = b; }

        // Rebuild the slider/label view from current param state. Frame-driven
        // (9.15): the editor calls this from its surface-frame so the MZ tracks
        // external param changes (controller turns, audio-thread writes) through
        // the invalidation channel instead of a perpetual 30 Hz poll. Safe to call
        // mid-drag — it runs outside onValueChange and the rotaries ignore
        // setValue() while dragging.
        void refreshSliders();


        // Called when the user clicks "Manage pool..." from the sample picker menu.
        std::function<void()> onOpenPoolManager;

        // 5.5: Called after a Euclidean meta-band encoder write so the editor can
        // re-apply the live euclid pattern to the armed track.
        std::function<void()> onEuclidParamChanged;

        // 9.14: Called after a Step-Position encoder write so the editor can run its
        // canonical full surface refresh (grid + step preview), matching the realtime
        // feedback the nav ←/→ keys give.
        std::function<void()> onStepPositionChanged;


    public:
        // MHX §26.2: 8 encoders in a 4x2 staggered band.  Single constant so the
        // hardware-grow path (4 → 8) was a one-line change.
        static constexpr int kMZSlots = 8;
        // §26.4.1: height of the persistent header strip at the top of the MZ.
        static constexpr int kHeaderH = 14;

    private:
        static constexpr int kNumSlots = kMZSlots;

        void showMappingMenu(int slotIndex);
        void showSamplePicker(int absoluteSlot);

        // Staggered-grid geometry helpers — single source of truth used by
        // resized(), paintOverChildren() (CC badges, learn overlay, morph chips).
        [[nodiscard]] juce::Rectangle<int> slotCellBounds(int i) const;
        [[nodiscard]] juce::Rectangle<int> slotKnobBounds(int i) const;

        LockstepProcessor& processor_;
        KeyboardArea& area_;
        int slotOffset_ = 0;
        MetaBand band_ = MetaBand::None;
        int swingScope_ = 0;   // 0=none, 1=song-all, 2=scene-all delta, 3=song-track delta
        juce::String normalTitle_;   // section name for normal (band==None) mode
        int normalPage_ = 0;         // 0-based current page index
        int normalPageCount_ = 0;    // total pages for this section (0 = unpaginated)
        UiState* uiState_ = nullptr;
        int morphQualifier_ = 0;   // 0=blend, 1=A-pole preview, 2=B-pole preview
        bool morphHeld_ = false;

        // Index of the slot column currently in "listening for CC" state, or -1.
        int learningSlotIndex_ = -1;

        MetaRotaryLookAndFeel laf_;
        std::array<MetaRotary, kNumSlots> sliders_;
        std::array<juce::Label, kNumSlots> labels_;
        std::array<juce::Label, kNumSlots> valueLabels_;
        std::array<juce::TextButton, kNumSlots> clearBtns_;
        juce::TextButton samplePickerBtn_;  // replaces sliders_[i] when a sample slot is in view
        bool updatingFromTimer_ = false;

        // Incremental-delta tracking for on-screen density master edits.
        // JUCE RotaryHorizontalVerticalDrag accumulates from the drag origin and
        // ignores setValue() mid-drag, so master writes must diff successive events.
        std::array<float, kNumSlots> lastSlotValue_{};
        bool lastSlotValid_ = false;
    };
}
