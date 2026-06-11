#include "AbsoluteCC.h"

namespace lockstep
{
    float AbsoluteCCRouter::route(float currentValue, float incomingNormalised)
    {
        if (!crossed_)
        {
            if (lastIncoming_ < 0.0f)
            {
                lastIncoming_ = incomingNormalised;
                return currentValue;
            }
            const bool crossedNow =
                (lastIncoming_ <= currentValue && incomingNormalised >= currentValue) ||
                (lastIncoming_ >= currentValue && incomingNormalised <= currentValue);
            lastIncoming_ = incomingNormalised;
            if (!crossedNow)
                return currentValue;
            crossed_ = true;
        }
        return incomingNormalised;
    }
}
