#pragma once

#include <cstdint>

namespace lockstep
{
    // MD.9: Fill interaction rule for a step's trig condition.
    enum class FillRule : std::uint8_t
    {
        Always,    // fire regardless of fill state (default)
        OnlyFill,  // fire only while Fill scope is held
        NeverFill, // fire only while Fill scope is NOT held
    };

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

        // Fill interaction (MD.9). Conjoined with probability and m:n.
        FillRule fillRule = FillRule::Always;

        // True when the condition imposes no restriction — equivalent to
        // the default-constructed value. Used for Override-ELSE-Base
        // fallthrough: if a step's condition is trivial, the track's
        // baseCond is used instead.
        [[nodiscard]] bool isTrivial() const
        {
            return probabilityPercent >= 100
                && iterNumerator == 1
                && iterDenominator == 1
                && prevDependency == 0
                && fillRule == FillRule::Always;
        }
    };
}
