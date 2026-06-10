#pragma once

// CellAppearance.h — compile-time appearance table for all CellState tokens.
//
// Consumption pattern:
//   const auto& ap = appearanceOf(state);
//   ap.screenFill   → base ARGB fill colour for the screen renderer
//   ap.screenAccent → accent ARGB (0 = none defined)
//   ap.pushPad      → Push 1 firmware palette index (uint8)
//   ap.xtouchVel    → X-Touch Mini LED velocity: 0=off 1=flash 127=solid
//
// The table is generated from CellStates.def; add new tokens only by
// appending rows there (add-only rule per CellState contract).
//
// KEEP IN SYNC: when CellStates.def grows, re-run the static_assert
// count check at the bottom of this header.

#include "SurfaceModel.h"  // CellState enum
#include <array>
#include <cstdint>

namespace lockstep
{
    struct CellAppearance
    {
        uint32_t screenFill   = 0;
        uint32_t screenAccent = 0;
        uint8_t  pushPad      = 0;
        uint8_t  xtouchVel    = 0;
    };

    namespace detail
    {
        // Build the flat constexpr look-up array from the X-macro table.
        // Entries are ordered by CellState numeric value (dense enough to use
        // as a small array indexed by value % 256).
        //
        // Because values are non-contiguous we use a linear-scan helper
        // (the array is small — < 60 entries).
        struct CellEntry
        {
            uint16_t      value;
            CellAppearance ap;
        };

        inline constexpr CellEntry kTable[] = {
#define LS_CELLSTATE(token, value, fill, accent, push, xvel) \
            { static_cast<uint16_t>(value), { fill, accent, push, xvel } },
#include "CellStates.def"
#undef LS_CELLSTATE
        };

        inline constexpr std::size_t kTableSize = std::size(kTable);
    }

    // Linear scan look-up (table is small; no hash needed).
    [[nodiscard]] inline constexpr CellAppearance appearanceOf(
        CellState state,
        CellAppearance fallback = { 0xFF303030u, 0u, 2u, 0u }) noexcept
    {
        const auto v = static_cast<uint16_t>(state);
        for (std::size_t i = 0; i < detail::kTableSize; ++i)
            if (detail::kTable[i].value == v) return detail::kTable[i].ap;
        return fallback;
    }

    // -------------------------------------------------------------------------
    // Static assertions pinning a handful of values against accidental renumber
    // (add-only rule: check that specific tokens still resolve to expected values)

    static_assert(appearanceOf(CellState::StepTrigCertain).pushPad  == 21,
                  "StepTrigCertain pidx drifted — check CellStates.def");
    static_assert(appearanceOf(CellState::StepEmpty).xtouchVel      ==  0,
                  "StepEmpty xtouchVel drifted");
    static_assert(appearanceOf(CellState::SelectorCurrent).xtouchVel ==  1,
                  "SelectorCurrent xtouchVel drifted");
    static_assert(appearanceOf(CellState::MuteMuted).pushPad        ==  5,
                  "MuteMuted pidx drifted — check CellStates.def");
}
