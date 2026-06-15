// AccentTest — validates the print-auto-velocity accent formula.
// Tests the core math from writeAccentVelocities (PluginEditor.cpp).
// Strategy: re-implement the formula here and validate against
// MetricGrid::metricWeight properties; no JUCE editor dependency needed.

#include "TestHarness.h"
#include "../src/core/MetricGrid.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    // Mirror of writeAccentVelocities logic, operating on a plain array.
    // Returned array indexed by step; -1 means step was not a trig (untouched).
    static std::array<int, 16> computeAccentVels(
        const std::array<bool, 16>& trigs,
        float depth,
        int center,
        double divPpq,
        double barPpq,
        int numerator,
        int denominator)
    {
        std::array<int, 16> out;
        out.fill(-1);
        for (int si = 0; si < 16; ++si)
        {
            if (!trigs[static_cast<std::size_t>(si)]) { continue; }
            const double ppqInBar = std::fmod(
                static_cast<double>(si) * divPpq, barPpq);
            const float w = MetricGrid::metricWeight(ppqInBar, barPpq, numerator, denominator);
            const float maxSwing = static_cast<float>(
                std::min(center - 1, 127 - center));
            const float vel = std::round(
                std::clamp(static_cast<float>(center) + (depth * maxSwing * ((2.0f * w) - 1.0f)),
                           1.0f, 127.0f));
            out[static_cast<std::size_t>(si)] = static_cast<int>(vel);
        }
        return out;
    }

    // -----------------------------------------------------------------------

    // Depth=0 → every trig gets exactly Center regardless of metric position.
    static void testAccentDepthZeroFlat()
    {
        std::array<bool, 16> all{};
        all.fill(true);
        const auto vels = computeAccentVels(all, 0.0f, 90, 0.25, 4.0, 4, 4);
        for (int i = 0; i < 16; ++i)
        {
            CHECK(vels[static_cast<std::size_t>(i)] == 90, "depth=0: all steps == center(90)");
        }
    }

    // Bar downbeat (step 0) is the strongest position — must receive the
    // highest velocity when Depth > 0.
    static void testAccentDownbeatIsLoudest()
    {
        std::array<bool, 16> all{};
        all.fill(true);
        // 4/4, 16th-note divider (0.25 PPQ per step, barPpq=4.0)
        const auto vels = computeAccentVels(all, 1.0f, 90, 0.25, 4.0, 4, 4);
        const int downbeatVel = vels[0];
        for (int i = 1; i < 16; ++i)
        {
            CHECK(vels[static_cast<std::size_t>(i)] <= downbeatVel,
                  "4/4: downbeat vel >= all others");
        }
    }

    // The finest offbeat (step 1 in a 4/4 16-step bar = the "e" of beat 1)
    // has weight 0 and must receive the lowest velocity.
    static void testAccentOffbeatIsQuietest()
    {
        std::array<bool, 16> all{};
        all.fill(true);
        const auto vels = computeAccentVels(all, 1.0f, 90, 0.25, 4.0, 4, 4);
        const int quietest = vels[1];
        for (int i = 0; i < 16; ++i)
        {
            CHECK(vels[static_cast<std::size_t>(i)] >= quietest,
                  "4/4: offbeat step1 <= all others");
        }
    }

    // Only trig steps are written; non-trig steps stay at -1.
    static void testAccentOnlyTrigsWritten()
    {
        // Trigs only on beats 0, 4, 8, 12 (quarter notes in 4/4).
        std::array<bool, 16> sparse{};
        for (int i : {0, 4, 8, 12}) { sparse[static_cast<std::size_t>(i)] = true; }

        const auto vels = computeAccentVels(sparse, 0.8f, 90, 0.25, 4.0, 4, 4);
        for (int i = 0; i < 16; ++i)
        {
            const bool isTrig = (i == 0 || i == 4 || i == 8 || i == 12);
            if (isTrig)
            {
                CHECK(vels[static_cast<std::size_t>(i)] > 0, "trig step has a velocity");
            }
            else
            {
                CHECK(vels[static_cast<std::size_t>(i)] == -1, "non-trig step untouched");
            }
        }
    }

    // All velocities must be clamped to [1, 127].
    static void testAccentClamping()
    {
        std::array<bool, 16> all{};
        all.fill(true);
        // center=127, full depth — floor steps would go below 1 without clamp.
        auto vels = computeAccentVels(all, 1.0f, 127, 0.25, 4.0, 4, 4);
        for (int i = 0; i < 16; ++i)
        {
            CHECK(vels[static_cast<std::size_t>(i)] >= 1,   "clamped >= 1 (center=127)");
            CHECK(vels[static_cast<std::size_t>(i)] <= 127, "clamped <= 127 (center=127)");
        }
        // center=1, full depth — peak steps would exceed 127 without clamp.
        vels = computeAccentVels(all, 1.0f, 1, 0.25, 4.0, 4, 4);
        for (int i = 0; i < 16; ++i)
        {
            CHECK(vels[static_cast<std::size_t>(i)] >= 1,   "clamped >= 1 (center=1)");
            CHECK(vels[static_cast<std::size_t>(i)] <= 127, "clamped <= 127 (center=1)");
        }
    }

    // Downbeat velocity must equal center + depth*maxSwing (highest possible).
    static void testAccentDownbeatVelocityMath()
    {
        std::array<bool, 16> all{};
        all.fill(true);
        const float depth    = 0.6f;
        const int   center   = 90;
        const float maxSwing = static_cast<float>(std::min(center - 1, 127 - center));
        const int expected   = static_cast<int>(
            std::round(std::clamp(static_cast<float>(center) + (depth * maxSwing),
                                  1.0f, 127.0f)));

        const auto vels = computeAccentVels(all, depth, center, 0.25, 4.0, 4, 4);
        CHECK(vels[0] == expected, "downbeat vel = center + depth*maxSwing");
    }

    // In 3/4, downbeat is strongest; beats 2+3 are equal (both level-1 heads).
    static void testAccentThreeFour()
    {
        std::array<bool, 16> trigs{};
        trigs[0] = true; trigs[1] = true; trigs[2] = true;
        // divPpq=1.0 (quarter note), barPpq=3.0 (3 quarter-note bar)
        const auto vels = computeAccentVels(trigs, 1.0f, 90, 1.0, 3.0, 3, 4);
        CHECK(vels[0] > vels[1], "3/4: beat 1 louder than beat 2");
        CHECK(vels[0] > vels[2], "3/4: beat 1 louder than beat 3");
        CHECK(vels[1] == vels[2], "3/4: beats 2 and 3 equal");
    }

    // In 6/8, the two dotted-quarter beats (pulses 0 and 3) are louder
    // than the inner eighth pulses.
    static void testAccentSixEight()
    {
        // 6 pulses, barPpq=6.0 (sixth-note reference), divPpq=1.0.
        std::array<bool, 16> trigs{};
        for (int i = 0; i < 6; ++i) { trigs[static_cast<std::size_t>(i)] = true; }
        const auto vels = computeAccentVels(trigs, 1.0f, 90, 1.0, 6.0, 6, 8);
        // w[0]=3 (downbeat), w[3]=2 (second dotted beat), w[1]=w[2]=w[4]=w[5]=1
        CHECK(vels[0] > vels[1], "6/8: downbeat > inner eighth");
        CHECK(vels[3] > vels[1], "6/8: second dotted beat > inner eighth");
        CHECK(vels[0] > vels[3], "6/8: downbeat > second dotted beat");
        CHECK(vels[1] == vels[2], "6/8: inner eighth pulses equal");
    }

    // -----------------------------------------------------------------------

    void runAccentTests()
    {
        testAccentDepthZeroFlat();
        testAccentDownbeatIsLoudest();
        testAccentOffbeatIsQuietest();
        testAccentOnlyTrigsWritten();
        testAccentClamping();
        testAccentDownbeatVelocityMath();
        testAccentThreeFour();
        testAccentSixEight();
    }
}
