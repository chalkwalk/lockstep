#pragma once

#include "PLock.h"
#include "TrigCondition.h"

namespace lockstep
{
    struct Step
    {
        bool trig = false;
        TrigCondition condition;
        PLock overrides;
    };
}
