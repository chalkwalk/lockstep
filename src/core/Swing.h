#pragma once
#include <algorithm>

namespace lockstep
{
    // Signed off-beat displacement, ±50% of step length (DESIGN §19.2).
    // Three additive levels: song-all (the piece's groove), song-track delta
    // (per-musician feel), and scene-all delta (section-wide push/pull).
    // Scene-per-track is a reserved but unbuilt addend; add it here when ready.
    [[nodiscard]] inline float effectiveSwing(float songAll, float songTrk, float sceneAll)
    {
        return std::clamp(songAll + songTrk + sceneAll, -0.5f, 0.5f);
    }

    // Combined cap: total sub-step shift on any one step (DESIGN §19.2).
    // swingDelta is effectiveSwing(t) for odd steps, 0 for even steps.
    [[nodiscard]] inline float totalStepShift(float swingDelta, float microOffset)
    {
        return std::clamp(swingDelta + microOffset, -0.5f, 0.5f);
    }
}
