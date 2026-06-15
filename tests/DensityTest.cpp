// DensityTest — headless tests for src/core/Density.h.

#include "TestHarness.h"
#include "../src/core/Density.h"
#include <cmath>
#include <cstdint>

namespace lockstep
{
    using namespace Density;

    // -----------------------------------------------------------------------
    // metricDrop ordering

    static void testMetricDropDownbeat()
    {
        // 4/4 bar at 480 PPQ/beat = 1920 PPQ/bar.
        constexpr double barPpq = 1920.0;
        float db = metricDrop(0.0, barPpq);
        CHECK(db == 0.0f, "metricDrop: downbeat = 0 (survives longest)");
    }

    static void testMetricDropHalfBar()
    {
        constexpr double barPpq = 1920.0;
        constexpr double sixteenth = barPpq / 16.0;
        float beat3 = metricDrop(8.0 * sixteenth, barPpq);
        // k=8 → 3 trailing zeros → depth=3=maxDepth → Dmetric = 0.
        CHECK(beat3 <= 0.01f, "metricDrop: half-bar (beat 3) also low propensity");
    }

    static void testMetricDropOrdering()
    {
        // Verify: downbeat ≤ half-bar ≤ quarter-offbeat ≤ eighth-offbeat ≤ sixteenth-offbeat.
        constexpr double barPpq = 1920.0;
        constexpr double sixteenth = barPpq / 16.0;

        float db      = metricDrop(0.0,          barPpq); // k=0
        float beat3   = metricDrop(8.0 * sixteenth, barPpq); // k=8
        float beat2   = metricDrop(4.0 * sixteenth, barPpq); // k=4
        float eighth  = metricDrop(2.0 * sixteenth, barPpq); // k=2
        float sixtn   = metricDrop(1.0 * sixteenth, barPpq); // k=1

        CHECK(db <= beat3,  "ordering: downbeat <= half-bar");
        CHECK(beat3 <= beat2, "ordering: half-bar <= quarter offbeat");
        CHECK(beat2 <= eighth, "ordering: quarter <= eighth offbeat");
        CHECK(eighth <= sixtn, "ordering: eighth <= sixteenth offbeat");
        CHECK(sixtn >= 0.99f, "metricDrop: finest 16th offbeat = 1 (dies first)");

        float beat4 = metricDrop(12.0 * sixteenth, barPpq); // k=12, same depth as k=4
        CHECK(std::abs(beat2 - beat4) < 0.001f, "metricDrop: beats 2 and 4 equal propensity");
    }

    static void testMetricDropEdgeCases()
    {
        // barPpq = 0 should not crash (returns 0).
        float r = metricDrop(100.0, 0.0);
        CHECK(r == 0.0f, "metricDrop: barPpq=0 returns 0 safely");

        // ppqInBar > barPpq: fmod keeps it in range.
        float wrapped = metricDrop(1921.0, 1920.0);
        CHECK(wrapped >= 0.0f && wrapped <= 1.0f, "metricDrop: ppqInBar > barPpq wraps safely");
    }

    // -----------------------------------------------------------------------
    // densityScrubHash decorrelation

    static void testScrubHashDecorrelation()
    {
        // For the same (trackIdx, stepPos) the Scrub hash must produce a different
        // distribution than TrigEvaluator::deterministicPercent.
        auto tevHash = [](std::size_t t, std::int64_t s) -> int {
            auto h = (static_cast<uint32_t>(t) * 2654435761u)
                   ^ static_cast<uint32_t>(static_cast<uint64_t>(s) * 2246822519ull);
            h ^= h >> 16u;
            h *= 0x45d9f3bu;
            h ^= h >> 16u;
            return static_cast<int>(h % 100u);
        };

        int sameCount = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const auto t = static_cast<std::size_t>(i);
            const auto s = static_cast<std::int64_t>(i);
            int density_pct = static_cast<int>(densityScrubHash(t, s, 50) % 100u);
            int tev_pct = tevHash(t, s);
            if (density_pct == tev_pct) { ++sameCount; }
        }
        // Over 1000 comparisons in [0,99] ≈10% coincidental matches expected;
        // >500 would indicate strong correlation.
        CHECK(sameCount < 200, "scrubHash: low accidental correlation with deterministicPercent");
    }

    static void testScrubHashReshuffles()
    {
        // Changing quantizedKnobLevel should change most step selections.
        int sameCount = 0;
        for (int step = 0; step < 200; ++step)
        {
            auto h50 = densityScrubHash(0, static_cast<std::int64_t>(step), 50);
            auto h51 = densityScrubHash(0, static_cast<std::int64_t>(step), 51);
            if ((h50 % 10000u) == (h51 % 10000u)) { ++sameCount; }
        }
        CHECK(sameCount < 20, "scrubHash: knob-level change reshuffles most steps");
    }

    // -----------------------------------------------------------------------
    // densitySurvives — statistical properties

    static void testSurvivingCountUniform()
    {
        constexpr int kSteps = 10000;
        constexpr float kEffective = 0.5f;
        constexpr double kBarPpq = 1920.0;
        const int qLevel = static_cast<int>(std::round(kEffective * 100.0f));

        int survivedScrub = 0;
        int survivedReroll = 0;
        for (int i = 0; i < kSteps; ++i)
        {
            const double ppqInBar = kBarPpq * (static_cast<double>(i % 16) / 16.0);
            const float rerollR = static_cast<float>(i % 997) / 997.0f;
            const auto ti = static_cast<std::size_t>(i);
            const auto si = static_cast<std::int64_t>(i);

            if (densitySurvives(kEffective, 0.0f, ppqInBar, kBarPpq,
                                Musicality::Uniform, DensitySelection::Scrub,
                                ti, si, qLevel, rerollR))
            {
                ++survivedScrub;
            }
            if (densitySurvives(kEffective, 0.0f, ppqInBar, kBarPpq,
                                Musicality::Uniform, DensitySelection::Reroll,
                                ti, si, qLevel, rerollR))
            {
                ++survivedReroll;
            }
        }
        CHECK(survivedScrub > 4000 && survivedScrub < 6000,
              "densitySurvives Uniform/Scrub: surviving count ≈ 50% (±10%)");
        CHECK(survivedReroll > 4000 && survivedReroll < 6000,
              "densitySurvives Uniform/Reroll: surviving count ≈ 50% (±10%)");
    }

    static void testSurvivingCountMetric()
    {
        // Metric mode changes *which* steps survive based on metric importance.
        // With the importance→probability model, average p ≈ effective across the grid.
        // Verify the count is non-zero, non-trivial, and close to the expected average.
        constexpr int kSteps = 10000;
        constexpr float kEffective = 0.5f;
        constexpr double kBarPpq = 1920.0;
        const int qLevel = static_cast<int>(std::round(kEffective * 100.0f));

        int survived = 0;
        for (int i = 0; i < kSteps; ++i)
        {
            const double ppqInBar = kBarPpq * (static_cast<double>(i % 16) / 16.0);
            const float rerollR = static_cast<float>(i % 997) / 997.0f;
            if (densitySurvives(kEffective, 0.0f, ppqInBar, kBarPpq,
                                Musicality::Metric, DensitySelection::Scrub,
                                static_cast<std::size_t>(i), static_cast<std::int64_t>(i),
                                qLevel, rerollR))
            {
                ++survived;
            }
        }
        // Average p ≈ effective = 0.5, so expect roughly 30–70% survival.
        CHECK(survived > 3000 && survived < 7000,
              "densitySurvives Metric: survival count near effective average");
    }

    // Regression guard: Scrub and Reroll must produce different surviving sets
    // in Metric mode. Pre-fix, the selection value had zero weight at m=1, so
    // both modes produced identical results (the zero-weight bug).
    static void testMetricScrubVsRerollDiffer()
    {
        constexpr float kEffective = 0.5f;
        constexpr double kBarPpq = 1920.0;
        constexpr double kSixteenth = kBarPpq / 16.0;
        const int qLevel = static_cast<int>(std::round(kEffective * 100.0f));

        int sameCount = 0;
        constexpr int kTrials = 500;
        for (int i = 0; i < kTrials; ++i)
        {
            const double ppqInBar = kSixteenth * static_cast<double>(i % 16);
            const float rerollR = static_cast<float>(i % 997) / 997.0f;
            const auto ti = static_cast<std::size_t>(i);
            const auto si = static_cast<std::int64_t>(i);

            const bool scrub  = densitySurvives(kEffective, 0.0f, ppqInBar, kBarPpq,
                                                Musicality::Metric, DensitySelection::Scrub,
                                                ti, si, qLevel, rerollR);
            const bool reroll = densitySurvives(kEffective, 0.0f, ppqInBar, kBarPpq,
                                                Musicality::Metric, DensitySelection::Reroll,
                                                ti, si, qLevel, rerollR);
            if (scrub == reroll) { ++sameCount; }
        }
        // The Scrub r comes from a hash; rerollR is an independent sequence.
        // Expect significant disagreement (not 100% same = not the old zero-weight bug).
        CHECK(sameCount < kTrials,
              "densitySurvives Metric: Scrub and Reroll produce different selections");
    }

    // Gradual thinning: as density decreases from 1, offbeats drop before downbeats.
    // Specifically at effective=0.1, very few sixteenth offbeats survive but a healthy
    // fraction of downbeats do — no whole-tier cliff.
    static void testMetricGradualThinning()
    {
        constexpr double kBarPpq = 1920.0;
        constexpr double kSixteenth = kBarPpq / 16.0;
        constexpr int kTrials = 1000;

        auto countSurvived = [&](float effective, double ppqInBar) -> int {
            const int qLevel = static_cast<int>(std::round(effective * 100.0f));
            int count = 0;
            for (int i = 0; i < kTrials; ++i)
            {
                const auto ti = static_cast<std::size_t>(i * 7 + 3);
                const auto si = static_cast<std::int64_t>(i);
                const float rerollR = static_cast<float>(i % 997) / 997.0f;
                if (densitySurvives(effective, 0.0f, ppqInBar, kBarPpq,
                                    Musicality::Metric, DensitySelection::Scrub,
                                    ti, si, qLevel, rerollR))
                {
                    ++count;
                }
            }
            return count;
        };

        // At low density (0.15), downbeats survive far more than sixteenth offbeats.
        const int downbeat  = countSurvived(0.15f, 0.0);               // w=1
        const int sixteenth = countSurvived(0.15f, 1.0 * kSixteenth);  // w=0

        CHECK(downbeat > sixteenth * 3,
              "gradual thinning: downbeats survive significantly more than finest offbeats at low density");
        CHECK(downbeat > 0,
              "gradual thinning: downbeats still survive at density=0.15");
        CHECK(sixteenth < kTrials,
              "gradual thinning: sixteenth offbeats are thinned at density=0.15");
    }

    static void testFloorAndCeiling()
    {
        // Ceiling: effective=1.0 → all steps survive (K < 1.0 always since K ∈ [0,1)).
        int full = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const float rerollR = static_cast<float>(i % 997) / 997.0f;
            if (densitySurvives(1.0f, 0.0f, 0.0, 1920.0,
                                Musicality::Uniform, DensitySelection::Reroll,
                                0, static_cast<std::int64_t>(i), 100, rerollR))
            {
                ++full;
            }
        }
        CHECK(full == 1000, "densitySurvives: effective=1.0 → all steps survive");

        // Floor: at effective=0.01 very few steps survive (~1%); verify < 10%.
        int floored = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const float rerollR = static_cast<float>(i % 997) / 997.0f;
            if (densitySurvives(0.01f, -1.0f, 0.0, 1920.0,
                                Musicality::Uniform, DensitySelection::Scrub,
                                0, static_cast<std::int64_t>(i), 1, rerollR))
            {
                ++floored;
            }
        }
        CHECK(floored < 100, "densitySurvives: effective=1% → very few steps survive");
    }

    static void testMetricModeOrdering()
    {
        // Metric mode: downbeats survive more than finest offbeats.
        constexpr float kEffective = 0.5f;
        constexpr double kBarPpq = 1920.0;
        constexpr double kSixteenth = kBarPpq / 16.0;
        const int qLevel = static_cast<int>(std::round(kEffective * 100.0f));

        int survivedDownbeat = 0;
        int survivedOffbeat = 0;
        constexpr int kTrials = 1000;
        for (int i = 0; i < kTrials; ++i)
        {
            const float rerollR = static_cast<float>(i % 997) / 997.0f;
            const auto si = static_cast<std::int64_t>(i);
            // Vary trackIdx to avoid hash coincidences dominating.
            const std::size_t ti = (static_cast<std::size_t>(i) * 7u) + 3u;

            if (densitySurvives(kEffective, 0.0f, 0.0, kBarPpq,
                                Musicality::Metric, DensitySelection::Scrub,
                                ti, si, qLevel, rerollR))
            {
                ++survivedDownbeat;
            }
            if (densitySurvives(kEffective, 0.0f, 1.0 * kSixteenth, kBarPpq,
                                Musicality::Metric, DensitySelection::Scrub,
                                ti, si, qLevel, rerollR))
            {
                ++survivedOffbeat;
            }
        }
        CHECK(survivedDownbeat > survivedOffbeat,
              "densitySurvives Metric: downbeats survive more than finest offbeats");
    }

    static void testMasterOffset()
    {
        // Positive master offset raises effective density → more survivors.
        constexpr double kBarPpq = 1920.0;
        int survivedLow = 0;
        int survivedHigh = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const float rerollR = static_cast<float>(i % 997) / 997.0f;
            const double ppq = kBarPpq * (static_cast<double>(i % 16) / 16.0);
            const auto si = static_cast<std::int64_t>(i);

            if (densitySurvives(0.3f, 0.0f, ppq, kBarPpq,
                                Musicality::Uniform, DensitySelection::Reroll,
                                0u, si, 30, rerollR))
            {
                ++survivedLow;
            }
            if (densitySurvives(0.3f, 0.4f, ppq, kBarPpq,
                                Musicality::Uniform, DensitySelection::Reroll,
                                0u, si, 70, rerollR))
            {
                ++survivedHigh;
            }
        }
        CHECK(survivedHigh > survivedLow,
              "densitySurvives: positive master offset raises survival rate");
    }

    void runDensityTests()
    {
        testMetricDropDownbeat();
        testMetricDropHalfBar();
        testMetricDropOrdering();
        testMetricDropEdgeCases();
        testScrubHashDecorrelation();
        testScrubHashReshuffles();
        testSurvivingCountUniform();
        testSurvivingCountMetric();
        testFloorAndCeiling();
        testMetricModeOrdering();
        testMasterOffset();
        testMetricScrubVsRerollDiffer();
        testMetricGradualThinning();
    }

} // namespace lockstep
