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
        // Harness runs 48 kHz / 120 BPM; barPpq 4 → samplesPerBar 96000 = a 2 s bar.
        const double kSr = 48000.0, kSpb = 96000.0, kBarPpq = 4.0;

        // ── No tape → strip is inactive but the ruler + caption still live ───
        {
            EngineHarness h;
            h.renderBlocks(4);  // advance the transport
            const auto m = buildTimelineModel(h.processor(), kSpb, kBarPpq, kSr);
            CHECK(! m.active, "no tape → the strip is inactive (no extent/markers)");
            CHECK(m.domainBars == 32, "no tape → the domain floors at 32 bars");
            CHECK(m.position.isNotEmpty(), "no tape → the bars.beats caption is still driven");
            CHECK(feq(static_cast<float>(m.secondsPerBar), 2.0f),
                  "secondsPerBar = samplesPerBar / sampleRate (2 s at 120 BPM 4/4)");
            CHECK(m.wallTime.isNotEmpty(), "no tape → the wall-clock caption is still driven");
            CHECK(m.tapeEnds.empty(), "no tape → no end lugs");
        }

        // ── A tape with a take → active, extent, markers, one focused end lug ─
        {
            EngineHarness h;
            auto& proc = h.processor();
            proc.setTrackMachine(0, TapeMachine::kMachineId);
            proc.writeParam(0, proc.slotForId(0, "medium_length"), 20.0f);  // 20 s reel
            h.renderBlocks(1);

            proc.tapeApplyVerb(0, 1);  // punch in
            for (int b = 0; b < 90; ++b) h.renderBlocks(1);
            proc.tapeApplyVerb(0, 1);  // punch out
            proc.dropTapeMarker(0);    // drop a mark at the current position

            const auto m = buildTimelineModel(proc, kSpb, kBarPpq, kSr);
            CHECK(m.active, "a tape makes the strip active");
            CHECK(m.domainBars == 32, "a short tape keeps the 32-bar floor");
            CHECK(m.recordedExtent01 > 0.0f && m.recordedExtent01 <= 1.0f,
                  "the recorded extent is a bar-domain fraction");
            CHECK(m.markers.size() == 1, "the dropped marker appears");
            CHECK(m.markers[0].pos01 >= 0.0f && m.markers[0].pos01 <= 1.0f,
                  "the marker is placed as a bar-domain fraction");
            CHECK(m.tapeEnds.size() == 1, "one tape → one end lug");
            CHECK(m.tapeEnds[0].focused, "the sole tape is the chosen one (focused)");
            CHECK(m.tapeEnds[0].end01 > 0.0f, "the end lug sits past the origin");
        }

        // ── The cursor follows the transport position ────────────────────────
        {
            EngineHarness h;
            auto& proc = h.processor();
            proc.setTrackMachine(0, TapeMachine::kMachineId);
            proc.writeParam(0, proc.slotForId(0, "medium_length"), 60.0f);
            h.renderBlocks(1);

            proc.tapeApplyVerb(0, 1);
            for (int b = 0; b < 5; ++b) h.renderBlocks(1);
            const auto early = buildTimelineModel(proc, kSpb, kBarPpq, kSr);
            for (int b = 0; b < 200; ++b) h.renderBlocks(1);
            const auto later = buildTimelineModel(proc, kSpb, kBarPpq, kSr);
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
