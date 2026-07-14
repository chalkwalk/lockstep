// TrigConditionTest -- the iteration rule (m:n), DESIGN §4.4.
//
// m:n means "fire on m of every n cycles, maximally evenly distributed" -- a
// count, not a phase offset. The numerator was historically read as a phase
// (iter % n == m - 1), so 2:3 fired once per three cycles and n:n never
// all-fired. These are property tests over the whole (m, n) space rather than
// a handful of examples, because that is what protects the class of bug: any
// future rewrite of the distribution must still satisfy "exactly m fires in
// ANY window of n consecutive cycles" and "gaps differ by at most one".
//
// The same helper backs the audio path (TrigEvaluator::shouldFire) and the
// grid preview (SurfaceModel::stepPagePreview) -- the divergence that let the
// display agree with a buggy engine is now structurally impossible.

#include "TestHarness.h"
#include "../src/core/TrigEvaluator.h"

#include <vector>

namespace lockstep
{
    using TrigEvaluator::iterCyclePasses;

    // Fire pattern over `cycles` consecutive iterations starting at `from`.
    static std::vector<bool> fires(int m, int n, std::int64_t from, int cycles)
    {
        std::vector<bool> out;
        out.reserve(static_cast<std::size_t>(cycles));
        for (int i = 0; i < cycles; ++i)
            out.push_back(iterCyclePasses(from + i, m, n));
        return out;
    }

    // ------------------------------------------------------------------------
    // Property 1 -- density. Exactly m fires in ANY n consecutive cycles.
    // Sliding-window (not just window-aligned): a rule that fires m times per
    // aligned block but clumps them at a boundary would pass an aligned count
    // and fail here, which is precisely the musical complaint.
    static void testDensityOverEveryWindow()
    {
        for (int n = 1; n <= 8; ++n)
        {
            for (int m = 1; m <= n; ++m)
            {
                // Start off-zero and off-phase so no assertion leans on iter 0.
                const auto pat = fires(m, n, 3, 8 * n + n);
                for (std::size_t start = 0; start + static_cast<std::size_t>(n) <= pat.size(); ++start)
                {
                    int count = 0;
                    for (int k = 0; k < n; ++k)
                        if (pat[start + static_cast<std::size_t>(k)]) ++count;
                    CHECK(count == m,
                          juce::String("iter ") + juce::String(m) + ":" + juce::String(n)
                              + " -- window of " + juce::String(n) + " cycles held "
                              + juce::String(count) + " fires, expected " + juce::String(m));
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Property 2 -- evenness. Gaps between consecutive fires differ by at most
    // one. This is what "euclidean" buys musically: 3:8 must not be x..x..x.
    // bunched into xxx..... .
    static void testGapsAreMaximallyEven()
    {
        for (int n = 2; n <= 8; ++n)
        {
            for (int m = 1; m <= n; ++m)
            {
                const auto pat = fires(m, n, 0, 4 * n);
                std::vector<int> gaps;
                int last = -1;
                for (int i = 0; i < static_cast<int>(pat.size()); ++i)
                {
                    if (!pat[static_cast<std::size_t>(i)]) continue;
                    if (last >= 0) gaps.push_back(i - last);
                    last = i;
                }
                CHECK(!gaps.empty(), "iter rule: no fires at all");
                int lo = gaps.front(), hi = gaps.front();
                for (int g : gaps) { lo = std::min(lo, g); hi = std::max(hi, g); }
                CHECK(hi - lo <= 1,
                      juce::String("iter ") + juce::String(m) + ":" + juce::String(n)
                          + " -- gaps range " + juce::String(lo) + ".." + juce::String(hi)
                          + " (not maximally even)");
            }
        }
    }

    // ------------------------------------------------------------------------
    // Property 3 -- the anchors. 1:n keeps the historic "first of every n"
    // (no migration needed for the overwhelmingly common condition); n:n fires
    // on every cycle (the case the old formula got flatly wrong); denominator
    // <= 1 is unconditional.
    static void testAnchorCases()
    {
        for (int n = 1; n <= 8; ++n)
        {
            for (std::int64_t iter = 0; iter < 4 * n; ++iter)
            {
                CHECK(iterCyclePasses(iter, 1, n) == (iter % n == 0),
                      "iter 1:n must fire on the first cycle of every n");
                CHECK(iterCyclePasses(iter, n, n),
                      "iter n:n must fire on every cycle");
            }
        }

        CHECK(iterCyclePasses(7, 1, 1), "iter 1:1 is unconditional");
        CHECK(iterCyclePasses(7, 1, 0), "iter with denominator 0 is unconditional");
    }

    // ------------------------------------------------------------------------
    // Property 4 -- robustness at the edges. A numerator larger than the
    // denominator (reachable by turning the COND encoders in either order)
    // clamps to all-fire rather than falling silent; negative iterations (a
    // backward host relocate) stay on the same phase as their positive
    // counterparts rather than mirroring the pattern.
    static void testEdgeCases()
    {
        for (std::int64_t iter = 0; iter < 12; ++iter)
            CHECK(iterCyclePasses(iter, 9, 4), "iter m>n must clamp to all-fire, not silence");

        for (int n = 2; n <= 8; ++n)
        {
            for (int m = 1; m <= n; ++m)
            {
                for (std::int64_t k = 1; k <= 3; ++k)
                {
                    const auto neg = -k * n + 1;   // same phase as (+1) mod n
                    CHECK(iterCyclePasses(neg, m, n) == iterCyclePasses(1, m, n),
                          "iter rule: negative iteration must keep the same phase");
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Property 5 -- the rule reaches shouldFire. The engine gate is the thing
    // users hear; assert the helper is actually wired into it (and that a
    // trig-off step still never fires, whatever the condition says).
    static void testShouldFireHonoursTheRule()
    {
        constexpr int kLen = 16;
        Step on;
        on.trig = true;

        TrigCondition c;
        c.iterNumerator = 2;
        c.iterDenominator = 3;

        int firedCycles = 0;
        for (std::int64_t cycle = 0; cycle < 3; ++cycle)
        {
            const auto absStep = cycle * kLen;  // step 0 of each cycle
            if (TrigEvaluator::shouldFire(on, c, 0, absStep, kLen, false))
                ++firedCycles;
        }
        CHECK(firedCycles == 2, "shouldFire: 2:3 must fire on two of every three cycles");

        Step off;
        off.trig = false;
        CHECK(!TrigEvaluator::shouldFire(off, c, 0, 0, kLen, false),
              "shouldFire: a trig-off step must never fire, whatever the condition");
    }

    void runTrigConditionTests()
    {
        testDensityOverEveryWindow();
        testGapsAreMaximallyEven();
        testAnchorCases();
        testEdgeCases();
        testShouldFireHonoursTheRule();
    }
}
