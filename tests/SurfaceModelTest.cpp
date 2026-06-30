// SurfaceModelTest — headless unit tests for pure SurfaceModel logic.
//
// Tests resolveKeyLabel() for key scenarios without a LockstepProcessor.
// Run via: lockstep_tests (exit 0 = pass, exit 1 = fail).

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/ui/KeyLabel.h"
#include "../src/ui/SurfaceModel.h"
#include "../src/ui/PageNav.h"
#include "../src/ui/CellAppearance.h"
#include "../src/ui/ScopedSectionMatrix.h"
#include "../src/ui/GridDisplayMode.h"
#include "../src/machine/IMachine.h"
#include "../src/machine/LooperMachine.h"
#include "../src/machine/VAMachine.h"
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
        // Scene+FX is dim — FX is not scene-scoped (track FX = Track scope, 9.14).
        CHECK(!scopedCell(PS::Scene, 5).hasContent, "Scene+FX is dim (no FX content)");

        // Morph scope: SRC and AMP/MOD/FX match canonical; FLTR is a genuine abbreviation.
        CHECK(scopedCell(PS::Morph, 1).label == can[1], "Morph+SRC matches canonical");
        CHECK(scopedCell(PS::Morph, 3).label == can[3], "Morph+AMP matches canonical");
        CHECK(scopedCell(PS::Morph, 4).label == can[4], "Morph+MOD matches canonical");
        CHECK(scopedCell(PS::Morph, 5).label == can[5], "Morph+FX matches canonical");
        CHECK(juce::String(scopedCell(PS::Morph, 2).label) == "FLTR",
              "Morph+FILTER override is FLTR (abbreviated)");

        // Song scope: index 0 is "TIME" (unified TIME sticky); index 5 is "FX" (master-bus FX).
        CHECK(juce::String(scopedCell(PS::Song, 5).label) == "FX",
              "Song+FX override is FX");
        CHECK(juce::String(scopedCell(PS::Song, 0).label) == "TIME",
              "Song+TRIG override is TIME (TIME page sticky)");
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
            ui.overlay = Overlay::Density;
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
            ui.overlay = Overlay::Density;
            ui.densitySubPage = UiState::DensitySubPage::Musicality;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kdMod, ui, ec);
            CHECK(!kl.primary.isEmpty(),
                  "density-sticky: MOD key primary must not be empty");
        }
    }

    // =========================================================================
    // 9.10 §19: F/J home-key anchor markers present in every layer
    // =========================================================================

    static void testHomeKeyAnchors()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        EditContext ec;

        const SurfaceModel model = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        // F (index 1) and J (index 4) must have homeKey=true in Base layer.
        CHECK(model.step[1].homeKey, "step[1] (F) homeKey=true in Base layer");
        CHECK(model.step[4].homeKey, "step[4] (J) homeKey=true in Base layer");

        // All other step cells must have homeKey=false.
        for (int i = 0; i < 16; ++i)
        {
            if (i == 1 || i == 4) continue;
            CHECK(!model.step[static_cast<std::size_t>(i)].homeKey,
                  juce::String("step[") + juce::String(i) + "] homeKey=false");
        }
    }

    // -------------------------------------------------------------------------
    // Test (#1): Track scope held on a Looper relabels the U/I/O verb cells to
    // loop controls (REC/PLAY/ERASE), state-aware, instead of COPY/PASTE/CLEAR.
    // A non-looper track keeps the clipboard verbs. functionRow[6]=U, [7]=I, [8]=O.
    // -------------------------------------------------------------------------
    static void testLooperVerbRelabel()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;

        // Non-looper track under Track scope: clipboard verbs survive.
        proc.setTrackMachine(0, VAMachine::kMachineId);
        {
            UiState ui; ui.trackHeld = true;
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.functionRow[6].primary == "COPY",  "non-looper U stays COPY");
            CHECK(m.functionRow[7].primary == "PASTE", "non-looper I stays PASTE");
            CHECK(m.functionRow[8].primary == "CLEAR", "non-looper O stays CLEAR");
        }

        // Looper track, Idle, under Track scope: U=REC, I=PLAY, O=ERASE.
        proc.setTrackMachine(1, LooperMachine::kMachineId);
        proc.setFocusTrack(1);
        {
            UiState ui; ui.trackHeld = true;
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 1, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.functionRow[6].primary == "REC",   "looper Idle: U = REC");
            CHECK(m.functionRow[7].primary == "PLAY",  "looper Idle: I = PLAY");
            CHECK(m.functionRow[8].primary == "ERASE", "looper Track-scope: O = ERASE");
            // #3: the verb cells carry distinct looper CellState tokens (not the
            // generic clipboard colours), so they read as their own family.
            CHECK(m.functionRow[6].base == CellState::LooperRecReady,
                  "looper Idle: U cell = LooperRecReady token");
            CHECK(m.functionRow[7].base == CellState::LooperPlayReady,
                  "looper Idle: I cell = LooperPlayReady token");
            CHECK(m.functionRow[8].base == CellState::LooperErase,
                  "looper: O cell = LooperErase token");
        }

        // Without Track scope held, the relabel does not apply: O is the bare CLEAR
        // verb, not ERASE (bare U is legitimately "REC", so O is the discriminator).
        {
            UiState ui;  // nothing held
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 1, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.functionRow[8].primary != "ERASE",
                  "looper relabel requires Track scope held");
        }
    }

    // -------------------------------------------------------------------------
    // Test: generator hub model populates c.primary (Bug B regression guard).
    // When euclidHeld=true the model must carry EUCLID/DENSITY/VEL primary text
    // so the generic hub renderer can draw it without a screen-only residual.
    // -------------------------------------------------------------------------
    static void testGeneratorHubPrimary()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.generatorHubHeld = true;
        EditContext ec;

        const SurfaceModel model = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        CHECK(model.step[0].primary == "EUCLID",  "hub cell 0 primary = EUCLID");
        CHECK(model.step[1].primary == "DENSITY", "hub cell 1 primary = DENSITY");
        CHECK(model.step[2].primary == "VEL",     "hub cell 2 primary = VEL");
        CHECK(model.step[3].primary == "MELODY",  "hub cell 3 primary = MELODY");
        CHECK(model.step[4].primary == "CHORD",   "hub cell 4 primary = CHORD");
        // Cells 5-15 must not carry text (they are dark/inactive).
        for (int i = 5; i < 16; ++i)
            CHECK(model.step[static_cast<std::size_t>(i)].primary.isEmpty(),
                  juce::String("hub cell ") + juce::String(i) + " primary empty");
    }

    // -------------------------------------------------------------------------
    // Test: machine picker model populates c.primary (model-driven label SSOT).
    // -------------------------------------------------------------------------
    static void testMachinePickerPrimary()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.funcTrackHeld = true;
        EditContext ec;

        const SurfaceModel model = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        const int numMachines = proc.numAvailableMachines();
        for (int i = 0; i < 16; ++i)
        {
            const auto& c = model.step[static_cast<std::size_t>(i)];
            if (i < numMachines)
                CHECK(c.primary.isNotEmpty(),
                      juce::String("machine picker cell ") + juce::String(i) + " primary non-empty");
            else
                CHECK(c.primary.isEmpty(),
                      juce::String("machine picker out-of-range cell ") + juce::String(i) + " primary empty");
        }
    }

    // ── 9.12 anti-drift oracle: slot strings must equal grammar resolution ────────
    // Ensures deriveSlots() is consistent with resolveBinding() per gesture.
    static void testDeriveSlotEqualsGrammar()
    {
        using CB = ControllerButton;
        using SL = SurfaceLayer;
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        EditContext ec;
        const SurfaceModel model = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        // TapTempo (model.tap): promoted=Hold, hold=GEN HUB, dbl=none.
        {
            const auto& c = model.tap;
            const auto tap  = resolveBinding(CB::TapTempo, -1, kModNone, SL::Base, Gesture::Tap);
            const auto hold = resolveBinding(CB::TapTempo, -1, kModNone, SL::Base, Gesture::Hold);
            const auto dbl  = resolveBinding(CB::TapTempo, -1, kModNone, SL::Base, Gesture::DoubleTap);
            CHECK(c.primaryGesture == Gesture::Hold, "TapTempo: primaryGesture=Hold");
            CHECK(c.primary == juce::String(hold.primary), "TapTempo: primary=GEN HUB");
            // tapLabel present (secondary, since primary is hold)
            CHECK(c.tapLabel == juce::String(tap.primary), "TapTempo: tapLabel=TAP");
            CHECK(c.holdLabel.isEmpty(), "TapTempo: holdLabel empty (hold is primary)");
            CHECK(c.doubleTapLabel.isEmpty() == (dbl.action == ActionId::None),
                  "TapTempo: doubleTapLabel consistent with grammar");
        }

        // Func (modifiers[0]): promoted=Hold, primary=FUNC, dbl=ESCAPE. The legacy
        // Tap row shares the HoldFuncScope action, so the tap rail is suppressed.
        {
            const auto& c = model.modifiers[0];
            const auto hold = resolveBinding(CB::Func, -1, kModNone, SL::Base, Gesture::Hold);
            const auto dbl  = resolveBinding(CB::Func, -1, kModNone, SL::Base, Gesture::DoubleTap);
            CHECK(c.primaryGesture == Gesture::Hold, "Func: primaryGesture=Hold");
            CHECK(c.primary == juce::String(hold.primary), "Func: primary=FUNC");
            CHECK(c.tapLabel.isEmpty(), "Func: tapLabel empty (tap duplicates hold action)");
            CHECK(c.doubleTapLabel == juce::String(dbl.primary), "Func: doubleTapLabel=ESCAPE");
        }

        // Modifier scope key (Track, modifiers[1]): hold-to-scope, no phantom tap.
        // primary = bare "TRACK"; tap rail suppressed (tap/hold share HoldTrackScope);
        // dbl = LATCH; func variant = KIT.
        {
            const auto& c = model.modifiers[1];
            const auto hold = resolveBinding(CB::TrackScope, -1, kModNone, SL::Base, Gesture::Hold);
            const auto dbl  = resolveBinding(CB::TrackScope, -1, kModNone, SL::Base, Gesture::DoubleTap);
            CHECK(c.primaryGesture == Gesture::Hold, "Track: primaryGesture=Hold");
            CHECK(c.primary == juce::String(hold.primary), "Track: primary=TRACK (bare)");
            CHECK(c.tapLabel.isEmpty(), "Track: tapLabel empty (no phantom tap action)");
            CHECK(c.holdLabel.isEmpty(), "Track: holdLabel empty (hold is primary)");
            CHECK(c.doubleTapLabel == juce::String(dbl.primary), "Track: doubleTapLabel=LATCH");
            CHECK(c.funcHint == juce::String(u8"KIT"), "Track: funcHint=KIT");
        }

        // Held-modifier context promotion: with Func held, a key's primary must show
        // the most-specific action for that context, not the bare at-rest label.
        // Func+Song = GLOBAL (the func variant), promoted into the large slot.
        {
            UiState fui;
            fui.funcHeld = true;
            const SurfaceModel fm = buildSurfaceModel(
                fui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            const auto& c = fm.modifiers[5];  // Song
            const auto fn = resolveBinding(CB::SongScope, -1, kModFunc, SL::Base, Gesture::Tap);
            CHECK(c.primary == juce::String(fn.primary), "Func+Song: primary=GLOBAL (func variant)");
            CHECK(c.primaryGesture == Gesture::Tap, "Func+Song: primaryGesture=Tap");
            // Bare SONG hold + LATCH must NOT leak into the func context.
            CHECK(c.holdLabel.isEmpty(), "Func+Song: bare SONG hold suppressed");
            CHECK(c.doubleTapLabel.isEmpty(), "Func+Song: bare LATCH suppressed");
        }

        // VerbPlay (functionRow[7]): promoted=Tap, tap=PLAY, dbl=STOP.
        {
            const auto& c = model.functionRow[7];  // VerbPlay
            CHECK(c.primaryGesture == Gesture::Tap, "VerbPlay: primaryGesture=Tap");
            const auto tap = resolveBinding(CB::VerbPlay, -1, kModNone, SL::Base, Gesture::Tap);
            const auto dbl = resolveBinding(CB::VerbPlay, -1, kModNone, SL::Base, Gesture::DoubleTap);
            CHECK(c.primary == juce::String(tap.primary), "VerbPlay: primary=PLAY");
            CHECK(c.tapLabel.isEmpty(), "VerbPlay: tapLabel empty (tap is primary)");
            CHECK(c.doubleTapLabel == juce::String(dbl.primary), "VerbPlay: doubleTapLabel=STOP");
        }
    }

    // -------------------------------------------------------------------------
    // Test: section-row Func-layer secondaries must match what dispatch honors.
    //
    // Regression guard for the "label promises a panel that never opens" class:
    // 9.10 relocated the velocity / density generators to the generator hub on
    // `3`, freeing Func+AMP / Func+MOD. Dispatch (PluginEditor MetaSection case)
    // ignores those presses via isReservedMeta — but the SurfaceModel display
    // map kept advertising "VEL" on AMP and "DENS" on MOD, so the section row
    // showed velocity/density hints that brought up nothing in the MZ.
    //
    // Every non-empty Func secondary on the section row must correspond to a
    // dispatchable Func+section action:
    //   TRIG → COND, SRC → NOTE  (routed via selectMetaSection)
    //   FILTER → TRSP            (Func+7 = transport globals + per-track Scale,
    //                             README §5.8 — selectMetaSection(2) → Transport)
    //   FX                       (no Func action — picker moved to hold gesture,
    //                             9.14 Stage 2; "PICK FX" is the hold rail, not Func)
    //   AMP / MOD                (no Func action → must dim, hint empty)
    // -------------------------------------------------------------------------
    static void testSectionFuncHintsMatchDispatch()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;          // resting state: no modifier held
        EditContext ec;

        const SurfaceModel model = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        const char* expected[IMachine::kMaxSections] = {
            "COND", "NOTE", "TRSP", "", "", ""
        };
        for (int s = 0; s < IMachine::kMaxSections; ++s)
        {
            const auto& c = model.section[static_cast<std::size_t>(s)];
            CHECK(c.funcHint == juce::String(expected[s]),
                  juce::String("section ") + juce::String(s)
                      + " Func secondary must match dispatch (got '" + c.funcHint + "')");
        }

        // The specific regression: AMP/MOD must not re-advertise the relocated
        // velocity/density generators (now on the hub key `3`).
        CHECK(model.section[3].funcHint != "VEL",
              "AMP must not advertise VEL — generator moved to hub on 3 (9.10)");
        CHECK(model.section[4].funcHint != "DENS",
              "MOD must not advertise DENS — generator moved to hub on 3 (9.10)");
    }

    // -------------------------------------------------------------------------
    // 9.14: the FX section key always keeps its builder identity ("FX") as the
    // primary; the picker is scope-gated, so the "PICK FX"/"PICK MASTER FX" hold
    // label only appears under the scope where the picker actually fires (Track =
    // track inserts, Song = master). Bare FX advertises no picker.
    // -------------------------------------------------------------------------
    static void testFxSectionPrimaryNotPicker()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;

        // Bare (no scope): primary "FX", no picker advertised on any rail.
        {
            UiState ui;
            const SurfaceModel m = buildSurfaceModel(
                ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            const auto& fx = m.section[5];
            CHECK(fx.primary == "FX", "bare FX primary is 'FX'");
            CHECK(fx.holdLabel.isEmpty(), "bare FX advertises no picker (scope-gated)");
            CHECK(fx.funcHint.isEmpty(), "bare FX has no Func secondary");
        }

        // Track held: primary stays "FX", track picker on the hold rail.
        {
            UiState ui;
            ui.trackHeld = true;
            const SurfaceModel m = buildSurfaceModel(
                ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            const auto& fx = m.section[5];
            CHECK(fx.primary == "FX", "Track+FX primary stays 'FX' (not 'PICK FX')");
            CHECK(fx.holdLabel == "PICK FX", "Track+hold FX = track picker (hold rail)");
        }

        // Song held: primary stays "FX" (not the promoted "PICK MASTER FX"),
        // master picker on the hold rail.
        {
            UiState ui;
            ui.songHeld = true;
            const SurfaceModel m = buildSurfaceModel(
                ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            const auto& fx = m.section[5];
            CHECK(fx.primary == "FX", "Song+FX primary stays 'FX' (not 'PICK MASTER FX')");
            CHECK(fx.holdLabel == "PICK MASTER FX", "Song+hold FX = master picker (hold rail)");
            CHECK(fx.funcHint.isEmpty(), "Song+FX has no stale Func secondary");
        }

        // Song+Func: the picker is a non-Func gesture (Func remaps Section→
        // MetaSection and never reaches the picker arming), so no picker rail is
        // advertised — the label must not promise something that won't fire.
        {
            UiState ui;
            ui.songHeld = true;
            ui.funcHeld = true;
            const SurfaceModel m = buildSurfaceModel(
                ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            CHECK(m.section[5].holdLabel.isEmpty(),
                  "Song+Func FX advertises no picker (Func is not a picker gesture)");
            CHECK(m.section[5].disabled,
                  "Song+Func FX is inert (no Func-layer FX action; was the old GLOBAL slot)");
        }

        // Guard the tie-break the other way: VerbSnapshot under Func must still
        // promote the label-bearing hold row (→ FLOOR), since its tap row carries
        // its own primary (RESTORE).
        UiState fui;
        fui.funcHeld = true;
        const SurfaceModel fm = buildSurfaceModel(
            fui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
        const auto hold = resolveBinding(ControllerButton::VerbSnapshot, -1, kModFunc,
                                         SurfaceLayer::Base, Gesture::Hold);
        CHECK(fm.functionRow[5].primary == juce::String(hold.primary),
              "VerbSnapshot+Func still promotes hold label (tie-break unbroken)");
    }

    // -------------------------------------------------------------------------
    // 9.14 fix 3: with a step held (StepInspector), SRC announces the note editor
    // as its primary ("NOTE") with no Func hint — it must not advertise the
    // retired Func+SRC access path. At rest it reads "SRC".
    // -------------------------------------------------------------------------
    static void testSrcAnnouncesNoteEditWhenStepHeld()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;

        {
            UiState ui;  // at rest
            const SurfaceModel model = buildSurfaceModel(
                ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            CHECK(model.section[1].primary == "SRC", "SRC reads 'SRC' at rest");
        }
        {
            UiState ui;  // step held → inspector
            ui.pLockClearMode = true;
            ui.pLockClearTrack = 0;
            ui.pLockClearStep = 2;
            const SurfaceModel model = buildSurfaceModel(
                ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            CHECK(model.section[1].primary == "NOTE",
                  "SRC announces 'NOTE' when a step is held");
            CHECK(model.section[1].funcHint.isEmpty(),
                  "SRC drops the Func hint in the inspector (no 'hold Func' path)");
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
        testHomeKeyAnchors();
        testLooperVerbRelabel();
        testGeneratorHubPrimary();
        testMachinePickerPrimary();
        testDeriveSlotEqualsGrammar();
        testSectionFuncHintsMatchDispatch();
        testFxSectionPrimaryNotPicker();
        testSrcAnnouncesNoteEditWhenStepHeld();
    }

} // namespace lockstep
