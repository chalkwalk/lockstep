#pragma once

#include "PLock.h"
#include "TrigCondition.h"

namespace lockstep
{
    struct Step
    {
        bool trig = true;
        TrigCondition condition;
        PLock overrides;
    };
}
