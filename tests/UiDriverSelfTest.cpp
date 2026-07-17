// UiDriverSelfTest -- the harness has to be trustworthy before anything written
// with it is worth believing.
//
// Three things are proved here:
//  1. Virtual time reproduces gesture semantics, INCLUDING at the boundary. A
//     wall-clock harness can only test "definitely inside the window" (sleep 0)
//     and "definitely outside" (sleep past it); it cannot assert kDoubleTapMs-10
//     latches and +10 does not, because it cannot hit +/-10 ms reliably. That
//     boundary is precisely where a regression in the detector would land.
//  2. A real gesture scenario (the cue console's long-press open) is expressible
//     with zero sleeps and gives the same answer as the golden's hand-rolled
//     version.
//  3. The driver's ergonomics claim is real: a scenario the golden spells out in
//     ~15 lines of event pairs reads as a handful of verbs.

#include "UiDriver.h"

#include "../src/PluginProcessor.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    void testDoubleTapWindow(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [UiDriver/doubleTap] %s\n", what); ++failed; }
        };

        // The plain verb: a double-tap latches the scope (3.10 virtual hold).
        {
            UiDriver d;
            check(!d.ui().latch.track, "baseline: Track is not latched");
            d.doubleTap(CB::TrackScope);
            check(d.ui().latch.track, "double-tap Track latches the scope");
        }

        // Two deliberate presses, spaced past the window, must NOT latch -- this is
        // the artifact that fooled the golden's first run (a microsecond-fast script
        // read as a double-tap and silently latched).
        {
            UiDriver d;
            d.tap(CB::TrackScope).gap().tap(CB::TrackScope);
            check(!d.ui().latch.track, "two taps spaced past the window do not latch");
        }

        // The boundary, from both sides. Only virtual time can ask this.
        {
            UiDriver d;
            d.tap(CB::TrackScope);
            d.advanceMs(GestureRecognizer::kDoubleTapMs - 10.0);
            d.tap(CB::TrackScope);
            check(d.ui().latch.track, "second tap just INSIDE the window latches");
        }
        {
            UiDriver d;
            d.tap(CB::TrackScope);
            d.advanceMs(GestureRecognizer::kDoubleTapMs + 10.0);
            d.tap(CB::TrackScope);
            check(!d.ui().latch.track, "second tap just OUTSIDE the window does not latch");
        }
    }

    // The golden's CueConsole scenario, re-expressed. Same assertions, no sleep:
    // the original spent kLongPressMs+60 of real time to ask one question.
    void testCueConsoleLongPress(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [UiDriver/cueConsole] %s\n", what); ++failed; }
        };

        UiDriver d;
        d.press(CB::Func).press(CB::TapTempo);           // Func+3 = momentary Cue scope
        check(d.ui().cueHeld, "Func+3 enters the momentary Cue scope");

        d.press(CB::Section, LockstepProcessor::kAmpSecIdx);
        check(d.ui().overlay != Overlay::Cue, "a short AMP press does not open the console");

        d.advanceMs(GestureRecognizer::kLongPressMs + 60.0);
        d.release(CB::Section, LockstepProcessor::kAmpSecIdx);
        check(d.ui().overlay == Overlay::Cue, "Cue+hold(AMP) opens the console");
        check(d.ui().cueParamPage, "console opens straight to the param (mixer) page");
    }

    // Ergonomics, demonstrated rather than asserted: the golden's
    // "copy track 0 -> paste onto track 5" scenario, which is ~15 lines of
    // hand-built ControllerEvent pairs plus a sleep, as five verbs.
    void testCopyPasteReadsAsProse(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [UiDriver/copyPaste] %s\n", what); ++failed; }
        };

        UiDriver d;
        d.chord({ CB::TrackScope }, CB::VerbRecord);   // COPY TRACK (track 0, seeded with trigs)
        check(d.ui().latch.track == false, "the copy chord does not latch Track");

        d.gap().tap(CB::SelectTrack, 5);
        check(d.activeTrack() == 5, "focus moved to track 5");

        d.gap().chord({ CB::TrackScope }, CB::VerbPlay);   // PASTE TRACK

        // The rig seeds trigs on steps 0,4,8,12 of tracks 0-3; track 5 starts empty,
        // so the paste is visible as its trigs arriving.
        const auto& t5 = d.proc().sequence().tracks[5];
        int trigs = 0;
        for (int s = 0; s < 16; ++s)
            if (t5.steps[static_cast<std::size_t>(s)].trig)
                ++trigs;
        check(trigs == 4, "paste landed track 0's four trigs on track 5");
    }
    // surface() must reflect the LIVE editor, or an interaction test would be
    // asserting on a stub that happens to sit nearby. Held keys are the sharpest
    // proof available: the model reads them from the same PressTracker the real
    // paint path passes, so a synthetic press has to show up as a pressed cell.
    void testSurfaceReflectsLiveState(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [UiDriver/surface] %s\n", what); ++failed; }
        };

        UiDriver d;
        check(!d.surface().step[0].pressed, "baseline: step 0's cell is not pressed");

        // Hold 'D' (step 0's key) without releasing. The model reads presses from the
        // same PressTracker the real paint path passes it, so the synthetic key has
        // to appear -- on exactly step 0's cell and no other.
        d.keyDown('D');
        check(d.surface().step[0].pressed, "a held key shows as its own cell, pressed");
        check(!d.surface().step[1].pressed, "and not as its neighbour's");

        d.keyUp('D');
        check(!d.surface().step[0].pressed, "releasing the key un-presses the cell");
    }
}   // namespace

void runUiDriverSelfTests(int& failed)
{
    testDoubleTapWindow(failed);
    testCueConsoleLongPress(failed);
    testCopyPasteReadsAsProse(failed);
    testSurfaceReflectsLiveState(failed);
}
}   // namespace lockstep
