#pragma once

#include "TrigCondition.h"
#include <algorithm>
#include <cstdint>

namespace lockstep::TrigEvaluator
{
    // Maps (trackIdx, absoluteStep) to a deterministic value in [0, 99].
    inline int deterministicPercent(std::size_t trackIdx, std::int64_t absoluteStep)
    {
        auto h = (static_cast<std::uint32_t>(trackIdx) * 2654435761u)
               ^ static_cast<std::uint32_t>(static_cast<std::uint64_t>(absoluteStep) * 2246822519ull);
        h ^= h >> 16;
        h *= 0x45d9f3bu;
        h ^= h >> 16;
        return static_cast<int>(h % 100u);
    }

    // Returns true if the step should fire. Call only when step.trig is true.
    // absoluteStep  — track-local step counter (nextTriggerPpq / divPpq), not
    //                 the pattern-wrapped index.
    // trackLen      — active track length; used to derive pattern iteration.
    // prevFired     — whether the immediately preceding step slot fired.
    inline bool shouldFire(const TrigCondition& cond,
                            std::size_t trackIdx,
                            std::int64_t absoluteStep,
                            int trackLen,
                            bool prevFired)
    {
        // Iteration rule: {numerator, denominator} → fire on iteration
        // `numerator` of every `denominator` loops (1-indexed, so numerator=1
        // fires on iterations 0, D, 2D, …).
        // denominator=1 is the default (every loop) and bypasses the check.
        if (cond.iterDenominator > 1)
        {
            const auto len   = static_cast<std::int64_t>(std::max(trackLen, 1));
            const auto denom = static_cast<std::int64_t>(cond.iterDenominator);
            const auto iter  = absoluteStep / len;
            if (iter % denom != static_cast<std::int64_t>(cond.iterNumerator) - 1)
            {
                return false;
            }
        }

        // Previous-dependency gate.
        // 1 = fire only if the preceding step fired.
        // 2 = fire only if the preceding step did NOT fire.
        if (cond.prevDependency == 1 && !prevFired) { return false; }
        if (cond.prevDependency == 2 &&  prevFired) { return false; }

        // Probability check.
        if (cond.probabilityPercent >= 100) { return true; }
        if (cond.probabilityPercent == 0)   { return false; }
        return deterministicPercent(trackIdx, absoluteStep)
               < static_cast<int>(cond.probabilityPercent);
    }
}
