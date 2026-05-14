#pragma once

#include <cstdint>

namespace lockstep
{
    // Controls how the step-grid and keyboard rows are rendered.
    // Persisted as a global preference (not project state).
    enum class GridDisplayMode : std::uint8_t
    {
        Staggered,    // ANSI stagger + key letter + function labels
        Ortholinear,  // Uniform grid + key letter + function labels
        Clean         // Uniform grid + function labels only (no key letter)
    };

    // Staggered-mode geometry, expressed in half-units so all offsets are integers.
    //
    // Row stagger (in half-units):  number=0, Q=1, A=2, Z=3
    // Key width = 2 half-units
    // Total span = 3 (max stagger) + 8×2 (key columns) = 19 half-units
    //
    // Visually: Q left-edge sits halfway between keys 1 and 2,
    //           Z left-edge sits between Q and W (one key right of Q),
    //           A left-edge sits halfway between Q and Z.
    inline constexpr int kStaggerHalfUnits = 19;

    // Half-unit size in pixels for a component of the given width.
    inline int staggerHalfUnit(int componentWidth) noexcept
    { return componentWidth / kStaggerHalfUnits; }

    // Cell width in staggered mode.
    inline int staggerCellW(int halfUnit) noexcept { return halfUnit * 2; }

    // Row left-edge offsets in pixels.
    // Number row offset is 0 — pass directly.
    inline int staggerOffsetQ(int halfUnit) noexcept { return halfUnit; }
    inline int staggerOffsetA(int halfUnit) noexcept { return halfUnit * 2; }
    inline int staggerOffsetZ(int halfUnit) noexcept { return halfUnit * 3; }
}
