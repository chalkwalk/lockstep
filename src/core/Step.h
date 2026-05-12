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
    };

    struct Step
    {
        bool          trig = false;
        TrigCondition condition;
        PLock         overrides;     // machine ParamFrame P-Locks
        TrigOverride  trigOverride;  // sequencer-scope trig field overrides
    };
}
