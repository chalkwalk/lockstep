// MetricSelectTest — validates the tier+Euclid deterministic Scrub selector.
// Tests MetricSelect::build + MetricSelect::scrubSurvives (DESIGN §39).

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
    static Table buildFor(int n, int num, int den, double barPpq = 1920.0)
    {
        std::array<float, 64> wts{};
        for (int s = 0; s < n && s < 64; ++s)
        {
            const double ppqPos = static_cast<double>(s) / static_cast<double>(n) * barPpq;
            wts[static_cast<std::size_t>(s)] =
                MetricGrid::metricWeight(ppqPos, barPpq, num, den);
        }
        return build(wts, n < 64 ? n : 64);
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
    // mask[T] has exactly T bits set for all T in [0, n].

    static void testMaskCardinality()
    {
        const Table t = buildFor(16, 4, 4);
        for (int T = 0; T <= t.n; ++T)
        {
            const int c = popcount(t.mask[static_cast<std::size_t>(T)], t.n);
            CHECK(c == T, "mask cardinality: mask[T] has exactly T bits set");
        }
    }

    static void testMaskCardinality7_8()
    {
        const Table t = buildFor(7, 7, 8);
        for (int T = 0; T <= t.n; ++T)
        {
            const int c = popcount(t.mask[static_cast<std::size_t>(T)], t.n);
            CHECK(c == T, "7/8 mask cardinality: mask[T] has exactly T bits set");
        }
    }

    // -----------------------------------------------------------------------
    // 4/4, 16-step: weight tier order is 0 > 8 > {4,12} > {2,6,10,14} > odds.
    // Verify that stronger positions enter the mask first.

    static void testFourFourTierOrder()
    {
        const Table t = buildFor(16, 4, 4);

        // T=1: only the bar downbeat (step 0)
        CHECK((t.mask[1] & (std::uint64_t(1) << 0)) != 0, "4/4 T=1: step 0 (downbeat) set");
        CHECK(t.mask[1] == (std::uint64_t(1) << 0), "4/4 T=1: only step 0");

        // T=2: add the half-bar (step 8)
        CHECK((t.mask[2] & (std::uint64_t(1) << 0)) != 0, "4/4 T=2: step 0 set");
        CHECK((t.mask[2] & (std::uint64_t(1) << 8)) != 0, "4/4 T=2: step 8 (half-bar) set");

        // T=4: add both quarter-note beats (steps 4 and 12); tier is whole
        CHECK((t.mask[4] & (std::uint64_t(1) << 4))  != 0, "4/4 T=4: step 4 set");
        CHECK((t.mask[4] & (std::uint64_t(1) << 12)) != 0, "4/4 T=4: step 12 set");
        CHECK((t.mask[4] & (std::uint64_t(1) << 0))  != 0, "4/4 T=4: step 0 still set");
        CHECK((t.mask[4] & (std::uint64_t(1) << 8))  != 0, "4/4 T=4: step 8 still set");

        // T=8: even steps {0,2,4,6,8,10,12,14} — all four "and" positions added
        for (int s = 0; s < 16; s += 2)
            CHECK((t.mask[8] & (std::uint64_t(1) << s)) != 0, "4/4 T=8: even steps set");
        for (int s = 1; s < 16; s += 2)
            CHECK((t.mask[8] & (std::uint64_t(1) << s)) == 0, "4/4 T=8: odd steps not set");

        // Odd steps (weakest tier, w≈0) must NOT appear until T>8.
        for (int s = 1; s < 16; s += 2)
            CHECK((t.mask[8] & (std::uint64_t(1) << s)) == 0, "4/4: odd steps absent at T=8");
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
        CHECK((t.mask[5] & (std::uint64_t(1) << 2))  != 0, "T=5: boundary step 2 selected");
        CHECK((t.mask[5] & (std::uint64_t(1) << 6))  == 0, "T=5: boundary step 6 not selected");
        CHECK((t.mask[5] & (std::uint64_t(1) << 10)) == 0, "T=5: boundary step 10 not selected");
        CHECK((t.mask[5] & (std::uint64_t(1) << 14)) == 0, "T=5: boundary step 14 not selected");

        // T=6: bjorklund(4,2) → positions 0,2 in tier → steps 2 and 10
        CHECK((t.mask[6] & (std::uint64_t(1) << 2))  != 0, "T=6: step 2 set");
        CHECK((t.mask[6] & (std::uint64_t(1) << 10)) != 0, "T=6: step 10 set");
        CHECK((t.mask[6] & (std::uint64_t(1) << 6))  == 0, "T=6: step 6 not set");
        CHECK((t.mask[6] & (std::uint64_t(1) << 14)) == 0, "T=6: step 14 not set");
    }

    // -----------------------------------------------------------------------
    // Non-monotonic membership: a position can be absent at T, present at T+1,
    // absent at T+2. This is by design (per-count Euclid, not drop-point).
    //
    // In 4/4 16-step, boundary tier of odds (8 positions, indices 0-7
    // = steps 1,3,5,7,9,11,13,15):
    //   T=11: bjorklund(8,3) → indices 0,3,6 → steps 1, 7, 13
    //   T=12: bjorklund(8,4) → indices 0,2,4,6 → steps 1, 5, 9, 13
    // Step 7: in mask[11], NOT in mask[12].

    static void testNonMonotonicMembership()
    {
        const Table t = buildFor(16, 4, 4);

        const bool step7_at11 = (t.mask[11] & (std::uint64_t(1) << 7)) != 0;
        const bool step7_at12 = (t.mask[12] & (std::uint64_t(1) << 7)) != 0;

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
        CHECK(t.mask[1] == (std::uint64_t(1) << 0), "7/8 T=1: only pulse 0");

        // T=3: group heads {0,3,5} and nothing else
        const std::uint64_t expected3 =
            (std::uint64_t(1) << 0) | (std::uint64_t(1) << 3) | (std::uint64_t(1) << 5);
        CHECK(t.mask[3] == expected3, "7/8 T=3: set = {0,3,5}");

        // Inner pulses {1,2,4,6} absent at T=3
        for (int s : {1, 2, 4, 6})
            CHECK((t.mask[3] & (std::uint64_t(1) << s)) == 0, "7/8 T=3: inner pulse absent");
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
        CHECK(t.mask[2] == expected2, "6/8 T=2: set = {0,3}");

        // Inner pulses {1,2,4,5} absent at T=2
        for (int s : {1, 2, 4, 5})
            CHECK((t.mask[2] & (std::uint64_t(1) << s)) == 0, "6/8 T=2: inner pulse absent");
    }

    // -----------------------------------------------------------------------
    // scrubSurvives — Uniform (m=0, P=0): pure hash < T/N.
    // Regression: must match old Scrub-Uniform behaviour.

    static void testScrubUniformRegression()
    {
        const Table t = buildFor(16, 4, 4);
        constexpr int N = 16;
        constexpr int T = 8;   // effective = 0.5
        constexpr int P = 0;   // m=0 → Uniform

        int survivors = 0;
        constexpr int kTrials = 10000;
        for (int trial = 0; trial < kTrials; ++trial)
        {
            // Fabricate a hash that gives a known hashFrac.
            // hashFrac = (trial % 10000) / 10000.0 → covers full [0,1) uniformly.
            const std::uint32_t hash = static_cast<std::uint32_t>(trial % 10000);
            const int stepInBar = trial % N;
            if (scrubSurvives(t, stepInBar, N, T, P, hash))
                ++survivors;
        }
        // With P=0 the decision is purely hash < T/N = 0.5 → ~50% survival.
        CHECK(survivors > 4500 && survivors < 5500,
              "Uniform (P=0): survival ≈ 50% across random steps (±5%)");
    }

    // -----------------------------------------------------------------------
    // scrubSurvives — Metric (m=1, P=T): only metricMask[T] survives.
    // In 4/4, step 0 (downbeat) must survive whenever T>=1;
    // step 1 (finest offbeat) must survive only when T >= 9 (first odd included).

    static void testScrubMetricOrdering()
    {
        const Table t = buildFor(16, 4, 4);
        constexpr int N = 16;
        constexpr std::uint32_t hash = 9999u; // high hash → hash branch never fires

        // Step 0 survives as soon as T=1.
        CHECK(scrubSurvives(t, 0, N, 1, 1, hash), "Metric: step 0 survives at T=1");

        // Step 8 (half-bar) survives from T=2.
        CHECK(scrubSurvives(t, 8, N, 2, 2, hash),  "Metric: step 8 survives at T=2");
        CHECK(!scrubSurvives(t, 8, N, 1, 1, hash), "Metric: step 8 absent at T=1");

        // Step 1 (finest offbeat) absent until T=9 (first odd added).
        for (int T = 1; T <= 8; ++T)
            CHECK(!scrubSurvives(t, 1, N, T, T, hash),
                  "Metric: step 1 absent at T<=8");
        CHECK(scrubSurvives(t, 1, N, 9, 9, hash), "Metric: step 1 present at T=9");
    }

    // -----------------------------------------------------------------------
    // scrubSurvives — Mixed (m=0.5): top P positions metric-protected,
    // remainder hash-filled. Total count ≈ T (count-honesty).

    static void testScrubMixedCountHonesty()
    {
        const Table t = buildFor(16, 4, 4);
        constexpr int N = 16;
        constexpr int T = 8;
        constexpr int P = 4;  // m=0.5 → P = round(0.5 * T) = 4

        // Metric-protected positions (mask[4] = {0,4,8,12}) must always survive
        // regardless of hash.
        for (int step : {0, 4, 8, 12})
        {
            CHECK(scrubSurvives(t, step, N, T, P, 9999u),
                  "Mixed: metric-protected position survives regardless of hash");
        }

        // Count total survivors over all grid positions with a sweep of hashes.
        // With T=8, P=4, remainder fill = (8-4)/(16-4) = 4/12 ≈ 0.333.
        // 4 metric-protected + 12 * 0.333 ≈ 8 total → count-honest.
        int totalSurvived = 0;
        constexpr int kSweep = 1000;
        for (int trial = 0; trial < kSweep; ++trial)
        {
            const auto hash = static_cast<std::uint32_t>(trial * 97 + 3); // spread
            for (int s = 0; s < N; ++s)
                if (scrubSurvives(t, s, N, T, P, hash)) ++totalSurvived;
        }
        // Average survivors per trial should be ≈ T = 8; allow ±2 tolerance.
        const float avgSurvivors = static_cast<float>(totalSurvived) / static_cast<float>(kSweep);
        CHECK(avgSurvivors >= 6.0f && avgSurvivors <= 10.0f,
              "Mixed: avg survivors per bar ≈ T (count-honest, ±2)");
    }

    // -----------------------------------------------------------------------
    // Edge cases: T=0 → nothing survives; T=N → everything survives.

    static void testEdgeCases()
    {
        const Table t = buildFor(16, 4, 4);
        constexpr int N = 16;

        // T=0: no step survives regardless of hash or P.
        for (int s = 0; s < N; ++s)
            CHECK(!scrubSurvives(t, s, N, 0, 0, 0u), "T=0: nothing survives");

        // T=N, P=N: all positions survive (metric mask covers all).
        for (int s = 0; s < N; ++s)
            CHECK(scrubSurvives(t, s, N, N, N, 9999u), "T=N, P=N: all survive");

        // T=N, P=0: hash fill with ratio = N/N = 1.0 → all positions survive.
        for (int s = 0; s < N; ++s)
            CHECK(scrubSurvives(t, s, N, N, 0, 0u), "T=N, P=0: all survive via hash fill");
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
        testScrubUniformRegression();
        testScrubMetricOrdering();
        testScrubMixedCountHonesty();
        testEdgeCases();
    }

} // namespace lockstep
