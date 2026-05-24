#pragma once

#include "PLock.h"
#include "TrigCondition.h"
#include <array>

namespace lockstep
{
    inline constexpr int kMaxNotesPerStep = 4;

    // Per-step override for sequencer-scope trig fields.
    // Each field is independently optional (Override-ELSE-Base).
    // noteCount == 0: no note override (use track default, monophonic).
    // noteCount  > 0: notes[0..noteCount-1] form the chord; notes[0] is the primary.
    struct TrigOverride
    {
        int   noteCount   = 0;
        std::array<int, kMaxNotesPerStep> notes{};
        bool  hasVelocity = false;
        int   velocity    = 100;
        bool  hasGate     = false;
        float gateMs      = 0.0f;
        // MG.5: Sound Pool step override — applies pool entry's baseParams as the
        // base param set for this step (P-Locks on top still win).
        bool  hasSoundId  = false;
        int   soundId     = -1;
    };

    struct Step
    {
        bool          trig = false;
        TrigCondition condition;
        PLock         overrides;     // machine ParamFrame P-Locks
        TrigOverride  trigOverride;  // sequencer-scope trig field overrides
    };
}
