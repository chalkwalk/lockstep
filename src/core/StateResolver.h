#pragma once

#include "../machine/IMachine.h"
#include "Track.h"

namespace lockstep
{
    // Effective Value = Step Override State [if exists] ELSE Track Base State.
    // The resolver merges the two into a single ParamFrame the IMachine sees.
    namespace StateResolver
    {
        ParamFrame resolve(const Track& track, int stepIndex);
    }
}
