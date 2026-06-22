#pragma once

#include <array>
#include <cstdint>
#include "MusicalGate.h"
#include "Step.h"
#include "Subdivision.h"
#include "../machine/IMachine.h"

namespace lockstep
{
    inline constexpr int kMaxStepsPerTrack = 64;

    // Track-level defaults for sequencer-scope trig fields.
    // Resolved against per-step TrigOverride via Override-ELSE-Base.
    struct TrigDefaults
    {
        int note = 60;                  // MIDI note number (0-127)
        int velocity = 100;                 // MIDI velocity (1-127)
        MusicalGate gateValue = MusicalGate::None;   // musical gate; None = play to AHDSR end
    };

    // When a chord step holds more notes than the machine's current polyphony
    // can voice, the sequencer picks K notes from the N held notes using a
    // "spread with bias" algorithm: top/bottom first, then middle positions
    // chosen to spread the remaining voices, with the bias resolving ties.
    enum class NoteSelection : std::uint8_t
    {
        TopBias = 0,
        BottomBias = 1
    };

    // A track owns its step length, clock divider, base parameter values
    // (one per IMachine slot), and the steps themselves.
    struct Track
    {
        // 1..kMaxStepsPerTrack. SSOT note (PRINCIPLES §20): this working copy and the
        // APVTS `trackLength` param (which the audio thread reads) must agree. UI edits
        // go through LockstepProcessor::setTrackLength (writes both); scene/song switches
        // set this via projectPhraseToTrack then mirror it to the param via
        // syncTrackParamsFromActiveKit. Never write this raw from a UI/edit path.
        int length = 16;
        int subdivIndex = kSubdivDefault; // musical subdivision combined index (0-26)
        ParamFrame baseParams{}; // track-level "default" values
        TrigCondition baseCond{};  // track-level condition; step condition overrides if non-trivial
        TrigDefaults trigDefaults{};
        NoteSelection noteSelection = NoteSelection::TopBias;

        std::array<Step, kMaxStepsPerTrack> steps{};
    };
}
