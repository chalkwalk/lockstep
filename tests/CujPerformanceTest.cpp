// CujPerformanceTest -- Group D: the overlays a player drives mid-set.
//
// D1 (mute) is the most-pressed gesture in the instrument and the one that must be
// exactly reversible. It has two behaviours by design (9.17): stopped, a mute takes
// effect at once; rolling, it ARMS to the track's launch grid and lands at the
// boundary, so a mute never cuts mid-bar. And two independent lanes ride the same
// key -- `Mute+step` is the global mute that follows the track everywhere,
// `Scene+Mute+step` is the per-scene one. They share a surface and must not share a
// bit. The journey walks all four corners of that, and ends on the only assertion a
// player would accept: the sound goes away.
//
// D5 (checkpoints) is 9.4's regression guard. Marks are per-scope stacks; a mark is
// pushed by `Y` under the scope it belongs to, walked back down by `Func+Y`, and a
// restore is itself undoable with `Func+O`. The phantom 9.4 fixed was a key that
// said SNAP for months and pushed nothing, so a depth counter is not enough -- the
// journey asserts the STATE that comes back.
//
// FOUND WHILE WRITING THIS, two dispatch gaps (filed, not fixed -- see ROADMAP 9.36).
//
// (1) UNDO IS UNREACHABLE FROM THE KEYBOARD. KeyBindings declares
// `{VerbClear, kModFunc} -> VerbUndo, "UNDO"`, DESIGN §13.6 makes Func+O the safety
// net under every destructive op, and CommandCore handles the action -- but the O key
// never gets there: `clearVerbTap` routes to the table only when primaryScope() is
// neither None nor Func, and with only Func held it IS Func, so the press falls
// through to "clear the active P-Lock slot" and returns. A row advertising a verb
// dispatch never reaches is the exact disease 9.14 st.5 named. This journey therefore
// asserts that a restore ARMS the undo (it does) and stops there.
//
// (2) DESIGN §13.6 says
// `Func+Y` walks the stack of "whatever scope is currently held", but dispatch
// RESERVES both Snapshot and Restore while any section-suite scope is held
// (`sectionSuiteScopeHeld` -> return, PluginEditor.cpp) on the grounds that
// Func+scope+Y is that scope's secondary. So a Track/Scene/Phrase mark can be pushed
// (`Track+Y`) and has no gesture that pops it -- the mirror image of the phantom 9.4
// fixed on the push side. This journey therefore drives the Song stack, which is
// reachable, and asserts the Track PUSH separately.
//
// See tests/CUJ_CATALOGUE.md.

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

    // Peak RMS across a roll: "did this ever make a sound", robust to whichever block
    // the roll ends on (the amp envelope may have decayed by then).
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

    // Mute + the track's cell. ToggleMute is the synthetic button a step key becomes
    // while Mute is held, and its index is the TRACK: the mute view re-skins the grid
    // to one cell per track.
    void muteTrack(UiDriver& d, int track)
    {
        d.gap();   // two Mute presses inside the double-tap window would LATCH the scope
        d.press(CB::MuteScope);
        d.tap(CB::ToggleMute, track);
        d.release(CB::MuteScope);
    }

    void sceneMuteTrack(UiDriver& d, int track)
    {
        d.gap();
        d.press(CB::SceneScope);
        d.press(CB::MuteScope);
        d.tap(CB::ToggleMute, track);
        d.release(CB::MuteScope);
        d.release(CB::SceneScope);
    }

    // D1 -- Mute.
    void testMute(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/D1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        installRealMachine(d.rig(), 1);

        if (!test::expectReached(d, [](UiDriver& dd) { return !dd.proc().getGlobalMute(0); },
                                 "track 0 starts audible", failed))
            return;

        // --- Stopped: the mute is immediate --------------------------------------
        muteTrack(d, 0);
        check(d.proc().getGlobalMute(0), "stopped, Mute+step mutes that track at once");
        check(!d.proc().getGlobalMute(1), "and leaves every other track alone");

        // The mute view says so, on the cell that stands for the track.
        d.press(CB::MuteScope);
        {
            const auto surf = d.surface();
            check(surf.step[0].base == CellState::MuteMuted,
                  "the muted track reads as muted on the mute view");
            check(surf.step[1].base == CellState::MuteAudible,
                  "its neighbour still reads audible");
        }
        d.release(CB::MuteScope);

        // The gesture is its own inverse.
        muteTrack(d, 0);
        check(!d.proc().getGlobalMute(0), "pressing it again unmutes");

        // --- Scene mute is a SEPARATE lane on the same key ------------------------
        check(!d.proc().getPatternMute(1), "track 1 starts un-scene-muted");
        sceneMuteTrack(d, 1);
        check(d.proc().getPatternMute(1), "Scene+Mute+step scene-mutes that track");
        check(!d.proc().getGlobalMute(1),
              "...without touching its global mute -- two lanes, one key, separate bits");

        // Put it back, so the audio leg below measures track 0 alone.
        sceneMuteTrack(d, 1);
        if (!test::expectReached(d, [](UiDriver& dd) { return !dd.proc().getPatternMute(1); },
                                 "track 1 is audible again", failed))
            return;

        // --- Rolling: the mute ARMS to the launch grid ----------------------------
        // Track 1 is cleared first so the peak across a roll is track 0's voice alone:
        // the thing the mute has to remove.
        d.tap(CB::SelectTrack, 1);
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
        const float loud = peakRmsOverRoll(d, 48);
        if (!test::expectReached(d, [&](UiDriver&) { return loud > 0.0f; },
                                 "the pattern is audible while rolling", failed))
            return;

        muteTrack(d, 0);
        check(d.proc().hasPendingMute(0), "rolling, Mute+step arms the mute to the launch grid");
        check(!d.proc().getGlobalMute(0), "...and does not cut the track mid-bar");

        d.play(8.0);   // cross the boundary
        check(!d.proc().hasPendingMute(0), "the armed mute is consumed at the boundary");
        const float quiet = peakRmsOverRoll(d, 48);
        check(quiet < loud, "and from there the muted track stops making sound");
        check(!d.hasNaN(), "the audio path stays finite through a mute");
    }

    // D5 -- Checkpoints.
    void testCheckpoints(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/D5] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        d.tap(CB::SelectTrack, 0);
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
        d.gap();
        d.step(0).step(4).step(8);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 3; },
                                 "a three-trig pattern to mark", failed))
            return;

        // --- A mark goes on the stack of the scope that was held ------------------
        d.gap();
        d.chord({ CB::TrackScope }, CB::VerbSnapshot);
        check(d.proc().checkpointDepth(CheckpointScope::Track, 0) == 1,
              "Track+SNAP pushes a mark on the TRACK stack");
        check(d.proc().checkpointDepth(CheckpointScope::Song, 0) == 0,
              "...and not on the Song's -- the stacks are per scope, and do not interact");

        // --- The reachable journey: mark the Song, wander, walk back --------------
        // (Bare Y with no scope held IS the Song scope -- DESIGN §13's bare-verb rule.)
        d.gap();
        d.tap(CB::VerbSnapshot);
        check(d.proc().checkpointDepth(CheckpointScope::Song, 0) == 1, "bare SNAP marks the Song");

        d.gap();
        d.step(2).step(6);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 5; },
                                 "the pattern moved on from the mark", failed))
            return;

        // Func+Y, nothing else held: tap = pop one. (Resolved on key-UP -- a hold is
        // "jump to the floor" -- so it has to be a real press/release pair.)
        d.gap();
        d.press(CB::Func);
        d.tap(CB::VerbSnapshot);   // resolveLayer turns Func+Y into Restore
        d.release(CB::Func);

        check(trigCount(d, 0) == 3, "restore brings the marked pattern back");
        {
            const auto& t0 = d.proc().sequence().tracks[0];
            check(t0.steps[0].trig && t0.steps[4].trig && t0.steps[8].trig
                      && !t0.steps[2].trig && !t0.steps[6].trig,
                  "...exactly the marked pattern, not merely the right trig count");
        }
        check(d.proc().checkpointDepth(CheckpointScope::Song, 0) == 0,
              "the popped mark is gone from the stack");

        // --- A restore is itself a destructive op, so it arms the undo stack ------
        // Which is as far as this can be driven: Func+O, the gesture that spends that
        // entry, does not reach the undo action (gap (1) in the header note). When it
        // does, the next line here is `Func+O` -> trigCount back to 5.
        check(d.proc().undoDepth(CheckpointScope::Song, 0) > 0,
              "restoring arms an undo -- overwriting live state is what destructive means");
    }
}   // namespace

void runCujPerformanceTests(int& failed)
{
    testMute(failed);
    testCheckpoints(failed);
}
}   // namespace lockstep
