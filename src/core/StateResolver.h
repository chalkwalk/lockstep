#pragma once

#include "../machine/IMachine.h"
#include "Scene.h"
#include "Track.h"

namespace lockstep
{
    // Resolved sequencer-scope trig values for one step event.
    // Produced by resolveTrig(); handed to the sequencer when emitting note-on/off.
    // noteCount >= 1 always (defaults to 1 with the track's base note).
    struct TrigFields
    {
        int  noteCount = 1;
        std::array<int, kMaxNotesPerStep>     notes{ 60, 0, 0, 0 };
        int         velocity          = 100;
        bool        hasNoteVelocities = false;
        std::array<uint8_t, kMaxNotesPerStep> velocities{};
        MusicalGate gateValue         = MusicalGate::None;
    };

    // Morph context passed to the resolver — carries scene, track index, and fader
    // position so the resolver can apply the three-tier P-Lock ▷ morph ▷ kit-base
    // stack (DESIGN §17.2). Null = no morph (resolves as before).
    struct MorphContext
    {
        const Scene* scene      = nullptr;
        int          trackIndex = 0;
        float        fader      = 0.0f;  // 0 = A, 1 = B
    };

    // Returns the morph-blended value for (trackIdx, slot) using mirror resolution:
    // absent endpoint reads as the other endpoint, then falls to kitBase.
    // Returns kitBase unchanged if the slot is in neither morphA nor morphB.
    inline float morphBlend(const Scene& scene, int trackIdx, int slot,
                            float kitBase, float fader) noexcept
    {
        const auto key = std::make_pair(trackIdx, slot);
        const auto itA = scene.morphA.find(key);
        const auto itB = scene.morphB.find(key);
        const bool hasA = itA != scene.morphA.end();
        const bool hasB = itB != scene.morphB.end();
        if (!hasA && !hasB) { return kitBase; }
        float aVal = kitBase;
        float bVal = kitBase;
        if (hasA) { aVal = itA->second; } else if (hasB) { aVal = itB->second; }
        if (hasB) { bVal = itB->second; } else if (hasA) { bVal = itA->second; }
        return aVal + ((bVal - aVal) * fader);
    }

    // Effective Value = Step Override State [if exists] ELSE Track Base State.
    // When fillActive, FillOverride takes precedence over Override (three-tier resolution).
    // When morph != nullptr, the morph tier is applied between base and P-Lock
    // (P-Lock ▷ morph-lerp ▷ kit-base — DESIGN §17.2).
    namespace StateResolver
    {
        ParamFrame resolve(const Track& track, int stepIndex,
                           bool fillActive = false,
                           const MorphContext* morph = nullptr);

        // Resolves sequencer-scope trig fields (note / velocity / gate) for one
        // fired step using Override-ELSE-Base against the track's TrigDefaults.
        // When fillActive, fill-layer trig overrides take precedence.
        TrigFields resolveTrig(const Track& track, int stepIndex, bool fillActive = false);
    }
}
