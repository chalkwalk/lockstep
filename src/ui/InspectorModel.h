#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include "../io/ControllerEvent.h"
#include "../state/UiState.h"      // ConfirmKind
#include "../io/ClipboardType.h"
#include "../io/EditMode.h"        // PrimaryScope

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

    // 9.30 §42.2 — the status taxonomy. Three kinds, distinguished by WHAT MAKES
    // THEM GO AWAY. The distinction is not cosmetic: it is the whole fix.
    //
    //   State   — lives until the STATE changes. Re-derived every frame, never fades.
    //   Alert   — lives until the CONDITION clears (missing samples).
    //   Event   — lives until a TIMER expires (a fading toast: "Copied phrase 3").
    //
    // The rule: anything that changes what the next key press does is STATE, and it
    // must render for as long as it is armed. The confirm prompt broke that rule --
    // sticky arming, fading announcement -- so `Confirm` is its own kind: a State that
    // renders as the double-height pop-over (§42.3).
    enum class StatusKind : std::uint8_t
    {
        None,
        Idle,     // state AT REST: where the MZ's knobs are about to write (see below)
        Event,    // fading toast
        Alert,    // persistent until cleared
        State,    // re-derived from UiState; never fades
        Confirm,  // a State, rendered as the pop-over -- armed-but-invisible is impossible
    };

    // The editor-owned status inputs the model cannot derive from UiState: the toast
    // (text + age) and the pool's missing-sample count. Everything else the lane shows
    // is STATE, and state is read straight from UiState -- which is precisely why an
    // armed confirm can no longer fade out from under the user.
    struct StatusInput
    {
        juce::String toast;              // last setStatus() message ("" = none)
        juce::uint32 toastAgeMs = 0;     // ms since it was set
        juce::uint32 toastDurationMs = 1500;
        int missingSamples = 0;          // pool alert (0 = none)

        // 9.30 st.2 — the two badges the deleted header dashboard carried. Both are
        // scope-qualified STATE ("what would a paste paste? how deep can I undo?"), so
        // they belong beside the scope in the HELD region rather than in a far corner
        // of the chrome the eye never visits.
        ClipboardType clipboard = ClipboardType::None;
        int checkpointDepth = 0;         // depth of the HELD scope's checkpoint stack

        // 9.14 st.5 — the scope the VERBS will act on (EditMode::primaryScope()). It is
        // an editor-owned fact because it depends on held STEPS and SECTION keys, which
        // are not modifiers and so are not in UiState. Without it the lane could only
        // guess whether Record copies right now, and guessing is how the Song keys came
        // to advertise a COPY that did not exist.
        EditMode::PrimaryScope scope = EditMode::PrimaryScope::None;
    };

    struct InspectorModel
    {
        juce::String key;      // e.g. "3 — tap TEMPO / hold GEN HUB"
        juce::String held;     // e.g. "SCENE — verbs → scene, dbl=LATCH"
        juce::String overlay;  // e.g. "SELECT GENERATOR — pick cell"
        juce::String edit;     // e.g. "step 5  vel 110  gate 0.5"

        // The STATUS lane (9.30 st.1). One organ, one model, two surfaces: the screen
        // paints it, and an external controller display gets it for free (§19).
        juce::String status;
        StatusKind statusKind = StatusKind::None;
        float statusAlpha = 1.0f;        // < 1 only while an Event fades

        // The confirm pop-over (§42.3). Populated iff statusKind == Confirm.
        juce::String confirmPrompt;      // "DELETE TRACK 3?"
        juce::String confirmActions;     // "[P] CONFIRM     [any other key] CANCEL"

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
                                       int focusedIndex,
                                       const StatusInput& si = {}) noexcept;

    // The lane's RESTING content: where the Manipulation Zone's knobs are about to
    // write. This is the Override-ELSE-Base rule made visible — the single fact that
    // decides what every encoder does, and it was displayed nowhere on the surface.
    //
    // It is also what earns the inspector its position: the lane sits directly above the
    // MZ, so it captions it. "MZ -> TRACK 3 base" and "MZ -> STEP 5 override (P-LOCK)"
    // are the same knobs pointed at two different layers, and the difference is
    // invisible in the knobs themselves.
    [[nodiscard]] juce::String mzWriteTarget(const UiState& ui,
                                             const EditContext& ec,
                                             const LockstepProcessor& proc) noexcept;

    // The pending-confirm prompt, derived from the CONFIRM STATE itself rather than
    // from a message captured when it was armed. That is the structural half of the fix:
    // there is no code path in which the arming exists and the text does not.
    [[nodiscard]] juce::String confirmPromptFor(ConfirmKind kind, int target) noexcept;

} // namespace lockstep
