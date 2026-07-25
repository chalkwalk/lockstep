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
// FOUND WHILE WRITING THIS (filed, not fixed -- see ROADMAP 9.36): `Mute+Song+step`,
// documented in dispatch itself as "empty + Mute: blank default song" and claimed by
// ROADMAP 5.3, cannot fire. The Mute LAYER rewrites every step key to ToggleMute
// (kLayerRemaps) before the Step case can look at songHeld, and (ToggleMute, Mute|Song)
// matches no binding row -- so the press is swallowed and the blank-song branch is
// dead code. The bare create-on-select path (which this journey does cover) is fine;
// only the Mute variant is unreachable.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

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

        // (`Mute+Song+step` -- the blank-song variant -- is not asserted: the gesture
        // cannot reach its handler at all. See the header note; the leg lands when the
        // Mute-layer shadow is resolved.)
    }
}   // namespace

void runCujArrangementTests(int& failed)
{
    testScene(failed);
    testSong(failed);
}
}   // namespace lockstep
