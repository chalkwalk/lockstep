// LoopFitTest — headless tests for stretchmath::fitTargetLength (S7).
// The FreeLen pitch-preserved fit rounds a recorded loop length UP to the next
// launch-quant multiple (extend-only), with a jitter tolerance that snaps a take
// sitting a hair above a multiple back onto it instead of stretching a whole
// period. Verifies the boundaries and the no-grid fallbacks.

#include "TestHarness.h"
#include "../src/machine/StretchMath.h"

namespace lockstep
{
    using stretchmath::fitTargetLength;

    static void testFitRoundUp()
    {
        const double period = 48000.0;  // one "bar" = 48000 samples

        // Genuinely between multiples → extend up to the next one (ratio > 1).
        CHECK(fitTargetLength(50000, period) == 96000, "50k rounds up to 96k");
        CHECK(fitTargetLength(1, period) == 48000, "sub-period take extends to one period");
        CHECK(fitTargetLength(47000, period) == 48000, "47k rounds up to 48k");

        // Exact multiple → itself (ratio 1, no stretch).
        CHECK(fitTargetLength(48000, period) == 48000, "exact multiple unchanged");
        CHECK(fitTargetLength(96000, period) == 96000, "exact 2x multiple unchanged");

        // Result is always a multiple of the period.
        CHECK(fitTargetLength(100000, period) == 144000, "100k → 3 periods");
    }

    static void testFitJitterTolerance()
    {
        const double period = 48000.0;
        const double tol = 64.0;  // ~1.3 ms at 48k

        // A hair ABOVE a multiple (capture jitter) snaps DOWN to the multiple,
        // not up a whole period.
        CHECK(fitTargetLength(48000 + 30, period, tol) == 48000, "jitter above snaps down");
        CHECK(fitTargetLength(96000 + 64, period, tol) == 96000, "jitter (== tol) snaps down");

        // Just past the tolerance → genuine extend up.
        CHECK(fitTargetLength(48000 + 65, period, tol) == 96000, "past tol extends up");

        // Below the first multiple never snaps to 0.
        CHECK(fitTargetLength(20, period, tol) == 48000, "sub-period never snaps to 0");

        // A hair BELOW the next multiple extends up to it (ratio ~1), always
        // extend-only regardless of tolerance.
        CHECK(fitTargetLength(96000 - 20, period, tol) == 96000, "just below extends up");
    }

    static void testFitFallbacks()
    {
        // No launch-quant grid but a bar period given → use the bar.
        CHECK(fitTargetLength(50000, 0.0, 0.0, 48000.0) == 96000, "bar-period fallback");
        // No grid at all → native length unchanged (ratio 1, no fit).
        CHECK(fitTargetLength(50000, 0.0) == 50000, "no grid → native");
        CHECK(fitTargetLength(50000, -1.0) == 50000, "negative period → native");
        // Degenerate lengths.
        CHECK(fitTargetLength(0, 48000.0) == 0, "zero length → 0");
        CHECK(fitTargetLength(-100, 48000.0) == 0, "negative length → 0");
    }

    void runLoopFitTests()
    {
        testFitRoundUp();
        testFitJitterTolerance();
        testFitFallbacks();
    }
}
