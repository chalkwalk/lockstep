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

    namespace
    {
        // A deck sub-track's tap is an ordinary tap-fork edge; a four-sub-track deck
        // simply has four of them (DESIGN §40.3). No new edge class, no push side.
        constexpr std::size_t kSubs = 4;

        template <std::size_t N>
        routing::TapEdges<N, kSubs> noTaps()
        {
            routing::TapEdges<N, kSubs> t{};
            for (auto& row : t) row.fill(-1);
            return t;
        }
    }

    // A deck that taps two different tracks runs after BOTH of them, in the same
    // block. That is the whole point of the pull matrix: two sources, one consumer,
    // zero latency.
    void testDeckTapsSeveralSourcesInOneBlock()
    {
        constexpr std::size_t N = 6;
        std::array<int, N> dest{};
        dest.fill(-1);

        auto tap = noTaps<N>();
        tap[1][0] = 4;   // deck on track 1, sub-track 0 taps track 4
        tap[1][1] = 5;   // sub-track 1 taps track 5

        const auto order = routing::computeOrder(dest, tap);
        CHECK(posOf(order, 4) < posOf(order, 1), "a tapped source runs before the deck");
        CHECK(posOf(order, 5) < posOf(order, 1), "and so does the deck's other source");
    }

    // Two sub-tracks reading the SAME track is one constraint, not two. Counting it
    // twice would leave the in-degree stuck above zero and drop the consumer into
    // the cycle fallback — it would still emit, but after everything else.
    void testDuplicateSubTrackTapsCountOnce()
    {
        constexpr std::size_t N = 4;
        std::array<int, N> dest{};
        dest.fill(-1);

        auto tap = noTaps<N>();
        tap[3][0] = 2;
        tap[3][1] = 2;
        tap[3][2] = 2;

        const auto order = routing::computeOrder(dest, tap);
        CHECK(posOf(order, 2) < posOf(order, 3), "the shared source still runs first");
        CHECK(! routing::hasCycle(dest, tap), "and reading it three times is not a cycle");
        // Track 3 has no other constraint, so it lands immediately after 2 — proof
        // the in-degree drained rather than falling through the cycle path.
        CHECK(posOf(order, 3) == posOf(order, 2) + 1, "it drains cleanly, not via the cycle path");
    }

    // A cycle closed through the SECOND sub-track is still a cycle. The refusal
    // must look at the column being edited, not just the first.
    void testCycleThroughASubTrackIsRefused()
    {
        constexpr std::size_t N = 3;
        std::array<int, N> dest{};
        dest.fill(-1);

        auto tap = noTaps<N>();
        tap[0][2] = 1;   // deck 0's third sub-track taps track 1
        tap[1][0] = 0;   // track 1 taps track 0
        CHECK(routing::hasCycle(dest, tap), "a tap cycle through a sub-track is a cycle");

        tap[1][0] = -1;
        CHECK(! routing::hasCycle(dest, tap), "and breaking it clears");
    }

    // A sub-track tap composes with the mix edges exactly as the single tap did.
    void testSubTrackTapCrossesEdgeClasses()
    {
        constexpr std::size_t N = 3;
        std::array<int, N> dest{};
        dest.fill(-1);
        dest[0] = 1;     // track 0 mixes into track 1

        auto tap = noTaps<N>();
        tap[0][1] = 1;   // ...and deck 0's second sub-track taps track 1: a cycle
        CHECK(routing::hasCycle(dest, tap), "mix + sub-track tap close a cycle across classes");
    }

    // The one-tap-per-track callers still get their old answer.
    void testSingleTapWrapperAgrees()
    {
        constexpr std::size_t N = 4;
        std::array<int, N> dest{};
        dest.fill(-1);
        std::array<int, N> narrow{};
        narrow.fill(-1);
        narrow[3] = 1;

        auto wide = noTaps<N>();
        wide[3][0] = 1;

        CHECK(routing::computeOrder(dest, narrow) == routing::computeOrder(dest, wide),
              "the narrow overload is the wide one with K=1");
        CHECK(routing::hasCycle(dest, narrow) == routing::hasCycle(dest, wide),
              "and so is its cycle check");
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

        // 11.3: the tap side is a table now — a deck reads up to four sources.
        testDeckTapsSeveralSourcesInOneBlock();
        testDuplicateSubTrackTapsCountOnce();
        testCycleThroughASubTrackIsRefused();
        testSubTrackTapCrossesEdgeClasses();
        testSingleTapWrapperAgrees();
    }
}
