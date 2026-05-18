#pragma once

#include <array>
#include <string>
#include "../machine/IMachine.h"
#include "Sequence.h"  // kNumTracks

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

        // Machine parameter defaults — one float per slot, sized to the
        // machine's numParams() at attachment time.
        ParamFrame baseParams{};
    };

    // Part owns the per-track machine identity and base parameter frame.
    // FLTR/AMP block state per track is stubbed here; ME adds it later.
    struct Part
    {
        std::array<PartTrack, kNumTracks> tracks{};
    };
}
