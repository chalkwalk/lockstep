// EuclideanTest — pure headless tests for the Bjorklund / Euclidean generator.

#include "TestHarness.h"
#include "../src/core/Euclidean.h"

namespace lockstep
{
    static void testBjorklundClassicPatterns()
    {
        // E(3,8) = [1,0,0,1,0,0,1,0] — "tresillo"
        auto r38 = bjorklund(8, 3);
        CHECK(r38.size() == 8u, "E(3,8) has 8 steps");
        int cnt = 0;
        for (bool b : r38)
            if (b) ++cnt;
        CHECK(cnt == 3, "E(3,8) has 3 onsets");
        // Verify the canonical pattern: onsets at 0, 3, 6.
        CHECK(r38[0] && !r38[1] && !r38[2] && r38[3] && !r38[4] && !r38[5] && r38[6] && !r38[7],
              "E(3,8) = tresillo [1,0,0,1,0,0,1,0]");

        // E(5,8) = [1,0,1,1,0,1,1,0] — "cinquillo"
        auto r58 = bjorklund(8, 5);
        CHECK(r58.size() == 8u, "E(5,8) has 8 steps");
        cnt = 0;
        for (bool b : r58)
            if (b) ++cnt;
        CHECK(cnt == 5, "E(5,8) has 5 onsets");

        // E(4,16) — all 4 onsets evenly spaced.
        auto r416 = bjorklund(16, 4);
        CHECK(r416[0] && r416[4] && r416[8] && r416[12], "E(4,16) onsets at 0,4,8,12");
        CHECK(!r416[1] && !r416[5] && !r416[9] && !r416[13], "E(4,16) rests at 1,5,9,13");
    }

    static void testBjorklundEdgeCases()
    {
        // 0 pulses → all rests.
        auto r0 = bjorklund(8, 0);
        CHECK(r0.size() == 8u, "E(0,8) has 8 steps");
        for (bool b : r0) CHECK(!b, "E(0,8) all rests");

        // pulses == length → all onsets.
        auto rfull = bjorklund(8, 8);
        CHECK(rfull.size() == 8u, "E(8,8) has 8 steps");
        for (bool b : rfull) CHECK(b, "E(8,8) all onsets");

        // pulses > length → clamp to length (all onsets).
        auto rclamp = bjorklund(4, 10);
        CHECK(rclamp.size() == 4u, "E(10,4) has 4 steps");
        for (bool b : rclamp) CHECK(b, "E(10,4) clamps to all onsets");

        // Empty length.
        auto rempty = bjorklund(0, 0);
        CHECK(rempty.empty(), "E(0,0) empty");
    }

    static void testBjorklundRotation()
    {
        // E(3,8) with +1 offset → rotate right by 1 (onset at 1,4,7 instead of 0,3,6).
        auto r = bjorklund(8, 3, 1);
        CHECK(!r[0] && r[1] && !r[2] && !r[3] && r[4] && !r[5] && !r[6] && r[7],
              "E(3,8,+1) rotates right by 1");

        // Negative offset: rotate left by 1 (onset at 7,2,5 — wraps).
        auto rl = bjorklund(8, 3, -1);
        CHECK(rl[7] && rl[2] && rl[5], "E(3,8,-1) wraps correctly");
    }

    static void testEuclideanAccentsVelocities()
    {
        // All accents = no accents → all onsets at velocity 64.
        auto v = euclideanAccents(8, 3, 0, 0);
        int onsets = 0;
        for (int x : v)
            if (x > 0) ++onsets;
        CHECK(onsets == 3, "accent pass: 3 onsets in E(3,8)");
        for (int x : v) CHECK(x == 0 || x == 64, "no accents → velocities are 0 or 64");

        // 1 accent over 3 pulses → one velocity-100 onset.
        auto va = euclideanAccents(8, 3, 0, 1);
        int acc = 0;
        for (int x : va)
            if (x == 100) ++acc;
        CHECK(acc == 1, "1 accent in 3 pulses → 1 velocity-100 step");
    }

    void runEuclideanTests()
    {
        testBjorklundClassicPatterns();
        testBjorklundEdgeCases();
        testBjorklundRotation();
        testEuclideanAccentsVelocities();
    }
}
