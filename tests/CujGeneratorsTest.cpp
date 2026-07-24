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
}   // namespace

void runCujGeneratorsTests(int& failed)
{
    testEuclidGenerateCommitCancel(failed);
}
}   // namespace lockstep
