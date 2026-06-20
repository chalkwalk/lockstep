// InspectorModelTest — unit tests for buildInspectorModel() pure builder.
//
// Verifies: correct region text per context; idle fallbacks are non-empty;
// KEY region matches KeyAffordances SSOT; HELD/OVERLAY change on mode entry.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/ui/InspectorModel.h"
#include "../src/io/EditContext.h"
#include "../src/io/ControllerEvent.h"
#include "../src/state/UiState.h"

namespace lockstep
{
    // ─── helpers ──────────────────────────────────────────────────────────────

    static InspectorModel build(const UiState& ui, const EditContext& ec,
                                const LockstepProcessor& proc,
                                ControllerButton btn = ControllerButton::None,
                                int idx = -1) noexcept
    {
        return buildInspectorModel(ui, ec, proc, btn, idx);
    }

    // ─── idle fallbacks ────────────────────────────────────────────────────────

    static void testIdleFallbacks()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui{};
        EditContext ec{};

        const auto m = build(ui, ec, proc);

        // All four regions must be populated even at idle.
        CHECK(m.key.isNotEmpty(), "idle: key region non-empty");
        CHECK(m.held.isNotEmpty(), "idle: held region non-empty");
        CHECK(m.overlay.isNotEmpty(), "idle: overlay region non-empty");
        CHECK(m.edit.isNotEmpty(), "idle: edit region non-empty");

        // Idle EDIT must be "--" (no held step).
        CHECK(m.edit == u8"--", "idle: edit region is '--'");

        // Idle HELD mentions the track number.
        CHECK(m.held.contains("track"), "idle: held region mentions track");
    }

    // ─── KEY region matches KeyAffordances ────────────────────────────────────

    static void testKeyRegionMatchesAffordance()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui{};
        EditContext ec{};

        // TapTempo: primaryIsHold=true → key region shows "GEN HUB" (primary).
        const auto tap = build(ui, ec, proc, ControllerButton::TapTempo, -1);
        CHECK(tap.key.containsIgnoreCase("GEN HUB"), "TapTempo key region shows GEN HUB");
        CHECK(tap.key.containsIgnoreCase("TAP TEMPO"), "TapTempo key region shows TAP TEMPO gesture");

        // VerbPlay: tap label = PLAY, dbl-tap = STOP.
        const auto play = build(ui, ec, proc, ControllerButton::VerbPlay, -1);
        CHECK(play.key.containsIgnoreCase("PLAY"), "VerbPlay key region shows PLAY");
        CHECK(play.key.containsIgnoreCase("STOP"), "VerbPlay key region shows STOP (dbl)");

        // Step button: no affordance entry → "step N".
        const auto step = build(ui, ec, proc, ControllerButton::Step, 4);
        CHECK(step.key.containsIgnoreCase("step 5"), "step index 4 shows 'step 5'");

        // No focused button → "--".
        const auto none = build(ui, ec, proc, ControllerButton::None, -1);
        CHECK(none.key == u8"--", "None button → '--'");
    }

    // ─── HELD region reflects modifier state ──────────────────────────────────

    static void testHeldRegionModifiers()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec{};

        // Func held → FUNC in region.
        {
            UiState ui{};
            ui.funcHeld = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.held.containsIgnoreCase("FUNC"), "funcHeld → FUNC in held region");
        }

        // Scene held → SCENE in region.
        {
            UiState ui{};
            ui.sceneHeld = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.held.containsIgnoreCase("SCENE"), "sceneHeld → SCENE in held region");
        }

        // Track held → TRACK + LATCH hint.
        {
            UiState ui{};
            ui.trackHeld = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.held.containsIgnoreCase("TRACK"), "trackHeld → TRACK in held region");
            CHECK(m.held.containsIgnoreCase("LATCH"), "trackHeld → LATCH hint present");
        }
    }

    // ─── OVERLAY region reflects active picker / sticky overlay ───────────────

    static void testOverlayRegion()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec{};

        // Generator hub held → generator hub text.
        {
            UiState ui{};
            ui.generatorHubHeld = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.overlay.containsIgnoreCase("GENERATOR HUB"),
                  "generatorHubHeld → GENERATOR HUB in overlay region");
        }

        // Machine picker → MACHINE PICKER text.
        {
            UiState ui{};
            ui.funcTrackHeld = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.overlay.containsIgnoreCase("MACHINE PICKER"),
                  "funcTrackHeld → MACHINE PICKER in overlay region");
        }

        // Density sticky overlay.
        {
            UiState ui{};
            ui.overlay = Overlay::Density;
            const auto m = build(ui, ec, proc);
            CHECK(m.overlay.containsIgnoreCase("DENSITY"),
                  "Overlay::Density → DENSITY in overlay region");
        }

        // Idle → scene number present.
        {
            UiState ui{};
            const auto m = build(ui, ec, proc);
            CHECK(m.overlay.containsIgnoreCase("scene"),
                  "idle overlay region contains 'scene'");
        }
    }

    // ─── EDIT region idle vs held step ────────────────────────────────────────

    static void testEditRegionHeldStep()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui{};
        EditContext ec{};

        // No step held → "--".
        CHECK(build(ui, ec, proc).edit == u8"--", "no held step → '--'");

        // Held step → "step N".
        ec.hold(0, 2);
        const auto m = build(ui, ec, proc);
        CHECK(m.edit.containsIgnoreCase("step 3"), "held step 2 → 'step 3'");
    }

    // ─── entry point ──────────────────────────────────────────────────────────

    void runInspectorModelTests()
    {
        testIdleFallbacks();
        testKeyRegionMatchesAffordance();
        testHeldRegionModifiers();
        testOverlayRegion();
        testEditRegionHeldStep();

        juce::Logger::writeToLog("Completed tests in InspectorModel / 9.11 context inspector");
    }

} // namespace lockstep
