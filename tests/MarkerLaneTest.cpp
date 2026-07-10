// MarkerLaneTest — dc::MarkerLane (DESIGN §40.4).
//
// Markers are dumb navigation points. The lane's whole job is: drop (merging a
// near-duplicate rather than stacking), delete by a stable ordinal, stay sorted,
// and answer nearest / next / prev for cueing. It fires nothing — there is no
// method here that could, which is the fence-#1 line expressed as an absence.

#include "TestHarness.h"
#include "../src/deckcore/MarkerLane.h"

namespace lockstep
{
    void runMarkerLaneTests()
    {
        // ── Drop keeps the lane sorted; ordinals are stable drop order ───────
        {
            dc::MarkerLane lane;
            const int a = lane.drop(1000.0);   // ordinal 0
            const int b = lane.drop(200.0);    // ordinal 1, but sorts before a
            const int c = lane.drop(500.0);    // ordinal 2

            CHECK(a == 0 && b == 1 && c == 2, "ordinals are the drop order");
            CHECK(lane.count() == 3, "three distinct positions, three markers");
            CHECK(lane.at(0).positionSamples < lane.at(1).positionSamples
                      && lane.at(1).positionSamples < lane.at(2).positionSamples,
                  "the lane is kept sorted by position");
            // The ordinal is stable across the sort — index != ordinal.
            CHECK(lane.at(0).ordinal == b, "the earliest position is the second drop");
        }

        // ── A near-duplicate drop merges rather than stacks ──────────────────
        {
            dc::MarkerLane lane;
            const int first = lane.drop(1000.0, 7);
            const int again = lane.drop(1000.4, 9);  // within mergeEps of 1000
            CHECK(lane.count() == 1, "two markers a fraction apart are one place");
            CHECK(again == first, "the merge returns the original ordinal");
            CHECK(lane.at(0).labelId == 9, "and updates the label to the newer drop");
        }

        // ── Delete by ordinal; ordinals are never reused ─────────────────────
        {
            dc::MarkerLane lane;
            lane.drop(100.0);        // ord 0
            const int mid = lane.drop(200.0);  // ord 1
            lane.drop(300.0);        // ord 2
            CHECK(lane.remove(mid), "remove reports it deleted one");
            CHECK(lane.count() == 2, "the lane shrank");
            CHECK(! lane.remove(mid), "removing it again does nothing");

            const int fresh = lane.drop(250.0);  // ord 3, NOT 1
            CHECK(fresh == 3, "a new drop never reuses a deleted ordinal");
        }

        // ── nearest / next / prev for cueing ─────────────────────────────────
        {
            dc::MarkerLane lane;
            lane.drop(1000.0);
            lane.drop(2000.0);
            lane.drop(3000.0);

            CHECK(lane.nearest(1900.0) == 1, "nearest picks the closest either side");
            CHECK(lane.nearest(2600.0) == 2, "...on the other side too");

            CHECK(lane.next(1500.0) == 1, "next = the first marker strictly after");
            CHECK(lane.next(2000.0) == 2, "strictly after — a marker on the spot does not count");
            CHECK(lane.next(3500.0) == -1, "no marker after the last");

            CHECK(lane.prev(2500.0) == 1, "prev = the first marker strictly before");
            CHECK(lane.prev(1000.0) == -1, "nothing before the first");

            CHECK(dc::MarkerLane{}.nearest(0.0) == -1, "an empty lane has no nearest");
        }

        // ── Load restores ordinals so references survive a reload ────────────
        {
            dc::MarkerLane lane;
            lane.load({ { 500.0, 5, 0 }, { 100.0, 2, 3 } });
            CHECK(lane.count() == 2, "loaded two markers");
            CHECK(lane.at(0).positionSamples == 100.0, "load sorts by position");
            CHECK(lane.at(0).ordinal == 2, "and keeps the saved ordinals");
            const int fresh = lane.drop(900.0);
            CHECK(fresh == 6, "the next drop continues past the highest loaded ordinal");
        }
    }
}
