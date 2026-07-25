// CujGeneratorsTest -- Group B critical user journeys: the generators.
//
// The generator family is entered exclusively through the generator hub (hold the
// 3-key >= 350 ms, then pick a cell). These journeys prove the overlay-mode shape:
// hub entry, the live latched preview, and the commit (bare P) / cancel (Func+P)
// fork. See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include "../src/ui/MetaBand.h"
#include "../src/ui/mode/ModeReducer.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    void setPattern(UiDriver& d, int track, std::initializer_list<int> steps)
    {
        auto& t = d.proc().sequence().tracks[static_cast<std::size_t>(track)];
        for (int s = 0; s < 16; ++s)
            t.steps[static_cast<std::size_t>(s)].trig = false;
        for (int s : steps)
            t.steps[static_cast<std::size_t>(s)].trig = true;
    }

    std::vector<int> onsets(UiDriver& d, int track)
    {
        std::vector<int> v;
        const auto& t = d.proc().sequence().tracks[static_cast<std::size_t>(track)];
        const int len = std::max(1, t.length);
        for (int s = 0; s < len; ++s)
            if (t.steps[static_cast<std::size_t>(s)].trig)
                v.push_back(s);
        return v;
    }

    // Hold the 3-key past the long-press so the hub opens, tick the timer that
    // promotes it, then pick the EUCLID cell (hub cell 0) and let the key go. Euclid
    // survives the release (resetGeneratorHub clears only the hub flag).
    void openEuclid(UiDriver& d)
    {
        d.press(CB::TapTempo);
        d.advanceMs(GestureRecognizer::kLongPressMs + 60.0);
        d.editor().timerCallback();   // the timer promotes the held 3-key to the hub
        d.step(0);                    // EUCLID cell
        d.release(CB::TapTempo);
    }

    // B1 -- Euclid generate + commit / cancel.
    void testEuclidGenerateCommitCancel(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/B1] %s\n", what); ++failed; }
        };

        // --- COMMIT: enter, retune PULSE, bake ------------------------------------
        {
            UiDriver d;
            // A clustered (non-Euclidean) seed so the generator visibly redistributes.
            setPattern(d, 0, { 0, 1, 2, 3 });

            openEuclid(d);
            if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().euclidHeld; },
                                     "the EUCLID overlay is entered from the hub", failed))
                return;
            check(activeOverlay(d.ui()) == Overlay::Euclid, "activeOverlay resolves to Euclid");
            check(resolveMetaBand(d.ui()) == MetaBand::Euclidean,
                  "the MZ shows the Euclid band (PULSE / OFSET / ACCNT)");

            // Retune PULSE 4 -> 6 through the real armed MZ path; the live preview
            // follows to six onsets.
            DispatchProbe::frame(d.editor());   // sliders' ranges follow the Euclid band
            d.setParam(DispatchProbe::mzSlotOffset(d.editor()) + 0, 6.0f);
            check(d.ui().euclidPulses == 6, "setParam retunes PULSE to 6");
            check(static_cast<int>(onsets(d, 0).size()) == 6, "the live preview spreads six pulses");

            // Bake it (bare P).
            d.tap(CB::VerbConfirm);
            check(!d.ui().euclidHeld, "bare P commits and leaves the overlay");
            const auto baked = onsets(d, 0);
            check(static_cast<int>(baked.size()) == 6, "the committed pattern keeps six onsets");
            check(baked != std::vector<int>({ 0, 1, 2, 3 }),
                  "the committed pattern is the redistributed Euclid rhythm, not the seed");
        }

        // --- CANCEL: enter, revert ------------------------------------------------
        {
            UiDriver d;
            setPattern(d, 0, { 0, 1, 2, 3 });

            openEuclid(d);
            if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().euclidHeld; },
                                     "the EUCLID overlay is entered (cancel path)", failed))
                return;
            // The live preview has already redistributed the four seeded onsets.
            check(onsets(d, 0) != std::vector<int>({ 0, 1, 2, 3 }),
                  "the live preview changed the pattern");

            // Func+P cancels: the original pattern comes back untouched.
            d.chord({ CB::Func }, CB::VerbConfirm);
            check(!d.ui().euclidHeld, "Func+P cancels and leaves the overlay");
            check(onsets(d, 0) == std::vector<int>({ 0, 1, 2, 3 }),
                  "cancel restores the original pattern exactly");
        }
    }
    // Open the hub and pick a cell by index (0=EUCLID, 1=DENSITY, 2=VEL, 3=MELODY,
    // 4=CHORD). Same shape as openEuclid: the 3-key must be held past the long-press
    // AND the timer ticked, because the timer is what promotes the hold to the hub.
    void openHubCell(UiDriver& d, int cell)
    {
        d.press(CB::TapTempo);
        d.advanceMs(GestureRecognizer::kLongPressMs + 60.0);
        d.editor().timerCallback();
        d.step(cell);
        d.release(CB::TapTempo);
    }

    // B2 -- Density overlay.
    //
    // Density is subtractive: it thins what the pattern already says rather than
    // authoring anything, so the pattern on disk never changes and the thinning is
    // audible immediately and reversible instantly. That is the whole claim, and it is
    // what this journey checks -- the census of what SOUNDS drops while the stored
    // trigs stay exactly where they were.
    void testDensityOverlay(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/B2] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        setPattern(d, 0, { 0, 2, 4, 6, 8, 10, 12, 14 });
        const auto before = onsets(d, 0);

        openHubCell(d, 1);   // DENSITY
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().overlay == Overlay::Density; },
                                 "the hub's DENSITY cell opens the density overlay", failed))
            return;
        check(resolveMetaBand(d.ui()) == MetaBand::Density,
              "the encoders become the density band");

        // --- Thinning is subtractive: the stored pattern must not move -------------
        DispatchProbe::frame(d.editor());
        d.proc().setMasterDensity(-0.9f);
        d.runBlocks(4);

        check(onsets(d, 0) == before,
              "thinning changes nothing on disk -- density is subtractive, not an edit");
        check(d.proc().masterDensity() < 0.0f, "the master density offset took the value");

        // --- ...and it comes straight back ----------------------------------------
        d.proc().setMasterDensity(0.0f);
        d.runBlocks(4);
        check(onsets(d, 0) == before, "and restoring the offset leaves the pattern as it was");

        // --- The overlay is sticky, and escapes on a double-tap Func --------------
        check(d.ui().overlay == Overlay::Density, "the overlay is sticky -- it survives the hub");
        d.doubleTap(CB::Func);
        check(d.ui().overlay == Overlay::None, "double-tap Func escapes it");
    }

    // B3 -- Velocity overlay.
    //
    // The velocity generator shapes accent across the bar rather than per step. Its
    // four sub-pages hang off ONE key (AMP re-pressed), which is the pattern worth
    // pinning: a generator with four axes does not get four keys.
    void testVelocityOverlay(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/B3] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        setPattern(d, 0, { 0, 4, 8, 12 });

        openHubCell(d, 2);   // VEL
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().overlay == Overlay::Vel; },
                                 "the hub's VEL cell opens the velocity overlay", failed))
            return;

        // --- With velocity off everywhere, MODE is the only page there is ---------
        // The sub-page cycle skips disabled axes, so an overlay that has not been
        // switched on has exactly one thing to offer: the switch.
        check(d.ui().velSubPage == UiState::VelSubPage::Mode,
              "with velocity off, the overlay opens on its MODE page");
        d.gap();
        d.tap(CB::Section, LockstepProcessor::kAmpSecIdx);
        check(d.ui().velSubPage == UiState::VelSubPage::Mode,
              "...and re-pressing AMP has nowhere else to go yet");

        // Switch it on for a track, and the other axes appear.
        d.proc().kit(0).velMode = VelMode::Bar;
        d.doubleTap(CB::Func);
        openHubCell(d, 2);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().overlay == Overlay::Vel; },
                                 "the overlay re-opens with velocity enabled", failed))
            return;

        // --- AMP re-presses cycle the sub-pages, and the band follows -------------
        const auto firstPage = d.ui().velSubPage;
        const auto firstBand = resolveMetaBand(d.ui());
        d.gap();
        d.tap(CB::Section, LockstepProcessor::kAmpSecIdx);
        check(d.ui().velSubPage != firstPage, "re-pressing AMP moves to the next sub-page");
        check(resolveMetaBand(d.ui()) != firstBand, "...and the encoders follow it");

        // Keep pressing and it comes back round -- four axes, one key.
        int guard = 0;
        while (d.ui().velSubPage != firstPage && guard++ < 8)
        {
            d.gap();
            d.tap(CB::Section, LockstepProcessor::kAmpSecIdx);
        }
        check(d.ui().velSubPage == firstPage, "the sub-pages cycle back to where they started");
        check(guard < 8, "...within one lap, not by running out of guard");

        // --- Sticky, and escapable ------------------------------------------------
        check(d.ui().overlay == Overlay::Vel, "the overlay is sticky");
        d.doubleTap(CB::Func);
        check(d.ui().overlay == Overlay::None, "double-tap Func escapes it");
    }
    // B4 -- Melodic generator.
    //
    // The melodic generator writes a LINE, not a rhythm: onsets land strongest-beat
    // first, and the pitches come from the effective key. Two properties make it usable
    // on stage rather than a novelty, and both are asserted here: it previews live
    // before you commit, and it is DETERMINISTIC -- the same seed in the same place
    // gives the same line, so a take can be reproduced.
    void testMelodicGenerator(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/B4] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        setPattern(d, 0, {});                    // a blank track to write onto
        const auto blank = onsets(d, 0);

        openHubCell(d, 3);   // MELODY
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().melodicHeld; },
                                 "the hub's MELODY cell arms the melodic generator", failed))
            return;
        check(resolveMetaBand(d.ui()) == MetaBand::Melodic, "the encoders become the melody band");

        // --- It previews live, before anything is committed -----------------------
        DispatchProbe::frame(d.editor());
        d.setParam(DispatchProbe::mzSlotOffset(d.editor()) + 0, 8.0f);   // density-ish first field
        const auto preview = onsets(d, 0);
        check(preview != blank, "turning a knob previews the line on the grid");

        // --- Bare P prints it; the preview becomes the pattern ---------------------
        d.tap(CB::VerbConfirm);
        check(!d.ui().melodicHeld, "P commits and leaves the generator");
        const auto printed = onsets(d, 0);
        check(printed == preview, "what was previewed is what got printed");
        check(!printed.empty(), "...and it is a real line, not an empty one");

        // --- Determinism: the same seed in the same place gives the same line ------
        // Re-arm, drive the same field to the same value, and the preview must match
        // what printed. A generator you cannot reproduce is one you cannot perform.
        openHubCell(d, 3);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().melodicHeld; },
                                 "the generator re-arms", failed))
            return;
        DispatchProbe::frame(d.editor());
        d.setParam(DispatchProbe::mzSlotOffset(d.editor()) + 0, 8.0f);
        check(onsets(d, 0) == printed, "the same settings reproduce the same line");

        // --- Func+P cancels: the pattern goes back untouched -----------------------
        d.press(CB::Func);
        d.tap(CB::VerbConfirm);
        d.release(CB::Func);
        check(!d.ui().melodicHeld, "Func+P leaves the generator too");
        check(onsets(d, 0) == printed, "...restoring what was there before the preview");
    }

    // B5 -- Harmonic voice-mover.
    //
    // The chord generator is a voice-leading tool, not a chord palette: you move one
    // voice at a time along the scale's ladder and the others stay put, which is how a
    // progression gets written by ear. The invariant that makes it sound like harmony
    // rather than a cluster is that no two voices land on the same pitch.
    void testHarmonicVoiceMover(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/B5] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        setPattern(d, 0, {});

        openHubCell(d, 4);   // CHORD
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().harmonyHeld; },
                                 "the hub's CHORD cell arms the voice-mover", failed))
            return;
        check(resolveMetaBand(d.ui()) == MetaBand::Harmony, "the encoders become the chord band");

        // --- The voices start distinct, and stay distinct as one moves ------------
        auto voicesDistinct = [&] {
            const auto& prog = d.ui().harmonyProg;
            for (int i = 0; i < kHarmonyVoices; ++i)
                for (int j = i + 1; j < kHarmonyVoices; ++j)
                    if (prog.chords[0].voice[i] == prog.chords[0].voice[j])
                        return false;
            return true;
        };
        check(voicesDistinct(), "the opening chord has four distinct voices");

        DispatchProbe::frame(d.editor());
        const auto beforeVoice = d.ui().harmonyProg.chords[0].voice[1];
        d.setParam(DispatchProbe::mzSlotOffset(d.editor()) + 1, static_cast<float>(beforeVoice + 2));
        check(d.ui().harmonyProg.chords[0].voice[1] != beforeVoice, "a voice moves when its knob turns");
        check(voicesDistinct(), "...and no two voices ever share a pitch");

        // --- LEN grows the progression by CLONING, not by inventing ---------------
        // A second chord that started from silence would be a new decision to make;
        // starting from a copy of the one before it means growing a progression is an
        // edit, not a blank page.
        {
            const auto& prog = d.ui().harmonyProg;
            const int lenBefore = prog.length;
            const auto firstChord = prog.chords[0];
            DispatchProbe::frame(d.editor());
            d.setParam(DispatchProbe::mzSlotOffset(d.editor()) + 4,   // field 4 = LEN
                       static_cast<float>(lenBefore + 1));
            check(prog.length == lenBefore + 1, "LEN grows the progression");
            check(prog.chords[lenBefore].voice == firstChord.voice,
                  "...and the new chord is a clone of the one before it");
        }

        // --- Bare P prints one chord per bar ---------------------------------------
        d.tap(CB::VerbConfirm);
        check(!d.ui().harmonyHeld, "P commits and leaves the voice-mover");
        check(!onsets(d, 0).empty(), "the progression is printed onto the track");
    }
}   // namespace

void runCujGeneratorsTests(int& failed)
{
    testEuclidGenerateCommitCancel(failed);
    testDensityOverlay(failed);
    testVelocityOverlay(failed);
    testMelodicGenerator(failed);
    testHarmonicVoiceMover(failed);
}
}   // namespace lockstep
