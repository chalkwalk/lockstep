#include "StateResolver.h"

namespace lockstep::StateResolver
{
    ParamFrame resolve(const Track& track, int stepIndex)
    {
        ParamFrame frame = track.baseParams;

        if (stepIndex < 0 || stepIndex >= static_cast<int>(track.steps.size()))
            return frame;

        const auto& step = track.steps[static_cast<std::size_t>(stepIndex)];
        if (step.overrides.empty())
            return frame;

        for (int slot = 0; slot < static_cast<int>(frame.size()); ++slot)
        {
            if (step.overrides.has(slot))
                frame[static_cast<std::size_t>(slot)] = step.overrides.get(slot, frame[static_cast<std::size_t>(slot)]);
        }
        return frame;
    }
}
