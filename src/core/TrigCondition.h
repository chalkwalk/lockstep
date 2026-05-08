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
        // 2 = fire only if previous step did NOT fire. (Encoding finalised
        // when the evaluator lands.)
        std::uint8_t prevDependency = 0;
    };
}
