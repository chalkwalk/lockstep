#pragma once

#include <array>
#include "Bank.h"    // kept for Stage A backward-compat; removed in Stage B
#include "Song.h"
#include "SoundPool.h"

namespace lockstep
{
    inline constexpr int kNumBanks = 8;  // legacy; use kNumSongs for new code

    // Set — the top-level container; one Set = one plugin-instance state.
    // Global state (sample pool, CC mappings, focus, clock) lives in
    // LockstepProcessor rather than here so audio-thread access stays simple.
    //
    // Phase 7 migration: the new container hierarchy (Songs + playhead + working
    // buffer) now lives in LockstepProcessor::arrangement_ (src/core/Arrangement.h).
    // What remains here is the still-legacy sound path and project-global state.
    struct Project
    {
        // Global launch-quantize amount in core-time bars (default 1 bar).
        int launchQuantizeBars = 1;

        // Legacy hierarchy — provides per-track FLTR/AMP sound state until the
        // 7.9e-pre "go direct" stage moves it into TrackKit and drops these.
        std::array<Bank, kNumBanks> banks{};

        SoundPool soundPool{};  // project-scope sound library
    };
}
