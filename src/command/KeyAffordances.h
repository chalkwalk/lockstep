#pragma once
#include <juce_core/juce_core.h>
#include "../io/ControllerEvent.h"

namespace lockstep
{
    // =========================================================================
    // KeyAffordances — gesture-affordance display SSOT (DESIGN §19 / 9.11)
    //
    // Each ControllerButton has at most one entry describing its full gesture set:
    //   tap action   — fires on a short press + release  (most keys)
    //   hold action  — fires while key stays pressed (modifiers, GEN hub key)
    //   dblTap action — fires on a double-tap sequence
    //   func action  — shown by funcHint; sourced from KeyBindings (not here)
    //
    // `primaryIsHold` = true when the "strongest" / primary action fires on hold
    // rather than tap (e.g. key 3: primary = GEN, tap = TAP TEMPO).
    //
    // Keys with no entry render exactly as today (primary + funcHint only).
    // All string literals are ASCII-only (juce::String UTF-8 assert guard).
    // =========================================================================

    struct KeyAffordance
    {
        ControllerButton button = ControllerButton::None;
        const char8_t* tapLabel      = nullptr; // "" / nullptr = gesture absent
        const char8_t* holdLabel     = nullptr;
        const char8_t* doubleTapLabel = nullptr;
        bool primaryIsHold = false;    // when true, primary reads the hold action
    };

    // Returns the affordance entry for a button, or nullptr if none registered.
    const KeyAffordance* findAffordance(ControllerButton button) noexcept;

} // namespace lockstep
