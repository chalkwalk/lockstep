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

    // Track-level defaults for sequencer-scope trig fields.
    // Resolved against per-step TrigOverride via Override-ELSE-Base.
    struct TrigDefaults
    {
        int   note     = 60;    // MIDI note number (0-127)
        int   velocity = 100;   // MIDI velocity (1-127)
        float gateMs   = 0.0f;  // gate duration in ms; 0 = play to natural AHDSR end
    };

    // A track owns its step length, clock divider, base parameter values
    // (one per IMachine slot), and the steps themselves.
    struct Track
    {
        int length = 16;       // 1..kMaxStepsPerTrack
        int divider = 1;       // clock divider; 1 = base 16th grid
        ParamFrame baseParams{}; // track-level "default" values
        TrigCondition baseCond{};  // track-level condition; step condition overrides if non-trivial
        TrigDefaults trigDefaults{};
        NoteMode noteMode = NoteMode::Pitch;

        std::array<Step, kMaxStepsPerTrack> steps{};
    };
}
