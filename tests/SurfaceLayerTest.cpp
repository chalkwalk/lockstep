// SurfaceLayerTest — characterisation tests for resolveActiveLayer.
// Pins the priority order of SurfaceLayer so refactors cannot silently change
// which overlay is shown when multiple conditions are simultaneously true.

#include "TestHarness.h"
#include "../src/command/SurfaceLayer.h"
#include "../src/state/UiState.h"
#include "../src/io/EditContext.h"

namespace lockstep
{
    using SL = SurfaceLayer;

    // Helper: build a minimal LayerFacts with Play mode and track 0.
    static LayerFacts facts(TrackInputMode mode = TrackInputMode::Play, int track = 0)
    {
        return LayerFacts{ mode, track };
    }

    // Helper: empty EditContext (satisfies reference parameter without JUCE deps).
    static EditContext& ec()
    {
        static EditContext kCtx;
        return kCtx;
    }

    // ── Base case ─────────────────────────────────────────────────────────────
    static void testBaseLayer()
    {
        UiState ui;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::Base,
              "empty UiState → Base");
    }

    // ── Single-condition cases ─────────────────────────────────────────────────
    static void testSingleConditions()
    {
        {
            UiState ui;
            ui.trigGridMode = TrigGridMode::SoundPool;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::SoundPool,
                  "SoundPool trigGridMode → SoundPool");
        }
        {
            UiState ui;
            ui.trigGridMode = TrigGridMode::Retrig;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::RetrigPicker,
                  "Retrig trigGridMode → RetrigPicker");
        }
        {
            UiState ui;
            ui.masterFxPickerOpen = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MasterFxPicker,
                  "masterFxPickerOpen → MasterFxPicker");
        }
        {
            UiState ui;
            ui.funcFxHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::TrackFxPicker,
                  "funcFxHeld → TrackFxPicker");
        }
        {
            UiState ui;
            ui.machinePickerOpen = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MachinePicker,
                  "machinePickerOpen → MachinePicker");
        }
        {
            UiState ui;
            ui.noteEditMode = true;
            ui.noteEditSteps.insert(0);
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::NoteEdit,
                  "noteEditMode+steps → NoteEdit");
        }
        {
            // noteEditMode set but steps empty → not NoteEdit
            UiState ui;
            ui.noteEditMode = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) != SL::NoteEdit,
                  "noteEditMode without steps → not NoteEdit");
        }
        {
            UiState ui;
            ui.pLockClearMode = true;
            ui.pLockClearTrack = 0;
            ui.pLockClearStep = 2;
            CHECK(resolveActiveLayer(ui, ec(), facts(TrackInputMode::Play, 0)) == SL::PLockClear,
                  "pLockClearMode → PLockClear when track matches");
        }
        {
            // pLockClearTrack != activeTrack → not PLockClear
            UiState ui;
            ui.pLockClearMode = true;
            ui.pLockClearTrack = 3;
            ui.pLockClearStep = 2;
            CHECK(resolveActiveLayer(ui, ec(), facts(TrackInputMode::Play, 0)) != SL::PLockClear,
                  "pLockClearMode but track mismatch → not PLockClear");
        }
        {
            CHECK(resolveActiveLayer(UiState{}, ec(),
                                     facts(TrackInputMode::Chromatic)) == SL::ChromaticInput,
                  "Chromatic inputMode → ChromaticInput");
        }
        {
            CHECK(resolveActiveLayer(UiState{}, ec(),
                                     facts(TrackInputMode::Levels)) == SL::LevelsInput,
                  "Levels inputMode → LevelsInput");
        }
        {
            UiState ui;
            ui.morphHeld = true;
            ui.muteHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MorphMuteView,
                  "morphHeld+muteHeld → MorphMuteView");
        }
        {
            UiState ui;
            ui.muteHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MuteView,
                  "muteHeld alone → MuteView");
        }
        {
            // 9.17: Mute + Play (relaunchHeld) → the relaunch/retrigger view,
            // which outranks bare MuteView.
            UiState ui;
            ui.muteHeld = true;
            ui.relaunchHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MuteRelaunchView,
                  "muteHeld+relaunchHeld → MuteRelaunchView");
        }
        {
            UiState ui;
            ui.phraseScopeHeld = true;
            ui.funcHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::LengthEdit,
                  "phraseScopeHeld+funcHeld → LengthEdit");
        }
        {
            UiState ui;
            ui.morphHeld = true;
            ui.funcHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::LengthEdit,
                  "morphHeld+funcHeld → LengthEdit");
        }
        {
            UiState ui;
            ui.morphHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MorphStepView,
                  "morphHeld alone → MorphStepView");
        }
        {
            UiState ui;
            ui.trackHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::ScopeSelector,
                  "trackHeld → ScopeSelector");
        }
        {
            UiState ui;
            ui.phraseScopeHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::ScopeSelector,
                  "phraseScopeHeld → ScopeSelector");
        }
        {
            UiState ui;
            ui.sceneHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::ScopeSelector,
                  "sceneHeld → ScopeSelector");
        }
        {
            UiState ui;
            ui.songHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::ScopeSelector,
                  "songHeld → ScopeSelector");
        }
    }

    // ── Pairwise priority conflicts (the key invariants) ─────────────────────

    static void testSoundPoolBeatsOthers()
    {
        // SoundPool beats MachinePicker, NoteEdit, PLockClear, MorphMuteView
        {
            UiState ui;
            ui.trigGridMode = TrigGridMode::SoundPool;
            ui.machinePickerOpen = true;  // would be MachinePicker
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::SoundPool,
                  "SoundPool beats MachinePicker");
        }
        {
            UiState ui;
            ui.trigGridMode = TrigGridMode::SoundPool;
            ui.noteEditMode = true;
            ui.noteEditSteps.insert(0);
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::SoundPool,
                  "SoundPool beats NoteEdit");
        }
        {
            UiState ui;
            ui.trigGridMode = TrigGridMode::SoundPool;
            ui.morphHeld = true;
            ui.muteHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::SoundPool,
                  "SoundPool beats MorphMuteView");
        }
    }

    static void testMasterFxPickerBeatsOthers()
    {
        {
            UiState ui;
            ui.masterFxPickerOpen = true;
            ui.funcFxHeld = true;  // would be TrackFxPicker
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MasterFxPicker,
                  "MasterFxPicker beats TrackFxPicker");
        }
        {
            UiState ui;
            ui.masterFxPickerOpen = true;
            ui.machinePickerOpen = true;  // would be MachinePicker
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MasterFxPicker,
                  "MasterFxPicker beats MachinePicker");
        }
    }

    static void testGeneratorHub()
    {
        {
            UiState ui;
            ui.generatorHubHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::GeneratorHub,
                  "generatorHubHeld → GeneratorHub");
        }
        {
            // GeneratorHub outranks NoteEdit (MachinePicker priority > GeneratorHub > NoteEdit)
            UiState ui;
            ui.generatorHubHeld = true;
            ui.noteEditMode = true;
            ui.noteEditSteps.insert(0);
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::GeneratorHub,
                  "GeneratorHub beats NoteEdit");
        }
        {
            // MachinePicker still beats GeneratorHub
            UiState ui;
            ui.machinePickerOpen = true;
            ui.generatorHubHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MachinePicker,
                  "MachinePicker beats GeneratorHub");
        }
    }

    static void testMachinePickerBeatsNoteEdit()
    {
        UiState ui;
        ui.machinePickerOpen = true;
        ui.noteEditMode = true;
        ui.noteEditSteps.insert(0);
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MachinePicker,
              "MachinePicker beats NoteEdit");
    }

    static void testNoteEditBeatsPLockClear()
    {
        UiState ui;
        ui.noteEditMode = true;
        ui.noteEditSteps.insert(0);
        ui.pLockClearMode = true;
        ui.pLockClearTrack = 0;
        ui.pLockClearStep = 1;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::NoteEdit,
              "NoteEdit beats PLockClear");
    }

    static void testNoteEditBeatsChromaticAndLevels()
    {
        {
            UiState ui;
            ui.noteEditMode = true;
            ui.noteEditSteps.insert(0);
            CHECK(resolveActiveLayer(ui, ec(), facts(TrackInputMode::Chromatic)) == SL::NoteEdit,
                  "NoteEdit beats Chromatic");
        }
        {
            UiState ui;
            ui.noteEditMode = true;
            ui.noteEditSteps.insert(0);
            CHECK(resolveActiveLayer(ui, ec(), facts(TrackInputMode::Levels)) == SL::NoteEdit,
                  "NoteEdit beats Levels");
        }
    }

    static void testMorphMuteViewBeatsMuteView()
    {
        UiState ui;
        ui.morphHeld = true;
        ui.muteHeld = true;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MorphMuteView,
              "morphHeld+muteHeld → MorphMuteView (not bare MuteView)");
    }

    static void testLengthEditBeatsMorphStepView()
    {
        UiState ui;
        ui.morphHeld = true;
        ui.funcHeld = true;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::LengthEdit,
              "morphHeld+funcHeld → LengthEdit (not MorphStepView)");
    }

    static void testMorphStepViewBeatsScopeSelector()
    {
        // morphHeld alone → MorphStepView; NOT ScopeSelector (morph is not in
        // the ScopeSelector condition: trackHeld||phraseScopeHeld||sceneHeld||songHeld).
        UiState ui;
        ui.morphHeld = true;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MorphStepView,
              "morphHeld alone → MorphStepView, not ScopeSelector");
    }

    static void testFuncTrackHeldBlocksLengthEdit()
    {
        // machinePickerOpen → MachinePicker even if phrase+func would give LengthEdit
        UiState ui;
        ui.phraseScopeHeld = true;
        ui.funcHeld = true;
        ui.machinePickerOpen = true;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::MachinePicker,
              "machinePickerOpen wins over LengthEdit (MachinePicker has higher priority)");
    }

    static void testPendingConfirmLayer()
    {
        // PendingConfirm is now driven by ui.confirm.pending() — no LayerFacts field.
        UiState ui;
        ui.confirm.kind = ConfirmKind::DeletePhrase;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::PendingConfirm,
              "confirm.pending() → PendingConfirm (highest priority)");

        // PendingConfirm beats everything
        ui.trigGridMode = TrigGridMode::SoundPool;
        ui.machinePickerOpen = true;
        ui.morphHeld = true;
        ui.muteHeld = true;
        CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::PendingConfirm,
              "PendingConfirm beats all other conditions");
    }

    static void testDeletePickerLayer()
    {
        // DeletePicker is driven by ui.deletePicker.active().
        {
            UiState ui;
            ui.deletePicker.scope = DeleteScope::Phrase;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::DeletePicker,
                  "deletePicker.active() → DeletePicker");
        }
        // DeletePicker beats SoundPool, MachinePicker, LengthEdit, etc.
        {
            UiState ui;
            ui.deletePicker.scope = DeleteScope::Scene;
            ui.trigGridMode = TrigGridMode::SoundPool;
            ui.machinePickerOpen = true;
            ui.phraseScopeHeld = true;
            ui.funcHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::DeletePicker,
                  "DeletePicker beats SoundPool, MachinePicker, LengthEdit");
        }
        // PendingConfirm beats DeletePicker.
        {
            UiState ui;
            ui.deletePicker.scope = DeleteScope::Track;
            ui.confirm.kind = ConfirmKind::DeleteTrack;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::PendingConfirm,
                  "PendingConfirm beats DeletePicker");
        }
    }

    // ── StepInspector (9.14 Stage 3) ─────────────────────────────────────────

    static EditContext& ecWithStep(int track, int step)
    {
        static EditContext kCtx;
        kCtx.release();
        kCtx.hold(track, step);
        return kCtx;
    }

    static void testStepInspectorLayer()
    {
        // Holding a step → StepInspector (outranks PLockClear).
        {
            UiState ui;
            ui.pLockClearMode = true;
            ui.pLockClearTrack = 0;
            ui.pLockClearStep = 3;
            CHECK(resolveActiveLayer(ui, ecWithStep(0, 3), facts()) == SL::StepInspector,
                  "heldStepIndex >= 0 → StepInspector (beats PLockClear)");
        }
        // No step held + pLockClearMode → PLockClear (unchanged).
        {
            UiState ui;
            ui.pLockClearMode = true;
            ui.pLockClearTrack = 0;
            ui.pLockClearStep = 3;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::PLockClear,
                  "no held step + pLockClearMode → PLockClear");
        }
        // NoteEdit beats StepInspector.
        {
            UiState ui;
            ui.noteEditMode = true;
            ui.noteEditSteps.insert(5);
            CHECK(resolveActiveLayer(ui, ecWithStep(0, 5), facts()) == SL::NoteEdit,
                  "NoteEdit beats StepInspector");
        }
        // MachinePicker beats StepInspector.
        {
            UiState ui;
            ui.machinePickerOpen = true;
            CHECK(resolveActiveLayer(ui, ecWithStep(0, 2), facts()) == SL::MachinePicker,
                  "MachinePicker beats StepInspector");
        }
        // StepInspector released → falls through to Base.
        {
            UiState ui;
            CHECK(resolveActiveLayer(ui, ec(), facts()) == SL::Base,
                  "no held step + empty UiState → Base");
        }
        // 9.14 fix 2: while moving a held step, drop back to the sequencer view
        // (Base) so the step is visible hopping — outranks StepInspector.
        {
            UiState ui;
            ui.stepMoveActive = true;
            CHECK(resolveActiveLayer(ui, ecWithStep(0, 4), facts()) == SL::Base,
                  "stepMoveActive → Base (sequencer view) even with a step held");
        }
    }

    // ── MachineConsole (7b) ──────────────────────────────────────────────────

    static LayerFacts factsConsole(ConsoleMode m)
    {
        LayerFacts f{ TrackInputMode::Play, 0, false };
        f.activeTrackConsoleMode = m;
        return f;
    }

    static void testMachineConsoleLayer()
    {
        // AlwaysOn console shows whenever nothing higher is active (like looper).
        {
            UiState ui;
            CHECK(resolveActiveLayer(ui, ec(), factsConsole(ConsoleMode::AlwaysOn))
                      == SL::MachineConsole,
                  "AlwaysOn console → MachineConsole");
        }
        // OnDemand shows only while the open flag is set.
        {
            UiState ui;
            CHECK(resolveActiveLayer(ui, ec(), factsConsole(ConsoleMode::OnDemand))
                      == SL::Base,
                  "OnDemand console closed → Base");
            ui.machineConsoleOpen = true;
            CHECK(resolveActiveLayer(ui, ec(), factsConsole(ConsoleMode::OnDemand))
                      == SL::MachineConsole,
                  "OnDemand console open → MachineConsole");
        }
        // None never shows a console even with the flag set.
        {
            UiState ui;
            ui.machineConsoleOpen = true;
            CHECK(resolveActiveLayer(ui, ec(), factsConsole(ConsoleMode::None)) == SL::Base,
                  "None console mode → Base regardless of open flag");
        }
        // A held scope chord still outranks the console (it sits below every modal).
        {
            UiState ui;
            ui.trackHeld = true;
            CHECK(resolveActiveLayer(ui, ec(), factsConsole(ConsoleMode::AlwaysOn))
                      == SL::ScopeSelector,
                  "ScopeSelector beats MachineConsole");
        }
    }

    void runSurfaceLayerTests()
    {
        testBaseLayer();
        testSingleConditions();
        testSoundPoolBeatsOthers();
        testMasterFxPickerBeatsOthers();
        testGeneratorHub();
        testMachinePickerBeatsNoteEdit();
        testNoteEditBeatsPLockClear();
        testNoteEditBeatsChromaticAndLevels();
        testMorphMuteViewBeatsMuteView();
        testLengthEditBeatsMorphStepView();
        testMorphStepViewBeatsScopeSelector();
        testFuncTrackHeldBlocksLengthEdit();
        testPendingConfirmLayer();
        testDeletePickerLayer();
        testStepInspectorLayer();
        testMachineConsoleLayer();
    }
}
