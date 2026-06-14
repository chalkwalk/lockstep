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
        };

        void applyView(const View& v);

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
