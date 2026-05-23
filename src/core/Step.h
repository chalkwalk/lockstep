#pragma once

#include "PLock.h"
#include "TrigCondition.h"

namespace lockstep
{
    // Per-step override for sequencer-scope trig fields.
    // Each field is independently optional (Override-ELSE-Base).
    struct TrigOverride
    {
        bool  hasNote     = false;
        int   note        = 60;
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
