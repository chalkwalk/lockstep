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

        // TapTempo: primaryGesture=Hold → key region shows "GEN HUB" (primary) + tap="TAP".
        const auto tapKey = build(ui, ec, proc, ControllerButton::TapTempo, -1);
        CHECK(tapKey.key.containsIgnoreCase("GEN HUB"), "TapTempo key region shows GEN HUB");
        CHECK(tapKey.key.containsIgnoreCase("TAP"), "TapTempo key region shows TAP gesture");

        // VerbPlay: tap label = PLAY, dbl-tap = CUT, triple-tap = MASTER CUT (layered stop).
        const auto play = build(ui, ec, proc, ControllerButton::VerbPlay, -1);
        CHECK(play.key.containsIgnoreCase("PLAY"), "VerbPlay key region shows PLAY");
        CHECK(play.key.containsIgnoreCase("dbl=CUT"), "VerbPlay key region shows CUT (dbl)");
        CHECK(play.key.containsIgnoreCase("triple=MASTER CUT"), "VerbPlay key region shows MASTER CUT (triple)");

        // Step button: index + shared gesture summary (no per-cell label on the grid).
        const auto step = build(ui, ec, proc, ControllerButton::Step, 4);
        CHECK(step.key.containsIgnoreCase("step 5"), "step index 4 shows 'step 5'");
        CHECK(step.key.containsIgnoreCase("hold=P-LOCK"), "step region narrates hold=P-LOCK");
        CHECK(step.key.containsIgnoreCase("dbl=LATCH"), "step region narrates dbl=LATCH");

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

        // Func + Phrase → length-edit hint (must win over the generic FUNC text).
        {
            UiState ui{};
            ui.funcHeld = true;
            ui.phraseScopeHeld = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.held.containsIgnoreCase("LENGTH"), "Func+Phrase → LENGTH hint");
            CHECK(m.held.containsIgnoreCase("set"), "Func+Phrase → 'set length' guidance");
        }

        // Func + Morph → all-tracks length-edit hint.
        {
            UiState ui{};
            ui.funcHeld = true;
            ui.morphHeld = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.held.containsIgnoreCase("LENGTH"), "Func+Morph → LENGTH hint");
            CHECK(m.held.containsIgnoreCase("all tracks"), "Func+Morph → all-tracks hint");
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

        // Machine picker (9.29: Track+hold(SRC)) -> PICK MACHINE text.
        {
            UiState ui{};
            ui.machinePickerOpen = true;
            const auto m = build(ui, ec, proc);
            CHECK(m.overlay.containsIgnoreCase("PICK MACHINE"),
                  "machinePickerOpen → PICK MACHINE in overlay region");
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

    // ─── 9.30: the STATUS lane and the taxonomy ───────────────────────────────
    //
    // These are the tests the old code could not have had, because the old code did not
    // know there was a rule. THE RULE: anything that changes what the next key press
    // does is STATE, and must render for as long as it is armed (§42.2).

    static void testConfirmIsStateNotEvent()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ec;
        UiState ui;
        ui.confirm = { ConfirmKind::DeleteTrack, 2 };

        // The killer case: a confirm is armed, and the toast that announced it has LONG
        // since expired. The old surface showed nothing at all here -- zero pixels
        // saying the next P destroys track 3 -- while the arming was still fully live.
        StatusInput si;
        si.toast = "Delete which TRACK?";
        si.toastAgeMs = 99999;          // the toast is long dead
        si.missingSamples = 0;

        const auto m = buildInspectorModel(ui, ec, proc, ControllerButton::None, -1, si);

        CHECK(m.statusKind == StatusKind::Confirm,
              "an armed confirm is a CONFIRM (a State), whatever the toast is doing");
        CHECK(m.statusAlpha == 1.0f, "it does NOT fade -- a State never fades");
        CHECK(m.confirmPrompt.contains("TRACK 3"),
              "the prompt is derived from the STATE (kind + target), not from a stale message");
        CHECK(m.confirmActions.contains("CONFIRM") && m.confirmActions.contains("CANCEL"),
              "both exits are stated: the user must be able to answer without guessing");
    }

    static void testStateOutranksToast()
    {
        EngineHarness h;
        EditContext ec;
        UiState ui;
        ui.deletePicker.scope = DeleteScope::Track;   // armed, waiting for a target

        StatusInput si;
        si.toast = "Copied phrase 3";                 // a FRESH toast
        si.toastAgeMs = 0;

        const auto m = buildInspectorModel(ui, ec, h.processor(), ControllerButton::None, -1, si);
        CHECK(m.statusKind == StatusKind::State,
              "an armed picker outranks even a brand-new toast: a fading event must never "
              "hide something waiting for the next key");
        CHECK(m.status.containsIgnoreCase("WHICH TRACK"), "and it says what it is waiting for");
    }

    static void testEventFadesAndExpires()
    {
        EngineHarness h;
        EditContext ec;
        UiState ui;

        StatusInput si;
        si.toast = "Quantized";
        si.toastDurationMs = 1000;

        si.toastAgeMs = 0;
        auto m = buildInspectorModel(ui, ec, h.processor(), ControllerButton::None, -1, si);
        CHECK(m.statusKind == StatusKind::Event && m.statusAlpha == 1.0f, "a fresh toast is opaque");

        si.toastAgeMs = 500;
        m = buildInspectorModel(ui, ec, h.processor(), ControllerButton::None, -1, si);
        CHECK(m.statusKind == StatusKind::Event && m.statusAlpha > 0.4f && m.statusAlpha < 0.6f,
              "an Event fades -- it is the ONE kind that may");

        si.toastAgeMs = 2000;
        m = buildInspectorModel(ui, ec, h.processor(), ControllerButton::None, -1, si);
        CHECK(m.statusKind == StatusKind::None, "and then it is gone");
    }

    static void testAlertSurvivesTheToast()
    {
        EngineHarness h;
        EditContext ec;
        UiState ui;

        StatusInput si;
        si.missingSamples = 3;
        si.toast = "Copied track 1";
        si.toastAgeMs = 0;

        auto m = buildInspectorModel(ui, ec, h.processor(), ControllerButton::None, -1, si);
        CHECK(m.statusKind == StatusKind::Event, "the newest news shows first");

        // ...and the alert is still there underneath, because an Alert is not consumed
        // by being covered: it lives until the CONDITION clears.
        si.toastAgeMs = 99999;
        m = buildInspectorModel(ui, ec, h.processor(), ControllerButton::None, -1, si);
        CHECK(m.statusKind == StatusKind::Alert, "when the toast dies the alert is still there");
        CHECK(m.status.contains("3 samples missing"), "and still counting");
    }

    // ─── entry point ──────────────────────────────────────────────────────────

    void runInspectorModelTests()
    {
        testIdleFallbacks();
        testKeyRegionMatchesAffordance();
        testHeldRegionModifiers();
        testOverlayRegion();
        testEditRegionHeldStep();
        testConfirmIsStateNotEvent();
        testStateOutranksToast();
        testEventFadesAndExpires();
        testAlertSurvivesTheToast();

        juce::Logger::writeToLog("Completed tests in InspectorModel / 9.11 context inspector");
    }

} // namespace lockstep
