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

    // Col-1 modifier keys (Q/PAT, A/SCN, Z/MUT) — indigo. MHY identities.
    inline constexpr uint32_t kModInactive  = 0xFF1E1A2Eu;
    inline constexpr uint32_t kModActive    = 0xFF5040A0u;
    inline constexpr uint32_t kModAccent    = 0xFF5040A0u;

    // Col-2 modifier keys (2/TRK, W/PRT, S/MST, X/FIL) — violet. MHY identities.
    inline constexpr uint32_t kPerfInactive = 0xFF1E1430u;
    inline constexpr uint32_t kPerfActive   = 0xFF7040B0u;
    inline constexpr uint32_t kPerfAccent   = 0xFF9060D0u;

    // Navigation keys (E/<, R/^, T/v, Y/>) — navy
    inline constexpr uint32_t kNavInactive  = 0xFF0E1E30u;
    inline constexpr uint32_t kNavActive    = 0xFF3060A0u;
    inline constexpr uint32_t kNavAccent    = 0xFF3060A0u;

    // Record-arm key (9 / ARM) — warm red
    inline constexpr uint32_t kRecInactive  = 0xFF2E1010u;
    inline constexpr uint32_t kRecActive    = 0xFFA03030u;
    inline constexpr uint32_t kRecAccent    = 0xFFA03030u;

    // Section keys (4-9) — teal
    inline constexpr uint32_t kSecInactive  = 0xFF0E2020u;
    inline constexpr uint32_t kSecActive    = 0xFF206060u;
    inline constexpr uint32_t kSecAccent    = 0xFF3EC8C8u;

    // Action / verb keys (U/REC, I/PLY, O/STP) — slate blue / transport green
    inline constexpr uint32_t kActInactive  = 0xFF101828u;
    inline constexpr uint32_t kActActive    = 0xFF204878u;
    inline constexpr uint32_t kActAccent    = 0xFF4090C0u;

    // Tap Tempo key (P / TAP) — grey
    inline constexpr uint32_t kTapInactive  = 0xFF1A1A20u;
    inline constexpr uint32_t kTapActive    = 0xFF505060u;
    inline constexpr uint32_t kTapAccent    = 0xFF8090A0u;

    // Transport keys (0/PLY, I/PLY verb) — green
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
    // Scope colour grammar (MHZ.1.4, DESIGN §6.6)
    // Canonical palette per scope. Taxonomy is fixed; specific RGB values
    // are deferred to a later visual-design pass (placeholder colours are
    // distinguishable but not yet "designed").
    // -------------------------------------------------------------------------

    // Default / no-scope held: light grey
    inline constexpr uint32_t kScopeStep    = 0xFF8898A8u;

    // Section-suite scope modifiers — each gets a distinct hue.
    inline constexpr uint32_t kScopeTrack   = 0xFF30A0C0u;  // cyan-blue
    inline constexpr uint32_t kScopePattern = 0xFF8040C0u;  // purple
    inline constexpr uint32_t kScopePart    = 0xFF20A060u;  // green
    inline constexpr uint32_t kScopeScene   = 0xFFD06020u;  // orange
    inline constexpr uint32_t kScopeMaster  = 0xFFC0A000u;  // gold

    // Part+SRC machine picker: visibly distinct from Part itself.
    inline constexpr uint32_t kScopeMachine = 0xFF50C030u;  // lime

    // -------------------------------------------------------------------------
    // Helper: build a juce::Colour from a packed ARGB uint32
    // -------------------------------------------------------------------------
    inline juce::Colour col(uint32_t argb) noexcept
    {
        return juce::Colour(argb);
    }
}
