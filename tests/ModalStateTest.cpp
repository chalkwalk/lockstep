// ModalStateTest -- the unified Modal accessor (9.13 reducer foundation).
//
// activeModal() is the read-only SSOT query that collapses the scattered modal
// flags into one value. These tests pin its priority + agreement with the legacy
// accessors it subsumes, so the upcoming reducer migration (which moves storage
// into one field) is provably behaviour-preserving.

#include "TestHarness.h"
#include "../src/state/UiState.h"
#include "../src/ui/mode/ModalState.h"
#include "../src/ui/mode/ModeReducer.h"
#include "../src/ui/mode/FuncReskin.h"

namespace lockstep
{
    static void testActiveModalSingles()
    {
        UiState none;
        CHECK(activeModal(none) == Modal::None, "default -> None");

        { UiState u; u.masterFxPickerOpen = true; CHECK(activeModal(u) == Modal::MasterFxPicker, "masterFx"); }
        { UiState u; u.funcFxHeld = true;         CHECK(activeModal(u) == Modal::TrackFxPicker,  "trackFx"); }
        { UiState u; u.funcTrackHeld = true;      CHECK(activeModal(u) == Modal::MachinePicker,  "machine"); }
        { UiState u; u.generatorHubHeld = true;   CHECK(activeModal(u) == Modal::GeneratorHub,   "genHub"); }
        { UiState u; u.noteEditMode = true;       CHECK(activeModal(u) == Modal::NoteEdit,       "noteEdit"); }
        { UiState u; u.pLockClearMode = true;     CHECK(activeModal(u) == Modal::PLockClear,     "pLockClear"); }
        { UiState u; u.euclidHeld = true;         CHECK(activeModal(u) == Modal::Euclid,         "euclid"); }
        { UiState u; u.overlay = Overlay::Time;   CHECK(activeModal(u) == Modal::Time,           "time"); }
        { UiState u; u.overlay = Overlay::Density;CHECK(activeModal(u) == Modal::Density,         "density"); }
        { UiState u; u.overlay = Overlay::Vel;    CHECK(activeModal(u) == Modal::Vel,            "vel"); }
    }

    static void testActiveModalPriority()
    {
        {
            UiState u; u.masterFxPickerOpen = true; u.funcTrackHeld = true; u.overlay = Overlay::Density;
            CHECK(activeModal(u) == Modal::MasterFxPicker, "masterFx outranks lower modals");
        }
        {
            UiState u; u.funcTrackHeld = true; u.euclidHeld = true;
            CHECK(activeModal(u) == Modal::MachinePicker, "grid picker outranks euclid");
        }
        {
            UiState u; u.euclidHeld = true; u.overlay = Overlay::Density;
            CHECK(activeModal(u) == Modal::Euclid, "euclid outranks MZ overlay field");
        }
    }

    // activeModal must agree with the legacy accessors it subsumes — the contract
    // the reducer migration preserves.
    static void testActiveModalMatchesLegacy()
    {
        { UiState u; u.euclidHeld = true;
          CHECK(activeOverlay(u) == Overlay::Euclid && activeModal(u) == Modal::Euclid, "euclid agrees"); }
        { UiState u; u.overlay = Overlay::Vel;
          CHECK(activeOverlay(u) == Overlay::Vel && activeModal(u) == Modal::Vel, "vel agrees"); }
        { UiState u; u.funcTrackHeld = true;
          CHECK(activeFuncReskin(u) == FuncReskin::MachinePicker && activeModal(u) == Modal::MachinePicker, "machine agrees"); }
        { UiState u; u.masterFxPickerOpen = true;
          CHECK(activeFuncReskin(u) == FuncReskin::MasterFxPicker && activeModal(u) == Modal::MasterFxPicker, "masterFx agrees"); }
    }

    void runModalStateTests()
    {
        testActiveModalSingles();
        testActiveModalPriority();
        testActiveModalMatchesLegacy();
    }
}
