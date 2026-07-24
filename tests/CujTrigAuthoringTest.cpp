// CujTrigAuthoringTest -- Group A critical user journeys: authoring the pattern.
//
// A journey drives a whole user task the way a person performs it and asserts the
// first-order outcomes a manual tester would tick off: the durable processor state
// AND the visible affordance on the surface the editor would actually paint. See
// tests/CUJ_CATALOGUE.md for the catalogue this implements against.

#include "UiDriver.h"

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
        const float peak = peakRmsOverRoll(d, 48);
        check(peak > 0.0f, "playing the beat produces audible output");
        check(!d.hasNaN(), "the audio path stays finite");
    }
}   // namespace

void runCujTrigAuthoringTests(int& failed)
{
    testTwoTrackDrumBeat(failed);
}
}   // namespace lockstep
