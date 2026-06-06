// SurfaceModelTest — headless unit tests for pure SurfaceModel logic.
//
// Tests resolveKeyLabel() for key scenarios without a LockstepProcessor.
// Run via: lockstep_tests (exit 0 = pass, exit 1 = fail).

#include "TestHarness.h"
#include "../src/ui/KeyLabel.h"
#include "../src/ui/SurfaceModel.h"
#include "../src/ui/PageNav.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // Minimal stubs for types resolveKeyLabel() needs but doesn't deeply use.
    // -------------------------------------------------------------------------

    // UiState stub — default is: nothing held, no latch, nothing active.
    // Only funcHeld/trackHeld/patternScopeHeld/partHeld/stepHeld matter here.
    static UiState makeUiState()
    {
        UiState ui{};
        return ui;
    }

    // EditContext stub — default: no step held, no active edit.
    static EditContext makeEditContext() { return EditContext{}; }


    // -------------------------------------------------------------------------
    // Test: resolveKeyLabel() for VerbClear (PANIC/O key)
    //
    // New rule: PANIC has no funcLayer → hint is always empty.
    // CPC (COPY/PASTE/CLEAR) relabelling happens at the builder level under
    // scope modifiers, not in resolveKeyLabel.
    // -------------------------------------------------------------------------
    static void testPanicKeyLabel()
    {
        const KeyDef kd {
            KeyRole::VerbClear,
            "PANIC",  // natural
            "",       // funcLayer — no Func action on PANIC; hint absent
            -1, true
        };

        // 1. No modifier held: primary="PANIC", hint="" (hint absent when no funcLayer).
        {
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC no-mod: primary should be PANIC");
            CHECK(kl.hint.isEmpty(),     "PANIC no-mod: hint should be empty (no funcLayer)");
            CHECK(!kl.disabled,          "PANIC no-mod: should not be disabled");
        }

        // 2. Func held: still primary="PANIC", hint="" — Func is a no-op on PANIC.
        {
            auto ui = makeUiState();
            ui.funcHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC func-held: primary must stay PANIC");
            CHECK(kl.hint.isEmpty(),     "PANIC func-held: hint must be empty");
        }

        // 3. Track scope held: primary="PANIC", hint="" (CPC relabel is builder-level).
        {
            auto ui = makeUiState();
            ui.trackHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC track-scope: primary must stay PANIC");
            CHECK(kl.hint.isEmpty(),     "PANIC track-scope: hint must be empty");
        }

        // 4. Step held: primary="PANIC", hint="".
        {
            auto ui = makeUiState();
            ui.stepHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC step-held: primary must stay PANIC");
            CHECK(kl.hint.isEmpty(),     "PANIC step-held: hint must be empty");
        }
    }

    // -------------------------------------------------------------------------
    // Test: resolveKeyLabel() for a Nav key with funcLayer secondary.
    // resolveKeyLabel's "All other keys" branch returns { natural, funcLayer }
    // regardless of modifier state — Func-hint promotion to primary is handled
    // at the builder level (SurfaceModel.cpp), not inside resolveKeyLabel.
    // -------------------------------------------------------------------------
    static void testNavKeyFuncPromotion()
    {
        // Use ASCII to avoid juce::String(const char*) non-ASCII assert.
        // (Real builder stores char8_t* and uses the char8_t* constructor path.)
        const KeyDef kd {
            KeyRole::Nav,
            "NAV",   // stand-in for → (ASCII, avoids juce::String assert)
            "ROT",   // funcLayer (Func+→ = rotate in Slice 6)
            -1, true
        };

        // No modifier: primary=natural, hint=funcLayer (dim)
        {
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "NAV", "Nav key: primary should be natural");
            CHECK(kl.hint    == "ROT", "Nav key: hint should be funcLayer");
        }

        // Func held: resolveKeyLabel still returns { natural, funcLayer } for Nav.
        // Builder promotion (primary=funcLayer, hint={}) happens after resolveKeyLabel.
        {
            auto ui = makeUiState();
            ui.funcHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "NAV", "Nav key func-held: resolveKeyLabel primary stays natural");
            CHECK(kl.hint    == "ROT", "Nav key func-held: hint stays funcLayer (builder promotes)");
        }

        // No funcLayer: hint is empty (Func is a no-op on keys without funcLayer).
        {
            const KeyDef kdNoFunc { KeyRole::Nav, "NAV", "", -1, true };
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kdNoFunc, ui, ec);
            CHECK(kl.primary == "NAV", "Nav key no funcLayer: primary stays natural");
            CHECK(kl.hint.isEmpty(),   "Nav key no funcLayer: hint absent");
        }
    }

    // -------------------------------------------------------------------------
    // Test: resolveKeyLabel() for a SectionKey
    // -------------------------------------------------------------------------
    static void testSectionKeyLabel()
    {
        const KeyDef kd {
            KeyRole::SectionKey,
            "TRIG",   // natural (canonical TRIG section)
            "COND",   // funcLayer (meta section label)
            0, true   // sectionIdx=0, machineHasSection=true
        };

        // No modifier: primary="TRIG", hint="COND" (meta hint)
        {
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "TRIG", "TRIG no-mod: primary should be TRIG");
            CHECK(!kl.disabled,         "TRIG no-mod: should not be disabled");
        }

        // Track scope held: primary comes from scoped matrix, not natural
        {
            auto ui = makeUiState();
            ui.trackHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            // scoped label depends on ScopedSectionMatrix — just verify non-disabled
            CHECK(!kl.disabled, "TRIG track-scope: should not be disabled (slot 0 always has content)");
        }

        // Machine doesn't have this section: disabled
        {
            const KeyDef kdNoSection {
                KeyRole::SectionKey,
                "MOD", "MOD",
                4, false   // machineHasSection=false
            };
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kdNoSection, ui, ec);
            CHECK(kl.disabled, "MOD no-machine: should be disabled when machine lacks section");
        }
    }

    // -------------------------------------------------------------------------
    // Test: lengthEditCellState() — the §34.4 length-edit re-skin classifier.
    // Boundary rule: in-run for abs+1 < len, boundary at abs+1 == len, out
    // beyond. Shared by buildSurfaceModel() (focused Phrase+Func and broadcast
    // Morph+Func paths use the same mapping) so the visual cannot diverge.
    // -------------------------------------------------------------------------
    static void testLengthEditCellState()
    {
        // length 4: steps 0..2 in-run, step 3 boundary, step 4+ out.
        CHECK(lengthEditCellState(0, 4) == CellState::LengthInRun,
              "abs 0 / len 4: in-run");
        CHECK(lengthEditCellState(2, 4) == CellState::LengthInRun,
              "abs 2 / len 4: in-run");
        CHECK(lengthEditCellState(3, 4) == CellState::LengthBoundary,
              "abs 3 / len 4: boundary (last step)");
        CHECK(lengthEditCellState(4, 4) == CellState::LengthOutRun,
              "abs 4 / len 4: out-of-run");

        // length 1: only step 0 exists and it is the boundary.
        CHECK(lengthEditCellState(0, 1) == CellState::LengthBoundary,
              "abs 0 / len 1: boundary");
        CHECK(lengthEditCellState(1, 1) == CellState::LengthOutRun,
              "abs 1 / len 1: out-of-run");

        // A page-2 step (abs 16) against a single-page length is out-of-run.
        CHECK(lengthEditCellState(16, 16) == CellState::LengthOutRun,
              "abs 16 / len 16: out-of-run (empty page)");
        CHECK(lengthEditCellState(15, 16) == CellState::LengthBoundary,
              "abs 15 / len 16: boundary (full first page)");
    }

    // -------------------------------------------------------------------------
    // Test: clampStepPage() — the §34.4 double-tap scroll-past-end clamp.
    // In-range pages are [0, numPages-1]; one empty page (numPages) is reachable
    // only while unlocked, and the unlock auto-clears once back in range.
    // -------------------------------------------------------------------------
    static void testScrollPastEndClamp()
    {
        // Locked: cannot step past the last in-range page (numPages 2 → max 1).
        {
            const auto r = clampStepPage(/*desiredPage=*/2, /*numPages=*/2, /*unlocked=*/false);
            CHECK(r.page == 1,    "locked: clamps to last in-range page");
            CHECK(!r.unlocked,    "locked: stays locked");
        }
        // Unlocked: the empty page (index numPages) becomes reachable.
        {
            const auto r = clampStepPage(/*desiredPage=*/2, /*numPages=*/2, /*unlocked=*/true);
            CHECK(r.page == 2,    "unlocked: reaches the empty page");
            CHECK(r.unlocked,     "unlocked: stays unlocked while on the empty page");
        }
        // Unlocked but navigated back into range → auto-relock.
        {
            const auto r = clampStepPage(/*desiredPage=*/1, /*numPages=*/2, /*unlocked=*/true);
            CHECK(r.page == 1,    "unlocked+back: lands on last in-range page");
            CHECK(!r.unlocked,    "unlocked+back: auto-relocks once in range");
        }
        // Unlocked, but a longer length grew numPages so the former empty page is
        // now in range → auto-relock (page unchanged, still valid).
        {
            const auto r = clampStepPage(/*desiredPage=*/2, /*numPages=*/3, /*unlocked=*/true);
            CHECK(r.page == 2,    "length grew: page now in range");
            CHECK(!r.unlocked,    "length grew: auto-relocks");
        }
        // Cannot reach two empty pages: unlocked grants exactly one.
        {
            const auto r = clampStepPage(/*desiredPage=*/3, /*numPages=*/2, /*unlocked=*/true);
            CHECK(r.page == 2,    "unlocked grants exactly one empty page");
            CHECK(r.unlocked,     "still on the (single) empty page");
        }
        // Negative desired clamps to 0.
        {
            const auto r = clampStepPage(/*desiredPage=*/-1, /*numPages=*/4, /*unlocked=*/false);
            CHECK(r.page == 0,    "negative desired clamps to first page");
        }
    }

    void runSurfaceModelTests()
    {
        testPanicKeyLabel();
        testNavKeyFuncPromotion();
        testSectionKeyLabel();
        testLengthEditCellState();
        testScrollPastEndClamp();
    }

} // namespace lockstep
