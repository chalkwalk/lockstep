#pragma once

#include "../machine/IMachine.h"
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

    // Effective Value = Step Override State [if exists] ELSE Track Base State.
    // When fillActive, FillOverride takes precedence over Override (three-tier resolution).
    // The resolver merges the two (or three) into a single ParamFrame the IMachine sees.
    namespace StateResolver
    {
        ParamFrame resolve(const Track& track, int stepIndex, bool fillActive = false);

        // Resolves sequencer-scope trig fields (note / velocity / gate) for one
        // fired step using Override-ELSE-Base against the track's TrigDefaults.
        // When fillActive, fill-layer trig overrides take precedence.
        TrigFields resolveTrig(const Track& track, int stepIndex, bool fillActive = false);
    }
}
