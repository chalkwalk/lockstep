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

    TrigFields resolveTrig(const Track& track, int stepIndex)
    {
        TrigFields result;
        result.noteCount  = 1;
        result.notes[0]   = track.trigDefaults.note;
        result.velocity   = track.trigDefaults.velocity;
        result.gateMs     = track.trigDefaults.gateMs;

        if (stepIndex < 0 || stepIndex >= kMaxStepsPerTrack)
            return result;

        const auto& ov = track.steps[static_cast<std::size_t>(stepIndex)].trigOverride;
        if (ov.noteCount > 0)
        {
            result.noteCount = ov.noteCount;
            result.notes     = ov.notes;
        }
        if (ov.hasVelocity) result.velocity = ov.velocity;
        if (ov.hasGate)     result.gateMs   = ov.gateMs;
        return result;
    }
}
