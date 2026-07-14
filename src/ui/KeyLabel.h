#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ScopedSectionMatrix.h"
#include "ScopeSectionSelect.h"
#include "UITheme.h"
#include "mode/ModeReducer.h"
#include "../io/EditMode.h"
#include "../io/EditContext.h"
#include "../state/UiState.h"
#include "../command/ScopePriority.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // Scope colour helper (MHZ.1.4, DESIGN §6.6)
    //
    // Returns the canonical scope colour for the given primary scope.
    // Pass machinePicker=true for the Part+SRC machine picker, which uses a
    // distinct hue to signal it is its own mode (not just Part scope).
    // -------------------------------------------------------------------------

    inline juce::Colour scopeColour(EditMode::PrimaryScope scope,
                                    bool machinePicker = false) noexcept
    {
        using namespace theme;
        using PS = EditMode::PrimaryScope;
        if (machinePicker) return col(kScopeMachine);
        switch (scope)
        {
            case PS::Track:  return col(kScopeTrack);
            case PS::Phrase: return col(kScopePhrase);
            case PS::Scene:  return col(kScopeScene);
            case PS::Morph:  return col(kScopeMorph);
            case PS::Song:   return col(kScopeSong);
            case PS::Mute:   return col(kScopeMute);
            case PS::Fill:   return col(kScopeFill);
            // Func is the universal qualifier; its section-row secondaries glow
            // in the Func hue (DESIGN §6.1 rule 3).
            case PS::Func:   return col(kScopeFunc);
            // Non-section scopes and None use the default step colour.
            case PS::None:
            case PS::Trig:
            case PS::Cue:
            case PS::Section:
                break;
        }
        return col(kScopeStep);
    }

    // Item 7: the section-stack layer colour. Maps a resolved SecOrigin onto the
    // scope palette so a section button and the MZ page read the *winning* scope's
    // colour, whether it was reached by a held modifier or by fall-through (a
    // track-DSP FILTER reads cyan even unheld). Machine = neutral step colour (no
    // tint). Global has its own azure hue (9.22) — reached by Func+Song, visibly
    // distinct from plain Song even though transport globals sit under the Song
    // umbrella.
    inline juce::Colour originColour(SecOrigin origin) noexcept
    {
        using namespace theme;
        using PS = EditMode::PrimaryScope;
        switch (origin)
        {
            case SecOrigin::Machine: return col(kScopeStep);
            case SecOrigin::Track:   return scopeColour(PS::Track);
            case SecOrigin::Phrase:  return scopeColour(PS::Phrase);
            case SecOrigin::Scene:   return scopeColour(PS::Scene);
            case SecOrigin::Song:    return scopeColour(PS::Song);
            case SecOrigin::Global:  return col(kScopeGlobal);
        }
        return col(kScopeStep);
    }

    // Item 7 (7e): on a SURFACE KEY the Func outline is live — shown iff the Func
    // layer is currently held AND the key is func-qualified. It must not latch
    // there, because on a key the outline is the *preview* of what the next press
    // does, and the next press changes when Func comes up.
    inline bool funcOutlineActive(bool funcHeld, bool pageFuncQualified) noexcept
    {
        return funcHeld && pageFuncQualified;
    }

    // 9.31: the MZ's outline is the opposite — a property of how the shown PAGE
    // was reached, not of what is held now. It latches through the Func release
    // (you let Func go to reach an encoder, which is exactly when you look at the
    // MZ) and clears on the next page change: a page selected without Func is
    // reached without Func, so the flag simply falls out of page selection.
    inline bool funcOutlineLatched(bool pageEnteredViaFunc) noexcept
    {
        return pageEnteredViaFunc;
    }

    // Item 7: the banner word for a resolved page origin (MZ header + any
    // controller). The word is the durable signal; the colour a learned shorthand
    // (DESIGN §6.1.1). Exhaustive over SecOrigin — a new layer is a compile error.
    inline const char* originWord(SecOrigin origin) noexcept
    {
        switch (origin)
        {
            case SecOrigin::Machine: return "MACHINE";
            case SecOrigin::Track:   return "TRACK";
            case SecOrigin::Phrase:  return "PHRASE";
            case SecOrigin::Scene:   return "SCENE";
            case SecOrigin::Song:    return "SONG";
            case SecOrigin::Global:  return "GLOBAL";
        }
        return "MACHINE";
    }

    // Returns the scope colour that corresponds to the held modifier state in
    // UiState — whichever section-suite scope is currently held (priority from
    // firstHeldSectionSuiteScope), then Mute/Fill, or the step colour if none.
    inline juce::Colour scopeColourFromState(const UiState& ui) noexcept
    {
        using namespace theme;
        using PS = EditMode::PrimaryScope;
        const PS s = firstHeldSectionSuiteScope(ui);
        if (s != PS::None) { return scopeColour(s); }
        if (ui.muteHeld) { return col(ui.funcHeld ? kScopePMute : kScopeMute); }
        if (ui.fillHeld) { return scopeColour(PS::Fill); }
        return scopeColour(PS::None);
    }

    // -------------------------------------------------------------------------
    // Key label resolver (MHZ.1.3 + MHZ.1.5, DESIGN §6.5)
    //
    // resolveKeyLabel() applies the single hint-band rule:
    //   • hint = funcLayer label (dim at rest; promoted to primary when Func held).
    //   • hint is absent when Func has no action on the key (funcLayer empty).
    //   • disabled = true for no-content scoped cells.
    // Func-layer promotion itself (swapping primary ↔ hint) is performed at the
    // builder level (SurfaceModel.cpp), not inside this function.
    // -------------------------------------------------------------------------

    // Role of a key within the performance grammar.
    enum class KeyRole : uint8_t
    {
        Modifier,    // Func, Track, Pattern, Part, Scene, Master, Mute, Fill
        SectionKey,  // TRIG / SRC / FILTER / AMP / MOD / FX (index 0-5)
        VerbCopy,    // U / REC    — COPY when scope+verb compound; no Func action
        VerbPaste,   // I / PLAY   — PASTE when scope+verb compound; Func = PANIC
        VerbClear,   // O / CLEAR  — CLEAR when scope+verb compound; Func = DEL
        VerbSnapshot,     // Y / SNAP   — Snapshot; scope variant under scope; Func = RESTORE
        VerbConfirm,      // P / CONFIRM — Confirm (primary); Cancel (Func+P); dims under scope
        Nav,         // E / R / T  — navigation
        Utility,     // TAP (3), NavUp (4), anything else
    };

    // Compile-time description of one key cell.
    struct KeyDef
    {
        KeyRole role = KeyRole::Utility;
        const char* natural = "";   // primary when no modifier changes it
        const char* funcLayer = "";   // Func-layer secondary; "" = none
        int sectionIdx = -1;   // for SectionKey: 0-5
        bool machineHasSection = true; // for SectionKey: false = no machine slots
    };

    // Resolved paint strings for one key cell.
    struct KeyLabel
    {
        juce::String primary;           // main centred label
        juce::String hint;              // bottom-strip secondary (reduced alpha unless Func)
        bool disabled = false;  // true → render in Disabled state
    };

    // Returns the label pair for one key given the current UI and edit-context
    // state.  Pure function — all contextual resolution happens here.
    inline KeyLabel resolveKeyLabel(const KeyDef& def,
                                    const UiState& ui,
                                    [[maybe_unused]] const EditContext& ec) noexcept
    {
        using PS = EditMode::PrimaryScope;

        // Determine the held section-suite scope modifier (if any).
        const PS sectionScope = firstHeldSectionSuiteScope(ui);
        const bool isScopedMode = (sectionScope != PS::None);

        // --- Section keys (TRIG / SRC / FILTER / AMP / MOD / FX) ----------
        if (def.role == KeyRole::SectionKey)
        {
            // Overlay-internal section keys get a dynamic label from the descriptor
            // (e.g. Density MOD → next subpage name, Vel AMP → next subpage, Time TRIG → "TIME").
            // Centralised in ModeReducer so adding a new overlay can't miss this relabel.
            if (def.sectionIdx >= 0)
            {
                const char* overlayLabel = overlayInternalSectionLabel(ui, def.sectionIdx);
                if (overlayLabel != nullptr)
                    return { juce::String(overlayLabel), {}, false };
            }
            if (!isScopedMode)
            {
                // Normal mode: machine availability drives disabled state.
                // The funcLayer field carries the meta-section secondary.
                return { juce::String(def.natural),
                         juce::String(def.funcLayer),
                         !def.machineHasSection };
            }
            // Scoped mode: matrix lookup overrides label and availability.
            const ScopedCellInfo info = scopedCell(sectionScope, def.sectionIdx);
            const char* label = (info.label != nullptr) ? info.label : def.natural;
            return { juce::String(label), {}, !info.hasContent };
        }

        // --- All other keys (including verb keys) --------------------------------
        // hint = funcLayer (empty when Func has no action on this key).
        // Func-layer promotion to primary happens in the builder (SurfaceModel.cpp).
        return { juce::String(def.natural), juce::String(def.funcLayer), false };
    }
}
