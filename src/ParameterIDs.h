#pragma once
#include <string>

namespace lockstep::ParamIDs
{
    // Global parameters owned by the host (APVTS-tracked).
    // Per-step P-Locks live in lockstep::PLock, not here.
    inline constexpr auto outputGain = "output_gain";

    // Transport sync mode (Locked = 0, Auto = 1).
    inline constexpr auto syncMode = "sync_mode";

    // MIDI channel mode (Omni = 0, PerTrack = 1).
    inline constexpr auto channelMode = "channel_mode";

    // Per-track structural parameters (sequencer, not DSP).
    inline std::string trackLength(int t)  { return "track_" + std::to_string(t) + "_length"; }
    inline std::string trackDivider(int t) { return "track_" + std::to_string(t) + "_divider"; }
    inline std::string trackMute(int t)    { return "track_" + std::to_string(t) + "_mute"; }
    inline std::string trackSolo(int t)   { return "track_" + std::to_string(t) + "_solo"; }

    // Swing parameters (DESIGN §19.2). Both APVTS (instance-global).
    // Signed: 0 = straight, +0.5 = half-step late, -0.5 = half-step early.
    inline constexpr auto globalSwing = "swing";
    inline std::string trackSwing(int t) { return "track_" + std::to_string(t) + "_swing"; }
}
