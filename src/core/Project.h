#pragma once

#include "SoundPool.h"
#include "TimeSig.h"
#include "Scale.h"
#include "LaunchQuant.h"

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
        // Global launch-quantize grid — the single authority for every
        // deferrable action (PRINCIPLES §25, DESIGN §4.8). Enum-valued int so
        // it serializes as a plain property; default Bar.
        int launchQuant = static_cast<int>(LaunchQuant::Bar);

        // Set-level default time signature (DESIGN §4.8 hierarchy: Set → Song → Scene).
        // Song/Scene overrides inherit from this when their hasTimeSig flag is false.
        TimeSig defaultTimeSig{};

        // Set-level default key signature (DESIGN §4.10 hierarchy: Set → Song → Scene).
        // Song/Scene overrides inherit from this when their hasKeySig flag is false.
        // Default = C Ionian.
        KeySig defaultKeySig{};

        // Per-slot capacity of the volatile (RAM-only) REC buffers, in seconds
        // (DESIGN §28). The buffers are allocated lazily — untouched pages are never
        // committed — so a generous ceiling costs address space, not memory, until
        // you actually record into a slot. Default 60 s.
        double volatileMaxSeconds = 60.0;

        SoundPool soundPool{};  // project-scope sound library
    };
}
