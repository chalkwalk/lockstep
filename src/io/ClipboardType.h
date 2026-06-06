#pragma once

#include <cstdint>

namespace lockstep
{
    // Indicates what type of data is currently in the in-memory clipboard.
    // The clipboard is typed so that a step clipboard cannot be pasted into
    // a track scope, etc. (DESIGN §13.2).
    //
    // Full implementation lands in MD; this enum is introduced here so that
    // the transport chrome can show the clipboard type chip (MB.6).
    enum class ClipboardType : std::uint8_t
    {
        None,
        Step,     // one or more step trigs + P-Locks
        Section,  // all slots of one section across all steps on a track
        Track,    // entire track
        Pattern,  // entire pattern
        Scene,    // scene floor + all track phrases (DESIGN §23.3)
        All,      // omni grab: full live stack; unqualified paste requires scope
    };
}
