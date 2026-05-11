#pragma once

#include <array>
#include "Step.h"
#include "../machine/IMachine.h"

namespace lockstep
{
    inline constexpr int kMaxStepsPerTrack = 64;

    enum class NoteMode
    {
        Pitch,        // MIDI note controls pitch: note 60 = 0 semitones, chromatic
        SampleSelect, // MIDI note selects sample from pool: note 60 = index 0
    };

    // A track owns its step length, clock divider, base parameter values
    // (one per IMachine slot), and the steps themselves.
    struct Track
    {
        int length = 16;       // 1..kMaxStepsPerTrack
        int divider = 1;       // clock divider; 1 = base 16th grid
        ParamFrame baseParams{}; // track-level "default" values
        TrigCondition baseCond{};  // track-level condition; step condition overrides if non-trivial
        NoteMode noteMode = NoteMode::Pitch;

        std::array<Step, kMaxStepsPerTrack> steps{};
    };
}
