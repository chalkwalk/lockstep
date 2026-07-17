#pragma once

// DesignCanvas — the one place the Item E scale is expressed.
//
// The editor is authored on a FIXED logical canvas (kDesignW x kDesignH) and
// scaled to whatever size the window is: resized() lays every child and every
// editor-painted region out in LOGICAL coordinates, applyChildScale() stamps an
// AffineTransform on each child, and paint()/paintOverChildren() addTransform the
// same scale before drawing. Inside that world a rectangle means design pixels.
//
// The moment a coordinate crosses OUT of that world it means physical pixels, and
// the two are only the same at scale 1.0 — which is not the default (1.2 is). Two
// APIs cross the boundary:
//
//   * juce::Component::repaint(Rectangle) — takes PHYSICAL coords. A logical rect
//     handed straight to it invalidates pixels the paint never touches, so the
//     region silently never redraws. That was the VU-meter freeze: correct decay
//     maths, correct paint, invalidating empty space off to the upper left.
//   * mouse events — arrive in PHYSICAL coords and must be mapped back before
//     being compared against any cached region.
//
// Both directions live here so the conversion is a named thing that can be tested,
// rather than a `* uiScale_` open-coded at each site — which is how the forward
// direction came to be missing at all seven repaint call sites at once.

#include <juce_graphics/juce_graphics.h>

namespace lockstep::design
{
    // Logical (design-canvas) rect -> physical (component) rect.
    //
    // Rounded OUTWARD, then expanded by one pixel: at a fractional scale (1.2 is
    // the default; the test rig runs ~1.4141) a logical edge lands mid-pixel and
    // the renderer antialiases across the boundary. An exactly-rounded invalidation
    // would leave that fringe pixel stale — a one-pixel crust of the previous frame,
    // which on a meter that moves every tick reads as a shimmering edge.
    [[nodiscard]] inline juce::Rectangle<int> toPhysical(juce::Rectangle<int> logical,
                                                         double scale) noexcept
    {
        return logical.toFloat()
            .transformedBy(juce::AffineTransform::scale(static_cast<float>(scale)))
            .getSmallestIntegerContainer()
            .expanded(1);
    }

    // Physical (component) point -> logical (design-canvas) point.
    [[nodiscard]] inline juce::Point<int> toLogical(juce::Point<float> physical,
                                                    double scale) noexcept
    {
        return (physical / static_cast<float>(scale)).roundToInt();
    }
}   // namespace lockstep::design
