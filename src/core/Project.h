#pragma once

#include "SoundPool.h"
#include "TimeSig.h"
#include "Scale.h"

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

        // Set-level default time signature (DESIGN §4.8 hierarchy: Set → Song → Scene).
        // Song/Scene overrides inherit from this when their hasTimeSig flag is false.
        TimeSig defaultTimeSig{};

        // Set-level default key signature (DESIGN §4.10 hierarchy: Set → Song → Scene).
        // Song/Scene overrides inherit from this when their hasKeySig flag is false.
        // Default = C Ionian.
        KeySig defaultKeySig{};

        SoundPool soundPool{};  // project-scope sound library
    };
}
