#include "RelativeCC.h"

namespace lockstep
{
    float RelativeCCRouter::apply(float currentValue, int delta, float scale) const
    {
        return currentValue + static_cast<float>(delta) * scale;
    }
}
