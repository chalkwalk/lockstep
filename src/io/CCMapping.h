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

    struct CCMapping
    {
        int ccNumber = -1;      // 0-127
        CCScope scope = CCScope::Track;
        int trackIndex = 0;     // used when scope == Track
        int slot = -1;          // machine param slot (Track / SelectedTrack)
        std::string apvtsID;    // used when scope == Global
        AbsoluteCCRouter router;
    };
}
