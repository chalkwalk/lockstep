// KeyAffordanceTest — unit tests for the gesture-affordance SSOT (9.11).
//
// Verifies: findAffordance() returns expected entries; buildSurfaceModel()
// injects affordance labels into the appropriate SurfaceCell fields at rest
// but not when modifiers are held.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/command/KeyAffordances.h"
#include "../src/ui/SurfaceModel.h"
#include "../src/state/UiState.h"
#include "../src/io/EditContext.h"
#include "../src/ui/GridDisplayMode.h"
#include "../src/command/KeyBindings.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // Test: findAffordance() lookup
    // -------------------------------------------------------------------------
    static void testFindAffordance()
    {
        // TapTempo is the key 3 entry with primaryIsHold=true.
        const KeyAffordance* tap = findAffordance(ControllerButton::TapTempo);
        CHECK(tap != nullptr, "TapTempo affordance registered");
        if (tap)
        {
            CHECK(tap->primaryIsHold, "TapTempo primaryIsHold=true");
            CHECK(tap->holdLabel != nullptr, "TapTempo holdLabel non-null");
            CHECK(tap->tapLabel  != nullptr, "TapTempo tapLabel non-null");
        }

        // Func modifier registered.
        const KeyAffordance* func = findAffordance(ControllerButton::Func);
        CHECK(func != nullptr, "Func affordance registered");
        if (func)
        {
            CHECK(!func->primaryIsHold, "Func primaryIsHold=false");
            CHECK(func->holdLabel  != nullptr, "Func holdLabel non-null");
            CHECK(func->doubleTapLabel != nullptr, "Func doubleTapLabel non-null");
        }

        // VerbPlay has a doubleTapLabel (STOP).
        const KeyAffordance* play = findAffordance(ControllerButton::VerbPlay);
        CHECK(play != nullptr, "VerbPlay affordance registered");
        if (play)
            CHECK(play->doubleTapLabel != nullptr, "VerbPlay doubleTapLabel non-null");

        // Step keys have no affordance entry.
        CHECK(findAffordance(ControllerButton::Step) == nullptr,
              "Step has no affordance entry");
    }

    // -------------------------------------------------------------------------
    // Test: buildSurfaceModel injects affordances at rest (heldMods == kModNone)
    // -------------------------------------------------------------------------
    static void testAffordancesInjectedAtRest()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        EditContext ec;

        const SurfaceModel model = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        // model.tap = TapTempo; primaryIsHold → primary = GEN HUB, tapLabel = TAP TEMPO.
        CHECK(model.tap.primaryIsHold, "tap cell primaryIsHold=true at rest");
        CHECK(model.tap.holdLabel.isNotEmpty(), "tap cell holdLabel non-empty at rest");
        CHECK(model.tap.tapLabel.isNotEmpty(),  "tap cell tapLabel non-empty at rest");

        // Modifiers: modifiers[0] = Func; should have holdLabel + doubleTapLabel.
        CHECK(model.modifiers[0].holdLabel.isNotEmpty(),     "Func holdLabel non-empty");
        CHECK(model.modifiers[0].doubleTapLabel.isNotEmpty(),"Func doubleTapLabel non-empty");

        // functionRow: find Play (VerbPlay) — it has a doubleTapLabel (STOP).
        bool foundPlay = false;
        for (const auto& c : model.functionRow)
        {
            if (c.button == ControllerButton::VerbPlay)
            {
                foundPlay = true;
                CHECK(c.doubleTapLabel.isNotEmpty(), "VerbPlay doubleTapLabel non-empty at rest");
                break;
            }
        }
        CHECK(foundPlay, "VerbPlay found in functionRow");
    }

    // -------------------------------------------------------------------------
    // Test: affordances NOT injected when a modifier is held (context-sensitive labels
    // take priority; affordance slots must remain clean).
    // -------------------------------------------------------------------------
    static void testAffordancesSkippedWhenModifierHeld()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.funcHeld = true;
        EditContext ec;

        const SurfaceModel model = buildSurfaceModel(
            ui, ec, nullptr, proc, 0, 0, GridDisplayMode::Ortholinear);

        // Func held → heldMods != kModNone → no affordance injection.
        CHECK(!model.tap.primaryIsHold, "tap primaryIsHold=false when Func held");
        CHECK(model.tap.tapLabel.isEmpty(),  "tap tapLabel empty when Func held");
        CHECK(model.tap.holdLabel.isEmpty(), "tap holdLabel empty when Func held");
    }

    void runKeyAffordanceTests()
    {
        testFindAffordance();
        testAffordancesInjectedAtRest();
        testAffordancesSkippedWhenModifierHeld();
    }

} // namespace lockstep
