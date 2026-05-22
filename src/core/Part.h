#pragma once

#include <array>
#include <string>
#include "../machine/IMachine.h"
#include "Sequence.h"       // kNumTracks
#include "TrackAmpState.h"  // ME.5 post-machine AMP params
#include "TrackFltrState.h" // ME.4 post-machine FLTR params

namespace lockstep
{
    // Per-track machine state owned by a Part.
    // Multiple patterns in a bank can reference the same Part so that
    // swapping patterns keeps the same machine/sound configuration ("the kit").
    struct PartTrack
    {
        // Stable machine string ID (e.g. "lockstep.sampler.v1").
        // Unknown IDs on load fall back to StubMachine.
        std::string machineId = "lockstep.sampler.v1";

        // MF.2: stable MIDI output device identifier (empty = none / not a MIDI-out track).
        // Persisted by device name so it survives device-list reordering between sessions.
        std::string destinationId = "";

        // Machine parameter defaults — one float per slot, sized to the
        // machine's numParams() at attachment time.
        ParamFrame baseParams{};

        // Post-machine FLTR block parameters (ME.4). Foundation-owned;
        // P-lockable via virtual slot indices above machine.numParams().
        TrackFltrState fltrState;

        // Post-machine AMP block parameters (ME.5). Foundation-owned;
        // P-lockable via virtual slot indices above the FLTR range.
        // Inactive when the machine opts in with hasInternalAmp() = true.
        TrackAmpState ampState;
    };

    // Part owns the per-track machine identity, base parameter frame,
    // and post-machine FLTR state.
    struct Part
    {
        std::array<PartTrack, kNumTracks> tracks{};
    };
}
