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
    // Total span = 3 (max stagger) + 9×2 (key columns) = 21 half-units
    //
    // Visually: Q left-edge sits halfway between keys 1 and 2,
    //           A left-edge sits halfway between Q and Z.
    //           Z left-edge sits one key right of Q.
    inline constexpr int kStaggerHalfUnits = 21;

    // Clean-mode geometry: horizontal gap between the modifier column (col 0)
    // and the remaining columns, applied in all four rows.
    inline constexpr int kClnColGap = 6;

    // Clean-mode geometry: vertical gap between the upper pair of rows (1-2,
    // SectionBar + FunctionBar) and the lower pair (3-4, StepGrid).
    inline constexpr int kClnRowGap = 6;

    // ORL-mode geometry: uniform gap between every pair of adjacent cells
    // (applied identically horizontally and vertically).
    inline constexpr int kOrlGap = 4;

    // Horizontal margin from the component edge to the outermost column,
    // applied in all three display modes.
    inline constexpr int kSideMargin = 8;

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
