#pragma once

#include <cstdint>

namespace lockstep
{
    // =========================================================================
    // Overlay — the mutually-exclusive sticky/modal family (Stage 1, DESIGN §mode)
    //
    // Exactly one value is active at a time, making illegal co-existence
    // unrepresentable.  UiState still carries the per-overlay param fields
    // (bank, subPage, entryScope, euclidPulses, …) until Stage 6 encapsulation.
    // =========================================================================
    enum class Overlay : uint8_t
    {
        None,     // no sticky overlay active
        Euclid,   // Euclidean generator (Phrase+Fill chord)
        Time,     // tempo + time-sig (Song/Scene+TRIG entry chord)
        Density,  // density editor (Func+MOD entry chord)
        Vel,      // velocity overlay (Func+AMP entry chord)
    };
}
