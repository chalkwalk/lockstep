#include "StateResolver.h"

namespace lockstep::StateResolver
{
    ParamFrame resolve(const Track& track, int stepIndex,
                       bool fillActive, const MorphContext* morph)
    {
        ParamFrame frame = track.baseParams;

        // Morph tier: between base and P-Lock (P-Lock ▷ morph ▷ kit-base).
        // Only applied when the scene has morph data; skipped when maps are empty.
        if (morph && morph->scene && (!morph->scene->morphA.empty() || !morph->scene->morphB.empty()))
        {
            // Clamp to machine capacity: a stale frame (e.g. after a scope restore that
            // changed machine type) must not drive paramSpec() out of range.
            const int morphLimit = morph->machine
                ? std::min(static_cast<int>(frame.size()), morph->machine->numParams())
                : static_cast<int>(frame.size());
            for (int slot = 0; slot < morphLimit; ++slot)
            {
                const bool stepped = morph->machine
                                         ? morph->machine->paramSpec(slot).isStepped
                                         : false;
                const bool equalPower = morph->machine && morph->machine->paramSpec(slot).role == ParamSpec::Role::Level;
                frame[static_cast<std::size_t>(slot)] =
                    morphBlend(*morph->scene, morph->trackIndex, slot,
                               frame[static_cast<std::size_t>(slot)],
                               morph->fader, stepped, equalPower);
            }
        }

        if (stepIndex < 0 || stepIndex >= static_cast<int>(track.steps.size()))
            return frame;

        const auto& step = track.steps[static_cast<std::size_t>(stepIndex)];

        // P-Lock Override layer (wins over morph).
        for (int slot = 0; slot < static_cast<int>(frame.size()); ++slot)
        {
            if (step.overrides.has(slot))
                frame[static_cast<std::size_t>(slot)] = step.overrides.get(slot, frame[static_cast<std::size_t>(slot)]);
        }

        // FillOverride layer on top (only when Fill is held).
        if (fillActive)
        {
            for (int slot = 0; slot < static_cast<int>(frame.size()); ++slot)
            {
                if (step.fillOverrides.has(slot))
                    frame[static_cast<std::size_t>(slot)] = step.fillOverrides.get(slot, frame[static_cast<std::size_t>(slot)]);
            }
        }

        return frame;
    }

    TrigFields resolveTrig(const Track& track, int stepIndex, bool fillActive)
    {
        TrigFields result;
        result.noteCount = 1;
        result.notes[0] = track.trigDefaults.note;
        result.velocity = track.trigDefaults.velocity;
        result.gateValue = track.trigDefaults.gateValue;

        if (stepIndex < 0 || stepIndex >= kMaxStepsPerTrack)
            return result;

        const auto& step = track.steps[static_cast<std::size_t>(stepIndex)];

        // Apply base Override layer.
        const auto applyTrigOv = [&](const TrigOverride& ov) {
            if (ov.noteCount > 0)
            {
                result.noteCount = ov.noteCount;
                result.notes = ov.notes;
            }
            if (ov.hasVelocity)
                result.velocity = ov.velocity;
            if (ov.hasNoteVelocities)
            {
                result.hasNoteVelocities = true;
                result.velocities = ov.velocities;
            }
            if (ov.hasGate)
                result.gateValue = ov.gateValue;
        };

        applyTrigOv(step.trigOverride);

        // FillOverride layer on top (only when Fill is held).
        if (fillActive)
            applyTrigOv(step.fillTrigOverride);

        return result;
    }
}
