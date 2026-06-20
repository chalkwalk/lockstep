#pragma once
#include <juce_core/juce_core.h>
#include "../io/ControllerEvent.h"

namespace lockstep
{
    class LockstepProcessor;
    struct UiState;
    class EditContext;

    // =========================================================================
    // InspectorModel — always-on context inspector data (DESIGN §19 / 9.11)
    //
    // 4 fixed captioned regions showing the current context:
    //
    //   KEY     — focused/last-touched key + its full gesture list
    //   HELD    — held modifier / chord grammar (scope + latch status)
    //   OVERLAY — active picker / overlay purpose + select/cancel hint
    //   EDIT    — held-step P-lock override or focused param value
    //
    // Each region always has content (idle fallback for empty context).
    // Built by a pure function so it can be unit-tested and reused by
    // external controller displays (dual-target, PRINCIPLES §19).
    // =========================================================================

    struct InspectorModel
    {
        juce::String key;      // e.g. "3 — tap TEMPO / hold GEN HUB"
        juce::String held;     // e.g. "SCENE — verbs → scene, dbl=LATCH"
        juce::String overlay;  // e.g. "SELECT GENERATOR — pick cell"
        juce::String edit;     // e.g. "step 5  vel 110  gate 0.5"

        // Idle fallbacks (regions are never blank):
        // key: "—"
        // held: "track <n>  <machineId>"
        // overlay: "scene <n>  phrase <n>"
        // edit: "—"
    };

    // Build the inspector model from current plugin state.
    // `focusedButton` / `focusedIndex` = last-touched control key (-1 = none).
    InspectorModel buildInspectorModel(const UiState& ui,
                                       const EditContext& ec,
                                       const LockstepProcessor& proc,
                                       ControllerButton focusedButton,
                                       int focusedIndex) noexcept;

} // namespace lockstep
