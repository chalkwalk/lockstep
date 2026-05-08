#pragma once

namespace lockstep::ParamIDs
{
    // Global parameters owned by the host (APVTS-tracked).
    // Per-step P-Locks live in lockstep::PLock, not here.
    inline constexpr auto outputGain = "output_gain";
}
