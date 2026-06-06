#pragma once

#include "SoundPool.h"

namespace lockstep
{
    // Set — the top-level container; one Set = one plugin-instance state.
    // Global state (sample pool, CC mappings, focus, clock) lives in
    // LockstepProcessor rather than here so audio-thread access stays simple.
    //
    // The new container hierarchy (Songs + playhead + working buffer) lives in
    // LockstepProcessor::arrangement_ (src/core/Arrangement.h).
    struct Project
    {
        // Global launch-quantize amount in core-time bars (default 1 bar).
        int launchQuantizeBars = 1;

        SoundPool soundPool{};  // project-scope sound library
    };
}
