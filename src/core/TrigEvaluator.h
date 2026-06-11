#pragma once

#include "Step.h"
#include "TrigCondition.h"
#include <algorithm>
#include <cstdint>

namespace lockstep::TrigEvaluator
{
    // Maps (trackIdx, absoluteStep) to a deterministic value in [0, 99].
    inline int deterministicPercent(std::size_t trackIdx, std::int64_t absoluteStep)
    {
        auto h = (static_cast<std::uint32_t>(trackIdx) * 2654435761u) ^ static_cast<std::uint32_t>(static_cast<std::uint64_t>(absoluteStep) * 2246822519ull);
        h ^= h >> 16;
        h *= 0x45d9f3bu;
        h ^= h >> 16;
        return static_cast<int>(h % 100u);
    }

    // Returns true if the step should fire.
    // step         — the full Step (for base trig + fill layer).
    // absoluteStep — track-local step counter (nextTriggerPpq / divPpq).
    // trackLen     — active track length; used to derive pattern iteration.
    // prevFired    — whether the immediately preceding step slot fired.
    // fillActive   — whether the Fill scope is currently held.
    inline bool shouldFire(const Step& step,
                           const TrigCondition& cond,
                           std::size_t trackIdx,
                           std::int64_t absoluteStep,
                           int trackLen,
                           bool prevFired,
                           bool fillActive = false,
                           float chanceScale = 1.0f)
    {
        // Fill trig state determines whether this step fires at all during fill.
        if (fillActive)
        {
            if (step.fillTrigState == FillTrigState::On)
            {
                // Always fires during fill — skip remaining condition checks.
                return true;
            }
            if (step.fillTrigState == FillTrigState::Off)
                return false;
            // Inherit: fall through to normal condition evaluation against step.trig.
        }

        // Base trig must be on for any further evaluation.
        if (!step.trig)
            return false;

        // Iteration rule.
        if (cond.iterDenominator > 1)
        {
            const auto len = static_cast<std::int64_t>(std::max(trackLen, 1));
            const auto denom = static_cast<std::int64_t>(cond.iterDenominator);
            const auto iter = absoluteStep / len;
            if (iter % denom != static_cast<std::int64_t>(cond.iterNumerator) - 1)
                return false;
        }

        // Previous-dependency gate.
        if (cond.prevDependency == 1 && !prevFired) { return false; }
        if (cond.prevDependency == 2 && prevFired) { return false; }

        // Probability check (scaled by the per-track Chance macro).
        const int scaledProb = std::clamp(
            static_cast<int>(static_cast<float>(cond.probabilityPercent) * chanceScale),
            0, 100);
        if (scaledProb >= 100) { return true; }
        if (scaledProb == 0) { return false; }
        return deterministicPercent(trackIdx, absoluteStep) < scaledProb;
    }
}
