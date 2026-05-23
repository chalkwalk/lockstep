#pragma once

#include <array>
#include "Bank.h"
#include "SoundPool.h"

namespace lockstep
{
    inline constexpr int kNumBanks = 8;

    // Project is the top-level container; one Project = one plugin-instance state.
    // Global state (sample pool, CC mappings, focus, clock) lives in
    // LockstepProcessor rather than here so audio-thread access stays simple.
    struct Project
    {
        std::array<Bank, kNumBanks> banks{};
        SoundPool soundPool{};  // MG.4: project-scope sound library
    };
}
