#pragma once

#include <cstdint>

namespace lockstep
{
    // Lock-free command record enqueued by the message thread and drained by the
    // audio thread at the top of each processBlock call (before the sequencer
    // advances). Each command is a bounded, allocation-free store — no heap touch
    // on the audio thread.
    //
    // Message thread responsibilities (done before enqueue):
    //   - Validation (range check, slot existence)
    //   - Pool-size clamping for sample-id slots
    //   - Zero-crossing snap
    //   - Control-All fan-out (one enqueue per target track)
    //
    // Audio thread responsibilities (at block drain):
    //   - Plain bounded store to the named field — no resolution logic.
    //
    // THREADING-DEBT(8.16): stopped-audio fallback (drain on message thread when
    // no block has fired for >100 ms) not yet implemented. Until then, edits made
    // while the transport is stopped may be applied one block late when transport
    // resumes. For editing while stopped this is imperceptible.
    struct EngineCmd
    {
        enum class Op : uint8_t
        {
            SetBaseParam,       // sequence.tracks[track].baseParams[slot] = value
                                // + kit(track).baseParams[slot] = value
            SetFltrSlot,        // kit(track).fltrState.setSlot(slot, value)
            SetAmpSlot,         // kit(track).ampState.setSlot(slot, value)
            SetInsertParam,     // kit(track).inserts[aux].baseParams[slot] = value
            SetMasterInsertParam, // masterKit.inserts[aux].baseParams[slot] = value
            SetMasterSendParam,   // masterKit.sends[aux].baseParams[slot] = value (8.26)
            SetStepOverride,    // sequence.tracks[track].steps[aux].overrides.set(slot, value)
            ClearStepOverride,  // sequence.tracks[track].steps[aux].overrides.clear(slot)
            SetFillOverride,    // sequence.tracks[track].steps[aux].fillOverrides.set(slot, value)
            ClearFillOverride,  // sequence.tracks[track].steps[aux].fillOverrides.clear(slot)
        };

        Op op = Op::SetBaseParam;
        uint8_t track = 0;   // track index (0..kNumTracks-1)
        uint8_t aux = 0;   // step index (SetStep*/ClearStep*) or insert slot (SetInsert*)
        uint8_t pad = 0;
        int16_t slot = 0;   // param slot
        float value = 0.0f;
    };
    static_assert(sizeof(EngineCmd) == 12, "EngineCmd size changed — update comment");
}
