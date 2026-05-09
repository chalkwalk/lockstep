#pragma once
#include <string>

namespace lockstep::ParamIDs
{
    // Global parameters owned by the host (APVTS-tracked).
    // Per-step P-Locks live in lockstep::PLock, not here.
    inline constexpr auto outputGain = "output_gain";

    // Per-track structural parameters (sequencer, not DSP).
    inline std::string trackLength(int t)  { return "track_" + std::to_string(t) + "_length"; }
    inline std::string trackDivider(int t) { return "track_" + std::to_string(t) + "_divider"; }
    inline std::string trackMute(int t)    { return "track_" + std::to_string(t) + "_mute"; }
}
