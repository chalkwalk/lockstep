#include "TestHarness.h"
#include "GestureHarness.h"

namespace lockstep
{
    using namespace test;
    using CB = ControllerButton;
    using PS = EditMode::PrimaryScope;
    using T  = ControllerEvent::Type;

    // -------------------------------------------------------------------------
    // Scenario 1: PS::Trig / VerbRecord copies held steps to clipboard

    static void scenario_trigCopy()
    {
        GestureFixture f;
        // Seed step 3 with a trig and a recognisable condition.
        auto& s3 = f.track(0).steps[3];
        s3.trig = true;
        s3.condition.probabilityPercent = 75;

        // Hold step 3 on track 0.
        f.holdStep(0, 3);

        const bool handled = f.verb(PS::Trig, CB::VerbRecord);
        CHECK(handled, "Trig+Record should be handled by CommandCore");
        CHECK(f.clipboard.type == ClipboardType::Step, "clipboard type should be Step");
        CHECK(f.clipboard.stepEntries.size() == 1, "one step entry copied");
        CHECK(f.clipboard.stepEntries[0].relOffset == 0, "anchor offset is 0");
        CHECK(f.clipboard.stepEntries[0].data.trig, "copied step has trig=true");
        CHECK(f.clipboard.stepEntries[0].data.condition.probabilityPercent == 75, "condition preserved");
    }

    // Scenario 2: PS::Trig / VerbPlay pastes with wrap-around

    static void scenario_trigPaste()
    {
        GestureFixture f;
        // Populate clipboard with a step entry at relative offset 0.
        auto& s5 = f.track(0).steps[5];
        s5.trig = true;
        f.holdStep(0, 5);
        f.verb(PS::Trig, CB::VerbRecord);
        f.releaseAllSteps();
        f.effects.reset();

        // Track length = 16 (default). Paste at step 15 — wraps to step 15+0=15.
        f.holdStep(0, 15);
        const bool handled = f.verb(PS::Trig, CB::VerbPlay);
        CHECK(handled, "Trig+Play should be handled");
        CHECK(f.track(0).steps[15].trig, "pasted trig at step 15");
    }

    // Scenario 3: PS::Trig / VerbClear with no Func and no active slot
    // → full clear (trig=false, condition reset, P-Locks wiped)

    static void scenario_trigClearFull()
    {
        GestureFixture f;
        auto& s2 = f.track(0).steps[2];
        s2.trig = true;
        s2.condition.probabilityPercent = 50;

        f.holdStep(0, 2);
        const bool handled = f.verb(PS::Trig, CB::VerbClear);
        CHECK(handled, "Trig+Clear should be handled");
        CHECK(!f.track(0).steps[2].trig, "trig cleared");
        CHECK(f.track(0).steps[2].condition.probabilityPercent == 100, "condition reset to default");
        CHECK(f.editContext.wasParamWritten(), "markParamWritten called");
    }

    // Scenario 4: PS::Trig / VerbClear with Func held → clear P-Locks only

    static void scenario_trigClearPLocks()
    {
        GestureFixture f;
        auto& s7 = f.track(0).steps[7];
        s7.trig = true;
        s7.overrides.set(0, 0.5f);  // P-Lock slot 0

        // Signal Func is held via editMode scope state.
        f.editMode.onScopeEvent({ T::ButtonDown, CB::Func, -1, 0 });
        f.holdStep(0, 7);

        const bool handled = f.verb(PS::Trig, CB::VerbClear);
        CHECK(handled, "Trig+Func+Clear should be handled");
        CHECK(f.track(0).steps[7].trig, "trig intact after P-Lock clear");
        // P-Lock cleared.
        CHECK(!f.track(0).steps[7].overrides.has(0), "P-Lock cleared");
    }

    // Scenario 5: PS::Trig / VerbNo with Func held → clear note/vel/gate overrides

    static void scenario_trigClearNotes()
    {
        GestureFixture f;
        auto& s1 = f.track(0).steps[1];
        s1.trig = true;
        s1.trigOverride.noteCount  = 2;
        s1.trigOverride.hasVelocity = true;
        s1.trigOverride.velocity   = 80;

        f.editMode.onScopeEvent({ T::ButtonDown, CB::Func, -1, 0 });
        f.holdStep(0, 1);

        const bool handled = f.verb(PS::Trig, CB::VerbNo);
        CHECK(handled, "Trig+Func+No should be handled");
        CHECK(f.track(0).steps[1].trigOverride.noteCount == 0, "noteCount cleared");
        CHECK(!f.track(0).steps[1].trigOverride.hasVelocity, "hasVelocity cleared");
        CHECK(f.track(0).steps[1].trig, "trig still set");
    }

    // Scenario 6: no held steps → trig copy returns false (no-op)

    static void scenario_trigCopyNoSteps()
    {
        GestureFixture f;
        // No steps held — should not be handled.
        const bool handled = f.verb(PS::Trig, CB::VerbRecord);
        CHECK(!handled, "Trig+Record with no held steps should not be handled");
    }

    // -------------------------------------------------------------------------
    // Test runner

    void runGestureTests()
    {
        scenario_trigCopy();
        scenario_trigPaste();
        scenario_trigClearFull();
        scenario_trigClearPLocks();
        scenario_trigClearNotes();
        scenario_trigCopyNoSteps();
    }
}
