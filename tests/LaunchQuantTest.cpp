// LaunchQuantTest — headless tests for src/core/LaunchQuant.h.
// The single launch-quantize authority (PRINCIPLES §25, DESIGN §4.8): grid
// periods, next-boundary maths, PhraseEnd degrade, per-track resolution, and
// the legacy pre-v25 int<->enum mapping.

#include "TestHarness.h"
#include "../src/core/LaunchQuant.h"
#include <cmath>

namespace lockstep
{
    static bool deq(double a, double b, double eps = 1e-9)
    {
        return std::abs(a - b) < eps;
    }

    // -----------------------------------------------------------------------
    // gridPpq — periods across meters
    static void testGridPeriods()
    {
        const TimeSig f44{4, 4};   // barPpq 4.0, beat 1.0
        CHECK(deq(gridPpq(LaunchQuant::Instant, f44), 0.0), "4/4 Instant period 0");
        CHECK(deq(gridPpq(LaunchQuant::Beat, f44), 1.0), "4/4 Beat period 1.0");
        CHECK(deq(gridPpq(LaunchQuant::Bar, f44), 4.0), "4/4 Bar period 4.0");
        CHECK(deq(gridPpq(LaunchQuant::Bars2, f44), 8.0), "4/4 Bars2 period 8.0");
        CHECK(deq(gridPpq(LaunchQuant::Bars4, f44), 16.0), "4/4 Bars4 period 16.0");
        CHECK(deq(gridPpq(LaunchQuant::Bars8, f44), 32.0), "4/4 Bars8 period 32.0");

        const TimeSig f78{7, 8};   // barPpq 7 * (4/8) = 3.5, beat 0.5
        CHECK(deq(gridPpq(LaunchQuant::Beat, f78), 0.5), "7/8 Beat period 0.5");
        CHECK(deq(gridPpq(LaunchQuant::Bar, f78), 3.5), "7/8 Bar period 3.5");
        CHECK(deq(gridPpq(LaunchQuant::Bars2, f78), 7.0), "7/8 Bars2 period 7.0");

        const TimeSig f128{12, 8}; // barPpq 12 * 0.5 = 6.0, beat 0.5
        CHECK(deq(gridPpq(LaunchQuant::Bar, f128), 6.0), "12/8 Bar period 6.0");
        CHECK(deq(gridPpq(LaunchQuant::Beat, f128), 0.5), "12/8 Beat period 0.5");
    }

    // -----------------------------------------------------------------------
    // nextBoundaryPpq — deferral + on-boundary + Instant
    static void testNextBoundary()
    {
        const TimeSig f44{4, 4};
        // mid-bar → next bar
        CHECK(deq(nextBoundaryPpq(2.3, LaunchQuant::Bar, f44), 4.0), "Bar boundary after 2.3 = 4.0");
        // exactly on a boundary → fire at that boundary (blockStart itself)
        CHECK(deq(nextBoundaryPpq(4.0, LaunchQuant::Bar, f44), 4.0), "on-boundary blockStart stays 4.0");
        CHECK(deq(nextBoundaryPpq(8.0, LaunchQuant::Bars2, f44), 8.0), "on-boundary Bars2 stays 8.0");
        // Beat grid lands mid-bar
        CHECK(deq(nextBoundaryPpq(2.3, LaunchQuant::Beat, f44), 3.0), "Beat boundary after 2.3 = 3.0");
        // Instant → fire now
        CHECK(deq(nextBoundaryPpq(2.3, LaunchQuant::Instant, f44), 2.3), "Instant fires now");
    }

    // -----------------------------------------------------------------------
    // PhraseEnd + cycle==0 degrade
    static void testPhraseEnd()
    {
        const TimeSig f44{4, 4};
        // phrase cycle 3.5 ppq (e.g. length-7 track at 1/16 divider)
        CHECK(deq(gridPpq(LaunchQuant::PhraseEnd, f44, 3.5), 3.5), "PhraseEnd uses supplied cycle");
        CHECK(deq(nextBoundaryPpq(1.0, LaunchQuant::PhraseEnd, f44, 3.5), 3.5),
              "PhraseEnd boundary after 1.0 with cycle 3.5 = 3.5");
        // cycle 0 → degrade to instant
        CHECK(deq(gridPpq(LaunchQuant::PhraseEnd, f44, 0.0), 0.0), "PhraseEnd cycle 0 degrades to 0");
        CHECK(deq(nextBoundaryPpq(1.7, LaunchQuant::PhraseEnd, f44, 0.0), 1.7),
              "PhraseEnd cycle 0 fires now");
    }

    // -----------------------------------------------------------------------
    // boundaryInBlock — in-block detection matches legacy (boundary < blockEnd)
    static void testBoundaryInBlock()
    {
        const TimeSig f44{4, 4};
        double b = -1.0;
        // block [3.9, 4.1) straddles the bar at 4.0 → hit
        CHECK(boundaryInBlock(3.9, 4.1, LaunchQuant::Bar, f44, b) && deq(b, 4.0),
              "block straddling bar boundary hits at 4.0");
        // block [4.1, 4.5) after the boundary → miss (next boundary 8.0 out of block)
        CHECK(!boundaryInBlock(4.1, 4.5, LaunchQuant::Bar, f44, b),
              "block past bar boundary misses");
        // Instant → always hits at blockStart
        CHECK(boundaryInBlock(4.1, 4.5, LaunchQuant::Instant, f44, b) && deq(b, 4.1),
              "Instant always hits at blockStart");
    }

    // -----------------------------------------------------------------------
    // resolveTrackQuant — override vs follow-global
    static void testResolveTrack()
    {
        CHECK(resolveTrackQuant(LaunchQuant::Bar, kFollowGlobal) == LaunchQuant::Bar,
              "kFollowGlobal inherits Set grid");
        CHECK(resolveTrackQuant(LaunchQuant::Bar, static_cast<int>(LaunchQuant::Beat)) == LaunchQuant::Beat,
              "concrete override wins over Set grid");
        CHECK(resolveTrackQuant(LaunchQuant::Bar, static_cast<int>(LaunchQuant::PhraseEnd)) == LaunchQuant::PhraseEnd,
              "PhraseEnd override reachable per-track");
    }

    // -----------------------------------------------------------------------
    // Legacy pre-v25 bar-count <-> enum mapping (behaviour-identical serializer)
    static void testLegacyMapping()
    {
        CHECK(legacyBarsToQuant(1) == LaunchQuant::Bar, "legacy 1 -> Bar");
        CHECK(legacyBarsToQuant(2) == LaunchQuant::Bars2, "legacy 2 -> Bars2");
        CHECK(legacyBarsToQuant(4) == LaunchQuant::Bars4, "legacy 4 -> Bars4");
        CHECK(legacyBarsToQuant(8) == LaunchQuant::Bars8, "legacy 8 -> Bars8");
        CHECK(legacyBarsToQuant(3) == LaunchQuant::Bar, "legacy unexpected -> Bar");

        CHECK(quantToLegacyBars(LaunchQuant::Bar) == 1, "Bar -> legacy 1");
        CHECK(quantToLegacyBars(LaunchQuant::Bars2) == 2, "Bars2 -> legacy 2");
        CHECK(quantToLegacyBars(LaunchQuant::Bars4) == 4, "Bars4 -> legacy 4");
        CHECK(quantToLegacyBars(LaunchQuant::Bars8) == 8, "Bars8 -> legacy 8");
        CHECK(quantToLegacyBars(LaunchQuant::Beat) == 1, "Beat -> legacy 1 (nearest fit)");
        // bar-family round-trips exactly
        for (auto q : {LaunchQuant::Bar, LaunchQuant::Bars2, LaunchQuant::Bars4, LaunchQuant::Bars8})
            CHECK(legacyBarsToQuant(quantToLegacyBars(q)) == q, "bar-family legacy round-trip");
    }

    void runLaunchQuantTests()
    {
        testGridPeriods();
        testNextBoundary();
        testPhraseEnd();
        testBoundaryInBlock();
        testResolveTrack();
        testLegacyMapping();
    }

} // namespace lockstep
