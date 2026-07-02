// LayerBannerTest — characterisation tests for layerBanner().
//
// Each SurfaceLayer value is exercised explicitly. Because the layerBanner()
// implementation uses an exhaustive switch (no default:), the compiler enforces
// that any new SurfaceLayer added without a banner case is a build error. These
// tests then lock the *intended* banner text as a golden so renames are caught.

#include "TestHarness.h"
#include "../src/command/SurfaceLayer.h"
#include "../src/state/UiState.h"

namespace lockstep
{
    using SL = SurfaceLayer;

    // ── Helpers ───────────────────────────────────────────────────────────────

    static bool bannerEq(const char* a, const char* b) noexcept
    {
        if (a == nullptr && b == nullptr) return true;
        if (a == nullptr || b == nullptr) return false;
        return juce::String(a) == juce::String(b);
    }

    // ── Static banners ────────────────────────────────────────────────────────

    static void testPendingConfirmBanner()
    {
        UiState ui;
        CHECK(bannerEq(layerBanner(SL::PendingConfirm, ui), "CONFIRM?"),
              "PendingConfirm banner must be 'CONFIRM?'");
    }

    static void testMachinePickerBanner()
    {
        UiState ui;
        CHECK(bannerEq(layerBanner(SL::MachinePicker, ui), "SELECT MACHINE"),
              "MachinePicker banner must be 'SELECT MACHINE'");
    }

    static void testGeneratorHubBanner()
    {
        UiState ui;
        CHECK(bannerEq(layerBanner(SL::GeneratorHub, ui), "SELECT GENERATOR"),
              "GeneratorHub banner must be 'SELECT GENERATOR'");
    }

    // ── DeletePicker: dynamic on deletePicker.scope ────────────────────────────

    static void testDeletePickerBanners()
    {
        {
            UiState ui;
            ui.deletePicker.scope = DeleteScope::Track;
            CHECK(bannerEq(layerBanner(SL::DeletePicker, ui), "DELETE WHICH TRACK?"),
                  "DeletePicker/Track banner");
        }
        {
            UiState ui;
            ui.deletePicker.scope = DeleteScope::Phrase;
            CHECK(bannerEq(layerBanner(SL::DeletePicker, ui), "DELETE WHICH PHRASE?"),
                  "DeletePicker/Phrase banner");
        }
        {
            UiState ui;
            ui.deletePicker.scope = DeleteScope::Scene;
            CHECK(bannerEq(layerBanner(SL::DeletePicker, ui), "DELETE WHICH SCENE?"),
                  "DeletePicker/Scene banner");
        }
        {
            UiState ui;
            ui.deletePicker.scope = DeleteScope::None;
            CHECK(layerBanner(SL::DeletePicker, ui) == nullptr,
                  "DeletePicker/None → nullptr");
        }
    }

    // ── ScopeSelector: dynamic on held scope modifier ─────────────────────────

    static void testScopeSelectorBanners()
    {
        {
            UiState ui;
            ui.trackHeld = true;
            CHECK(bannerEq(layerBanner(SL::ScopeSelector, ui), "SELECT TRACK"),
                  "ScopeSelector with trackHeld → 'SELECT TRACK'");
        }
        {
            UiState ui;
            ui.phraseScopeHeld = true;
            CHECK(bannerEq(layerBanner(SL::ScopeSelector, ui), "SELECT PHRASE"),
                  "ScopeSelector with phraseScopeHeld → 'SELECT PHRASE'");
        }
        {
            UiState ui;
            ui.sceneHeld = true;
            CHECK(bannerEq(layerBanner(SL::ScopeSelector, ui), "SELECT SCENE"),
                  "ScopeSelector with sceneHeld → 'SELECT SCENE'");
        }
        {
            UiState ui;
            ui.songHeld = true;
            CHECK(layerBanner(SL::ScopeSelector, ui) == nullptr,
                  "ScopeSelector with songHeld → nullptr (no banner for Song scope)");
        }
        {
            UiState ui;  // no scope held
            CHECK(layerBanner(SL::ScopeSelector, ui) == nullptr,
                  "ScopeSelector with no scope → nullptr");
        }
    }

    // ── No-banner layers: all must return nullptr ─────────────────────────────

    static void testNoBannerLayers()
    {
        UiState ui;
        CHECK(layerBanner(SL::SoundPool,     ui) == nullptr, "SoundPool → nullptr");
        CHECK(layerBanner(SL::RetrigPicker,  ui) == nullptr, "RetrigPicker → nullptr");
        CHECK(layerBanner(SL::MasterFxPicker,ui) == nullptr, "MasterFxPicker → nullptr");
        CHECK(layerBanner(SL::TrackFxPicker, ui) == nullptr, "TrackFxPicker → nullptr");
        CHECK(layerBanner(SL::NoteEdit,      ui) == nullptr, "NoteEdit → nullptr");
        CHECK(layerBanner(SL::PLockClear,    ui) == nullptr, "PLockClear → nullptr");
        CHECK(layerBanner(SL::ChromaticInput,ui) == nullptr, "ChromaticInput → nullptr");
        CHECK(layerBanner(SL::LevelsInput,   ui) == nullptr, "LevelsInput → nullptr");
        CHECK(layerBanner(SL::MorphMuteView, ui) == nullptr, "MorphMuteView → nullptr");
        CHECK(layerBanner(SL::MuteView,      ui) == nullptr, "MuteView → nullptr");
        CHECK(layerBanner(SL::LengthEdit,    ui) == nullptr, "LengthEdit → nullptr");
        CHECK(layerBanner(SL::MorphStepView, ui) == nullptr, "MorphStepView → nullptr");
        CHECK(layerBanner(SL::Base,          ui) == nullptr, "Base → nullptr");
        // GeneratorHub DOES have a banner ("SELECT GENERATOR") — verified in testGeneratorHubBanner.
    }

    // 9.17: the Mute+Play relaunch view carries a RELAUNCH banner.
    static void testMuteRelaunchBanner()
    {
        UiState ui;
        CHECK(bannerEq(layerBanner(SL::MuteRelaunchView, ui),
                       "RELAUNCH — tap a track to restart it from step 1"),
              "MuteRelaunchView → RELAUNCH banner");
    }

    // ── Priority: Track beats Song when both held ──────────────────────────────

    static void testScopeSelectorPriority()
    {
        UiState ui;
        ui.trackHeld = true;
        ui.songHeld  = true;
        CHECK(bannerEq(layerBanner(SL::ScopeSelector, ui), "SELECT TRACK"),
              "Track takes priority over Song in ScopeSelector banner");
    }

    // ─────────────────────────────────────────────────────────────────────────

    void runLayerBannerTests()
    {
        testPendingConfirmBanner();
        testMachinePickerBanner();
        testGeneratorHubBanner();
        testDeletePickerBanners();
        testScopeSelectorBanners();
        testNoBannerLayers();
        testMuteRelaunchBanner();
        testScopeSelectorPriority();
    }

} // namespace lockstep
