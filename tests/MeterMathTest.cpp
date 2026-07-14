// MeterMathTest -- meter geometry + the master-VU drag mapping (9.31).
//
// The gestures these back were previously written inline in paint()/mouseDrag(),
// which is why the track-VU drag could ship invisible and untested for months.
// The mapping is now pure, so the properties that make a fader usable -- it
// stays inside its cell, it clamps at both ends, a drag and its inverse cancel,
// and the tick agrees with the drag -- are assertions rather than hopes.

#include "TestHarness.h"
#include "../src/ui/MeterMath.h"

namespace lockstep
{
    using namespace lockstep::meter;

    static void testLevelTick()
    {
        constexpr int kW = 60;

        CHECK(levelTickX(0.0f, 1.0f, kW) == 0, "tick: zero level sits at the left edge");
        CHECK(levelTickX(0.5f, 1.0f, kW) == 30, "tick: half level sits at the middle");

        // Full level must stay INSIDE the cell -- a 2 px tick drawn at x == width
        // is drawn in the next track's cell.
        CHECK(levelTickX(1.0f, 1.0f, kW) == kW - 1, "tick: full level stays inside the cell");

        // Out-of-range and degenerate inputs clamp rather than escape.
        CHECK(levelTickX(9.0f, 1.0f, kW) == kW - 1, "tick: over-range level clamps inside");
        CHECK(levelTickX(-1.0f, 1.0f, kW) == 0, "tick: negative level clamps to the left edge");
        CHECK(levelTickX(0.5f, 0.0f, kW) >= 0, "tick: a zero-span spec must not divide by zero");
        CHECK(levelTickX(0.5f, 1.0f, 0) == 0, "tick: an unlaid-out (0 px) cell is safe");

        // A non-unit range (a spec whose max is not 1.0) maps proportionally.
        CHECK(levelTickX(1.0f, 2.0f, kW) == 30, "tick: level is read against the spec's max");
    }

    static void testMasterDrag()
    {
        constexpr float kMin = -60.0f, kMax = 6.0f;

        // No movement, no change.
        CHECK(feq(masterDragDb(0.0f, 0, kMin, kMax), 0.0f), "drag: no travel, no change");

        // Up is louder, down is quieter, and the scale is the full range over
        // kDragScalePx pixels.
        const float up = masterDragDb(-30.0f, static_cast<int>(kDragScalePx) / 2, kMin, kMax);
        CHECK(up > -30.0f, "drag: dragging up raises the gain");
        CHECK(feq(up, -30.0f + (kMax - kMin) * 0.5f, 0.5f),
              "drag: half the drag scale moves half the range");

        const float down = masterDragDb(-30.0f, -static_cast<int>(kDragScalePx) / 2, kMin, kMax);
        CHECK(down < -30.0f, "drag: dragging down lowers the gain");

        // Both ends clamp: a long drag cannot walk the gain out of its range.
        CHECK(feq(masterDragDb(0.0f, 5000, kMin, kMax), kMax), "drag: clamps at the ceiling");
        CHECK(feq(masterDragDb(0.0f, -5000, kMin, kMax), kMin), "drag: clamps at the floor");

        // Away-and-back returns to where it started (the drag is a pure function of
        // the START value and total travel, so it cannot accumulate error).
        const float there = masterDragDb(-12.0f, 40, kMin, kMax);
        const float back = masterDragDb(there, -40, kMin, kMax);
        CHECK(feq(back, -12.0f, 0.01f), "drag: away and back is a no-op");
    }

    static void testGainTick()
    {
        constexpr int kY = 10, kH = 100;
        constexpr float kFloor = 48.0f;  // meter spans -48..0 dBFS

        // 0 dB is the top of the meter's scale; the floor is the bottom.
        CHECK(gainTickY(0.0f, kY, kH, kFloor) == kY, "gain tick: 0 dB sits at the meter top");
        CHECK(gainTickY(-48.0f, kY, kH, kFloor) == kY + kH - 1,
              "gain tick: the floor sits at the meter bottom");
        CHECK(gainTickY(-24.0f, kY, kH, kFloor) == kY + kH / 2,
              "gain tick: half the scale sits at the middle");

        // The gain range runs past the meter's scale (+6 dB), and below it (-60 dB):
        // both pin to an edge rather than being drawn outside the meter.
        CHECK(gainTickY(6.0f, kY, kH, kFloor) == kY, "gain tick: boost pins to the top");
        CHECK(gainTickY(-60.0f, kY, kH, kFloor) == kY + kH - 1,
              "gain tick: below-floor pins to the bottom");

        CHECK(gainTickY(0.0f, kY, 0, kFloor) == kY, "gain tick: an unlaid-out meter is safe");
    }

    void runMeterMathTests()
    {
        testLevelTick();
        testMasterDrag();
        testGainTick();
    }
}
