#pragma once
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep::theme
{
    // -------------------------------------------------------------------------
    // Key-group colour palette
    // Each group has three values: inactive background, active/pressed background,
    // and the accent colour used for borders and mode-active outlines.
    // -------------------------------------------------------------------------

    // Func key (1 / FNC) — amber
    inline constexpr uint32_t kFuncInactive = 0xFF3A2800u;
    inline constexpr uint32_t kFuncActive   = 0xFFC07800u;
    inline constexpr uint32_t kFuncAccent   = 0xFFC07800u;

    // Modifier keys (Q/TRK, A/MUT, Z/FIL) — indigo
    inline constexpr uint32_t kModInactive  = 0xFF1E1A2Eu;
    inline constexpr uint32_t kModActive    = 0xFF5040A0u;
    inline constexpr uint32_t kModAccent    = 0xFF5040A0u;

    // Navigation keys (W/◄, E/▼, R/►, 3/▲) — navy
    inline constexpr uint32_t kNavInactive  = 0xFF0E1E30u;
    inline constexpr uint32_t kNavActive    = 0xFF3060A0u;
    inline constexpr uint32_t kNavAccent    = 0xFF3060A0u;

    // Record key (2 / REC) — warm red
    inline constexpr uint32_t kRecInactive  = 0xFF2E1010u;
    inline constexpr uint32_t kRecActive    = 0xFFA03030u;
    inline constexpr uint32_t kRecAccent    = 0xFFA03030u;

    // Section keys (4-9) — teal
    inline constexpr uint32_t kSecInactive  = 0xFF0E2020u;
    inline constexpr uint32_t kSecActive    = 0xFF206060u;
    inline constexpr uint32_t kSecAccent    = 0xFF3EC8C8u;

    // Action keys (Y/CPY, U/PST, I/CLR) — slate blue
    inline constexpr uint32_t kActInactive  = 0xFF101828u;
    inline constexpr uint32_t kActActive    = 0xFF204878u;
    inline constexpr uint32_t kActAccent    = 0xFF4090C0u;

    // Tap Tempo key (O / TAP) — grey
    inline constexpr uint32_t kTapInactive  = 0xFF1A1A20u;
    inline constexpr uint32_t kTapActive    = 0xFF505060u;
    inline constexpr uint32_t kTapAccent    = 0xFF8090A0u;

    // Transport key (T / PLY) — green
    inline constexpr uint32_t kTrnInactive  = 0xFF0C2010u;
    inline constexpr uint32_t kTrnActive    = 0xFF208040u;
    inline constexpr uint32_t kTrnAccent    = 0xFF30C060u;

    // -------------------------------------------------------------------------
    // Step-grid colours
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kStepActive    = 0xFF50B478u;  // green — certain fire
    inline constexpr uint32_t kStepFillOnly  = 0xFF7B4EC0u;  // violet — OnlyFill (fill off)
    inline constexpr uint32_t kStepInactive  = 0xFF2D3741u;  // dark — in-range, no trig
    inline constexpr uint32_t kStepOutRange  = 0xFF1C2026u;  // near-black — out of range
    inline constexpr uint32_t kStepPlayhead  = 0xFFFFCC44u;  // amber border
    inline constexpr uint32_t kStepHeld      = 0xFFFFFFFFu;  // white — held-step border
    inline constexpr uint32_t kStepPLock     = 0xFF3EC8C8u;  // cyan — P-Lock dot

    // -------------------------------------------------------------------------
    // Helper: build a juce::Colour from a packed ARGB uint32
    // -------------------------------------------------------------------------
    inline juce::Colour col(uint32_t argb) noexcept
    {
        return juce::Colour(argb);
    }
}
