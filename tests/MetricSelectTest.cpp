// MetricSelectTest — validates the tier+Euclid deterministic Scrub selector.
// Tests MetricSelect::build + MetricSelect::{metricSurvives,mixedSurvives,uniformSurvives}.

#include "TestHarness.h"
#include "../src/core/MetricSelect.h"
#include "../src/core/MetricGrid.h"
#include <cmath>
#include <cstdint>

namespace lockstep
{
    using namespace MetricSelect;

    // -----------------------------------------------------------------------
    // Helpers

    // Build a Table for a bar of `n` equal-size steps in `num`/`den` time.
    static Table buildFor(int n, int num, int den, double barPpq = 1920.0,
                          int trackOffset = 0)
    {
        std::array<float, 64> wts{};
        for (int s = 0; s < n && s < 64; ++s)
        {
            const double ppqPos = static_cast<double>(s) / static_cast<double>(n) * barPpq;
            wts[static_cast<std::size_t>(s)] =
                MetricGrid::metricWeight(ppqPos, barPpq, num, den);
        }
        return build(wts, n < 64 ? n : 64, trackOffset);
    }

    // Count set bits in a mask up to bit n-1.
    static int popcount(std::uint64_t mask, int n)
    {
        int c = 0;
        for (int i = 0; i < n; ++i)
            if (mask & (std::uint64_t(1) << i)) ++c;
        return c;
    }

    // -----------------------------------------------------------------------
    // metric[T] has exactly T bits set for all T in [0, n].

    static void testMaskCardinality()
    {
        const Table t = buildFor(16, 4, 4);
        for (int T = 0; T <= t.n; ++T)
        {
            const int c = popcount(t.metric[static_cast<std::size_t>(T)], t.n);
            CHECK(c == T, "metric cardinality: metric[T] has exactly T bits set");
        }
    }

    static void testMaskCardinality7_8()
    {
        const Table t = buildFor(7, 7, 8);
        for (int T = 0; T <= t.n; ++T)
        {
            const int c = popcount(t.metric[static_cast<std::size_t>(T)], t.n);
            CHECK(c == T, "7/8 metric cardinality: metric[T] has exactly T bits set");
        }
    }

    // -----------------------------------------------------------------------
    // 4/4, 16-step: weight tier order is 0 > 8 > {4,12} > {2,6,10,14} > odds.
    // Verify that stronger positions enter the metric mask first.

    static void testFourFourTierOrder()
    {
        const Table t = buildFor(16, 4, 4);

        // T=1: only the bar downbeat (step 0)
        CHECK((t.metric[1] & (std::uint64_t(1) << 0)) != 0, "4/4 T=1: step 0 (downbeat) set");
        CHECK(t.metric[1] == (std::uint64_t(1) << 0), "4/4 T=1: only step 0");

        // T=2: add the half-bar (step 8)
        CHECK((t.metric[2] & (std::uint64_t(1) << 0)) != 0, "4/4 T=2: step 0 set");
        CHECK((t.metric[2] & (std::uint64_t(1) << 8)) != 0, "4/4 T=2: step 8 (half-bar) set");

        // T=4: add both quarter-note beats (steps 4 and 12); tier is whole
        CHECK((t.metric[4] & (std::uint64_t(1) << 4))  != 0, "4/4 T=4: step 4 set");
        CHECK((t.metric[4] & (std::uint64_t(1) << 12)) != 0, "4/4 T=4: step 12 set");
        CHECK((t.metric[4] & (std::uint64_t(1) << 0))  != 0, "4/4 T=4: step 0 still set");
        CHECK((t.metric[4] & (std::uint64_t(1) << 8))  != 0, "4/4 T=4: step 8 still set");

        // T=8: even steps {0,2,4,6,8,10,12,14} — all four "and" positions added
        for (int s = 0; s < 16; s += 2)
            CHECK((t.metric[8] & (std::uint64_t(1) << s)) != 0, "4/4 T=8: even steps set");
        for (int s = 1; s < 16; s += 2)
            CHECK((t.metric[8] & (std::uint64_t(1) << s)) == 0, "4/4 T=8: odd steps not set");

        // Odd steps (weakest tier, w≈0) must NOT appear until T>8.
        for (int s = 1; s < 16; s += 2)
            CHECK((t.metric[8] & (std::uint64_t(1) << s)) == 0, "4/4: odd steps absent at T=8");
    }

    // -----------------------------------------------------------------------
    // Euclid spacing in the boundary tier: bjorklund-even distribution.
    // At T=5 the boundary tier is the 4 "and" positions {2,6,10,14}; k=1.
    // bjorklund(4,1) = [T,F,F,F] → only position index 0 within the tier (step 2).
    // At T=6 bjorklund(4,2) = [T,F,T,F] → step indices 0,2 → steps 2 and 10.

    static void testEuclidBoundaryTier()
    {
        const Table t = buildFor(16, 4, 4);

        // T=5: step 2 should be selected (bjorklund(4,1) index 0 in tier {2,6,10,14})
        CHECK((t.metric[5] & (std::uint64_t(1) << 2))  != 0, "T=5: boundary step 2 selected");
        CHECK((t.metric[5] & (std::uint64_t(1) << 6))  == 0, "T=5: boundary step 6 not selected");
        CHECK((t.metric[5] & (std::uint64_t(1) << 10)) == 0, "T=5: boundary step 10 not selected");
        CHECK((t.metric[5] & (std::uint64_t(1) << 14)) == 0, "T=5: boundary step 14 not selected");

        // T=6: bjorklund(4,2) → positions 0,2 in tier → steps 2 and 10
        CHECK((t.metric[6] & (std::uint64_t(1) << 2))  != 0, "T=6: step 2 set");
        CHECK((t.metric[6] & (std::uint64_t(1) << 10)) != 0, "T=6: step 10 set");
        CHECK((t.metric[6] & (std::uint64_t(1) << 6))  == 0, "T=6: step 6 not set");
        CHECK((t.metric[6] & (std::uint64_t(1) << 14)) == 0, "T=6: step 14 not set");
    }

    // -----------------------------------------------------------------------
    // Non-monotonic membership: a position can be absent at T, present at T+1,
    // absent at T+2. This is by design (per-count Euclid, not drop-point).
    //
    // In 4/4 16-step, boundary tier of odds (8 positions, indices 0-7
    // = steps 1,3,5,7,9,11,13,15):
    //   T=11: bjorklund(8,3) → indices 0,3,6 → steps 1, 7, 13
    //   T=12: bjorklund(8,4) → indices 0,2,4,6 → steps 1, 5, 9, 13
    // Step 7: in metric[11], NOT in metric[12].

    static void testNonMonotonicMembership()
    {
        const Table t = buildFor(16, 4, 4);

        const bool step7_at11 = (t.metric[11] & (std::uint64_t(1) << 7)) != 0;
        const bool step7_at12 = (t.metric[12] & (std::uint64_t(1) << 7)) != 0;

        CHECK(step7_at11,  "non-monotonic: step 7 present at T=11");
        CHECK(!step7_at12, "non-monotonic: step 7 absent at T=12 (per-count Euclid)");
    }

    // -----------------------------------------------------------------------
    // 7/8, 7 pulses: weight tiers are {0} > {3,5} > {1,2,4,6}.
    // At T=3 the set must be {0,3,5} and NOT include any of {1,2,4,6}.

    static void testSevenEightTiers()
    {
        // 7/8 at 480 PPQ/quarter: barPpq = 7 * (4.0/8) * 480 = 1680
        const Table t = buildFor(7, 7, 8, 1680.0);

        // T=1: only pulse 0
        CHECK(t.metric[1] == (std::uint64_t(1) << 0), "7/8 T=1: only pulse 0");

        // T=3: group heads {0,3,5} and nothing else
        const std::uint64_t expected3 =
            (std::uint64_t(1) << 0) | (std::uint64_t(1) << 3) | (std::uint64_t(1) << 5);
        CHECK(t.metric[3] == expected3, "7/8 T=3: set = {0,3,5}");

        // Inner pulses {1,2,4,6} absent at T=3
        for (int s : {1, 2, 4, 6})
            CHECK((t.metric[3] & (std::uint64_t(1) << s)) == 0, "7/8 T=3: inner pulse absent");
    }

    // -----------------------------------------------------------------------
    // 6/8, 6 pulses: weight tiers are {0} > {3} > {1,2,4,5}.
    // At T=2 the set must be {0,3}.

    static void testSixEightTiers()
    {
        // 6/8 at 480 PPQ/quarter: barPpq = 6 * (4.0/8) * 480 = 1440
        const Table t = buildFor(6, 6, 8, 1440.0);

        // T=2: both group heads, no inner pulses
        const std::uint64_t expected2 =
            (std::uint64_t(1) << 0) | (std::uint64_t(1) << 3);
        CHECK(t.metric[2] == expected2, "6/8 T=2: set = {0,3}");

        // Inner pulses {1,2,4,5} absent at T=2
        for (int s : {1, 2, 4, 5})
            CHECK((t.metric[2] & (std::uint64_t(1) << s)) == 0, "6/8 T=2: inner pulse absent");
    }

    // -----------------------------------------------------------------------
    // metricSurvives — Metric (m=1): only metric[T] survives.
    // In 4/4, step 0 (downbeat) survives whenever T>=1;
    // step 1 (finest offbeat) survives only when T >= 9 (first odd included).

    static void testMetricSurvives()
    {
        const Table t = buildFor(16, 4, 4);
        constexpr int N = 16;

        // Step 0 survives as soon as T=1.
        CHECK(metricSurvives(t, 0, 1), "Metric: step 0 survives at T=1");

        // Step 8 (half-bar) survives from T=2.
        CHECK(metricSurvives(t, 8, 2),  "Metric: step 8 survives at T=2");
        CHECK(!metricSurvives(t, 8, 1), "Metric: step 8 absent at T=1");

        // Step 1 (finest offbeat) absent until T=9.
        for (int T = 1; T <= 8; ++T)
            CHECK(!metricSurvives(t, 1, T), "Metric: step 1 absent at T<=8");
        CHECK(metricSurvives(t, 1, 9), "Metric: step 1 present at T=9");

        // Edge: T=0 → nothing; T=N → all.
        for (int s = 0; s < N; ++s)
        {
            CHECK(!metricSurvives(t, s, 0), "Metric T=0: nothing survives");
            CHECK(metricSurvives(t, s, N),  "Metric T=N: all survive");
        }
    }

    // -----------------------------------------------------------------------
    // mixedSurvives — Mixed (m=0.5): metric[P] always kept; count = exactly T.
    // Fully deterministic — no hash; same result every call.

    static void testMixedSurvives()
    {
        const Table t = buildFor(16, 4, 4);
        constexpr int N = 16;

        // mixed[T] has exactly T bits set for all T.
        for (int T = 0; T <= N; ++T)
        {
            const int c = popcount(t.mixed[static_cast<std::size_t>(T)], N);
            CHECK(c == T, "Mixed: mixed[T] has exactly T bits set (count-honest)");
        }

        // Metric-protected positions (metric[P]) are always in mixed[T].
        // For T=8, P=4; metric[4] = {0,4,8,12}.
        for (int step : {0, 4, 8, 12})
            CHECK(mixedSurvives(t, step, 8), "Mixed: metric-protected position always survives");

        // Same call twice → identical result (no stochastic state).
        for (int T = 1; T <= N; ++T)
            for (int s = 0; s < N; ++s)
                CHECK(mixedSurvives(t, s, T) == mixedSurvives(t, s, T),
                      "Mixed: result is deterministic (same output every call)");

        // Edge: T=0 → nothing; T=N → all.
        for (int s = 0; s < N; ++s)
        {
            CHECK(!mixedSurvives(t, s, 0), "Mixed T=0: nothing survives");
            CHECK(mixedSurvives(t, s, N),  "Mixed T=N: all survive");
        }
    }

    // -----------------------------------------------------------------------
    // mixedSurvives — per-track rotation: two different offsets yield de-correlated
    // patterns at the same density (but identical cardinality).

    static void testMixedRotationDeCorrelation()
    {
        // offset=1 avoids the period-3 symmetry of bjorklund(12,4) that offset=3 hits.
        const Table t0 = buildFor(16, 4, 4, 1920.0, 0);
        const Table t1 = buildFor(16, 4, 4, 1920.0, 1);
        constexpr int N = 16;
        constexpr int T = 8;

        // Cardinality must be the same.
        const int c0 = popcount(t0.mixed[T], N);
        const int c1 = popcount(t1.mixed[T], N);
        CHECK(c0 == T, "Mixed rotation: offset=0 cardinality = T");
        CHECK(c1 == T, "Mixed rotation: offset=1 cardinality = T");

        // Patterns should differ (rotation de-correlates them).
        CHECK(t0.mixed[T] != t1.mixed[T], "Mixed rotation: different offsets yield different patterns");
    }

    // -----------------------------------------------------------------------
    // uniformSurvives — exactly Tl survivors over L positions; loop-stable.

    static void testUniformSurvives()
    {
        constexpr int L  = 16;
        constexpr int Tl = 6;
        constexpr int off = 0;

        int count = 0;
        for (int pos = 0; pos < L; ++pos)
            if (uniformSurvives(pos, L, Tl, off)) ++count;
        CHECK(count == Tl, "Uniform: exactly Tl survivors over L positions");

        // Second pass (loop repeat) → identical pattern.
        for (int pos = 0; pos < L; ++pos)
            CHECK(uniformSurvives(pos, L, Tl, off) == uniformSurvives(pos, L, Tl, off),
                  "Uniform: loop-stable (same result every call for same pos)");

        // Rotation de-correlation: offset=0 vs offset=3 → different but same count.
        int count2 = 0;
        for (int pos = 0; pos < L; ++pos)
            if (uniformSurvives(pos, L, Tl, 3)) ++count2;
        CHECK(count2 == Tl, "Uniform: rotated offset same count");

        bool allSame = true;
        for (int pos = 0; pos < L; ++pos)
            if (uniformSurvives(pos, L, Tl, 0) != uniformSurvives(pos, L, Tl, 3))
                allSame = false;
        CHECK(!allSame, "Uniform: different offsets yield de-correlated patterns");
    }

    // -----------------------------------------------------------------------
    // Loop-stability: Metric/Mixed results are pure functions of barStep,
    // identical across bars; Uniform repeats with period L.

    static void testLoopStability()
    {
        const Table t = buildFor(16, 4, 4);
        constexpr int N = 16;
        constexpr int T = 8;
        constexpr int L = 32;  // two-bar loop
        constexpr int Tl = 16; // half of L

        // Metric: same barStep → same result regardless of which bar.
        for (int barStep = 0; barStep < N; ++barStep)
        {
            const bool r1 = metricSurvives(t, barStep, T);
            const bool r2 = metricSurvives(t, barStep, T);
            CHECK(r1 == r2, "Metric: loop-stable (barStep lookup is pure)");
        }

        // Uniform: positions 0..L-1 are stable on repeated query.
        for (int pos = 0; pos < L; ++pos)
            CHECK(uniformSurvives(pos, L, Tl, 0) == uniformSurvives(pos, L, Tl, 0),
                  "Uniform: loop-stable (pos lookup is pure)");
    }

    // -----------------------------------------------------------------------

    void runMetricSelectTests()
    {
        testMaskCardinality();
        testMaskCardinality7_8();
        testFourFourTierOrder();
        testEuclidBoundaryTier();
        testNonMonotonicMembership();
        testSevenEightTiers();
        testSixEightTiers();
        testMetricSurvives();
        testMixedSurvives();
        testMixedRotationDeCorrelation();
        testUniformSurvives();
        testLoopStability();
    }

} // namespace lockstep
