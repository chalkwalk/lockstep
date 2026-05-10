#pragma once

#include "AbsoluteCC.h"
#include <string>

namespace lockstep
{
    enum class CCScope
    {
        Global,       // targets a global APVTS parameter by ID
        Track,        // targets a fixed track by index
        SelectedTrack // targets whichever track is currently focused
    };

    // How a relative CC encodes its direction.
    // TwosComplement: raw 1-63 = +delta, raw 65-127 = -(128-raw). Most common.
    // BinOffset:      delta = raw - 64; raw 64 = no movement.
    enum class RelativeCCEncoding
    {
        TwosComplement,
        BinOffset
    };

    struct CCMapping
    {
        int ccNumber = -1;      // 0-127
        CCScope scope = CCScope::Track;
        int trackIndex = 0;     // used when scope == Track
        int slot = -1;          // machine param slot (Track / SelectedTrack)
        std::string apvtsID;    // used when scope == Global

        // --- Absolute mode (default) ---
        // AbsoluteCCRouter carries per-mapping soft-takeover state.
        AbsoluteCCRouter router;

        // --- Relative mode ---
        bool isRelative = false;
        float scale = 1.0f / 128.0f;   // normalised step per encoder detent
        RelativeCCEncoding encoding = RelativeCCEncoding::TwosComplement;
    };
}
