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
#include "../src/ui/SectionResolve.h"
#include "../src/ui/GridDisplayMode.h"
#include "../src/machine/IMachine.h"
#include "../src/ParameterIDs.h"
#include "../src/machine/LoopMachine.h"
#include "../src/machine/TapeMachine.h"
#include "../src/machine/AnalogMachine.h"
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
    // Test (#1): Track scope held on a Loop relabels the U/I/O verb cells to
    // loop controls (REC/PLAY/ERASE), state-aware, instead of COPY/PASTE/CLEAR.
    // A non-looper track keeps the clipboard verbs. functionRow[6]=U, [7]=I, [8]=O.
    // -------------------------------------------------------------------------
    static void testLooperConsole()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;

        // S3: the looper transport verbs (Track+U/I/O) are RETIRED. Under Track
        // scope on a looper the verbs revert to the ordinary clipboard grammar
        // (the layer is the scope selector, not a looper relabel).
        proc.setTrackMachine(0, AnalogMachine::kMachineId);
        proc.setTrackMachine(1, LoopMachine::kMachineId);
        proc.setFocusTrack(1);
        {
            UiState ui; ui.trackHeld = true;
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 1, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.functionRow[6].primary == "COPY",  "S3: looper Track+U reverts to COPY");
            CHECK(m.functionRow[7].primary == "PASTE", "S3: looper Track+I reverts to PASTE");
            CHECK(m.functionRow[8].primary == "CLEAR", "S3: looper Track+O reverts to CLEAR");
        }

        // S3: a focused looper with nothing held shows the always-on console on the
        // step grid — transport on the top row, performance on the bottom.
        {
            UiState ui;  // nothing held
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 1, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.step[0].primary == "REC",   "S3: console cell 0 = REC");
            CHECK(m.step[1].primary == "PLAY",  "S3: console cell 1 = PLAY");
            CHECK(m.step[2].primary == "STOP",  "S3: console cell 2 = STOP");
            CHECK(m.step[3].primary == "ERASE", "S3: console cell 3 = ERASE");
            CHECK(m.step[4].primary == "UNDO",  "S3: console cell 4 = UNDO");
            CHECK(m.step[7].primary == "DUB",   "S3: console cell 7 = DUB");
            // Idle state → REC cell carries the resting record token (not active).
            CHECK(m.step[0].base == CellState::LooperConRec,
                  "S3: idle console REC = LooperConRec token");
            // Bottom row = performance cells (beat-repeat / tape).
            CHECK(m.step[8].base == CellState::LooperConRpt,
                  "S3: console cell 8 = beat-repeat token");
            CHECK(m.step[12].base == CellState::LooperConTape,
                  "S3: console cell 12 = tape-FX token");
        }

        // A non-looper focused track is NOT a console — it shows the normal grid.
        {
            UiState ui;
            proc.setFocusTrack(0);
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.step[0].primary != "REC",
                  "S3: non-looper track does not show the console");
        }

        // 11.8 (§40.5): the TRACKS page. A single-sub-track deck has no TRACKS page,
        // so paging is a no-op; a 4-sub-track deck pages to the ARM/MUTE/SOLO/SRC
        // grid.
        {
            proc.setFocusTrack(1);
            // Single sub-track (default): page 1 falls back to the DECK layout.
            UiState ui; ui.deckConsolePage = 1;
            auto m = buildSurfaceModel(ui, ec, nullptr, proc, 1, 0,
                                       GridDisplayMode::Ortholinear);
            CHECK(m.step[0].primary == "REC",
                  "a single-sub-track deck has no TRACKS page (stays DECK)");

            // Grow to four sub-tracks, let the machine adopt it, then page.
            proc.writeParam(1, proc.slotForId(1, "subtrack_count"), 4.0f);
            h.renderBlocks(1);  // writeParam is queued; process propagates to the deck
            CHECK(proc.looperSubTrackCount(1) == 4, "the deck adopted four sub-tracks");

            m = buildSurfaceModel(ui, ec, nullptr, proc, 1, 0,
                                  GridDisplayMode::Ortholinear);
            // Row 0 = ARM/MUTE/SOLO/SRC of sub-track 0.
            CHECK(m.step[0].base == CellState::DeckTrkArmOn,
                  "TRACKS row 0 col 0 = ARM (armed by default)");
            CHECK(m.step[1].base == CellState::DeckTrkMute, "col 1 = MUTE (audible)");
            CHECK(m.step[2].base == CellState::DeckTrkSolo, "col 2 = SOLO (not soloed)");
            CHECK(m.step[3].base == CellState::DeckTrkSrc,  "col 3 = SRC");
            CHECK(m.step[3].primary == "Ext1", "SRC shows the source label (default Ext1)");
            // Rows 0-3 all present (count == 4), none empty. Sub 3 is DISARMED by
            // default (§40.3: only sub 0 is armed — the TRACKS console arms the rest).
            CHECK(m.step[12].base == CellState::DeckTrkArm, "row 3 present, disarmed by default");

            // Back to the DECK page.
            ui.deckConsolePage = 0;
            m = buildSurfaceModel(ui, ec, nullptr, proc, 1, 0,
                                  GridDisplayMode::Ortholinear);
            CHECK(m.step[0].primary == "REC", "page 0 is the DECK layout");
        }
    }

    // 11.4 (§40.5): a focused Tape shows the always-on console — transport on the
    // top row, markers on the bottom — via the MachineConsole layer dispatched on
    // machine type (not the Route matrix).
    static void testTapeConsole()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;
        proc.setTrackMachine(0, TapeMachine::kMachineId);
        proc.setFocusTrack(0);

        UiState ui;  // nothing held
        const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                         GridDisplayMode::Ortholinear);
        CHECK(m.step[0].primary == "REC",   "tape console cell 0 = REC");
        // Stage 4: cells 1/2 (PLAY/STOP) retired — the Tape follows the main transport.
        CHECK(m.step[1].primary == "",      "cell 1 blank (no separate Play)");
        CHECK(m.step[2].primary == "",      "cell 2 blank (no separate Stop)");
        CHECK(m.step[3].primary == "CLEAR", "cell 3 = CLEAR");
        CHECK(m.step[8].primary == "DROP",  "cell 8 = DROP marker");
        CHECK(m.step[10].primary == "CUE",  "cell 10 = CUE");
        // Idle Tape → REC cell carries the resting token, not the active one.
        CHECK(m.step[0].base == CellState::TapeConRec, "idle tape REC = resting token");

        // Punch in → the REC cell lights active.
        proc.tapeApplyVerb(0, 1);
        const auto m2 = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                          GridDisplayMode::Ortholinear);
        CHECK(m2.step[0].base == CellState::TapeConRecActive, "recording tape REC = active token");

        // §40.2 hold-to-wind cells (<< / >>) exist only when the transport is
        // windable. The harness reports as a plugin with the default Locked sync
        // (host owns the playhead), so they are suppressed.
        CHECK(! proc.transportWindable(), "harness default (hosted-locked) is not windable");
        CHECK(m.step[12].base == CellState::TapeConIdle, "no wind cell when suppressed");
        CHECK(m.step[13].base == CellState::TapeConIdle, "no wind cell when suppressed");

        // Switch to Auto (Lockstep owns the transport) → the wind cells appear.
        if (auto* sm = proc.apvts().getParameter(ParamIDs::syncMode))
            sm->setValueNotifyingHost(1.0f);
        CHECK(proc.transportWindable(), "Auto sync → windable");
        const auto m3 = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                          GridDisplayMode::Ortholinear);
        CHECK(m3.step[12].primary == "<<" && m3.step[12].base == CellState::TapeConRew,
              "windable → cell 12 = << rewind");
        CHECK(m3.step[13].primary == ">>" && m3.step[13].base == CellState::TapeConFwd,
              "windable → cell 13 = >> fast-forward");

        // Stage 6d (§40.5): the Tape is a 4-sub deck and shares the TRACKS page.
        // A single-sub tape has no TRACKS page (paging is a no-op); grow to 4 and
        // Nav pages to the ARM/MUTE/SOLO/SRC grid, same cells as the Loop.
        {
            UiState tui; tui.deckConsolePage = 1;
            auto s = buildSurfaceModel(tui, ec, nullptr, proc, 0, 0,
                                       GridDisplayMode::Ortholinear);
            CHECK(s.step[0].primary == "REC",
                  "a single-sub tape has no TRACKS page (stays on the tape console)");

            proc.writeParam(0, proc.slotForId(0, "subtrack_count"), 4.0f);
            h.renderBlocks(1);   // propagate the param into the deck
            CHECK(proc.looperSubTrackCount(0) == 4, "the tape adopted four sub-tracks");

            s = buildSurfaceModel(tui, ec, nullptr, proc, 0, 0,
                                  GridDisplayMode::Ortholinear);
            // Sub 0 source is External by default → armed; the SRC cell shows a label.
            CHECK(s.step[0].base == CellState::DeckTrkArmOn,
                  "tape TRACKS row 0 = ARM on (sub 0 sourced by default)");
            CHECK(s.step[1].base == CellState::DeckTrkMute, "col 1 = MUTE");
            CHECK(s.step[2].base == CellState::DeckTrkSolo, "col 2 = SOLO");
            CHECK(s.step[3].base == CellState::DeckTrkSrc,  "col 3 = SRC");
            // Sub 1 has no source by default → disarmed.
            CHECK(s.step[4].base == CellState::DeckTrkArm,
                  "tape TRACKS row 1 disarmed (no source by default)");

            // Back to the tape console page.
            tui.deckConsolePage = 0;
            s = buildSurfaceModel(tui, ec, nullptr, proc, 0, 0,
                                  GridDisplayMode::Ortholinear);
            CHECK(s.step[0].primary == "REC", "page 0 is the tape console");
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

        // VerbPlay (functionRow[7]) is transport-stateful (layered stop):
        //  - stopped: primary=PLAY, no CUT / MASTER CUT secondaries (nothing to cut);
        //  - running: primary=PAUSE, dbl=CUT, triple=MASTER CUT.
        {
            const auto dbl  = resolveBinding(CB::VerbPlay, -1, kModNone, SL::Base, Gesture::DoubleTap);
            const auto trip = resolveBinding(CB::VerbPlay, -1, kModNone, SL::Base, Gesture::TripleTap);

            proc.transportPause();  // force stopped
            const auto stopped = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                                   GridDisplayMode::Ortholinear);
            const auto& cs = stopped.functionRow[7];
            CHECK(cs.primaryGesture == Gesture::Tap, "VerbPlay: primaryGesture=Tap");
            CHECK(cs.primary == juce::String("PLAY"), "VerbPlay stopped: primary=PLAY");
            CHECK(cs.tapLabel.isEmpty(), "VerbPlay: tapLabel empty (tap is primary)");
            CHECK(cs.doubleTapLabel.isEmpty(), "VerbPlay stopped: no CUT (nothing to cut)");
            CHECK(cs.tripleTapLabel.isEmpty(), "VerbPlay stopped: no MASTER CUT");

            proc.transportPlay();  // -> running
            const auto running = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                                   GridDisplayMode::Ortholinear);
            const auto& cr = running.functionRow[7];
            CHECK(cr.primary == juce::String("PAUSE"), "VerbPlay running: primary=PAUSE");
            CHECK(cr.doubleTapLabel == juce::String(dbl.primary), "VerbPlay running: doubleTapLabel=CUT");
            CHECK(cr.tripleTapLabel == juce::String(trip.primary), "VerbPlay running: tripleTapLabel=MASTER CUT");
            proc.transportPause();  // restore
        }

        // VerbRecord (functionRow[6]): tap REC keeps the primary; RESET is the hold
        // secondary (the RESET row must not steal the big label).
        {
            const auto& c = model.functionRow[6];  // VerbRecord
            const auto tap  = resolveBinding(CB::VerbRecord, -1, kModNone, SL::Base, Gesture::Tap);
            const auto hold = resolveBinding(CB::VerbRecord, -1, kModNone, SL::Base, Gesture::Hold);
            CHECK(c.primaryGesture == Gesture::Tap, "VerbRecord: primaryGesture=Tap");
            CHECK(c.primary == juce::String(tap.primary), "VerbRecord: primary=REC");
            CHECK(c.holdLabel == juce::String(hold.primary), "VerbRecord: holdLabel=RESET");
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

    // 9.17: a mute armed to the launch grid (not yet fired) shows the pending
    // badge in the Mute view; unarmed tracks keep their normal audible/muted cell.
    static void testQuantizedMutePendingChrome()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;
        // Running + default Bar grid → the toggle defers and arms a pending lane.
        proc.queueGlobalMuteToggle(0, /*forceInstant=*/false);
        CHECK(proc.hasPendingMute(0), "pending chrome: mute armed");

        UiState ui; ui.muteHeld = true;
        const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                         GridDisplayMode::Ortholinear);
        CHECK(m.step[0].base == CellState::MutePendingMute,
              "pending chrome: armed track shows MutePendingMute");
        CHECK(m.step[1].base == CellState::MuteAudible,
              "pending chrome: unarmed track shows normal audible cell");
    }

    // -------------------------------------------------------------------------
    // Part 3: a bypassed FX in the track picker gets a distinct CellState token
    // (EffectLoadedBypassed) so it never looks identical to an active effect.
    // -------------------------------------------------------------------------
    static void testFxPickerBypassCell()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;

        // Load the first track-usable effect into track 0, insert slot 0.
        int effIdx = -1;
        for (int i = 0; i < proc.numAvailableEffects() && i < 16; ++i)
            if (!proc.availableEffectInfo(i).masterOnly) { effIdx = i; break; }
        CHECK(effIdx >= 0, "there is at least one track-usable effect");
        const auto eff = proc.availableEffectInfo(effIdx);
        proc.setTrackInsert(0, 0, eff.id);

        UiState ui;
        ui.funcFxHeld = true;         // enter TrackFxPicker
        ui.funcFxInsertSlot = 0;

        // Not bypassed → EffectLoaded.
        proc.setTrackInsertBypass(0, 0, false);
        {
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.step[static_cast<std::size_t>(effIdx)].base == CellState::EffectLoaded,
                  "loaded, non-bypassed effect cell = EffectLoaded");
        }
        // Bypassed → EffectLoadedBypassed (distinct visual, not 'active').
        proc.setTrackInsertBypass(0, 0, true);
        {
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.step[static_cast<std::size_t>(effIdx)].base == CellState::EffectLoadedBypassed,
                  "loaded, bypassed effect cell = EffectLoadedBypassed");
        }
    }

    // -------------------------------------------------------------------------
    // Part 4 (Func parallel stack): a Func-qualified section cell keeps its
    // origin fill and is marked by a Func-coloured *border*, not an all-orange
    // fill. Empty Func cells stay dim (strict) with no border.
    // -------------------------------------------------------------------------
    static void testFuncStackBorderNotOrangeFill()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;

        UiState ui;
        ui.funcHeld = true;
        const SurfaceModel m = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        // TRIG (0) carries a Func secondary (COND): Func border present + orange,
        // and the fill is NOT the old all-orange kScopeFunc.
        const auto& trig = m.section[0];
        CHECK(!trig.disabled, "Func+TRIG (COND) is a wired Func cell, not dim");
        CHECK(trig.border.present && trig.border.colour == theme::kScopeFunc,
              "Func+TRIG carries a Func-coloured border (the Func-stack marker)");
        CHECK(trig.baseColour != theme::kScopeFunc,
              "Func+TRIG fill is the origin/section colour, not all-orange");

        // A reserved section with no Func secondary stays dim with no border.
        const auto& mod = m.section[4];  // MOD — no Func-layer action
        CHECK(mod.disabled, "Func+MOD has no secondary → dim (strict)");
        CHECK(!mod.border.present, "dim Func cell carries no border");

        // 7e: the Func outline never latches. Latch the COND band (masterSection=0)
        // and release Func — the band stays, but the section-key border drops.
        {
            UiState latched;
            latched.funcHeld = false;
            latched.masterSection = 0;  // COND band latched (was reached via Func+TRIG)
            const SurfaceModel released = buildSurfaceModel(
                latched, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);
            CHECK(!released.section[0].border.present,
                  "7e: Func released → no Func border on TRIG even with COND latched");
        }

        // 7e helper: outline requires both the held layer and a func-qualified page.
        CHECK(!funcOutlineActive(false, true), "outline off when Func not held");
        CHECK(!funcOutlineActive(true, false), "outline off on a non-func page");
        CHECK(funcOutlineActive(true, true), "outline on: Func held + func page");
    }

    // -------------------------------------------------------------------------
    // Test (Item 7): section buttons are coloured by the resolver's *winning*
    // origin, not a blanket held-scope wash. Track+TRIG (DIV meta) reads cyan;
    // Phrase+TRIG (LEN) reads the Phrase hue; a Scene-scoped FILTER edit reads the
    // Scene hue even though the params are the machine's; a machine-owned section,
    // unqualified, carries no tint.
    // -------------------------------------------------------------------------
    static void testSectionWinnerColour()
    {
        EngineHarness h;
        auto& proc = h.processor();
        proc.setTrackMachine(0, AnalogMachine::kMachineId);
        EditContext ec;

        const auto build = [&](const UiState& ui) {
            return buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                     GridDisplayMode::Ortholinear);
        };

        {   // Unqualified TRIG falls through the underlay to the Track DIV: the
            // button is enabled, labelled DIV, and cyan — not a greyed "TRIG".
            UiState ui;  // nothing held
            const auto m = build(ui);
            CHECK(!m.section[0].disabled, "bare TRIG is enabled (underlay DIV, not greyed)");
            CHECK(m.section[0].primary == "DIV", "bare TRIG paints the DIV label");
            CHECK(m.section[0].scopeTint == originColour(SecOrigin::Track).getARGB(),
                  "bare TRIG paints the Track (cyan) winner colour");
        }
        {   // Track+TRIG → DIV meta, winner Track → cyan (same as unqualified).
            UiState ui; ui.trackHeld = true;
            const auto m = build(ui);
            CHECK(m.section[0].scopeTint == originColour(SecOrigin::Track).getARGB(),
                  "Track+TRIG paints the Track (cyan) winner colour");
        }
        {   // Phrase+TRIG → LEN meta, winner Phrase → phrase hue (≠ Track).
            UiState ui; ui.phraseScopeHeld = true;
            const auto m = build(ui);
            CHECK(m.section[0].scopeTint == originColour(SecOrigin::Phrase).getARGB(),
                  "Phrase+TRIG paints the Phrase winner colour");
            CHECK(originColour(SecOrigin::Phrase).getARGB()
                      != originColour(SecOrigin::Track).getARGB(),
                  "winner colour distinguishes Phrase from Track");
        }
        if (proc.section(0, 2).firstSlot >= 0)
        {   // Scene+FILTER: there is NO per-scope param layer (OEB rule), so holding
            // Scene does not make a scene filter — the key falls up to the machine's
            // own filter. Winner Machine → no scope tint (machine-neutral), NOT the
            // fictional Scene hue the shipped 9.20 painted. Honest colour-by-winner.
            UiState ui; ui.sceneHeld = true;
            const auto m = build(ui);
            CHECK(m.section[2].scopeTint != originColour(SecOrigin::Scene).getARGB(),
                  "Scene+FILTER is NOT scene-coloured (no scene param layer exists)");
            CHECK(m.section[2].scopeTint == 0u,
                  "Scene+FILTER falls up to the machine filter → machine-neutral (no tint)");
        }
        if (proc.section(0, 2).firstSlot >= 0)
        {   // Song+FILTER falls all the way up to the machine filter: the key is
            // ENABLED (no wasted real estate) and machine-neutral (no false Song
            // hue). Deep-scope keys no longer dim under the underlay.
            UiState ui; ui.songHeld = true;
            const auto m = build(ui);
            CHECK(!m.section[2].disabled, "Song+FILTER is enabled (falls up to machine filter)");
            CHECK(m.section[2].scopeTint == 0u,
                  "Song+FILTER is machine-neutral (no song param layer to colour)");
        }
        {   // SRC has only a machine layer, so it stays machine-coloured under ANY
            // scope hold — the pedagogy: no deeper layer exists, so no recolour.
            UiState ui; ui.sceneHeld = true;
            const auto m = build(ui);
            if (proc.section(0, 1).firstSlot >= 0 && proc.section(0, 1).firstSlot < proc.numParams(0))
                CHECK(m.section[1].scopeTint == 0u,
                      "Scene+SRC stays machine-coloured (no deeper SRC layer)");
        }
        {   // Unqualified, a machine-owned section → no tint (Machine winner).
            const int mnp = proc.numParams(0);
            int owned = -1;
            for (int k = 1; k < IMachine::kMaxSections; ++k)
                if (proc.section(0, k).firstSlot >= 0 && proc.section(0, k).firstSlot < mnp)
                {
                    owned = k;
                    break;
                }
            if (owned >= 0)
            {
                UiState ui;  // nothing held
                const auto m = build(ui);
                CHECK(m.section[static_cast<std::size_t>(owned)].scopeTint == 0u,
                      "unqualified machine-owned section carries no scope tint");
            }
        }
    }

    // -------------------------------------------------------------------------
    // Test (9.22): the Func colour model — every non-dim section key is coloured
    // by its content's true winner (NOT a flat orange), with the func-colour
    // border layered on as the modifier signal. Covers bare-Func metas and the
    // Func+Song=Global promotion.
    // -------------------------------------------------------------------------
    static void testFuncColourModel()
    {
        EngineHarness h;
        auto& proc = h.processor();
        proc.setTrackMachine(0, AnalogMachine::kMachineId);
        EditContext ec;
        const auto build = [&](const UiState& ui) {
            return buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                     GridDisplayMode::Ortholinear);
        };

        {   // Bare Func: COND (TRIG) is machine-neutral body + func border; TRSP
            // (FILTER) is Global azure body + func border (coloured by its origin,
            // not flat orange).
            UiState ui; ui.funcHeld = true;
            const auto m = build(ui);
            CHECK(!m.section[0].disabled && m.section[0].scopeTint == 0u
                      && m.section[0].border.present
                      && m.section[0].border.colour == theme::kScopeFunc,
                  "bare Func+TRIG (COND): machine-neutral body + func border");
            CHECK(!m.section[2].disabled
                      && m.section[2].scopeTint == originColour(SecOrigin::Global).getARGB()
                      && m.section[2].border.present,
                  "bare Func+FILTER (TRSP): Global azure body + func border");
        }
        {   // Func+Song = Global scope. FILTER surfaces TRSP (azure); TRIG falls to
            // Song TIME (Song hue). Both wear the func border (modifier held).
            UiState ui; ui.funcHeld = true; ui.songHeld = true;
            const auto m = build(ui);
            CHECK(!m.section[2].disabled
                      && m.section[2].scopeTint == originColour(SecOrigin::Global).getARGB()
                      && m.section[2].border.present,
                  "Func+Song+FILTER: TRSP Global azure + func border");
            CHECK(!m.section[0].disabled
                      && m.section[0].scopeTint == originColour(SecOrigin::Song).getARGB()
                      && m.section[0].border.present,
                  "Func+Song+TRIG: Song TIME (Song hue) + func border");
            CHECK(originColour(SecOrigin::Global).getARGB()
                      != originColour(SecOrigin::Song).getARGB(),
                  "Global azure is visibly distinct from Song under the promotion");
        }
    }

    // 9.26 Stage C: the "press again to cycle" page-dot affordance. Param-page
    // dots equal the selectScopeSections cycle length (the dispatcher's own path,
    // no double-count); an active subpage overlay overrides its owning key's dots.
    static void testOverlayPageDots()
    {
        EngineHarness h;
        auto& proc = h.processor();
        proc.setTrackMachine(0, AnalogMachine::kMachineId);  // a synth with real sections
        EditContext ec;

        // Param-page dots route through the same buildParamCandidates +
        // selectScopeSections path KeyboardArea::sectionsForKey pages with, so the
        // dot count equals the real re-press cycle length (no raw-schema drift).
        {
            UiState ui;
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            for (int s = 0; s < IMachine::kMaxSections; ++s)
            {
                const auto cands = buildParamCandidates(proc, 0, s);
                int expected = 0;
                for (const auto& c : selectScopeSections(cands, SecOrigin::Machine))
                    expected += std::max(1, c.pageCount);
                CHECK(m.pageDots[static_cast<std::size_t>(s)].count
                          == static_cast<uint8_t>(expected),
                      juce::String("section ") + juce::String(s)
                          + " page dots equal the selectScopeSections cycle length");
            }
        }

        // Vel overlay → AMP key (3): 4 subpage dots, active = current subpage,
        // overriding whatever param-page dots the key otherwise carried.
        {
            UiState ui;
            ui.overlay = Overlay::Vel;
            ui.velSubPage = UiState::VelSubPage::Mode;  // index 2
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.pageDots[3].count == 4 && m.pageDots[3].active == 2,
                  "Vel overlay → AMP key: 4 subpage dots, active = current subpage");
        }
        // Density overlay → MOD key (4): 3 subpage dots.
        {
            UiState ui;
            ui.overlay = Overlay::Density;
            ui.densitySubPage = UiState::DensitySubPage::Selection;  // index 2
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.pageDots[4].count == 3 && m.pageDots[4].active == 2,
                  "Density overlay → MOD key: 3 subpage dots, active = current subpage");
        }
        // Time overlay → TRIG key (0): 2 subpage dots (TIME<->KEY).
        {
            UiState ui;
            ui.overlay = Overlay::Time;
            ui.sigPage = UiState::SigPage::Key;  // index 1
            const auto m = buildSurfaceModel(ui, ec, nullptr, proc, 0, 0,
                                             GridDisplayMode::Ortholinear);
            CHECK(m.pageDots[0].count == 2 && m.pageDots[0].active == 1,
                  "Time overlay → TRIG key: 2 subpage dots (TIME<->KEY), active = KEY");
        }
    }

    void runSurfaceModelTests()
    {
        testPanicKeyLabel();
        testOverlayPageDots();
        testQuantizedMutePendingChrome();
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
        testLooperConsole();
        testTapeConsole();
        testGeneratorHubPrimary();
        testMachinePickerPrimary();
        testDeriveSlotEqualsGrammar();
        testSectionFuncHintsMatchDispatch();
        testFxSectionPrimaryNotPicker();
        testFxPickerBypassCell();
        testFuncStackBorderNotOrangeFill();
        testFuncColourModel();
        testSrcAnnouncesNoteEditWhenStepHeld();
        testSectionWinnerColour();
    }

} // namespace lockstep
