#pragma once

namespace lockstep
{
    enum class ChannelMode
    {
        Omni     = 0, // all channels accepted; notes route to focused track
        PerTrack = 1  // channel N (1-8) routes to track N; channels 9-16 ignored
    };
}
