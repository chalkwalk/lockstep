// StateResolverTest — the Override-ELSE-Base (OEB) merge that feeds every
// machine. This is the audio heart; the "go direct" consolidation changes how
// the resolver is *fed* (Phrase+Kit instead of the legacy merge buffer), so
// pinning its merge semantics here guards that refactor.

#include "TestHarness.h"
#include "../src/core/StateResolver.h"
#include "../src/core/Track.h"

namespace lockstep
{
    static Track makeTrack(std::initializer_list<float> base)
    {
        Track t;
        t.baseParams.assign(base.begin(), base.end());
        return t;
    }

    static void testResolveBaseOnly()
    {
        auto t = makeTrack({ 0.1f, 0.2f, 0.3f });

        // stepIndex < 0 → pure base, no overrides consulted.
        const auto f = StateResolver::resolve(t, -1, false);
        CHECK(f.size() == 3, "base-only: frame keeps base size");
        CHECK(feq(f[0], 0.1f) && feq(f[1], 0.2f) && feq(f[2], 0.3f),
              "base-only: values equal baseParams");
    }

    static void testResolveOverrideElseBase()
    {
        auto t = makeTrack({ 0.1f, 0.2f, 0.3f });
        t.steps[0].overrides.set(1, 0.9f);   // override only slot 1

        const auto f = StateResolver::resolve(t, 0, false);
        CHECK(feq(f[0], 0.1f), "OEB: un-overridden slot 0 falls through to base");
        CHECK(feq(f[1], 0.9f), "OEB: slot 1 takes the step override");
        CHECK(feq(f[2], 0.3f), "OEB: un-overridden slot 2 falls through to base");

        // A different step with no overrides resolves to base.
        const auto f2 = StateResolver::resolve(t, 1, false);
        CHECK(feq(f2[1], 0.2f), "OEB: step without override resolves to base");
    }

    static void testResolveFillPrecedence()
    {
        auto t = makeTrack({ 0.1f, 0.2f, 0.3f });
        t.steps[0].overrides.set(1, 0.9f);       // base-override layer
        t.steps[0].fillOverrides.set(1, 0.5f);   // fill layer on the same slot
        t.steps[0].fillOverrides.set(2, 0.7f);   // fill-only slot

        // Fill not held: fill layer is ignored entirely.
        const auto noFill = StateResolver::resolve(t, 0, false);
        CHECK(feq(noFill[1], 0.9f), "fill off: base override wins, fill ignored");
        CHECK(feq(noFill[2], 0.3f), "fill off: fill-only slot stays at base");

        // Fill held: fill layer wins over the base override (three-tier).
        const auto fill = StateResolver::resolve(t, 0, true);
        CHECK(feq(fill[1], 0.5f), "fill on: fill override beats base override");
        CHECK(feq(fill[2], 0.7f), "fill on: fill-only slot applies over base");
    }

    static void testResolveTrigDefaultsAndOverride()
    {
        Track t;
        t.trigDefaults.note = 60;
        t.trigDefaults.velocity = 100;

        // No override → defaults.
        const auto d = StateResolver::resolveTrig(t, 0, false);
        CHECK(d.noteCount == 1 && d.notes[0] == 60, "trig: default note from trigDefaults");
        CHECK(d.velocity == 100, "trig: default velocity from trigDefaults");

        // Note + velocity override.
        t.steps[0].trigOverride.noteCount = 2;
        t.steps[0].trigOverride.notes = { 64, 67, 0, 0 };
        t.steps[0].trigOverride.hasVelocity = true;
        t.steps[0].trigOverride.velocity = 80;
        const auto o = StateResolver::resolveTrig(t, 0, false);
        CHECK(o.noteCount == 2 && o.notes[0] == 64 && o.notes[1] == 67,
              "trig: chord override replaces default note");
        CHECK(o.velocity == 80, "trig: velocity override applies");
    }

    static void testResolveTrigFillLayer()
    {
        Track t;
        t.trigDefaults.note = 60;
        t.steps[0].trigOverride.noteCount = 1;
        t.steps[0].trigOverride.notes = { 62, 0, 0, 0 };
        t.steps[0].fillTrigOverride.noteCount = 1;
        t.steps[0].fillTrigOverride.notes = { 72, 0, 0, 0 };

        const auto noFill = StateResolver::resolveTrig(t, 0, false);
        CHECK(noFill.notes[0] == 62, "trig fill off: base trig override note");

        const auto fill = StateResolver::resolveTrig(t, 0, true);
        CHECK(fill.notes[0] == 72, "trig fill on: fill trig override note wins");
    }

    void runStateResolverTests()
    {
        testResolveBaseOnly();
        testResolveOverrideElseBase();
        testResolveFillPrecedence();
        testResolveTrigDefaultsAndOverride();
        testResolveTrigFillLayer();
    }
}
