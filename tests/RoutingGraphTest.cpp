// RoutingGraphTest — pure topological order + cycle refusal for the A2
// output-routing graph (DESIGN §27). Each track has at most one out edge.

#include "TestHarness.h"
#include "../src/core/RoutingGraph.h"

#include <array>

namespace lockstep
{
    using routing::computeOrder;
    using routing::soloAudibleMask;
    using routing::wouldCreateCycle;

    // Returns the position of track t in the order (0..N-1), or -1.
    template <std::size_t N>
    static int posOf(const std::array<int, N>& order, int t)
    {
        for (std::size_t i = 0; i < N; ++i)
            if (order[i] == t) return static_cast<int>(i);
        return -1;
    }

    static void testOrderNoEdgesIsIdentity()
    {
        std::array<int, 4> dest{ -1, -1, -1, -1 };
        const auto order = computeOrder(dest);
        // A full permutation; with no edges the deterministic scan is ascending.
        for (int i = 0; i < 4; ++i)
            CHECK(order[static_cast<std::size_t>(i)] == i, "no edges => ascending identity");
    }

    static void testFeederBeforeBus()
    {
        // Track 0 -> bus 2, track 3 -> bus 2. Both feeders precede the bus.
        std::array<int, 4> dest{ 2, -1, -1, 2 };
        const auto order = computeOrder(dest);
        CHECK(posOf(order, 0) < posOf(order, 2), "feeder 0 before bus 2");
        CHECK(posOf(order, 3) < posOf(order, 2), "feeder 3 before bus 2");
    }

    static void testChainOrder()
    {
        // 0 -> 1 -> 2 -> 3 (a chain of buses). Strictly increasing position.
        std::array<int, 4> dest{ 1, 2, 3, -1 };
        const auto order = computeOrder(dest);
        CHECK(posOf(order, 0) < posOf(order, 1), "0 before 1");
        CHECK(posOf(order, 1) < posOf(order, 2), "1 before 2");
        CHECK(posOf(order, 2) < posOf(order, 3), "2 before 3");
    }

    static void testOrderIsAlwaysPermutation()
    {
        // Even a (defensively tolerated) cycle yields a full permutation.
        std::array<int, 3> dest{ 1, 2, 0 };  // 0->1->2->0
        const auto order = computeOrder(dest);
        std::array<bool, 3> seen{ false, false, false };
        for (int i = 0; i < 3; ++i)
        {
            const int v = order[static_cast<std::size_t>(i)];
            CHECK(v >= 0 && v < 3, "order entry in range");
            seen[static_cast<std::size_t>(v)] = true;
        }
        CHECK(seen[0] && seen[1] && seen[2], "cycle still yields a full permutation");
    }

    static void testCycleRefusal()
    {
        // Existing edge 1 -> 0. Adding 0 -> 1 would close a 2-cycle.
        std::array<int, 4> dest{ -1, 0, -1, -1 };
        CHECK(wouldCreateCycle(dest, 0, 1), "0->1 closes the loop with 1->0");
        // Adding 0 -> 2 (2 has no outgoing edge) is fine.
        CHECK(!wouldCreateCycle(dest, 0, 2), "0->2 is acyclic");
    }

    static void testSelfLoopRefused()
    {
        std::array<int, 4> dest{ -1, -1, -1, -1 };
        CHECK(wouldCreateCycle(dest, 2, 2), "self-route is a cycle");
    }

    static void testMasterAndOffAreNotEdges()
    {
        std::array<int, 4> dest{ 1, 2, 3, -1 };
        // -1 destination (Master/Off) can never create a cycle.
        CHECK(!wouldCreateCycle(dest, 3, -1), "routing to Master/Off is never a cycle");
    }

    static void testLongerCycleRefused()
    {
        // Chain 1->2->3 exists; adding 3->1 closes a 3-cycle.
        std::array<int, 4> dest{ -1, 2, 3, -1 };
        CHECK(wouldCreateCycle(dest, 3, 1), "3->1 closes the 1->2->3 chain");
        CHECK(!wouldCreateCycle(dest, 3, 0), "3->0 leaves the chain acyclic");
    }

    static void testSoloBusKeepsFeeders()
    {
        // 0 -> 2, 1 -> 2 (bus), 3 standalone. Solo the bus (2).
        std::array<int, 4> dest{ 2, 2, -1, -1 };
        std::array<bool, 4> solo{ false, false, true, false };
        const auto a = soloAudibleMask(dest, solo);
        CHECK(a[2], "soloed bus is audible");
        CHECK(a[0] && a[1], "feeders of a soloed bus stay audible");
        CHECK(!a[3], "unrelated track is silent under solo");
    }

    static void testSoloFeederKeepsBusChain()
    {
        // 0 -> 2, 1 -> 2, 2 -> 3 (chain to a second bus). Solo feeder 0.
        std::array<int, 4> dest{ 2, 2, 3, -1 };
        std::array<bool, 4> solo{ true, false, false, false };
        const auto a = soloAudibleMask(dest, solo);
        CHECK(a[0], "soloed feeder is audible");
        CHECK(a[2] && a[3], "downstream bus chain stays audible so it reaches master");
        CHECK(!a[1], "sibling feeder is NOT pulled in by soloing one feeder");
    }

    static void testSoloNoEdgesIsJustSoloed()
    {
        std::array<int, 4> dest{ -1, -1, -1, -1 };
        std::array<bool, 4> solo{ false, true, false, false };
        const auto a = soloAudibleMask(dest, solo);
        CHECK(a[1], "soloed track audible");
        CHECK(!a[0] && !a[2] && !a[3], "no routing => only the soloed track audible");
    }

    using routing::hasCycle;

    static void testTapSourceBeforeTapper()
    {
        // Track 2 taps track 0 (tapSrc[2] = 0). No mix edges. 0 precedes 2.
        std::array<int, 4> dest{ -1, -1, -1, -1 };
        std::array<int, 4> tap{ -1, -1, 0, -1 };
        const auto order = computeOrder(dest, tap);
        CHECK(posOf(order, 0) < posOf(order, 2), "tapped source 0 before tapper 2");
    }

    static void testMixAndTapCombined()
    {
        // 0 -> bus 1 (mix), and 3 taps 1 (tap). Order: 0 before 1, and 1 before 3.
        std::array<int, 4> dest{ 1, -1, -1, -1 };
        std::array<int, 4> tap{ -1, -1, -1, 1 };
        const auto order = computeOrder(dest, tap);
        CHECK(posOf(order, 0) < posOf(order, 1), "feeder 0 before bus 1");
        CHECK(posOf(order, 1) < posOf(order, 3), "tapped bus 1 before tapper 3");
    }

    static void testCombinedOrderIsPermutation()
    {
        // A cross-class cycle (0 mixes->1, 1 taps->0) still yields a full permutation.
        std::array<int, 3> dest{ 1, -1, -1 };
        std::array<int, 3> tap{ -1, 0, -1 };
        const auto order = computeOrder(dest, tap);
        std::array<bool, 3> seen{ false, false, false };
        for (int i = 0; i < 3; ++i)
        {
            const int v = order[static_cast<std::size_t>(i)];
            CHECK(v >= 0 && v < 3, "order entry in range");
            seen[static_cast<std::size_t>(v)] = true;
        }
        CHECK(seen[0] && seen[1] && seen[2], "combined cycle still yields a permutation");
    }

    static void testHasCycleAcyclic()
    {
        // 0 -> bus 1 (mix); 2 taps 1 (tap). No cycle.
        std::array<int, 4> dest{ 1, -1, -1, -1 };
        std::array<int, 4> tap{ -1, -1, 1, -1 };
        CHECK(!hasCycle(dest, tap), "mix 0->1 + tap 2<-1 is acyclic");
    }

    static void testHasCycleCrossClass()
    {
        // 0 mixes -> 1 (0 before 1); 0 taps 1 (1 before 0). Cross-class 2-cycle.
        std::array<int, 4> dest{ 1, -1, -1, -1 };
        std::array<int, 4> tap{ 1, -1, -1, -1 };
        CHECK(hasCycle(dest, tap), "0->1 mix vs 1->0 tap is a cycle");
    }

    static void testHasCycleTapChain()
    {
        // Tap chain 2 taps 1, 1 taps 0 — acyclic; adding 0 taps 2 closes it.
        std::array<int, 4> destNone{ -1, -1, -1, -1 };
        std::array<int, 4> tapChain{ -1, 0, 1, -1 };
        CHECK(!hasCycle(destNone, tapChain), "tap chain 0<-1<-2 is acyclic");
        std::array<int, 4> tapLoop{ 2, 0, 1, -1 };  // 0 taps 2 closes 0<-1<-2<-0
        CHECK(hasCycle(destNone, tapLoop), "0 taps 2 closes the tap chain");
    }

    void runRoutingGraphTests()
    {
        testOrderNoEdgesIsIdentity();
        testFeederBeforeBus();
        testChainOrder();
        testOrderIsAlwaysPermutation();
        testCycleRefusal();
        testSelfLoopRefused();
        testMasterAndOffAreNotEdges();
        testLongerCycleRefused();
        testSoloBusKeepsFeeders();
        testSoloFeederKeepsBusChain();
        testSoloNoEdgesIsJustSoloed();
        testTapSourceBeforeTapper();
        testMixAndTapCombined();
        testCombinedOrderIsPermutation();
        testHasCycleAcyclic();
        testHasCycleCrossClass();
        testHasCycleTapChain();
    }
}
