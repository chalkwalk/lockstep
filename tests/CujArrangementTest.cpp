// CujArrangementTest -- Group E: launching. Scenes and Songs, from the hold
// selectors a player actually uses mid-set.
//
// E1 covers the Scene selector's whole create/launch/sync loop, and guards the 5.3
// round-2 work specifically: an occupied Scene cell must wear its OWN identity
// colour and show its NAME. That regressed once in a way no state test could see --
// the screen renderer consumed a hand-copied "is a selector up?" predicate that had
// dropped `songHeld`, so the model was right and the picture was wrong.
//
// E3 covers Song switching, which is the largest state swap in the instrument:
// a full reset with deviations cleared, create-on-select for an empty slot (the v36
// slot model), and `Mute+Song+step` for a blank one. It rolls audio ACROSS the
// switch on purpose -- the song-switch frame race (a short frame handed to a machine
// that has not been reinstalled yet) is invisible without a block running through it.
//
// FOUND WHILE WRITING THIS, fixed in 9.38: `Mute+Song+step`, documented in dispatch
// itself as "empty + Mute: blank default song" and claimed by ROADMAP 5.3, could not
// fire -- and did something else instead. The Mute LAYER rewrites every step key to
// ToggleMute (kLayerRemaps) before the Step case can look at songHeld; resolveBinding
// then matches on a SUBSET of held mods, and no ToggleMute row requires Song, so the
// winner was the plain `{ToggleMute, kModMute}` row and the press MUTED the track with
// that index (measured -- it armed a pending mute on track 3). The rule now lives in
// one function reached from both buttons, and the leg below asserts all three claims
// -- created, blank, and nothing muted -- because the way this gesture failed was by
// doing something else quietly rather than by doing nothing.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include "../src/ui/InspectorModel.h"

#include <cstdio>
#include <string>

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

    // Scene + cell. gap() first: two Scene presses inside the double-tap window latch
    // the scope, and a latched Scene changes what every later step press means.
    void sceneSelect(UiDriver& d, int idx)
    {
        d.gap();
        d.press(CB::SceneScope);
        d.tap(CB::Step, idx);
        d.release(CB::SceneScope);
    }

    void songSelect(UiDriver& d, int idx)
    {
        d.gap();
        d.press(CB::SongScope);
        d.tap(CB::Step, idx);
        d.release(CB::SongScope);
    }

    // E1 -- Scene.
    void testScene(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/E1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().activeSectionIdx() == 0; },
                                 "the set opens on scene 0", failed))
            return;

        // --- An empty cell creates and launches -----------------------------------
        // (Stopped, so a launch is immediate; rolling, it would arm to the bar.)
        check(!d.proc().sceneSlotOccupied(2), "scene 2 starts empty");
        sceneSelect(d, 2);
        check(d.proc().activeSectionIdx() == 2, "Scene+step launches that scene");
        check(d.proc().sceneSlotOccupied(2), "...creating it on the way -- create-on-select");

        // --- An occupied cell just launches ---------------------------------------
        sceneSelect(d, 0);
        check(d.proc().activeSectionIdx() == 0, "an occupied cell launches back");

        // --- Identity: the selector shows names and identity colours (5.3 WI-3) ---
        d.proc().setSceneName(d.proc().activePieceIdx(), 2, "CHORUS");
        d.proc().setSceneColour(d.proc().activePieceIdx(), 2, 3);
        d.gap();
        d.press(CB::SceneScope);
        {
            const auto surf = d.surface();
            const auto& cell = surf.step[2];
            check(cell.primary == juce::String("CHORUS"),
                  "the held Scene selector shows the scene's name");
            check(cell.baseColour != surf.step[5].baseColour,
                  "...and an identity-coloured cell no longer looks like its unnamed neighbour");
            check(cell.base == CellState::SelectorOccupied,
                  "an occupied, non-current scene reads as occupied");
            check(surf.step[0].base == CellState::SelectorCurrent,
                  "the playing scene reads as current");
        }
        d.release(CB::SceneScope);

        // --- Scene+O = SYNC: discard live deviations -------------------------------
        // Deviate track 0 onto another phrase, then sync it back home. The target has
        // to be a phrase row that EXISTS: `Phrase+step` is launch, not create, and is
        // inert on an un-created row (5.3 Item C) -- creating scene 2 above is what
        // brought phrase row 2 into being.
        d.gap();
        d.press(CB::PhraseScope);
        d.tap(CB::Step, 2);
        d.release(CB::PhraseScope);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().isTrackDeviated(0); },
                                 "track 0 is deviated off its home phrase", failed))
            return;

        d.gap();
        d.chord({ CB::SceneScope }, CB::VerbClear);
        check(!d.proc().isTrackDeviated(0), "Scene+CLEAR syncs the deviations away");

        // --- Scene+RECORD is the other half: BAKE, not discard --------------------
        // Record commits deviations into the scene as stored; Clear throws them away.
        // Commit and discard, on the two verbs that already mean commit and discard --
        // and the destructive one is confirm-gated.
        d.gap();
        d.press(CB::PhraseScope);
        d.tap(CB::Step, 2);
        d.release(CB::PhraseScope);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().isTrackDeviated(0); },
                                 "a deviation to bake", failed))
            return;

        d.gap();
        d.chord({ CB::SceneScope }, CB::VerbRecord);
        check(d.ui().confirm.kind == ConfirmKind::BakeScene, "Scene+RECORD arms a bake confirm");
        check(d.proc().isTrackDeviated(0), "...and bakes nothing until it is confirmed");

        d.tap(CB::VerbConfirm);
        check(d.ui().confirm.kind == ConfirmKind::None, "the confirm clears");
        check(!d.proc().isTrackDeviated(0),
              "the deviation is baked into the scene -- the track is home BECAUSE home moved");
    }

    // E3 -- Song.
    void testSong(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/E3] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        d.tap(CB::SelectTrack, 0);
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
        d.gap();
        d.step(0).step(4).step(8);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 3; },
                                 "song 0 has a pattern to leave behind", failed))
            return;

        // --- An empty Song cell creates on select, copying the current song -------
        check(!d.proc().songSlotOccupied(1), "song 1 starts empty");
        songSelect(d, 1);
        check(d.proc().activePieceIdx() == 1, "Song+step switches to that song");
        check(d.proc().songSlotOccupied(1), "...creating it -- create-on-select (v36 slots)");
        check(trigCount(d, 0) == 3, "an empty cell copies the song you were on");

        // Diverge, so the switch back is observable as more than a no-op.
        d.gap();
        d.step(12);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 4; },
                                 "song 1 diverges from song 0", failed))
            return;

        // --- Rolling across the switch: the frame race guard ----------------------
        // A song switch swaps the whole musical state; the machine reinstall lands
        // asynchronously, so a block rendered across the boundary is where a short
        // frame would reach a machine expecting a longer one.
        d.play(2.0);
        songSelect(d, 0);
        d.play(2.0);
        check(d.proc().activePieceIdx() == 0, "switching back lands on song 0");
        check(trigCount(d, 0) == 3, "song 0 still holds its own pattern");
        check(!d.hasNaN(), "audio stays finite across a song switch");

        // --- Song+CLEAR is PANIC: every voice stops, the pattern is untouched -----
        d.tap(CB::SelectTrack, 0);
        d.proc().triggerNote(0, 60, 2000, 110);
        float ringing = 0.0f;
        for (int i = 0; i < 40; ++i) { d.runBlocks(1); ringing = std::max(ringing, d.lastRms()); }
        if (!test::expectReached(d, [&](UiDriver&) { return ringing > 0.0f; },
                                 "a voice is ringing to panic", failed))
            return;

        const int trigsBefore = trigCount(d, 0);
        d.gap();
        d.chord({ CB::SongScope }, CB::VerbClear);
        d.runBlocks(20);
        check(d.lastRms() < ringing, "Song+CLEAR panics -- the voices stop");
        check(trigCount(d, 0) == trigsBefore, "...without touching a single trig");

        // --- Mute+Song+step on an empty slot creates a BLANK song ----------------
        // The other create keeps you where you were (it copies); this one starts you
        // from nothing. `Mute` is the "without the contents" qualifier it already is
        // in Mute+Func+Scene+Play. Asserted on all three of its claims, because the
        // way this gesture failed for two milestones was by doing something ELSE
        // quietly: the Mute layer rewrote the step key before Song could be read, and
        // the press armed a mute on the track with that index (9.38).
        check(!d.proc().songSlotOccupied(2), "song 2 starts empty");
        check(!d.proc().getGlobalMute(2), "...and track 2 starts unmuted");
        d.gap();
        d.press(CB::MuteScope);
        d.press(CB::SongScope);
        d.tap(CB::Step, 2);
        d.release(CB::SongScope);
        d.release(CB::MuteScope);
        d.runBlocks(4);

        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().activePieceIdx() == 2; },
                                 "Mute+Song+step reaches the song handler at all", failed))
            return;
        check(d.proc().songSlotOccupied(2), "...creating song 2");
        check(trigCount(d, 0) == 0, "...BLANK -- it did not copy the song you were on");
        check(!d.proc().getGlobalMute(2), "...and muted nothing on the way (the old failure)");
        check(!d.proc().hasPendingMute(2), "...not even a pending one");
    }
    // E2 -- Phrase.
    //
    // A phrase deviation is how one musician steps off the scene's arrangement without
    // taking anyone with them: hold Phrase, press a row, and just the focused track
    // plays that phrase. Add Scene and everyone moves. The diagonal row is home.
    void testPhrase(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/E2] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        installRealMachine(d.rig(), 1);

        // Bring phrase rows into being: a scene create is what fills the diagonal.
        // (Phrase+step is LAUNCH, not create -- an un-created row is inert, 5.3 Item C.)
        sceneSelect(d, 1);
        sceneSelect(d, 0);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().phraseSlotOccupied(0, 1); },
                                 "phrase row 1 exists to deviate onto", failed))
            return;

        d.tap(CB::SelectTrack, 0);
        check(!d.proc().isTrackDeviated(0), "nobody starts deviated");

        // --- Phrase+step moves the FOCUSED track only -----------------------------
        d.gap();
        d.press(CB::PhraseScope);
        d.tap(CB::Step, 1);
        d.release(CB::PhraseScope);

        check(d.proc().isTrackDeviated(0), "Phrase+step deviates the focused track");
        check(d.proc().deviationPhraseIdxForTrack(0) == 1, "...onto the row that was pressed");
        check(!d.proc().isTrackDeviated(1), "...and nobody else moves");

        // The surface carries it as a persistent badge, so a deviation is visible
        // without holding anything.
        {
            const auto surf = d.surface();
            check(surf.trackDeviated[0] && !surf.trackDeviated[1],
                  "the deviated track is badged on the surface");
        }

        // --- Scene+Phrase+step moves everyone -------------------------------------
        d.gap();
        d.press(CB::SceneScope);
        d.press(CB::PhraseScope);
        d.tap(CB::Step, 1);
        d.release(CB::PhraseScope);
        d.release(CB::SceneScope);

        check(d.proc().isTrackDeviated(1), "Scene+Phrase+step takes every track along");

        // --- The diagonal row is home: Scene+Phrase there un-deviates everyone ----
        // The scene's own row (phraseIdx == sceneIdx) is the arrangement, so landing
        // the ALL-tracks gesture on it is how the band comes back together.
        d.gap();
        d.press(CB::SceneScope);
        d.press(CB::PhraseScope);
        d.tap(CB::Step, 0);        // scene 0's diagonal
        d.release(CB::PhraseScope);
        d.release(CB::SceneScope);

        check(!d.proc().isTrackDeviated(0) && !d.proc().isTrackDeviated(1),
              "Scene+Phrase on the diagonal brings every track home");
        {
            const auto surf = d.surface();
            check(!surf.trackDeviated[0] && !surf.trackDeviated[1],
                  "...and the badges go out with it");
        }

        // --- The PER-TRACK gesture agrees with the all-tracks one on the diagonal --
        // Both spellings answer the same question -- "is this track playing something
        // other than the scene's own row?" -- so they must give the same answer. They
        // did not: `swapPhraseForTrack` set `deviated` unconditionally, so sending one
        // track to its own home row badged it as deviated while it played exactly the
        // scene's content. `deviateAllToPhrase` got it right, which is what made it an
        // asymmetry rather than a plain bug. One derived setter owns it now (9.38).
        d.gap();
        d.tap(CB::SelectTrack, 0);
        d.press(CB::PhraseScope);
        d.tap(CB::Step, 1);          // off home first, so the return is observable
        d.release(CB::PhraseScope);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().isTrackDeviated(0); },
                                 "the track is off its home row to come back from", failed))
            return;

        d.gap();
        d.press(CB::PhraseScope);
        d.tap(CB::Step, 0);          // scene 0's diagonal = this track's home
        d.release(CB::PhraseScope);

        check(!d.proc().isTrackDeviated(0),
              "Phrase+<own diagonal> brings the track home rather than badging it there");
        check(!d.surface().trackDeviated[0], "...and the badge goes out");

        // --- Phrase carries a clipboard of its own: copy, paste, clear ------------
        d.gap();
        d.tap(CB::SelectTrack, 0);
        d.chord({ CB::PhraseScope }, CB::VerbRecord);
        check(DispatchProbe::clip(d.editor()).type == ClipboardType::Pattern,
              "Phrase+RECORD copies the focused track's phrase");

        d.gap();
        d.tap(CB::SelectTrack, 1);
        const int sourceTrigs = trigCount(d, 0);
        d.chord({ CB::PhraseScope }, CB::VerbPlay);
        check(trigCount(d, 1) == sourceTrigs,
              "Phrase+PLAY pastes it onto another track, trig for trig");

        d.gap();
        d.chord({ CB::PhraseScope }, CB::VerbClear);
        check(d.ui().confirm.kind == ConfirmKind::ClearPhrase,
              "Phrase+CLEAR arms a confirm -- clearing every track's phrase is destructive");
        d.tap(CB::VerbConfirm);
        check(trigCount(d, 1) == 0, "the confirm clears the phrase");

        // NOT asserted, because the two paths disagree (filed -- see ROADMAP 9.36):
        // the per-track `Phrase+<diagonal>` marks the track deviated ONTO its own home
        // phrase (`swapPhraseForTrack` sets `deviated = true` unconditionally), so the
        // badge stays lit while the track plays exactly the scene's content.
        // `deviateAllToPhrase` gets this right -- it clears on `N == sceneIdx`.
    }

    // E4 -- Deletion picker.
    //
    // Deleting is the one thing on the surface that cannot be walked back by pressing
    // the key again, so it is the one gesture with a two-stage lock: a HOLD of Clear
    // under a deletable scope opens a picker (the grid becomes the list of things that
    // could go), and the slot you tap only ARMS a named confirm. The name in the
    // prompt is the safety feature -- it is what tells you the picker was pointed at
    // track 2 and not track 3.
    void testDeletionPicker(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/E4] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        installRealMachine(d.rig(), 2);

        d.tap(CB::SelectTrack, 2);
        if (!test::expectReached(d, [](UiDriver& dd) { return !dd.proc().isTrackEmpty(2); },
                                 "track 2 carries a machine to delete", failed))
            return;

        // --- Hold Clear under the Track scope: the grid becomes the picker --------
        d.gap();
        d.press(CB::TrackScope);
        d.longPress(CB::VerbClear);
        if (!test::expectReached(d, [](UiDriver& dd) {
                                     return dd.ui().deletePicker.scope == DeleteScope::Track;
                                 },
                                 "Track + hold(CLEAR) opens the delete picker", failed))
        {
            d.release(CB::TrackScope);
            return;
        }
        {
            const auto surf = d.surface();
            check(surf.activeLayer == SurfaceLayer::DeletePicker,
                  "the step grid re-skins to the picker");
        }
        // --- Tapping a slot ARMS a named confirm; it does not delete --------------
        // The arming chord is RELEASED FIRST, which is how a player performs this:
        // the picker is sticky by design (PRINCIPLES §16 -- "confirming must not
        // require re-holding the arming chord"), the grid stays lit, and you then
        // choose at leisure. This leg used to tap with Track still down, because the
        // Track branch matched only `SelectTrack` -- the name a step key wears while
        // Track is held -- so releasing first cancelled the picker instead (9.38).
        d.release(CB::TrackScope);
        check(d.ui().deletePicker.scope == DeleteScope::Track,
              "the picker survives the release of the chord that armed it");
        d.tap(CB::Step, 2);
        check(d.ui().confirm.kind == ConfirmKind::DeleteTrack, "the slot arms a delete confirm");
        check(d.ui().confirm.target == 2, "...pointed at the slot that was tapped");
        check(!d.proc().isTrackEmpty(2), "...and nothing is deleted yet");
        {
            const auto m = buildInspectorModel(d.ui(), d.proc().editContext(), d.proc(),
                                               CB::None, -1);
            check(m.statusKind == StatusKind::Confirm,
                  "the status lane shows a confirm, derived from state so it cannot fade");
            check(m.confirmPrompt.containsIgnoreCase("3"),
                  "the prompt NAMES the target (track 2 is TRACK 3 to a player)");
        }

        // --- Confirm deletes -------------------------------------------------------
        d.tap(CB::VerbConfirm);
        check(d.proc().isTrackEmpty(2), "P confirms, and the track is gone");
        check(!d.proc().isTrackEmpty(0), "...only that one");
        check(d.ui().confirm.kind == ConfirmKind::None, "the prompt clears with it");
    }
}   // namespace

void runCujArrangementTests(int& failed)
{
    testScene(failed);
    testSong(failed);
    testPhrase(failed);
    testDeletionPicker(failed);
}
}   // namespace lockstep
