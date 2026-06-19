// SurfaceModelTest — headless unit tests for pure SurfaceModel logic.
//
// Tests resolveKeyLabel() for key scenarios without a LockstepProcessor.
// Run via: lockstep_tests (exit 0 = pass, exit 1 = fail).

#include "TestHarness.h"
#include "../src/ui/KeyLabel.h"
#include "../src/ui/SurfaceModel.h"
#include "../src/ui/PageNav.h"
#include "../src/ui/CellAppearance.h"
#include "../src/ui/ScopedSectionMatrix.h"
#include "../src/machine/IMachine.h"
#include "../src/command/KeyBindings.h"

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
        const KeyDef kd{
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
            CHECK(kl.hint.isEmpty(), "PANIC no-mod: hint should be empty (no funcLayer)");
            CHECK(!kl.disabled, "PANIC no-mod: should not be disabled");
        }

        // 2. Func held: still primary="PANIC", hint="" — Func is a no-op on PANIC.
        {
            auto ui = makeUiState();
            ui.funcHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC func-held: primary must stay PANIC");
            CHECK(kl.hint.isEmpty(), "PANIC func-held: hint must be empty");
        }

        // 3. Track scope held: primary="PANIC", hint="" (CPC relabel is builder-level).
        {
            auto ui = makeUiState();
            ui.trackHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC track-scope: primary must stay PANIC");
            CHECK(kl.hint.isEmpty(), "PANIC track-scope: hint must be empty");
        }

        // 4. Step held: primary="PANIC", hint="".
        {
            auto ui = makeUiState();
            ui.stepHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC step-held: primary must stay PANIC");
            CHECK(kl.hint.isEmpty(), "PANIC step-held: hint must be empty");
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
        const KeyDef kd{
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
            CHECK(kl.hint == "ROT", "Nav key: hint should be funcLayer");
        }

        // Func held: resolveKeyLabel still returns { natural, funcLayer } for Nav.
        // Builder promotion (primary=funcLayer, hint={}) happens after resolveKeyLabel.
        {
            auto ui = makeUiState();
            ui.funcHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "NAV", "Nav key func-held: resolveKeyLabel primary stays natural");
            CHECK(kl.hint == "ROT", "Nav key func-held: hint stays funcLayer (builder promotes)");
        }

        // No funcLayer: hint is empty (Func is a no-op on keys without funcLayer).
        {
            const KeyDef kdNoFunc{ KeyRole::Nav, "NAV", "", -1, true };
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kdNoFunc, ui, ec);
            CHECK(kl.primary == "NAV", "Nav key no funcLayer: primary stays natural");
            CHECK(kl.hint.isEmpty(), "Nav key no funcLayer: hint absent");
        }
    }

    // -------------------------------------------------------------------------
    // Test: resolveKeyLabel() for a SectionKey
    // -------------------------------------------------------------------------
    static void testSectionKeyLabel()
    {
        const KeyDef kd{
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
            CHECK(!kl.disabled, "TRIG no-mod: should not be disabled");
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
            const KeyDef kdNoSection{
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
            CHECK(r.page == 1, "locked: clamps to last in-range page");
            CHECK(!r.unlocked, "locked: stays locked");
        }
        // Unlocked: the empty page (index numPages) becomes reachable.
        {
            const auto r = clampStepPage(/*desiredPage=*/2, /*numPages=*/2, /*unlocked=*/true);
            CHECK(r.page == 2, "unlocked: reaches the empty page");
            CHECK(r.unlocked, "unlocked: stays unlocked while on the empty page");
        }
        // Unlocked but navigated back into range → auto-relock.
        {
            const auto r = clampStepPage(/*desiredPage=*/1, /*numPages=*/2, /*unlocked=*/true);
            CHECK(r.page == 1, "unlocked+back: lands on last in-range page");
            CHECK(!r.unlocked, "unlocked+back: auto-relocks once in range");
        }
        // Unlocked, but a longer length grew numPages so the former empty page is
        // now in range → auto-relock (page unchanged, still valid).
        {
            const auto r = clampStepPage(/*desiredPage=*/2, /*numPages=*/3, /*unlocked=*/true);
            CHECK(r.page == 2, "length grew: page now in range");
            CHECK(!r.unlocked, "length grew: auto-relocks");
        }
        // Cannot reach two empty pages: unlocked grants exactly one.
        {
            const auto r = clampStepPage(/*desiredPage=*/3, /*numPages=*/2, /*unlocked=*/true);
            CHECK(r.page == 2, "unlocked grants exactly one empty page");
            CHECK(r.unlocked, "still on the (single) empty page");
        }
        // Negative desired clamps to 0.
        {
            const auto r = clampStepPage(/*desiredPage=*/-1, /*numPages=*/4, /*unlocked=*/false);
            CHECK(r.page == 0, "negative desired clamps to first page");
        }
    }

    // -------------------------------------------------------------------------
    // Test: appearanceOf() returns correct values from the CellStates.def table
    // and returns the fallback for an unknown token.
    // -------------------------------------------------------------------------
    static void testCellAppearance()
    {
        // StepTrigCertain — pidx 21 (green trig), solid on X-Touch
        CHECK(appearanceOf(CellState::StepTrigCertain).pushPad == 21,
              "StepTrigCertain pushPad");
        CHECK(appearanceOf(CellState::StepTrigCertain).xtouchVel == 127,
              "StepTrigCertain xtouchVel");

        // StepEmpty — off on X-Touch (step with no trig)
        CHECK(appearanceOf(CellState::StepEmpty).xtouchVel == 0,
              "StepEmpty xtouchVel");

        // MuteMuted — red (pidx 5), off on X-Touch
        CHECK(appearanceOf(CellState::MuteMuted).pushPad == 5,
              "MuteMuted pushPad");
        CHECK(appearanceOf(CellState::MuteMuted).xtouchVel == 0,
              "MuteMuted xtouchVel");

        // SelectorCurrent — flash (1) on X-Touch
        CHECK(appearanceOf(CellState::SelectorCurrent).xtouchVel == 1,
              "SelectorCurrent xtouchVel");

        // StepPlayhead — yellow screen fill
        CHECK(appearanceOf(CellState::StepPlayhead).screenFill == 0xFFFFCC44u,
              "StepPlayhead screenFill");

        // Unknown token → fallback (dark grey)
        const CellAppearance fb = appearanceOf(static_cast<CellState>(0xFFFF));
        CHECK(fb.screenFill == 0xFF303030u, "unknown token fallback screenFill");
        CHECK(fb.pushPad == 2, "unknown token fallback pushPad");

        // ConfirmYes — green (pidx 21, solid), ConfirmNo — red (pidx 5, solid)
        CHECK(appearanceOf(CellState::ConfirmYes).pushPad == 21, "ConfirmYes pushPad green");
        CHECK(appearanceOf(CellState::ConfirmYes).xtouchVel == 127, "ConfirmYes xtouchVel solid");
        CHECK(appearanceOf(CellState::ConfirmNo).pushPad == 5, "ConfirmNo pushPad red");
        CHECK(appearanceOf(CellState::ConfirmNo).xtouchVel == 127, "ConfirmNo xtouchVel solid");
    }

    // -------------------------------------------------------------------------
    // Test: PendingConfirm-layer binding rows — P shows YES/NO per Func state.
    // -------------------------------------------------------------------------
    static void testPendingConfirmBindings()
    {
        using CB = ControllerButton;
        using SL = SurfaceLayer;
        using CS = CellState;

        // Without Func: P resolves to VerbConfirm, "CONFIRM", ConfirmYes state
        const auto& yes = resolveBinding(CB::VerbConfirm, -1, kModNone, SL::PendingConfirm);
        CHECK(yes.action == ActionId::VerbConfirm, "PendingConfirm bare P → VerbConfirm");
        CHECK(juce::String(yes.primary) == "CONFIRM", "PendingConfirm bare P primary = CONFIRM");
        CHECK(yes.state == CS::ConfirmYes, "PendingConfirm bare P state = ConfirmYes");

        // With Func: P resolves to VerbCancel, "CANCEL", ConfirmNo state
        const auto& no = resolveBinding(CB::VerbConfirm, -1, kModFunc, SL::PendingConfirm);
        CHECK(no.action == ActionId::VerbCancel, "PendingConfirm Func+P → VerbCancel");
        CHECK(juce::String(no.primary) == "CANCEL", "PendingConfirm Func+P primary = CANCEL");
        CHECK(no.state == CS::ConfirmNo, "PendingConfirm Func+P state = ConfirmNo");
    }

    // -------------------------------------------------------------------------
    // Test: ScopedSectionMatrix canonical-name dedup
    // Cells that map to a canonical section name should equal the IMachine
    // constant, not a separate literal that could silently diverge.
    // -------------------------------------------------------------------------
    static void testScopedSectionMatrixCanonical()
    {
        using PS = EditMode::PrimaryScope;
        const auto& can = IMachine::kCanonicalSectionNames;

        // Track scope: indices 1-5 should match canonical names exactly.
        CHECK(scopedCell(PS::Track, 1).label == can[1], "Track+SRC matches canonical");
        CHECK(scopedCell(PS::Track, 2).label == can[2], "Track+FILTER matches canonical");
        CHECK(scopedCell(PS::Track, 3).label == can[3], "Track+AMP matches canonical");
        CHECK(scopedCell(PS::Track, 4).label == can[4], "Track+MOD matches canonical");
        CHECK(scopedCell(PS::Track, 5).label == can[5], "Track+FX matches canonical");

        // Track+TRIG is a genuine override ("DIV"), not canonical.
        CHECK(juce::String(scopedCell(PS::Track, 0).label) == "DIV",
              "Track+TRIG override is DIV");

        // Scene scope: index 0 is overridden to "TIME" (time-sig sticky); 2-5 match canonical.
        CHECK(juce::String(scopedCell(PS::Scene, 0).label) == "TIME",
              "Scene+TRIG override is TIME (time-sig sticky)");
        CHECK(scopedCell(PS::Scene, 2).label == can[2], "Scene+FILTER matches canonical");
        CHECK(scopedCell(PS::Scene, 3).label == can[3], "Scene+AMP matches canonical");
        CHECK(scopedCell(PS::Scene, 4).label == can[4], "Scene+MOD matches canonical");
        CHECK(scopedCell(PS::Scene, 5).label == can[5], "Scene+FX matches canonical");

        // Morph scope: SRC and AMP/MOD/FX match canonical; FLTR is a genuine abbreviation.
        CHECK(scopedCell(PS::Morph, 1).label == can[1], "Morph+SRC matches canonical");
        CHECK(scopedCell(PS::Morph, 3).label == can[3], "Morph+AMP matches canonical");
        CHECK(scopedCell(PS::Morph, 4).label == can[4], "Morph+MOD matches canonical");
        CHECK(scopedCell(PS::Morph, 5).label == can[5], "Morph+FX matches canonical");
        CHECK(juce::String(scopedCell(PS::Morph, 2).label) == "FLTR",
              "Morph+FILTER override is FLTR (abbreviated)");

        // Song scope: index 0 is "TEMPO" (tempo sticky); index 5 is "FX" (master-bus FX).
        CHECK(juce::String(scopedCell(PS::Song, 5).label) == "FX",
              "Song+FX override is FX");
        CHECK(juce::String(scopedCell(PS::Song, 0).label) == "TEMPO",
              "Song+TRIG override is TEMPO (tempo sticky)");
    }

    // -------------------------------------------------------------------------
    // Test: binding-table entries that drive scope-glow tints on nav + Mute.
    // The tint logic in SurfaceModel.cpp derives colour from these rows;
    // if the rows change their requiredMods the tint would silently break.
    // -------------------------------------------------------------------------
    static void testScopeTintBindings()
    {
        using CB = ControllerButton;
        using SL = SurfaceLayer;
        using AId = ActionId;

        // NavUp under Track → CycleInputModeUp; requiredMods includes kModTrack.
        {
            const auto& b = resolveBinding(CB::NavUp, -1, kModTrack, SL::Base);
            CHECK(b.action == AId::CycleInputModeUp, "NavUp+Track resolves to CycleInputModeUp");
            CHECK((b.requiredMods & kModTrack) != 0, "NavUp+Track row includes Track bit");
        }
        // NavDown under Track → CycleInputModeDown; requiredMods includes kModTrack.
        {
            const auto& b = resolveBinding(CB::NavDown, -1, kModTrack, SL::Base);
            CHECK(b.action == AId::CycleInputModeDown, "NavDown+Track resolves to CycleInputModeDown");
            CHECK((b.requiredMods & kModTrack) != 0, "NavDown+Track row includes Track bit");
        }
        // MuteScope under Scene → HoldSceneMuteView; requiredMods includes kModScene.
        {
            const auto& b = resolveBinding(CB::MuteScope, -1, kModScene, SL::Base);
            CHECK(b.action == AId::HoldSceneMuteView, "MuteScope+Scene resolves to HoldSceneMuteView");
            CHECK((b.requiredMods & kModScene) != 0, "MuteScope+Scene row includes Scene bit");
        }
        // VerbClear under Phrase → VerbDelete or VerbScopeClear with Phrase bit.
        {
            const auto& b = resolveBinding(CB::VerbClear, -1, kModPhrase | kModFunc, SL::Base);
            CHECK(b.action == AId::VerbDelete, "VerbClear+Phrase+Func → VerbDelete");
            CHECK((b.requiredMods & kModPhrase) != 0, "VerbClear+Phrase+Func row includes Phrase bit");
        }
    }

    // -------------------------------------------------------------------------
    // Test: density-sticky + Func held — MOD key primary must be non-empty.
    // Regression guard for the jassert(c.disabled || !c.primary.isEmpty()) crash:
    // density-sticky repurposes the MOD key (§39.5) with a non-empty primary and
    // empty hint; the SurfaceModel Func-promotion guard must not overwrite primary
    // with the empty hint when Func is held.
    // -------------------------------------------------------------------------
    static void testDensityStickyFuncInvariant()
    {
        const KeyDef kdMod{
            KeyRole::SectionKey,
            "MOD",   // natural
            "DENS",  // funcLayer — density-sticky entry hint
            4, true  // sectionIdx=4, machineHasSection=true
        };

        // density-sticky + Func: MOD key shows sub-page cycle label (not empty)
        {
            auto ui = makeUiState();
            ui.densityStickyMode = true;
            ui.funcHeld = true;
            ui.densitySubPage = UiState::DensitySubPage::Amount;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kdMod, ui, ec);
            CHECK(!kl.primary.isEmpty(),
                  "density-sticky+Func: MOD key primary must not be empty");
            CHECK(!kl.disabled,
                  "density-sticky+Func: MOD key must not be disabled");
        }

        // density-sticky alone (no Func): same invariant
        {
            auto ui = makeUiState();
            ui.densityStickyMode = true;
            ui.densitySubPage = UiState::DensitySubPage::Musicality;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kdMod, ui, ec);
            CHECK(!kl.primary.isEmpty(),
                  "density-sticky: MOD key primary must not be empty");
        }
    }

    void runSurfaceModelTests()
    {
        testPanicKeyLabel();
        testNavKeyFuncPromotion();
        testSectionKeyLabel();
        testLengthEditCellState();
        testScrollPastEndClamp();
        testCellAppearance();
        testScopedSectionMatrixCanonical();
        testPendingConfirmBindings();
        testScopeTintBindings();
        testDensityStickyFuncInvariant();
    }

} // namespace lockstep
