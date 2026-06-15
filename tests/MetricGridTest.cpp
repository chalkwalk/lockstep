// MetricGridTest — headless tests for src/core/MetricGrid.h.

#include "TestHarness.h"
#include "../src/core/MetricGrid.h"
#include <cmath>

namespace lockstep
{
    using namespace MetricGrid;

    // -----------------------------------------------------------------------
    // Internal pulse-weight tree validation (pin the plan's table)

    static void testPulseWeights44()
    {
        // 4/4 (n=4): assign(w, 0, 4) with binary splits → [3,1,2,1]
        std::array<int, 32> w{};
        assignWeights(w, 0u, 4);
        CHECK(w[0] == 3, "4/4 pulse weights: w[0]=3");
        CHECK(w[1] == 1, "4/4 pulse weights: w[1]=1");
        CHECK(w[2] == 2, "4/4 pulse weights: w[2]=2");
        CHECK(w[3] == 1, "4/4 pulse weights: w[3]=1");
    }

    static void testPulseWeights34()
    {
        // 3/4 (n=3): ternary split → [2,1,1]
        std::array<int, 32> w{};
        assignWeights(w, 0u, 3);
        CHECK(w[0] == 2, "3/4 pulse weights: w[0]=2");
        CHECK(w[1] == 1, "3/4 pulse weights: w[1]=1");
        CHECK(w[2] == 1, "3/4 pulse weights: w[2]=1");
    }

    static void testPulseWeights68()
    {
        // 6/8 (n=6): binary split 6→3+3, each 3→1+1+1 → [3,1,1,2,1,1]
        std::array<int, 32> w{};
        assignWeights(w, 0u, 6);
        CHECK(w[0] == 3, "6/8 pulse weights: w[0]=3");
        CHECK(w[1] == 1, "6/8 pulse weights: w[1]=1");
        CHECK(w[2] == 1, "6/8 pulse weights: w[2]=1");
        CHECK(w[3] == 2, "6/8 pulse weights: w[3]=2");
        CHECK(w[4] == 1, "6/8 pulse weights: w[4]=1");
        CHECK(w[5] == 1, "6/8 pulse weights: w[5]=1");
    }

    static void testPulseWeights98()
    {
        // 9/8 (n=9): ternary 9→3+3+3, each 3→1+1+1 → [3,1,1,2,1,1,2,1,1]
        std::array<int, 32> w{};
        assignWeights(w, 0u, 9);
        CHECK(w[0] == 3, "9/8 pulse weights: w[0]=3");
        CHECK(w[1] == 1, "9/8 pulse weights: w[1]=1");
        CHECK(w[2] == 1, "9/8 pulse weights: w[2]=1");
        CHECK(w[3] == 2, "9/8 pulse weights: w[3]=2");
        CHECK(w[4] == 1, "9/8 pulse weights: w[4]=1");
        CHECK(w[5] == 1, "9/8 pulse weights: w[5]=1");
        CHECK(w[6] == 2, "9/8 pulse weights: w[6]=2");
        CHECK(w[7] == 1, "9/8 pulse weights: w[7]=1");
        CHECK(w[8] == 1, "9/8 pulse weights: w[8]=1");
    }

    static void testPulseWeights78()
    {
        // 7/8 (n=7): greedy 3s → 3+2+2; each subdivides as leaves → [3,1,1,2,1,2,1]
        std::array<int, 32> w{};
        assignWeights(w, 0u, 7);
        CHECK(w[0] == 3, "7/8 pulse weights: w[0]=3");
        CHECK(w[1] == 1, "7/8 pulse weights: w[1]=1");
        CHECK(w[2] == 1, "7/8 pulse weights: w[2]=1");
        CHECK(w[3] == 2, "7/8 pulse weights: w[3]=2");
        CHECK(w[4] == 1, "7/8 pulse weights: w[4]=1");
        CHECK(w[5] == 2, "7/8 pulse weights: w[5]=2");
        CHECK(w[6] == 1, "7/8 pulse weights: w[6]=1");
    }

    // -----------------------------------------------------------------------
    // metricWeight — boundary values

    static void testDownbeatWeight()
    {
        // Bar downbeat (ppqInBar=0) must always be 1.0 for any meter.
        constexpr double kBarPpq = 1920.0;
        CHECK(metricWeight(0.0, kBarPpq, 4, 4) == 1.0f, "4/4 downbeat = 1.0");
        CHECK(metricWeight(0.0, kBarPpq, 3, 4) == 1.0f, "3/4 downbeat = 1.0");
        CHECK(metricWeight(0.0, kBarPpq, 6, 8) == 1.0f, "6/8 downbeat = 1.0");
        CHECK(metricWeight(0.0, kBarPpq, 7, 8) == 1.0f, "7/8 downbeat = 1.0");
    }

    static void testFinestOffbeatWeight()
    {
        // The finest sub-pulse offbeat (count=1) must normalise to 0.0.
        // In 4/4 at barPpq=1920 the "e/a" positions are at 120, 360, … PPQ.
        constexpr double kBarPpq = 1920.0;
        constexpr double kSixteenth = kBarPpq / 16.0;

        // ppq=120 → phase=0.0625, pulseFloat=0.25, f=0.25, subK=round(0.25*4)=1, tz=0 → count=1
        float w = metricWeight(1.0 * kSixteenth, kBarPpq, 4, 4);
        CHECK(w == 0.0f, "4/4: finest sub-pulse offbeat = 0.0");
    }

    static void testEdgeCases()
    {
        // barPpq=0: guard returns 1.0.
        CHECK(metricWeight(0.0, 0.0, 4, 4) == 1.0f, "barPpq=0 returns 1.0");
        // numerator<=0: guard returns 1.0.
        CHECK(metricWeight(0.0, 1920.0, 0, 4) == 1.0f, "numerator=0 returns 1.0");
        // ppqInBar > barPpq: fmod wraps to [0,1) safely.
        float wrapped = metricWeight(1921.0, 1920.0, 4, 4);
        CHECK(wrapped >= 0.0f && wrapped <= 1.0f, "ppqInBar > barPpq wraps safely");
    }

    // -----------------------------------------------------------------------
    // 4/4 count table — pin the plan's 5/4/3/2/1 mapping at 16-step positions

    static void testFourFourCountTable()
    {
        // barPpq=1920, 16-step grid (sixteenth=120 PPQ).
        constexpr double kBarPpq = 1920.0;
        constexpr double k16 = kBarPpq / 16.0;
        constexpr float kDenom = 4.0f; // downbeatCount-1 = 4

        // downbeat (k=0): count=5 → 4/4=1.0
        CHECK(std::abs(metricWeight(0.0 * k16,  kBarPpq, 4, 4) - 1.00f) < 0.001f,
              "4/4 k=0 (downbeat) = 1.0");
        // beat 3 (k=8): count=4 → 3/4=0.75
        CHECK(std::abs(metricWeight(8.0 * k16,  kBarPpq, 4, 4) - 3.0f/kDenom) < 0.001f,
              "4/4 k=8 (beat 3) = 0.75");
        // beat 2 (k=4): count=3 → 2/4=0.5
        CHECK(std::abs(metricWeight(4.0 * k16,  kBarPpq, 4, 4) - 2.0f/kDenom) < 0.001f,
              "4/4 k=4 (beat 2) = 0.5");
        // beat 4 (k=12): same as beat 2
        CHECK(std::abs(metricWeight(12.0 * k16, kBarPpq, 4, 4) - 2.0f/kDenom) < 0.001f,
              "4/4 k=12 (beat 4) = 0.5");
        // "&" (k=2): count=2 → 1/4=0.25
        CHECK(std::abs(metricWeight(2.0 * k16,  kBarPpq, 4, 4) - 1.0f/kDenom) < 0.001f,
              "4/4 k=2 ('&') = 0.25");
        // All k=6,10,14 also "&" with same weight.
        CHECK(std::abs(metricWeight(6.0 * k16,  kBarPpq, 4, 4) - 1.0f/kDenom) < 0.001f,
              "4/4 k=6 ('&') = 0.25");
        // "e/a" (k=1): count=1 → 0/4=0.0
        CHECK(std::abs(metricWeight(1.0 * k16,  kBarPpq, 4, 4) - 0.0f) < 0.001f,
              "4/4 k=1 ('e') = 0.0");
        CHECK(std::abs(metricWeight(3.0 * k16,  kBarPpq, 4, 4) - 0.0f) < 0.001f,
              "4/4 k=3 ('a') = 0.0");
    }

    // -----------------------------------------------------------------------
    // Ordering properties across meters

    static void testOrderingSevenEight()
    {
        // 7/8 pulse weights [3,1,1,2,1,2,1]: group heads at pulses 0,3,5.
        // Ordering: w[0] > w[3]=w[5] > w[1]=w[2]=w[4]=w[6].
        constexpr double kBarPpq = 1680.0; // 7 eighth-notes at 480 PPQ/quarter
        constexpr double pulsePpq = kBarPpq / 7.0;

        const float w0 = metricWeight(0.0 * pulsePpq, kBarPpq, 7, 8);
        const float w1 = metricWeight(1.0 * pulsePpq, kBarPpq, 7, 8);
        const float w3 = metricWeight(3.0 * pulsePpq, kBarPpq, 7, 8);
        const float w5 = metricWeight(5.0 * pulsePpq, kBarPpq, 7, 8);
        const float w6 = metricWeight(6.0 * pulsePpq, kBarPpq, 7, 8);

        CHECK(w0 == 1.0f,                    "7/8: bar downbeat = 1.0");
        CHECK(w3 > w1,                       "7/8: group head (3) stronger than inner (1)");
        CHECK(w5 > w1,                       "7/8: group head (5) stronger than inner (1)");
        CHECK(std::abs(w3 - w5) < 0.001f,   "7/8: group heads 3 and 5 equal");
        CHECK(std::abs(w1 - w6) < 0.001f,   "7/8: inner pulses 1 and 6 equal");
        CHECK(w3 < w0,                       "7/8: group heads weaker than bar downbeat");
    }

    static void testOrderingSixEight()
    {
        // 6/8 pulse weights [3,1,1,2,1,1]: two dotted-quarter groups.
        constexpr double kBarPpq = 1440.0; // 6 eighth-notes at 480 PPQ/quarter
        constexpr double pulsePpq = kBarPpq / 6.0;

        const float w0 = metricWeight(0.0 * pulsePpq, kBarPpq, 6, 8);
        const float w1 = metricWeight(1.0 * pulsePpq, kBarPpq, 6, 8);
        const float w3 = metricWeight(3.0 * pulsePpq, kBarPpq, 6, 8);

        CHECK(w0 == 1.0f, "6/8: bar downbeat = 1.0");
        CHECK(w3 > w1,    "6/8: second group head stronger than inner pulses");
        CHECK(w3 < w0,    "6/8: second group head weaker than bar downbeat");
    }

    static void testSixEightDiffersFromThreeFour()
    {
        // 6/8 and 3/4 have the same total duration but different groupings.
        // The second group head in 6/8 should be stronger than beat 2 in 3/4
        // because it heads a sub-group that 3/4's beat 2 does not.
        constexpr double kBarPpq = 1440.0;
        const float sixEightW3 = metricWeight(kBarPpq / 2.0, kBarPpq, 6, 8);
        const float threeFourW1 = metricWeight(kBarPpq / 3.0, kBarPpq, 3, 4);
        CHECK(sixEightW3 > threeFourW1, "6/8 group head stronger than 3/4 beat 2");
    }

    void runMetricGridTests()
    {
        testPulseWeights44();
        testPulseWeights34();
        testPulseWeights68();
        testPulseWeights98();
        testPulseWeights78();
        testDownbeatWeight();
        testFinestOffbeatWeight();
        testEdgeCases();
        testFourFourCountTable();
        testOrderingSevenEight();
        testOrderingSixEight();
        testSixEightDiffersFromThreeFour();
    }

} // namespace lockstep
