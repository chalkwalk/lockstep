// CujTrigAuthoringTest -- Group A critical user journeys: authoring the pattern.
//
// A journey drives a whole user task the way a person performs it and asserts the
// first-order outcomes a manual tester would tick off: the durable processor state
// AND the visible affordance on the surface the editor would actually paint. See
// tests/CUJ_CATALOGUE.md for the catalogue this implements against.

#include "UiDriver.h"

#include "../src/ui/MetaBand.h"

#include <algorithm>
#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    int trigCount(UiDriver& d, int track)
    {
        int n = 0;
        const auto& t = d.proc().sequence().tracks[static_cast<std::size_t>(track)];
        for (int s = 0; s < 16; ++s)
            if (t.steps[static_cast<std::size_t>(s)].trig)
                ++n;
        return n;
    }

    // Peak RMS across a roll -- "did the beat ever make a sound", robust to whichever
    // block the roll happens to end on (the amp envelope may have decayed). Same
    // discipline as the bridge self-test.
    float peakRmsOverRoll(UiDriver& d, int blocks)
    {
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            d.runBlocks(1);
            peak = std::max(peak, d.lastRms());
        }
        return peak;
    }

    // A1 -- Two-track drum beat.
    //
    // Focus a track, author a kick on the downbeats, make it polymetric, add a snare
    // on a second track, and prove the pattern both reads right on the surface and
    // actually sounds. Both tracks get a synth (FMMachine) so play() has something to
    // voice; the rig pre-seeds trigs, so the journey clears each track first via the
    // real Track+CLEAR gesture -- a "make a beat" journey starts from a blank kit.
    void testTwoTrackDrumBeat(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/A1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);   // track 0: kick voice
        installRealMachine(d.rig(), 1);   // track 1: snare voice

        // --- Track 0: clear, then author kicks on 0/4/8/12 -------------------------
        d.tap(CB::SelectTrack, 0);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.activeTrack() == 0; },
                                 "focus is on track 0", failed))
            return;

        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);   // CLEAR TRACK (confirmed)
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 0; },
                                 "track 0 starts from a blank kit", failed))
            return;

        d.step(0).step(4).step(8).step(12);
        check(trigCount(d, 0) == 4, "four kicks authored on track 0");
        {
            const auto& t0 = d.proc().sequence().tracks[0];
            check(t0.steps[0].trig && t0.steps[4].trig && t0.steps[8].trig && t0.steps[12].trig,
                  "the kicks land on the downbeats");
        }

        // The live surface shows those cells as certain trigs, the rest empty.
        {
            const auto surf = d.surface();
            check(surf.step[0].base == CellState::StepTrigCertain
                      && surf.step[4].base == CellState::StepTrigCertain,
                  "the authored steps read as certain trigs on the surface");
            check(surf.step[1].base == CellState::StepEmpty
                      && surf.step[2].base == CellState::StepEmpty,
                  "the off-beats read empty");
        }

        // --- Polymeter: Func+Phrase+step sets the focused track's length (§34.4) ---
        // step index 6 -> length 7. (The catalogue's earlier "Track+TRIG" note was
        // wrong: length is the phrase-length authoring gesture, not a TRIG field.)
        d.chord({ CB::Func, CB::PhraseScope }, CB::Step, 6);
        check(d.proc().sequence().tracks[0].length == 7,
              "Func+Phrase+step makes track 0 seven steps long (polymeter)");

        // --- Track 1: select, clear, author a backbeat snare on 4/12 ---------------
        d.tap(CB::SelectTrack, 1);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.activeTrack() == 1; },
                                 "focus moved to track 1", failed))
            return;
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
        d.step(4).step(12);
        check(trigCount(d, 1) == 2, "two snare hits authored on track 1");
        {
            const auto& t1 = d.proc().sequence().tracks[1];
            check(t1.steps[4].trig && t1.steps[12].trig, "the snares land on the backbeats");
        }
        {
            const auto surf = d.surface();   // now showing track 1
            check(surf.step[4].base == CellState::StepTrigCertain,
                  "track 1's snare reads as a certain trig on the surface");
        }

        // --- Liveness: the two-track beat actually sounds --------------------------
        const float peak = peakRmsOverRoll(d, 400);   // a full bar -- see CujPerformanceTest
        check(peak > 0.0f, "playing the beat produces audible output");
        check(!d.hasNaN(), "the audio path stays finite");
    }
    // A2 -- Trig conditions.
    //
    // The COND band is where a pattern stops being a loop and starts being a system:
    // a step that fires 50% of the time, or once every four passes. It is reached by
    // holding the step and pressing TRIG -- the section key promotes to the per-step
    // condition page -- and the same page with NO step held edits the track's base
    // condition. That fork is the whole journey: same band, same field, two targets.
    void testTrigConditions(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/A2] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        d.tap(CB::SelectTrack, 0);
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
        d.gap();
        d.step(0).step(4);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 2; },
                                 "two trigs to condition", failed))
            return;

        const auto& t0 = d.proc().sequence().tracks[0];
        check(t0.baseCond.isTrivial(), "the track starts unconditional");

        // --- Held step + TRIG promotes the section key to the step's COND page ----
        d.press(CB::Step, 0);
        d.tap(CB::Section, IMachine::kTrigSecIdx);
        if (!test::expectReached(d, [](UiDriver& dd) { return resolveMetaBand(dd.ui()) == MetaBand::Cond; },
                                 "held step + TRIG opens the COND band", failed))
        {
            d.release(CB::Step, 0);
            return;
        }

        // The band changed, so the MZ needs a frame before it is driven: the slider
        // ranges come from the page it is SHOWING, and writing 50 into a slider still
        // carrying the old page's range clamps it to that range's top (measured: 10).
        DispatchProbe::frame(d.editor());
        const int probField = DispatchProbe::mzSlotOffset(d.editor()) + 0;   // field 0 = Prob
        d.setParam(probField, 50.0f);
        check(t0.steps[0].condition.probabilityPercent == 50,
              "the condition lands on the held step");
        check(t0.baseCond.isTrivial(),
              "...and the track's base condition is untouched -- same rule as a P-Lock");
        d.release(CB::Step, 0);

        // --- The grid reads the difference ---------------------------------------
        // A conditional trig must not look like a certain one: it is the difference
        // between "this fires" and "this might".
        {
            const auto surf = d.surface();
            check(surf.step[0].base == CellState::StepTrigProbable,
                  "the conditioned step reads as probable on the grid");
            check(surf.step[4].base == CellState::StepTrigCertain,
                  "its unconditioned neighbour still reads certain");
        }

        // --- The same band with no step held edits the TRACK's condition ----------
        d.setParam(probField, 25.0f);
        check(t0.baseCond.probabilityPercent == 25, "with no step held the write is the base");
        check(t0.steps[0].condition.probabilityPercent == 50,
              "...and the step keeps its own, stronger condition");
        {
            const auto surf = d.surface();
            check(surf.step[4].base == CellState::StepTrigProbable,
                  "the base condition makes every unconditioned step read probable too");
        }

        // --- Iteration: fire once every N passes ---------------------------------
        d.press(CB::Step, 4);
        DispatchProbe::frame(d.editor());
        d.setParam(DispatchProbe::mzSlotOffset(d.editor()) + 2, 4.0f);   // field 2 = iter denominator
        check(t0.steps[4].condition.iterDenominator == 4,
              "the iteration denominator lands on the held step");
        d.release(CB::Step, 4);
    }

    // A3 -- Step editing.
    //
    // The step-hold is the instrument's other hand: it turns the grid into an
    // inspector and re-points the nav keys at the step itself. This journey covers
    // the two edits that MOVE a trig rather than change its value -- the bubble-swap
    // onto a neighbour, and the microtiming nudge that leaves it where it is but
    // early or late -- plus the quantize that undoes the nudge.
    void testStepEditing(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/A3] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        d.tap(CB::SelectTrack, 0);
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
        d.gap();
        d.step(4);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 1; },
                                 "one trig to push around", failed))
            return;

        const auto& t0 = d.proc().sequence().tracks[0];

        // --- Holding a step makes the surface its inspector -----------------------
        d.press(CB::Step, 4);
        {
            const auto surf = d.surface();
            check(surf.activeLayer == SurfaceLayer::StepInspector,
                  "holding a step re-skins the grid to the step inspector");
        }

        // --- Bare nav MOVES the step to its neighbour (bubble-swap) ---------------
        d.tap(CB::NavRight);
        check(!t0.steps[4].trig && t0.steps[5].trig, "held step + right carries the trig over");
        d.tap(CB::NavLeft);
        check(t0.steps[4].trig && !t0.steps[5].trig, "and left carries it back");

        // --- Func + nav nudges it off the grid instead (microtiming) --------------
        // TWO Func presses, deliberately. The FIRST Func over a held step is the latch
        // (W7): it frees the finger and is CONSUMED -- "no funcHeld", says the branch
        // itself -- so a player who presses Func once and then right gets the MOVE
        // above, not a nudge. The second press is the one that qualifies the nav.
        check(t0.steps[4].microOffset == 0.0f, "the step starts dead on the grid");
        d.tap(CB::Func);            // latch (consumed)
        d.press(CB::Func);          // now it registers as Func held
        d.tap(CB::NavRight);
        d.release(CB::Func);
        check(t0.steps[4].microOffset > 0.0f, "held step + Func+right pushes it late");
        check(t0.steps[4].trig, "...and the trig stays on its own step -- a nudge is not a move");

        // --- QUANT puts it back ---------------------------------------------------
        d.tap(CB::VerbConfirm);   // P under a held step = QUANT
        check(t0.steps[4].microOffset == 0.0f, "P quantizes the held step back onto the grid");
        d.release(CB::Step, 4);
    }
}   // namespace

void runCujTrigAuthoringTests(int& failed)
{
    testTwoTrackDrumBeat(failed);
    testTrigConditions(failed);
    testStepEditing(failed);
}
}   // namespace lockstep
