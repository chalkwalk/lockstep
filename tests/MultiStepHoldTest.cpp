// MultiStepHoldTest — Part 2 multi-step holds (Elektron flow).
//
// Covers the three new modalities per the CLAUDE.md rule (resolution, routing,
// round-trip through state):
//   1. computeBlockMoveSwaps — the pure block-move swap-order + clamp algorithm.
//   2. EditContext — multi-hold accumulation, remapHeldStep after a move, release.
//   3. Relative P-Lock fan-out — writeParam across several held steps lands
//      relative nudges on continuous slots and absolute values on stepped slots
//      (driven through the real LockstepProcessor + a DrumMachine schema).

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/core/StepBlockMove.h"
#include "../src/io/EditContext.h"
#include "../src/machine/DrumMachine.h"

namespace lockstep
{
    // ── 1. Block-move algorithm ────────────────────────────────────────────────

    static void testBlockMoveSwaps()
    {
        // Adjacent pair, move right: descending order so they don't double-move.
        {
            const auto sw = computeBlockMoveSwaps({ 3, 4 }, +1, 16);
            CHECK(sw.size() == 2, "adjacent right: two swaps");
            CHECK(sw[0] == std::make_pair(4, 5), "adjacent right: highest first (4->5)");
            CHECK(sw[1] == std::make_pair(3, 4), "adjacent right: then 3->4");
        }
        // Adjacent pair, move left: ascending order.
        {
            const auto sw = computeBlockMoveSwaps({ 3, 4 }, -1, 16);
            CHECK(sw.size() == 2, "adjacent left: two swaps");
            CHECK(sw[0] == std::make_pair(3, 2), "adjacent left: lowest first (3->2)");
            CHECK(sw[1] == std::make_pair(4, 3), "adjacent left: then 4->3");
        }
        // Non-adjacent set, move right: independent neighbour swaps.
        {
            const auto sw = computeBlockMoveSwaps({ 3, 7 }, +1, 16);
            CHECK(sw.size() == 2, "gap right: two swaps");
            CHECK(sw[0] == std::make_pair(7, 8), "gap right: 7->8 first");
            CHECK(sw[1] == std::make_pair(3, 4), "gap right: 3->4");
        }
        // Clamp: leading edge at the last index → no move.
        {
            CHECK(computeBlockMoveSwaps({ 14, 15 }, +1, 16).empty(),
                  "right clamp: block at end is a no-op");
            CHECK(computeBlockMoveSwaps({ 0, 5 }, -1, 16).empty(),
                  "left clamp: block at start is a no-op");
        }
        // Degenerate inputs.
        {
            CHECK(computeBlockMoveSwaps({}, +1, 16).empty(), "empty held: no swaps");
            CHECK(computeBlockMoveSwaps({ 3 }, 0, 16).empty(), "dir 0: no swaps");
            CHECK(computeBlockMoveSwaps({ 0 }, +1, 1).empty(), "len 1: no swaps");
        }
    }

    // ── 2. EditContext multi-hold ──────────────────────────────────────────────

    static void testEditContextMultiHold()
    {
        EditContext ctx;
        ctx.hold(0, 3);
        ctx.hold(0, 5);
        ctx.hold(0, 9);
        CHECK(ctx.heldSteps().size() == 3, "three steps accumulate");
        CHECK(ctx.heldStepIndex() == 3, "primary is first pressed");

        // Duplicate press is ignored (key-repeat).
        ctx.hold(0, 5);
        CHECK(ctx.heldSteps().size() == 3, "duplicate hold ignored");

        // A hold on a different track releases the previous set.
        ctx.hold(1, 2);
        CHECK(ctx.heldSteps().size() == 1, "cross-track hold clears previous");
        CHECK(ctx.heldTrackIndex() == 1, "track follows the new hold");

        // remapHeldStep follows a moved step, preserving order + latch membership.
        ctx.release();
        ctx.hold(0, 3);
        ctx.hold(0, 5);
        ctx.setLatched(5);
        ctx.remapHeldStep(5, 6);
        CHECK(ctx.heldSteps()[1] == 6, "remap moved held step 5 -> 6");
        CHECK(ctx.isLatched(6) && !ctx.isLatched(5), "latch follows the remap");
        CHECK(ctx.heldStepIndex() == 3, "primary unchanged by remap of another step");

        // Releasing one of several keeps the rest; last release clears the track.
        ctx.release(3);
        CHECK(ctx.heldSteps().size() == 1 && ctx.heldStepIndex() == 6,
              "release primary leaves the other held step");
        ctx.release(6);
        CHECK(ctx.heldSteps().empty() && ctx.heldTrackIndex() == -1,
              "last release clears the context");
    }

    // ── 3. Relative P-Lock fan-out across held steps ────────────────────────────

    // Install a DrumMachine on `track` (mirrors EngineTest::installMachine).
    static void installDrum(LockstepProcessor& proc, int track)
    {
        DrumMachine tmp;
        const int np = tmp.numParams();
        auto& k = proc.kit(track);
        k.machineId = DrumMachine::kMachineId;
        k.baseParams.resize(static_cast<std::size_t>(np));
        for (int i = 0; i < np; ++i)
            k.baseParams[static_cast<std::size_t>(i)] = tmp.paramSpec(i).defaultValue;
        proc.reinstallMachinesFromActiveKit();
    }

    static void testRelativeFanOut()
    {
        EngineHarness h;
        auto& proc = h.processor();
        installDrum(proc, 0);

        // Discover a continuous slot and a stepped slot from the schema.
        DrumMachine tmp;
        int contSlot = -1, stepSlot = -1;
        for (int i = 0; i < tmp.numParams(); ++i)
        {
            const auto sp = tmp.paramSpec(i);
            if (!sp.isStepped && contSlot < 0 && sp.maxValue > sp.minValue) contSlot = i;
            if (sp.isStepped && stepSlot < 0) stepSlot = i;
        }
        CHECK(contSlot >= 0, "drum schema has a continuous slot");

        const auto sp = tmp.paramSpec(contSlot);
        const float lo = sp.minValue, hi = sp.maxValue, span = hi - lo;
        const float base = proc.baseParamValue(0, contSlot);

        // Pre-seed differing overrides: step 2 (primary) and step 8; step 5 has none.
        auto& trk = proc.sequence().tracks[0];
        const float primOld = lo + 0.20f * span;
        const float otherOld = lo + 0.40f * span;
        trk.steps[2].overrides.set(contSlot, primOld);
        trk.steps[8].overrides.set(contSlot, otherOld);

        // Hold 2 (primary), 5, 8 and write a new value to the primary's position.
        proc.editContext().hold(0, 2);
        proc.editContext().hold(0, 5);
        proc.editContext().hold(0, 8);
        const float target = lo + 0.30f * span;   // delta = +0.10*span, no clamp
        proc.writeParam(0, contSlot, target);
        h.renderBlocks(1);   // drain the SetStepOverride engine commands

        const float v2 = trk.steps[2].overrides.get(contSlot, -999.0f);
        const float v5 = trk.steps[5].overrides.get(contSlot, -999.0f);
        const float v8 = trk.steps[8].overrides.get(contSlot, -999.0f);

        CHECK(feq(v2, target, 1e-4f), "primary held step lands the exact value");
        CHECK(feq(v8 - v2, otherOld - primOld, 1e-4f),
              "other held step preserves its offset from the primary (relative)");
        CHECK(feq(v5, base + (target - primOld), 1e-4f),
              "no-override held step nudged off base by the same delta");
        CHECK(!feq(v5, base, 1e-4f), "no-override step actually moved");

        // Stepped slot → absolute to all held steps.
        if (stepSlot >= 0)
        {
            const auto ssp = tmp.paramSpec(stepSlot);
            const float sval = ssp.minValue
                + std::round((ssp.maxValue - ssp.minValue) * 0.5f);
            // Pre-seed a different value on one step to prove it is overwritten.
            proc.sequence().tracks[0].steps[2].overrides.set(stepSlot, ssp.minValue);
            proc.writeParam(0, stepSlot, sval);
            h.renderBlocks(1);
            auto& t2 = proc.sequence().tracks[0];
            const float s2 = t2.steps[2].overrides.get(stepSlot, -999.0f);
            const float s5 = t2.steps[5].overrides.get(stepSlot, -999.0f);
            const float s8 = t2.steps[8].overrides.get(stepSlot, -999.0f);
            CHECK(feq(s2, sval, 1e-4f) && feq(s5, sval, 1e-4f) && feq(s8, sval, 1e-4f),
                  "stepped slot writes the absolute value to every held step");
        }
    }

    void runMultiStepHoldTests()
    {
        testBlockMoveSwaps();
        testEditContextMultiHold();
        testRelativeFanOut();
    }
}
