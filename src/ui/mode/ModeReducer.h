#pragma once

#include "Overlay.h"
#include "ModeEvent.h"
#include "../../state/UiState.h"

namespace lockstep
{
    // =========================================================================
    // OverlayDescriptor — one declarative record per overlay.
    //
    // The reducer, renderer, and gesture layer iterate this table instead of
    // branching on individual flags.  ExitPolicy has NO default — every
    // descriptor must state the full policy so "missing wire" becomes a
    // compile-time omission in the aggregate initialiser, not a silent bug.
    //
    // Adding a new overlay: add one entry to kOverlays in ModeReducer.cpp.
    // Adding a new exit trigger: add a field here — all existing entries will
    // fail to compile until they supply a value (aggregate init enforcement).
    // =========================================================================
    struct OverlayDescriptor
    {
        Overlay id;

        // ----- Section key policy -------------------------------------------
        // internalSection: section index that is NOT exited on.
        //   -1 = no internal section (every section press either exits or falls through).
        //   >= 0 = this index is "own"; behaviour depends on internalSectionConsumed.
        int internalSection;

        // internalSectionConsumed: what to do when internalSection is pressed.
        //   true  = consume and cycle the sub-page (Density/Vel).
        //   false = pass through (NotConsumed) so caller can handle toggle (Time/TRIG).
        bool internalSectionConsumed;

        // internalSectionRelabelFn: returns the dynamic primary label for the
        // internal section key when this overlay is active, or nullptr if none.
        // Replaces the three ad-hoc ifs in resolveKeyLabel.
        using RelabelFn = const char* (*)(const UiState&) noexcept;
        RelabelFn internalSectionRelabelFn;

        // exitOnSectionPressOther: any section press where index != internalSection
        // exits the overlay.  false = fall through unmodified.
        bool exitOnSectionPressOther;

        // ----- Scope key policy (which modifier scope buttons exit) ----------
        // true = "foreign" (pressing this scope exits the overlay).
        // false = "own" (scope is a live control; stays in the overlay).
        bool trackScopeForeign;
        bool phraseScopeForeign;
        bool sceneScopeForeign;
        bool morphScopeForeign;
        bool muteScopeForeign;
        bool fillScopeForeign;
        bool songScopeForeign;

        // ----- Func double-tap policy ----------------------------------------
        bool exitOnDoubleTapFunc;
    };

    // =========================================================================
    // OverlayResult — return value from handleOverlayEvent.
    // =========================================================================
    enum class OverlayResult : uint8_t
    {
        NotConsumed, // event was not handled; caller continues normal dispatch
        Consumed,    // event was fully handled (sub-page cycled, etc.); caller returns true
        Exited,      // overlay exited; caller refreshes band but continues dispatch
    };

    // =========================================================================
    // Public API
    // =========================================================================

    // Returns the currently-active overlay derived from the four UiState flags.
    // Priority: Euclid > Time > Density > Vel > None.
    [[nodiscard]] Overlay activeOverlay(const UiState& ui) noexcept;

    // Clears all UiState fields for the given overlay.
    // Equivalent to the per-mode escape* functions in the editor.
    void escapeOverlay(UiState& ui, Overlay ov) noexcept;

    // Enters a sticky overlay. The single owner of the transition INTO a mode,
    // mirroring escapeOverlay(), which owns the transition out.
    //
    // WHY THIS EXISTS. Exit was funnelled and entry was not: escapeOverlay() is
    // the only way out, but entry was six raw `ui.overlay = Overlay::X`
    // assignments in PluginEditor.cpp. A raw write is not merely untidy -- it
    // SKIPS THE OUTGOING OVERLAY'S RESET. Overlay parameters live beside the
    // field (densityBank, velSubPage, cueParamPage, samplePropsPoolIndex, the
    // identity and browser blocks) and escapeOverlay() is what clears them, so
    // assigning straight over a live overlay left the previous one's state
    // behind for the next time it was opened.
    //
    // This escapes whatever is active first, then sets the field, so the two
    // directions are symmetrical and a mode is always entered clean.
    //
    // Transient chord overlays (Euclid/Melodic/Harmony) are NOT stored in the
    // field -- they live in their own held flags and activeOverlay() reports
    // them ahead of it -- so this is a no-op for them. Writing the field for a
    // chord would manufacture exactly the illegal co-existence the single
    // field exists to prevent.
    void enterOverlay(UiState& ui, Overlay ov) noexcept;

    // Main event dispatcher.  Looks up the active overlay's descriptor and
    // applies exit/consume policy.  Mutates ui on Consumed or Exited.
    // The caller refreshes the meta band and repaints based on the result.
    [[nodiscard]] OverlayResult handleOverlayEvent(UiState& ui,
                                                   const ModeEvent& ev,
                                                   const ScopeCtx& ctx = {}) noexcept;

    // Returns the dynamic primary label for the internal section key of the
    // active overlay (e.g. "MUSIC" / "SELECT" / "AMOUNT" for Density subpages).
    // Returns nullptr when: no overlay is active, no relabel defined, or
    // sectionIdx != internalSection for the active overlay.
    // Used by resolveKeyLabel to replace the three scattered per-mode ifs.
    [[nodiscard]] const char* overlayInternalSectionLabel(const UiState& ui,
                                                          int sectionIdx) noexcept;
}
