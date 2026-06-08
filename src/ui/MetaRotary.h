#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SurfaceModel.h"  // RingMode, ReferenceMark

namespace lockstep
{
    // MetaRotary — a Slider carrying render metadata for the custom L&F.
    // Owned by ManipulationZone; MetaRotaryLookAndFeel casts Slider& to MetaRotary&
    // to read ringMode and marks during drawRotarySlider.
    struct MetaRotary : juce::Slider
    {
        MetaRotary()
            : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag,
                           juce::Slider::NoTextBox) {}

        RingMode ringMode = RingMode::UnipolarFill;
        std::array<ReferenceMark, 2> marks{};
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
