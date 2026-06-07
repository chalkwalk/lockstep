#pragma once
#include <algorithm>

namespace lockstep
{
    // Signed off-beat displacement, ±50% of step length (DESIGN §19.2).
    // Global + per-track levels are composed additively and clamped.
    // Reserved summation seam: add Scene/Song/Phrase as extra addends here.
    [[nodiscard]] inline float effectiveSwing(float globalSwing, float trackSwing)
    {
        return std::clamp(globalSwing + trackSwing, -0.5f, 0.5f);
    }

    // Combined cap: total sub-step shift on any one step (DESIGN §19.2).
    // swingDelta is effectiveSwing(t) for odd steps, 0 for even steps.
    [[nodiscard]] inline float totalStepShift(float swingDelta, float microOffset)
    {
        return std::clamp(swingDelta + microOffset, -0.5f, 0.5f);
    }
}
