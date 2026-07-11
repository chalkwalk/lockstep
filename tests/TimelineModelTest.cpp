// TimelineModelTest — the tape timeline strip model (DESIGN §40.6).
//
// A pure, display-only model: everything normalised to [0,1] across the reel so
// the renderer is a dumb mapper. What it must get right: hide when there is no
// tape, place the cursor and recorded extent as reel fractions, read the position
// in musical time, and carry the markers.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/ui/TimelineModel.h"
#include "../src/machine/TapeMachine.h"

namespace lockstep
{
    void runTimelineModelTests()
    {
        // ── No tape → the strip is inactive ──────────────────────────────────
        {
            EngineHarness h;
            const auto m = buildTimelineModel(h.processor(), 96000.0, 4.0);
            CHECK(! m.active, "no tape → the strip hides");
        }

        // ── A tape with a take → cursor, extent, markers ─────────────────────
        {
            EngineHarness h;
            auto& proc = h.processor();
            proc.setTrackMachine(0, TapeMachine::kMachineId);
            proc.writeParam(0, proc.slotForId(0, "medium_length"), 4.0f);  // 4 s reel
            h.renderBlocks(1);  // apply the length

            // Record a stretch of tape (harness input is silent; the extent still
            // grows). Render ~1 s so the recorded high-water is well past 0.
            proc.tapeApplyVerb(0, 1);  // punch in
            for (int b = 0; b < 90; ++b) h.renderBlocks(1);
            proc.tapeApplyVerb(0, 1);  // punch out
            proc.dropTapeMarker(0);    // drop a mark at the current position

            const double sr = 48000.0, spb = 96000.0, barPpq = 4.0;  // 2 s bar
            const auto m = buildTimelineModel(proc, spb, barPpq);
            CHECK(m.active, "a tape makes the strip active");
            CHECK(m.recordedExtent01 > 0.0f && m.recordedExtent01 <= 1.0f,
                  "the recorded extent is a reel fraction");
            CHECK(m.mediumFull01 == m.recordedExtent01, "the near-full gauge tracks the extent");
            CHECK(m.markers.size() == 1, "the dropped marker appears");
            CHECK(m.markers[0].pos01 >= 0.0f && m.markers[0].pos01 <= 1.0f,
                  "the marker is placed as a reel fraction");
            CHECK(m.position.isNotEmpty(), "the position caption is filled");
            (void)sr;
        }

        // ── The cursor follows the transport position ────────────────────────
        {
            EngineHarness h;
            auto& proc = h.processor();
            proc.setTrackMachine(0, TapeMachine::kMachineId);
            proc.writeParam(0, proc.slotForId(0, "medium_length"), 10.0f);  // 10 s = 480000
            h.renderBlocks(1);

            // Drive the transport forward and read the cursor climb. Recording along
            // the reel advances the tape position with the transport.
            proc.tapeApplyVerb(0, 1);
            for (int b = 0; b < 5; ++b) h.renderBlocks(1);
            const auto early = buildTimelineModel(proc, 96000.0, 4.0);
            for (int b = 0; b < 200; ++b) h.renderBlocks(1);
            const auto later = buildTimelineModel(proc, 96000.0, 4.0);
            CHECK(later.cursor01 >= early.cursor01, "the cursor advances with the transport");
            CHECK(later.recording, "while punched in, the cursor is hot (recording)");
        }

        // ── markerApproach01 (§19): the pure proximity ramp ──────────────────
        {
            const double win = 1000.0;
            CHECK(feq(markerApproach01(0.0, -1.0, win), 0.0f), "no marker ahead → 0");
            CHECK(feq(markerApproach01(500.0, 400.0, win), 0.0f), "playhead past the marker → 0");
            CHECK(feq(markerApproach01(0.0, 2000.0, win), 0.0f), "still outside the window → 0");
            CHECK(feq(markerApproach01(500.0, 500.0, win), 1.0f), "at the marker → 1");
            CHECK(feq(markerApproach01(0.0, 500.0, win), 0.5f), "half a window away → 0.5");
            CHECK(feq(markerApproach01(0.0, 500.0, 0.0), 0.0f), "a zero window never glows");
        }
    }
}
