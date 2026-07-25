// CujClipboardTest -- Group A4: copy / paste / clear, across the scopes.
//
// The canonical demonstration that Lockstep's grammar is scope+verb and not a pile
// of bespoke keys: ONE copy key, ONE paste key, and what they act on is whatever
// scope the other hand is holding. This journey walks a person doing that -- copy a
// step, copy a whole track, copy a section -- and checks three things a manual
// tester would tick off:
//
//   1. the clipboard is TYPED (a step clip cannot be pasted into a track),
//   2. the paste actually lands,
//   3. the surface tells the truth about which scopes can copy at all.
//
// (3) is the regression guard that matters most here: Song's rows advertised
// COPY/PASTE for years while verbs::song implemented neither, so the keys were
// silent no-ops wearing labels (9.14 st.5). The affordance matrix is now the one
// authority, and the surface must keep reading it.
//
// FOUND WHILE WRITING THIS (filed, not fixed -- see ROADMAP 9.36): using a held
// step as a copy/paste OPERAND also authors on release. verbs::trig marks the edit
// context param-written for Clear but not for Record/Play, so the step-release path
// falls through to its trig toggle: copy a step and it turns itself off; paste onto
// a step and the release inverts what just landed. The journey therefore asserts
// the paste WHILE THE STEP IS STILL HELD -- which is the honest question anyway
// ("did the paste land?") -- and never asserts the post-release state, so it will
// not have to be re-blessed when the toggle is suppressed.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // U (VerbRecord) = copy under a held scope. Q-row index per SurfaceModel's
    // cellFor() mapping.
    constexpr int kCopyKey = 6;

    int trigCount(UiDriver& d, int track)
    {
        int n = 0;
        const auto& t = d.proc().sequence().tracks[static_cast<std::size_t>(track)];
        for (int s = 0; s < 16; ++s)
            if (t.steps[static_cast<std::size_t>(s)].trig)
                ++n;
        return n;
    }

    // Blank the focused track through the real gesture. Track+CLEAR is destructive,
    // so it arms a confirm gate and fires only on the following P -- which this
    // journey also asserts on its own account, below.
    void clearTrack(UiDriver& d)
    {
        d.chord({ CB::TrackScope }, CB::VerbClear).tap(CB::VerbConfirm);
    }

    // A4 -- Copy / paste / clear across scopes.
    void testCopyPasteClearAcrossScopes(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/A4] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        installRealMachine(d.rig(), 2);

        d.tap(CB::SelectTrack, 0);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.activeTrack() == 0; },
                                 "focus is on track 0", failed))
            return;

        clearTrack(d);
        d.step(0).step(4);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 2; },
                                 "track 0 carries the two authored trigs", failed))
            return;

        // --- Step scope: hold a step, U copies it --------------------------------
        d.press(CB::Step, 0);
        d.tap(CB::VerbRecord);
        {
            const auto& clip = DispatchProbe::clip(d.editor());
            check(clip.type == ClipboardType::Step,
                  "copying under a held step types the clipboard Step");
            check(clip.stepEntries.size() == 1, "one held step makes a one-entry clip");
            check(!clip.stepEntries.empty() && clip.stepEntries[0].data.trig,
                  "the clip carries the step's trig");
        }
        d.release(CB::Step, 0);

        // --- The clipboard is typed: a Step clip will not paste into a track ------
        // The whole point of typing it. Asserted behaviourally (does track 1 change?)
        // rather than by calling the guard predicate, because the guard being right
        // only matters if dispatch actually consults it.
        d.tap(CB::SelectTrack, 1);
        const int track1Before = trigCount(d, 1);
        d.chord({ CB::TrackScope }, CB::VerbPlay);
        check(trigCount(d, 1) == track1Before,
              "a Step clip pasted under the Track scope is refused, not smeared over the track");

        // --- Step scope: paste onto a different step ------------------------------
        d.tap(CB::SelectTrack, 0);
        d.press(CB::Step, 9);
        d.tap(CB::VerbPlay);
        check(d.proc().sequence().tracks[0].steps[9].trig,
              "pasting under a held step stamps the copied step there");
        d.release(CB::Step, 9);

        // --- Track scope: U copies the whole track, I pastes it onto another ------
        // Re-author from blank: the two step holds above each authored a toggle on
        // release (the defect in the header note), so the pattern from here on is
        // established with plain taps, which is what a tap is FOR.
        clearTrack(d);
        d.step(0).step(4).step(12);
        if (!test::expectReached(d, [](UiDriver& dd) { return trigCount(dd, 0) == 3; },
                                 "track 0 re-authored with three trigs", failed))
            return;

        d.chord({ CB::TrackScope }, CB::VerbRecord);
        {
            const auto& clip = DispatchProbe::clip(d.editor());
            check(clip.type == ClipboardType::Track,
                  "copying under the Track scope types the clipboard Track");
            check(clip.clipTrack.steps[0].trig && clip.clipTrack.steps[4].trig
                      && clip.clipTrack.steps[12].trig,
                  "the track clip carries the authored pattern");
        }

        d.tap(CB::SelectTrack, 2);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.activeTrack() == 2; },
                                 "focus moved to track 2", failed))
            return;
        clearTrack(d);
        d.chord({ CB::TrackScope }, CB::VerbPlay);
        {
            const auto& t2 = d.proc().sequence().tracks[2];
            check(t2.steps[0].trig && t2.steps[4].trig && t2.steps[12].trig,
                  "the pattern lands on track 2");
            check(trigCount(d, 2) == trigCount(d, 0), "track 2 now matches the source track");
        }

        // --- Section scope: hold a section key, U copies that section -------------
        d.press(CB::Section, IMachine::kSrcSecIdx);
        d.tap(CB::VerbRecord);
        {
            const auto& clip = DispatchProbe::clip(d.editor());
            check(clip.type == ClipboardType::Section,
                  "copying under a held section types the clipboard Section");
            check(!clip.sectionSlots.empty(), "the section clip carries the section's slots");
        }
        d.release(CB::Section, IMachine::kSrcSecIdx);

        // --- Clear: destructive, so it is gated on a confirm ----------------------
        d.chord({ CB::TrackScope }, CB::VerbClear);
        check(trigCount(d, 2) > 0, "Track+CLEAR alone does not clear -- it arms a confirm");
        d.tap(CB::VerbConfirm);
        check(trigCount(d, 2) == 0, "the confirm fires the clear");

        // --- Surface honesty: only scopes that copy may say so --------------------
        // Song has no clipboard; its copy key must dim rather than advertise a verb
        // nothing implements. (The glow is wired for the section-SUITE scopes only --
        // a held step or section copies without lighting the key. Noted as a gap in
        // the catalogue, not asserted here, because no-signal is not a lie.)
        d.press(CB::SongScope);
        {
            const auto surf = d.surface();
            check(surf.functionRow[kCopyKey].disabled,
                  "under a held Song the copy key dims -- Song has no clipboard");
        }
        d.release(CB::SongScope);

        d.press(CB::TrackScope);
        {
            const auto surf = d.surface();
            check(!surf.functionRow[kCopyKey].disabled && surf.functionRow[kCopyKey].scopeTint != 0,
                  "under a held Track the copy key glows -- Track does copy");
        }
        d.release(CB::TrackScope);
    }
}   // namespace

void runCujClipboardTests(int& failed)
{
    testCopyPasteClearAcrossScopes(failed);
}
}   // namespace lockstep
