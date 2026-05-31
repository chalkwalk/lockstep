#pragma once

#include <array>
#include "Bank.h"    // kept for Stage A backward-compat; removed in Stage B
#include "Piece.h"
#include "SoundPool.h"

namespace lockstep
{
    inline constexpr int kNumBanks = 8;  // legacy; use kNumPieces for new code

    // Set — the top-level container; one Set = one plugin-instance state.
    // Global state (sample pool, CC mappings, focus, clock) lives in
    // LockstepProcessor rather than here so audio-thread access stays simple.
    //
    // Phase 7 migration: `pieces` is the new container hierarchy; `banks` is
    // kept for Stage A backward-compat and is removed in Stage B.
    struct Project
    {
        // New hierarchy (Phase 7 / DESIGN §4.7).
        std::array<Piece, kNumPieces> pieces{};

        // Global launch-quantize amount in core-time bars (default 1 bar).
        int launchQuantizeBars = 1;

        // Legacy hierarchy — kept until Stage B removes all references.
        std::array<Bank, kNumBanks> banks{};

        SoundPool soundPool{};  // project-scope sound library
    };
}
