#pragma once

#include <cstdint>

namespace lockstep
{
    // Stable identity for a pool entry, decoupled from its array position (9.18).
    // A sample reference (track base sample_id, P-Lock override, Stream track, an
    // insert slot's convolution IR ref) is stored as a SampleId so it survives pool
    // reorder / reload without pointing at the wrong entry (the flat-index rot fixed
    // here). Two identity domains:
    //   - Persistent (File / Stream) — backed by a disk file. key = content hash
    //     (SampleRef::hashXX32). Stable across reorder AND file move: reloading the
    //     file re-matches the hash and auto-relinks. Serialised.
    //   - Volatile (Record / Loop / Empty REC slot) — captured live, no file, not
    //     serialised. key = a session-local monotonic id assigned at slot creation,
    //     stable for the life of the session across pool reorder.
    // None = an unassigned / cleared reference (resolves to nothing).
    //
    // Extracted to its own JUCE-free header (9.24 S15) so lightweight core types
    // (TrackKit::InsertSlot) can carry a SampleId without pulling in the full pool.
    struct SampleId
    {
        enum class Domain : std::uint8_t { None = 0, Persistent, Volatile };
        Domain        domain = Domain::None;
        std::uint32_t key    = 0;

        bool valid() const { return domain != Domain::None; }
        bool operator==(const SampleId& o) const
        {
            return domain == o.domain && key == o.key;
        }
        bool operator!=(const SampleId& o) const { return !(*this == o); }
    };
}
