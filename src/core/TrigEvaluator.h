#pragma once

#include "TrigCondition.h"
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
    // absoluteStep is the track-local step counter (nextTriggerPpq / divPpq),
    // not the pattern-wrapped index.
    inline bool shouldFire(const TrigCondition& cond,
                            std::size_t trackIdx,
                            std::int64_t absoluteStep)
    {
        if (cond.probabilityPercent >= 100) { return true; }
        if (cond.probabilityPercent == 0)  { return false; }
        return deterministicPercent(trackIdx, absoluteStep)
               < static_cast<int>(cond.probabilityPercent);
    }
}
