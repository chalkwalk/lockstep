#pragma once

#include "MusicalGate.h"
#include "PLock.h"
#include "TrigCondition.h"
#include <array>
#include <cstdint>

namespace lockstep
{
    inline constexpr int kMaxNotesPerStep = 4;

    // Per-step override for sequencer-scope trig fields.
    // Each field is independently optional (Override-ELSE-Base).
    // noteCount == 0: no note override (use track default, monophonic).
    // noteCount  > 0: notes[0..noteCount-1] form the chord; notes[0] is the primary.
    //
    // Velocity layers:
    //   hasNoteVelocities + velocities[] — per-note, set by realtime record (MHZ.6.3).
    //   hasVelocity + velocity           — uniform override, set via MZ (takes effect only
    //                                      when hasNoteVelocities is false).
    struct TrigOverride
    {
        int   noteCount   = 0;
        std::array<int, kMaxNotesPerStep>     notes{};
        bool  hasVelocity     = false;
        int   velocity        = 100;
        bool  hasNoteVelocities = false;
        std::array<uint8_t, kMaxNotesPerStep> velocities{};
        bool        hasGate   = false;
        MusicalGate gateValue = MusicalGate::None;
        // MG.5: Sound Pool step override — applies pool entry's baseParams as the
        // base param set for this step (P-Locks on top still win).
        bool  hasSoundId  = false;
        int   soundId     = -1;

        // 5.7: per-step retrig rate — when hasRetrig, the step auto-ratchets at
        // retrigRate PPQ per repetition for the duration of the note gate.
        bool   hasRetrig  = false;
        double retrigRate = 0.25;  // PPQ per repetition (default = /16)
    };

    struct Step
    {
        bool          trig = false;
        TrigCondition condition;
        PLock         overrides;     // machine ParamFrame P-Locks
        TrigOverride  trigOverride;  // sequencer-scope trig field overrides

        // Sub-step timing nudge, ±50% of the step's length (DESIGN §19.1).
        // 0 = on-grid; +0.25 = quarter-step late; −0.5 = half-step early.
        // Captured automatically by live record; P-lockable via TRIG meta section.
        float microOffset = 0.0f;

        // Fill layer — evaluated only when Fill scope is held (FillOverride → Override → Base).
        FillTrigState fillTrigState  = FillTrigState::Inherit;
        PLock         fillOverrides;     // fill-specific machine param P-Locks
        TrigOverride  fillTrigOverride;  // fill-specific sequencer trig field overrides
    };
}
