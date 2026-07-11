#pragma once

#include <algorithm>

namespace lockstep
{
    // The channel policy (DESIGN §40.7), named once instead of scattered as
    // `min(2, …)` clamps that read as either policy or accident.
    //
    // Two facts, and the whole point is telling them apart:
    //
    //  1. **The engine boundary is a stereo invariant.** Output buffers, live
    //     input, monitor paths, volatile capture, WAV promotion — everything that
    //     crosses the machine↔bus boundary — emits and consumes at most two
    //     channels. Mono material reads as two identical channels. That clamp is
    //     `engineChannels()`, and it is correct wherever the audio is a track's
    //     stereo IO.
    //
    //  2. **A deck's MEDIUM is wider than the engine boundary.** A four-sub-track
    //     Loop records into an eight-channel slot (§40.3), and an operation that
    //     spans the whole take — decay, halve/double, a fold across sub-tracks —
    //     must touch every recorded channel, NOT the first two. Clamping such an
    //     operation to two silently processes only sub-track 0 (the latent bug this
    //     policy exists to name). Those sites use the slot's own `getNumChannels()`.
    //
    // So: `engineChannels(available)` for the stereo boundary; the buffer's real
    // channel count for a deck-medium-wide operation.

    inline constexpr int kEngineChannels = 2;  // the stereo invariant (§40.7)

    // Clamp an available channel count to the stereo engine boundary. Never widens
    // (a mono source stays one and is duplicated at the boundary, not here).
    [[nodiscard]] inline constexpr int engineChannels(int available) noexcept
    {
        return available < 0 ? 0 : std::min(available, kEngineChannels);
    }
}
