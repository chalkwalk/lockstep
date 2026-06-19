// FuncReskinTest — CUJ tests for activeFuncReskin() and exitFuncReskin().
//
// These cover the enter/cancel/exit invariants for all five Func-layer modes.
// The commit step (which calls processor_) is editor-owned and tested via
// EngineTests; here we verify that state transitions and cleanup are correct.

#include "TestHarness.h"
#include "../src/ui/mode/FuncReskin.h"
#include "../src/state/UiState.h"

namespace lockstep
{
    using FR = FuncReskin;

    // ── activeFuncReskin: single conditions ──────────────────────────────────

    static void testActiveNone()
    {
        UiState ui;
        CHECK(activeFuncReskin(ui) == FR::None,
              "empty UiState → None");
    }

    static void testActiveMasterFxPicker()
    {
        UiState ui;
        ui.masterFxPickerOpen = true;
        CHECK(activeFuncReskin(ui) == FR::MasterFxPicker,
              "masterFxPickerOpen → MasterFxPicker");
    }

    static void testActiveTrackFxPicker()
    {
        UiState ui;
        ui.funcFxHeld = true;
        CHECK(activeFuncReskin(ui) == FR::TrackFxPicker,
              "funcFxHeld → TrackFxPicker");
    }

    static void testActiveMachinePicker()
    {
        UiState ui;
        ui.funcTrackHeld = true;
        CHECK(activeFuncReskin(ui) == FR::MachinePicker,
              "funcTrackHeld → MachinePicker");
    }

    static void testActiveNoteEdit()
    {
        UiState ui;
        ui.noteEditMode = true;
        CHECK(activeFuncReskin(ui) == FR::NoteEdit,
              "noteEditMode → NoteEdit");
    }

    static void testActivePLockClear()
    {
        UiState ui;
        ui.pLockClearMode = true;
        CHECK(activeFuncReskin(ui) == FR::PLockClear,
              "pLockClearMode → PLockClear");
    }

    // ── activeFuncReskin: priority order ─────────────────────────────────────

    static void testPriorityMasterFxBeatsAll()
    {
        UiState ui;
        ui.masterFxPickerOpen = true;
        ui.funcFxHeld = true;
        ui.funcTrackHeld = true;
        ui.noteEditMode = true;
        ui.pLockClearMode = true;
        CHECK(activeFuncReskin(ui) == FR::MasterFxPicker,
              "MasterFxPicker has highest priority when all flags set");
    }

    static void testPriorityTrackFxBeatsLower()
    {
        UiState ui;
        ui.funcFxHeld = true;
        ui.funcTrackHeld = true;
        ui.noteEditMode = true;
        ui.pLockClearMode = true;
        CHECK(activeFuncReskin(ui) == FR::TrackFxPicker,
              "TrackFxPicker beats MachinePicker, NoteEdit, PLockClear");
    }

    static void testPriorityMachinePickerBeatsModeEditors()
    {
        UiState ui;
        ui.funcTrackHeld = true;
        ui.noteEditMode = true;
        ui.pLockClearMode = true;
        CHECK(activeFuncReskin(ui) == FR::MachinePicker,
              "MachinePicker beats NoteEdit and PLockClear");
    }

    static void testPriorityNoteEditBeatsPLockClear()
    {
        UiState ui;
        ui.noteEditMode = true;
        ui.pLockClearMode = true;
        CHECK(activeFuncReskin(ui) == FR::NoteEdit,
              "NoteEdit beats PLockClear");
    }

    // ── exitFuncReskin: clears each mode ─────────────────────────────────────

    static void testExitClearsMasterFxPicker()
    {
        UiState ui;
        ui.masterFxPickerOpen = true;
        ui.masterFxInsertSlot = 2;
        exitFuncReskin(ui);
        CHECK(!ui.masterFxPickerOpen,      "masterFxPickerOpen cleared");
        CHECK(ui.masterFxInsertSlot == 0,  "masterFxInsertSlot reset to 0");
        CHECK(activeFuncReskin(ui) == FR::None, "None after exit");
    }

    static void testExitClearsTrackFxPicker()
    {
        UiState ui;
        ui.funcFxHeld = true;
        ui.funcFxInsertSlot = 1;
        exitFuncReskin(ui);
        CHECK(!ui.funcFxHeld,             "funcFxHeld cleared");
        CHECK(ui.funcFxInsertSlot == 0,   "funcFxInsertSlot reset");
        CHECK(activeFuncReskin(ui) == FR::None, "None after exit");
    }

    static void testExitClearsMachinePicker()
    {
        UiState ui;
        ui.funcTrackHeld = true;
        exitFuncReskin(ui);
        CHECK(!ui.funcTrackHeld,          "funcTrackHeld cleared");
        CHECK(activeFuncReskin(ui) == FR::None, "None after exit");
    }

    static void testExitClearsNoteEdit()
    {
        UiState ui;
        ui.noteEditMode = true;
        ui.funcSrcHeld = true;
        ui.noteEditOctave = 5;
        ui.noteEditSteps.insert(3);
        ui.noteEditStaged[3].insert(60);
        exitFuncReskin(ui);
        CHECK(!ui.noteEditMode,               "noteEditMode cleared");
        CHECK(!ui.funcSrcHeld,                "funcSrcHeld cleared");
        CHECK(ui.noteEditOctave == 3,         "octave reset to 3");
        CHECK(ui.noteEditSteps.empty(),       "noteEditSteps cleared");
        CHECK(ui.noteEditStaged.empty(),      "noteEditStaged cleared");
        CHECK(activeFuncReskin(ui) == FR::None, "None after exit");
    }

    static void testExitClearsPLockClear()
    {
        UiState ui;
        ui.pLockClearMode = true;
        ui.pLockClearTrack = 3;
        ui.pLockClearStep = 7;
        ui.pLockClearStaged.insert(4);
        exitFuncReskin(ui);
        CHECK(!ui.pLockClearMode,             "pLockClearMode cleared");
        CHECK(ui.pLockClearTrack == -1,       "pLockClearTrack reset");
        CHECK(ui.pLockClearStep == -1,        "pLockClearStep reset");
        CHECK(ui.pLockClearStaged.empty(),    "pLockClearStaged cleared");
        CHECK(activeFuncReskin(ui) == FR::None, "None after exit");
    }

    // ── exitFuncReskin: safe when mode is not active ─────────────────────────

    static void testExitOnEmptyStateIsSafe()
    {
        UiState ui;  // no mode active
        exitFuncReskin(ui);  // must not assert or corrupt state
        CHECK(activeFuncReskin(ui) == FR::None, "None after noop exit");
        CHECK(!ui.noteEditMode, "noteEditMode still false");
        CHECK(!ui.pLockClearMode, "pLockClearMode still false");
        CHECK(!ui.funcTrackHeld, "funcTrackHeld still false");
    }

    // ── exitFuncReskin: cancels all when multiple flags set ───────────────────

    static void testExitClearsAllSimultaneously()
    {
        UiState ui;
        ui.masterFxPickerOpen = true;
        ui.funcFxHeld = true;
        ui.funcTrackHeld = true;
        ui.noteEditMode = true;
        ui.noteEditSteps.insert(0);
        ui.pLockClearMode = true;
        ui.pLockClearStaged.insert(1);
        exitFuncReskin(ui);
        CHECK(activeFuncReskin(ui) == FR::None, "all modes cleared");
        CHECK(!ui.masterFxPickerOpen, "masterFxPickerOpen cleared");
        CHECK(!ui.funcFxHeld, "funcFxHeld cleared");
        CHECK(!ui.funcTrackHeld, "funcTrackHeld cleared");
        CHECK(!ui.noteEditMode, "noteEditMode cleared");
        CHECK(!ui.pLockClearMode, "pLockClearMode cleared");
    }

    // ── funcSrcHeld cleared even when noteEditMode was never entered ──────────

    static void testExitClearsFuncSrcHeldWithoutNoteEdit()
    {
        UiState ui;
        ui.funcSrcHeld = true;   // armed but step was never released
        exitFuncReskin(ui);
        CHECK(!ui.funcSrcHeld, "funcSrcHeld cleared even without noteEditMode");
    }

    // ─────────────────────────────────────────────────────────────────────────

    void runFuncReskinTests()
    {
        testActiveNone();
        testActiveMasterFxPicker();
        testActiveTrackFxPicker();
        testActiveMachinePicker();
        testActiveNoteEdit();
        testActivePLockClear();
        testPriorityMasterFxBeatsAll();
        testPriorityTrackFxBeatsLower();
        testPriorityMachinePickerBeatsModeEditors();
        testPriorityNoteEditBeatsPLockClear();
        testExitClearsMasterFxPicker();
        testExitClearsTrackFxPicker();
        testExitClearsMachinePicker();
        testExitClearsNoteEdit();
        testExitClearsPLockClear();
        testExitOnEmptyStateIsSafe();
        testExitClearsAllSimultaneously();
        testExitClearsFuncSrcHeldWithoutNoteEdit();
    }

} // namespace lockstep
