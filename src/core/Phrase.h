#pragma once

#include "Track.h"   // TrigDefaults, NoteSelection, kMaxStepsPerTrack, Step

namespace lockstep
{
    // One musician's repeating musical idea.  Carries length, steps, and
    // per-phrase trig configuration.  Pure musical content — no kit/machine
    // info.  Shared by reference across Sections via Section::phraseIdx[t].
    //
    // Phase 7 / DESIGN §4.7.  This struct is structurally equivalent to Track
    // plus an `initialised` flag; Track is removed in Stage B once the
    // processor fully migrates to the new hierarchy.
    struct Phrase
    {
        // Active length in steps (1..kMaxStepsPerTrack).
        // Seeded from Section::coreTime on creation; freely editable thereafter.
        int length = 16;

        std::array<Step, kMaxStepsPerTrack> steps{};

        TrigDefaults  trigDefaults{};
        TrigCondition baseCond{};
        NoteSelection noteSelection = NoteSelection::TopBias;

        // True once explicitly initialised (seeded, loaded, or authored).
        bool initialised = false;
    };
}
