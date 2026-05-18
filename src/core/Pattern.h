#pragma once

#include <array>
#include "Sequence.h"

namespace lockstep
{
    // A Pattern owns the trig/step/condition data for all tracks (the Sequence)
    // plus a reference to the Part that provides the machine identity and base
    // parameter frame for those tracks.
    //
    // Multiple patterns in a Bank can reference the same Part index (shared kit).
    // "Fork Part" creates a new Part and updates partRef to point at it.
    struct Pattern
    {
        Sequence sequence{};  // per-track steps, conditions, trig defaults
        int      partRef = 0; // index into Bank::parts

        // MD.7: Pattern-scope mute mask. Per-track. Saved with the pattern.
        // Runtime mute = globalMute[i] || patternMutes[i].
        std::array<bool, kNumTracks> patternMutes{};
    };
}
