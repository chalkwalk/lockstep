#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Accent-velocity helpers.  JUCE-free, header-only.
// Shared by the engine (PluginProcessor emitTrig) and the MetaBand preview
// so they cannot diverge.

namespace lockstep
{
    // Durable per-track velocity overlay mode (TrackKit, v20+).
    enum class VelMode  : uint8_t { Off, Bar };       // Off = authored; Bar = metric weight vs coreTime
    enum class VelBlend : uint8_t { Replace, Mix };   // Replace = override; Mix = add on top

    // Metric-weight -> velocity curve.
    //   center : MIDI center velocity [1..127]
    //   depth  : [0..1] fraction of available headroom to use
    //   weight : [0..1] metric importance (1 = downbeat, 0 = finest offbeat)
    //
    // At weight=1 (downbeat) result approaches (center + depth * headroomHigh).
    // At weight=0 (offbeat)  result approaches (center - depth * headroomLow).
    // At weight=0.5          result == center.
    [[nodiscard]] inline int accentVelocity(int center, float depth, float weight) noexcept
    {
        const int headroomLow  = center - 1;
        const int headroomHigh = 127 - center;
        const float swing = (weight >= 0.5f)
            ? static_cast<float>(headroomHigh) * ((2.0f * weight) - 1.0f)
            : static_cast<float>(headroomLow)  * ((2.0f * weight) - 1.0f);
        const float vel = static_cast<float>(center) + (depth * swing);
        return std::clamp(static_cast<int>(std::lround(vel)), 1, 127);
    }

} // namespace lockstep
