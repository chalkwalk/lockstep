#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SurfaceModel.h"  // RingMode, ReferenceMark

namespace lockstep
{
    // MetaRotary — a Slider carrying render metadata for the custom L&F.
    // Owned by ManipulationZone; MetaRotaryLookAndFeel casts Slider& to MetaRotary&
    // to read ringMode and marks during drawRotarySlider.
    //
    // All mutable state (built-in slider properties AND custom fields) is written
    // exclusively via applyView(). This makes it impossible for one refreshSliders
    // branch to leave stale state from another branch — a complete View is always
    // applied in one shot. See ManipulationZone::refreshSliders.
    struct MetaRotary : juce::Slider
    {
        MetaRotary()
            : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag,
                           juce::Slider::NoTextBox) {}

        // Total view-model for one rotary cell. Build one of these per slot and
        // call applyView() — the sole writer for all slider + custom properties.
        struct View
        {
            double rangeLo = 0.0, rangeHi = 1.0, interval = 0.0;
            double skew = 1.0;
            bool doubleClickEnabled = false;
            double doubleClickValue = 0.0;
            double value = 0.0;
            bool enabled = true;
            float alpha = 1.0f;
            RingMode ringMode = RingMode::UnipolarFill;
            std::array<ReferenceMark, 2> marks{};
            bool densityCell = false;
            float densityMasterOffset = 0.0f;
            float densityEffective = 1.0f;
            bool harmonyVoiceCell = false;
            bool harmonyKnobTop = false;
            bool harmonyVoiceOff = false;
            bool harmonyChromatic = false;
            juce::String reelPrev, reelNow, reelNext;
            bool reelPrevWrapped = false;
            bool reelNextWrapped = false;

            // Tape reel scrub widget (§40.2, Stage 5): a drawn reel that spins with
            // the reel head so jog/wind is a visible affordance. tapeReelAngle is the
            // hub rotation in radians; tapeReelWinding tints it while a steady wind is
            // engaged (FF/RW cells) vs a settling jog.
            bool tapeReel = false;
            float tapeReelAngle = 0.0f;
            bool tapeReelWinding = false;
        };

        void applyView(const View& v);

        // Const getters for test access (L&F reads private members directly).
        [[nodiscard]] RingMode getRingMode() const noexcept { return ringMode; }
        [[nodiscard]] const std::array<ReferenceMark, 2>& getMarks() const noexcept { return marks; }
        [[nodiscard]] bool isDensityCell() const noexcept { return densityCell; }
        [[nodiscard]] float getDensityMasterOffset() const noexcept { return densityMasterOffset; }
        [[nodiscard]] float getDensityEffective() const noexcept { return densityEffective; }
        [[nodiscard]] bool isHarmonyVoiceCell() const noexcept { return harmonyVoiceCell; }
        [[nodiscard]] bool isTapeReel() const noexcept { return tapeReel; }
        [[nodiscard]] float getTapeReelAngle() const noexcept { return tapeReelAngle; }
        [[nodiscard]] bool isTapeReelWinding() const noexcept { return tapeReelWinding; }

    private:
        friend class MetaRotaryLookAndFeel;

        RingMode ringMode = RingMode::UnipolarFill;
        std::array<ReferenceMark, 2> marks{};

        // Density cell (DESIGN §39): when true, drawRotarySlider renders the
        // arc/overshoot/tick visual instead of the standard ring.
        // masterOffset: additive master-density value in [-1, 1].
        // effective:    clamp(perTrack + master, 0.01, 1.0) normalised to [0, 1].
        bool densityCell = false;
        float densityMasterOffset = 0.0f;
        float densityEffective = 1.0f;

        // Harmony voice reel (10.10): a note-name reel + half-knob instead of a
        // ring. See MetaFieldView's harmony fields for the semantics.
        bool harmonyVoiceCell = false;
        bool harmonyKnobTop = false;
        bool harmonyVoiceOff = false;
        bool harmonyChromatic = false;
        juce::String reelPrev, reelNow, reelNext;
        bool reelPrevWrapped = false;
        bool reelNextWrapped = false;

        // Tape reel scrub widget (§40.2, Stage 5).
        bool tapeReel = false;
        float tapeReelAngle = 0.0f;
        bool tapeReelWinding = false;
    };

    // MetaRotaryLookAndFeel — custom rotary renderer.
    // Handles all three RingMode variants and draws scope-coloured reference ticks.
    class MetaRotaryLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;
    };
}
