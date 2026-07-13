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
#include "../src/command/SurfaceLayer.h"
#include "../src/io/EditContext.h"

namespace lockstep
{
    static void testActiveModalSingles()
    {
        UiState none;
        CHECK(activeModal(none) == Modal::None, "default -> None");

        { UiState u; u.masterFxPickerOpen = true; CHECK(activeModal(u) == Modal::MasterFxPicker, "masterFx"); }
        { UiState u; u.funcFxHeld = true;         CHECK(activeModal(u) == Modal::TrackFxPicker,  "trackFx"); }
        { UiState u; u.machinePickerOpen = true;      CHECK(activeModal(u) == Modal::MachinePicker,  "machine"); }
        { UiState u; u.generatorHubHeld = true;   CHECK(activeModal(u) == Modal::GeneratorHub,   "genHub"); }
        { UiState u; u.noteEditMode = true;       CHECK(activeModal(u) == Modal::NoteEdit,       "noteEdit"); }
        { UiState u; u.pLockClearMode = true;     CHECK(activeModal(u) == Modal::PLockClear,     "pLockClear"); }
        { UiState u; u.euclidHeld = true;         CHECK(activeModal(u) == Modal::Euclid,         "euclid"); }
        { UiState u; u.overlay = Overlay::Time;   CHECK(activeModal(u) == Modal::Time,           "time"); }
        { UiState u; u.overlay = Overlay::Density;CHECK(activeModal(u) == Modal::Density,         "density"); }
        { UiState u; u.overlay = Overlay::Vel;    CHECK(activeModal(u) == Modal::Vel,            "vel"); }
        { UiState u; u.overlay = Overlay::SampleProps; CHECK(activeModal(u) == Modal::SampleProps, "sampleProps"); }
    }

    static void testModalNameGolden()
    {
        CHECK(juce::String(modalName(Modal::SampleProps)) == "SampleProps", "SampleProps name golden");
        CHECK(juce::String(modalName(Modal::None)) == "None", "None name golden");
    }

    static void testActiveModalPriority()
    {
        {
            UiState u; u.masterFxPickerOpen = true; u.machinePickerOpen = true; u.overlay = Overlay::Density;
            CHECK(activeModal(u) == Modal::MasterFxPicker, "masterFx outranks lower modals");
        }
        {
            UiState u; u.machinePickerOpen = true; u.euclidHeld = true;
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
        { UiState u; u.machinePickerOpen = true;
          CHECK(activeFuncReskin(u) == FuncReskin::MachinePicker && activeModal(u) == Modal::MachinePicker, "machine agrees"); }
        { UiState u; u.masterFxPickerOpen = true;
          CHECK(activeFuncReskin(u) == FuncReskin::MasterFxPicker && activeModal(u) == Modal::MasterFxPicker, "masterFx agrees"); }
    }

    // Drift guard: activeModal()'s grid-modal priority must agree with
    // resolveActiveLayer()'s cascade (the user-visible truth). This locks the two
    // parallel priority encodings together so they cannot silently diverge — the
    // whole point of having one read SSOT.
    static void testActiveModalMatchesResolveLayer()
    {
        EditContext ec;
        const LayerFacts f{ TrackInputMode::Play, 0 };

        { UiState u; u.masterFxPickerOpen = true;
          CHECK(activeModal(u) == Modal::MasterFxPicker
                && resolveActiveLayer(u, ec, f) == SurfaceLayer::MasterFxPicker, "masterFx layer"); }
        { UiState u; u.funcFxHeld = true;
          CHECK(activeModal(u) == Modal::TrackFxPicker
                && resolveActiveLayer(u, ec, f) == SurfaceLayer::TrackFxPicker, "trackFx layer"); }
        { UiState u; u.machinePickerOpen = true;
          CHECK(activeModal(u) == Modal::MachinePicker
                && resolveActiveLayer(u, ec, f) == SurfaceLayer::MachinePicker, "machine layer"); }
        { UiState u; u.generatorHubHeld = true;
          CHECK(activeModal(u) == Modal::GeneratorHub
                && resolveActiveLayer(u, ec, f) == SurfaceLayer::GeneratorHub, "genHub layer"); }
        { UiState u; u.noteEditMode = true; u.noteEditSteps.insert(0);
          CHECK(activeModal(u) == Modal::NoteEdit
                && resolveActiveLayer(u, ec, f) == SurfaceLayer::NoteEdit, "noteEdit layer"); }
        { UiState u; u.pLockClearMode = true; u.pLockClearTrack = 0; u.pLockClearStep = 0;
          CHECK(activeModal(u) == Modal::PLockClear
                && resolveActiveLayer(u, ec, f) == SurfaceLayer::PLockClear, "pLockClear layer"); }
    }

    void runModalStateTests()
    {
        testActiveModalSingles();
        testActiveModalPriority();
        testActiveModalMatchesLegacy();
        testActiveModalMatchesResolveLayer();
        testModalNameGolden();
    }
}
