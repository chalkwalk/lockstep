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

    // The iteration rule (m:n), DESIGN §4.4 — single owner.
    // Fires on `numerator` cycles of every `denominator`, maximally evenly
    // distributed over the cycle index (the same Bresenham/Euclid rule the
    // generator uses). n=1 reduces to "first of every m"; n=m always fires.
    // Both the audio path (shouldFire) and the grid preview (SurfaceModel)
    // call this — the formula must exist exactly once.
    [[nodiscard]] inline bool iterCyclePasses(std::int64_t iter,
                                              int numerator,
                                              int denominator) noexcept
    {
        if (denominator <= 1)
            return true;

        const auto den = static_cast<std::int64_t>(denominator);
        const auto num = static_cast<std::int64_t>(std::clamp(numerator, 1, denominator));
        const auto phase = ((iter % den) + den) % den;  // negative-safe
        return (phase * num) % den < num;
    }

    // Returns true if the step should fire.
    // step         — the full Step (for base trig + fill layer).
    // absoluteStep — track-local step counter (nextTriggerPpq / divPpq).
    // trackLen     — active track length; used to derive pattern iteration.
    // prevFired    — whether the immediately preceding step slot fired.
    // fillActive   — whether the Fill scope is currently held.
    // Density thinning is evaluated separately by the caller (DESIGN §39);
    // probability/conditions here are uniform and unscaled.
    inline bool shouldFire(const Step& step,
                           const TrigCondition& cond,
                           std::size_t trackIdx,
                           std::int64_t absoluteStep,
                           int trackLen,
                           bool prevFired,
                           bool fillActive = false)
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

        // Iteration rule (m:n) — m fires per n cycles, evenly distributed.
        {
            const auto len = static_cast<std::int64_t>(std::max(trackLen, 1));
            const auto iter = absoluteStep / len;
            if (!iterCyclePasses(iter, cond.iterNumerator, cond.iterDenominator))
                return false;
        }

        // Previous-dependency gate.
        if (cond.prevDependency == 1 && !prevFired) { return false; }
        if (cond.prevDependency == 2 && prevFired) { return false; }

        // Probability check — uniform, unscaled (Density thinning is caller-side).
        if (cond.probabilityPercent >= 100) { return true; }
        if (cond.probabilityPercent == 0) { return false; }
        return deterministicPercent(trackIdx, absoluteStep) < cond.probabilityPercent;
    }
}
