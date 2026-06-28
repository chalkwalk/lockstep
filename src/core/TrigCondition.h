#pragma once

#include <cstdint>

namespace lockstep
{
    // Per-step trigger condition. M0 carries the data; the evaluator and
    // previous-dependency state machine land in M4.
    struct TrigCondition
    {
        // 1..100. 100 = unconditional fire.
        std::uint8_t probabilityPercent = 100;

        // Iteration rule: fire on iteration `numerator` of every
        // `denominator` repeats. {1,1} means every time. {1,4} fires once
        // every four playthroughs of this step.
        std::uint8_t iterNumerator = 1;
        std::uint8_t iterDenominator = 1;

        // 0 = no previous-dependency. 1 = fire only if previous step fired.
        // 2 = fire only if previous step did NOT fire.
        std::uint8_t prevDependency = 0;

        // 5.6 one-shot (DESIGN §30): fire once, then spent until re-armed. The
        // armed/spent flag is RAM-only runtime state (held by the processor), so
        // the data stays deterministic given arm state. Composes with the other
        // condition fields and with lock-only / recorder trigs.
        bool oneShot = false;

        // True when the condition imposes no restriction — equivalent to
        // the default-constructed value. Used for Override-ELSE-Base
        // fallthrough: if a step's condition is trivial, the track's
        // baseCond is used instead.
        [[nodiscard]] bool isTrivial() const
        {
            return probabilityPercent >= 100 && iterNumerator == 1 && iterDenominator == 1
                   && prevDependency == 0 && !oneShot;
        }
    };

    // Per-step fill trig state. Stored directly on Step (not in TrigCondition)
    // because fill overrides are independent of the probability/iter/prev chain.
    //
    // Inherit — step plays the base trig unchanged (default).
    // On      — step always fires during fill, even if base trig is off.
    // Off     — step never fires during fill, even if base trig is on.
    enum class FillTrigState : std::uint8_t
    {
        Inherit = 0,
        On = 1,
        Off = 2
    };
}
