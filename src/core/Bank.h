#pragma once

#include <array>
#include "Part.h"
#include "Pattern.h"

namespace lockstep
{
    inline constexpr int kPatternsPerBank = 16;
    inline constexpr int kPartsPerBank    = 16;

    // A Bank holds a grid of Patterns and a pool of Parts.
    // Patterns reference Parts by index; multiple patterns can share one Part.
    struct Bank
    {
        std::array<Pattern, kPatternsPerBank> patterns{};
        std::array<Part,    kPartsPerBank>    parts{};
    };
}
