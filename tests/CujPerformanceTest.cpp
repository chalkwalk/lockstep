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
// This journey found the two gaps that became ROADMAP 9.37, and now proves the
// rulings that closed them: a per-scope mark could be pushed but never popped (the
// restore was reserved under any held scope), and UNDO could not be reached at all
// (`clearVerbTap` routed to the table only when primaryScope() was neither None nor
// Func -- and with only Func held it IS Func). Both gestures are driven here end to
// end, which is the acceptance test 9.37 set for itself: reachability, because
// reachability is precisely what nothing checked.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include <algorithm>
#include <cmath>
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

    // A full 16-step bar's worth of blocks at 48k/256, 120 bpm (~5 ms a block, ~2 s a
    // bar). Any shorter roll can pass BETWEEN two trigs and report silence from a
    // perfectly loud pattern -- which makes a "did the mute work" comparison
    // meaningless in the direction that matters. Measured, after D2 failed this way.
    constexpr int kBarBlocks = 400;

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

    // A param write is applied on the AUDIO thread (drainEngineCmds at the top of
    // processBlock), so engine state must be read after a block has run.
    void runBlock(UiDriver& d)
    {
        const int chans = juce::jmax(2, d.proc().getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(chans, 512);
        buf.clear();
        juce::MidiBuffer midi;
        d.proc().processBlock(buf, midi);
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
        const float loud = peakRmsOverRoll(d, kBarBlocks);
        if (!test::expectReached(d, [&](UiDriver&) { return loud > 0.0f; },
                                 "the pattern is audible while rolling", failed))
            return;

        muteTrack(d, 0);
        check(d.proc().hasPendingMute(0), "rolling, Mute+step arms the mute to the launch grid");
        check(!d.proc().getGlobalMute(0), "...and does not cut the track mid-bar");

        d.play(8.0);   // cross the boundary
        check(!d.proc().hasPendingMute(0), "the armed mute is consumed at the boundary");
        const float quiet = peakRmsOverRoll(d, kBarBlocks);
        check(quiet < loud, "and from there the muted track stops making sound");
        check(!d.hasNaN(), "the audio path stays finite through a mute");
    }

    // D2 -- Fill.
    //
    // Fill is the "and now the drums do the thing" hand: steps marked fill-only sit
    // dim and silent until the Fill key is down, then join the pattern. The state is
    // per-step (Inherit -> On -> Off, cycled by Fill+step) and the behaviour is
    // momentary, so the honest assertion is a census of SOUND with the key down
    // versus up -- the pattern here is nothing BUT fill steps, so silence and
    // not-silence are the two answers.
    void testFill(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/D2] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        d.tap(CB::SelectTrack, 0);
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 0; },
                                 "an empty track, so only fill steps can sound", failed))
            return;

        // --- Fill+step marks the step fill-only -----------------------------------
        const auto& t0 = d.proc().sequence().tracks[0];
        d.gap();
        d.press(CB::FillScope);
        d.tap(CB::Step, 0);
        d.tap(CB::Step, 8);
        {
            const auto surf = d.surface();
            check(surf.step[0].base == CellState::StepFillAdd,
                  "a fill-only step reads as a fill add while Fill is held");
        }
        d.release(CB::FillScope);

        check(t0.steps[0].fillTrigState == FillTrigState::On, "Fill+step marks the step fill-on");
        check(t0.steps[8].fillTrigState == FillTrigState::On, "...and the second one too");
        check(!t0.steps[0].trig, "...without authoring an ordinary trig");

        // --- Silent at rest, audible under the key --------------------------------
        // A full bar of blocks, not a handful: at 48k/256 a block is ~5 ms and a
        // 16-step bar at 120 bpm is ~2 s, so a 48-block roll can pass between two fill
        // steps and call the instrument silent. Long enough to be sure it came round.
        const float quiet = peakRmsOverRoll(d, kBarBlocks);
        check(quiet == 0.0f, "with Fill up, the fill-only steps stay silent");

        d.gap();
        d.press(CB::FillScope);
        const float loud = peakRmsOverRoll(d, kBarBlocks);
        d.release(CB::FillScope);
        check(loud > quiet, "holding Fill brings them in");
        check(!d.hasNaN(), "the audio path stays finite under fill");

        // --- The cycle continues to Off -------------------------------------------
        d.gap();
        d.press(CB::FillScope);
        d.tap(CB::Step, 0);
        d.release(CB::FillScope);
        check(t0.steps[0].fillTrigState == FillTrigState::Off,
              "pressing again cycles the step to fill-off");
    }

    // D3 -- Morph.
    //
    // The crossfader is the one control that is not a value but a *blend*: every
    // parameter can hold two poles, and the fader decides how much of each you hear.
    // The journey is the authoring loop a player uses -- capture a pole, capture the
    // other, sweep between them -- plus the two verbs that end it: BAKE (freeze what
    // you hear into the kit) and ERASE (throw the map away).
    void testMorph(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/D3] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        DispatchProbe::frame(d.editor());
        d.tap(CB::Section, IMachine::kSrcSecIdx);
        DispatchProbe::frame(d.editor());

        const int slot = DispatchProbe::mzSlotOffset(d.editor()) + 4;   // fm_fine_1
        auto baseOf = [&] {
            return d.proc().sequence().tracks[0].baseParams[static_cast<std::size_t>(slot)];
        };

        check(!d.proc().morphWidgetInfo(0, slot).exists, "no morph map to begin with");

        // --- Capture pole A, then pole B ------------------------------------------
        // Morph+Nav picks which pole the next write lands in (Morph+up = A, down = B),
        // which is what makes the fader inert until the two poles differ.
        // The pole qualifier is MOMENTARY -- the nav key-up clears it -- so the nav key
        // is HELD across the write, not tapped before it. And a frame after every
        // qualifier change, because the MZ mirrors morphHeld/morphQualifier into its own
        // members when the frame is built: drive a slider before that and the write
        // lands somewhere else entirely, looking exactly like morph not working.
        auto writePole = [&](CB navKey, float value) {
            d.press(navKey);
            DispatchProbe::frame(d.editor());
            d.setParam(slot, value);
            runBlock(d);
            d.release(navKey);
            DispatchProbe::frame(d.editor());
        };

        d.gap();
        d.press(CB::MorphScope);
        DispatchProbe::frame(d.editor());
        writePole(CB::NavUp, 10.0f);     // pole A
        writePole(CB::NavDown, 40.0f);   // pole B
        d.release(CB::MorphScope);
        DispatchProbe::frame(d.editor());

        const auto info = d.proc().morphWidgetInfo(0, slot);
        if (!test::expectReached(d, [&](UiDriver&) { return info.exists && info.inA && info.inB; },
                                 "the slot now holds both poles", failed))
            return;
        check(info.aValue < info.bValue, "the two poles hold different values");

        // --- The fader blends between them ----------------------------------------
        // Read the RESOLVED value, not baseParams: a morph map is not written into the
        // base, it is resolved on the way out (P-Lock > morph-lerp > kit base), which
        // is exactly why the fader can be moved live without editing anything.
        d.proc().setMorphFader(0.0f);
        runBlock(d);
        const float atA = d.proc().morphEffectiveValue(0, slot);
        d.proc().setMorphFader(1.0f);
        runBlock(d);
        const float atB = d.proc().morphEffectiveValue(0, slot);
        check(atA < atB, "sweeping the fader moves the value from pole A toward pole B");
        check(std::abs(atA - info.aValue) < 1.0e-3f, "...arriving exactly at A at one end");
        check(std::abs(atB - info.bValue) < 1.0e-3f, "...and exactly at B at the other");

        // --- BAKE freezes the blend, ERASE throws the map away ---------------------
        d.gap();
        d.chord({ CB::MorphScope }, CB::VerbClear);       // BAKE
        check(!d.proc().morphWidgetInfo(0, slot).exists,
              "Morph+CLEAR bakes the blend into the kit and retires the map");

        // Author a map again, then erase it instead.
        d.gap();
        d.press(CB::MorphScope);
        DispatchProbe::frame(d.editor());
        writePole(CB::NavUp, 5.0f);
        writePole(CB::NavDown, 25.0f);
        d.release(CB::MorphScope);
        DispatchProbe::frame(d.editor());
        if (!test::expectReached(d, [&](UiDriver& dd) { return dd.proc().morphWidgetInfo(0, slot).exists; },
                                 "a second map to erase", failed))
            return;

        d.gap();
        d.press(CB::Func);
        d.chord({ CB::MorphScope }, CB::VerbClear);       // Func+Morph+CLEAR = ERASE
        d.release(CB::Func);
        check(!d.proc().morphWidgetInfo(0, slot).exists, "Func+Morph+CLEAR erases the map");
    }

    // D4 -- Cue.
    //
    // Cue is the headphone bus: a per-track crossfade between the main output and the
    // cue output, so you can audition a track the room cannot hear. It is the one
    // scope with no key of its own -- Func+3 enters it (hardware parity, DESIGN §21) --
    // which makes "is it discoverable?" a real question the surface has to answer.
    void testCue(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/D4] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        installRealMachine(d.rig(), 1);
        d.tap(CB::SelectTrack, 1);

        check(d.proc().getCueBalance(1) == 0.0f, "the track starts fully in the room");

        // --- Func+3 enters the Cue scope ------------------------------------------
        d.gap();
        d.press(CB::Func);
        d.press(CB::TapTempo);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().cueHeld; },
                                 "Func+3 enters the Cue scope", failed))
        {
            d.release(CB::TapTempo);
            d.release(CB::Func);
            return;
        }

        // --- Cue+Mute flips the focused track into the cue bus ---------------------
        d.tap(CB::MuteScope);
        check(d.proc().getCueBalance(1) > 0.5f, "Cue+Mute sends the focused track to the cue bus");
        check(d.proc().getCueBalance(0) == 0.0f, "...and only that track");

        d.tap(CB::MuteScope);
        check(d.proc().getCueBalance(1) < 0.5f, "pressing again brings it back to the room");

        d.release(CB::TapTempo);
        d.release(CB::Func);
        if (!test::expectReached(d, [](UiDriver& dd) { return !dd.ui().cueHeld; },
                                 "releasing the 3-key leaves the Cue scope", failed))
            return;

        // --- The scope is DISCOVERABLE: key 3 says CUE under Func ------------------
        // 6.4's access pass exists because the scope was reachable but invisible.
        d.gap();
        d.press(CB::Func);
        {
            const auto surf = d.surface();
            check(surf.tap.primary.containsIgnoreCase("CUE"),
                  "under Func, the 3-key advertises CUE");
        }
        d.release(CB::Func);
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

        // --- A restore is itself a destructive op, so Func+O takes it back -------
        check(d.proc().undoDepth(CheckpointScope::Song, 0) > 0,
              "restoring arms an undo -- overwriting live state is what destructive means");
        d.gap();
        d.press(CB::Func);
        d.tap(CB::VerbClear);      // Func+O = UNDO (9.37 item B)
        d.release(CB::Func);
        check(trigCount(d, 0) == 5, "undo puts back what the restore overwrote");

        // --- A mark on a SCOPE is walked back under that scope (9.37 item A) ------
        // This is the half that was write-only: Track+Y pushed and nothing could pop.
        d.gap();
        d.chord({ CB::TrackScope }, CB::VerbSnapshot);
        const int trackDepth = d.proc().checkpointDepth(CheckpointScope::Track, 0);
        d.gap();
        d.step(1);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 6; },
                                 "the track moved on from its own mark", failed))
            return;

        d.gap();
        d.press(CB::TrackScope);
        d.press(CB::Func);
        d.tap(CB::VerbSnapshot);   // Func+Y under a held Track
        d.release(CB::Func);
        d.release(CB::TrackScope);

        check(trigCount(d, 0) == 5, "Track+Func+Y walks the TRACK's stack back");
        check(d.proc().checkpointDepth(CheckpointScope::Track, 0) == trackDepth - 1,
              "...popping that stack, not the Song's");

        // --- A held step does not retarget a checkpoint verb (9.37 item D) --------
        // A step outranked Track in primaryScope(), so marking a track with a step
        // down silently marked the SONG -- a scope the player never named.
        const int songBefore = d.proc().checkpointDepth(CheckpointScope::Song, 0);
        const int trkBefore = d.proc().checkpointDepth(CheckpointScope::Track, 0);
        d.gap();
        d.press(CB::Step, 12);
        d.press(CB::TrackScope);
        d.tap(CB::VerbSnapshot);
        d.release(CB::TrackScope);
        d.release(CB::Step, 12);

        check(d.proc().checkpointDepth(CheckpointScope::Track, 0) == trkBefore + 1,
              "a held step leaves Track+Y marking the TRACK");
        check(d.proc().checkpointDepth(CheckpointScope::Song, 0) == songBefore,
              "...and not the Song, which is where it used to land");
    }
}   // namespace

void runCujPerformanceTests(int& failed)
{
    testMute(failed);
    testFill(failed);
    testMorph(failed);
    testCue(failed);
    testCheckpoints(failed);
}
}   // namespace lockstep
