#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ScopedSectionMatrix.h"
#include "UITheme.h"
#include "../io/EditMode.h"
#include "../io/EditContext.h"
#include "../state/UiState.h"

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
            case PS::Track:   return col(kScopeTrack);
            case PS::Phrase: return col(kScopePhrase);
            case PS::Scene:    return col(kScopeScene);
            case PS::Morph:   return col(kScopeMorph);
            case PS::Song:  return col(kScopeSong);
            case PS::Mute:    return col(kScopeMute);
            case PS::Fill:    return col(kScopeFill);
            // Non-section scopes and None use the default step colour.
            case PS::None: case PS::Func: case PS::Trig:
            case PS::Cue: case PS::Section:
                break;
        }
        return col(kScopeStep);
    }

    // Returns the scope colour that corresponds to the held modifier state in
    // UiState — whichever section-suite scope is currently held, or the step
    // colour if none.
    inline juce::Colour scopeColourFromState(const UiState& ui) noexcept
    {
        using namespace theme;
        using PS = EditMode::PrimaryScope;
        if (ui.trackHeld)        return scopeColour(PS::Track);
        if (ui.phraseScopeHeld) return scopeColour(PS::Phrase);
        if (ui.sceneHeld)         return scopeColour(PS::Scene);
        if (ui.morphHeld)        return scopeColour(PS::Morph);
        if (ui.songHeld)       return scopeColour(PS::Song);
        if (ui.muteHeld)         return col(ui.funcHeld ? kScopePMute : kScopeMute);
        if (ui.fillHeld)         return scopeColour(PS::Fill);
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
        VerbCopy,    // U / REC — COPY when scope+verb compound; no Func action
        VerbPaste,   // I / PLY — PASTE when scope+verb compound; no Func action
        VerbClear,   // O / STP — CLEAR when scope+verb compound; no Func action
        VerbYes,     // Y / YES — unchanged across scopes
        VerbNo,      // P / NO  — unchanged across scopes
        Nav,         // E / R / T — navigation
        Utility,     // TAP (3), NavUp (4), anything else
    };

    // Compile-time description of one key cell.
    struct KeyDef
    {
        KeyRole     role             = KeyRole::Utility;
        const char* natural          = "";   // primary when no modifier changes it
        const char* funcLayer        = "";   // Func-layer secondary; "" = none
        int         sectionIdx       = -1;   // for SectionKey: 0-5
        bool        machineHasSection = true; // for SectionKey: false = no machine slots
    };

    // Resolved paint strings for one key cell.
    struct KeyLabel
    {
        juce::String primary;           // main centred label
        juce::String hint;              // bottom-strip secondary (reduced alpha unless Func)
        bool         disabled = false;  // true → render in Disabled state
    };

    // Returns the label pair for one key given the current UI and edit-context
    // state.  Pure function — all contextual resolution happens here.
    inline KeyLabel resolveKeyLabel(const KeyDef&      def,
                                    const UiState&     ui,
                                    [[maybe_unused]] const EditContext& ec) noexcept
    {
        using PS = EditMode::PrimaryScope;

        // Determine the held section-suite scope modifier (if any).
        PS sectionScope = PS::None;
        if      (ui.trackHeld)        sectionScope = PS::Track;
        else if (ui.phraseScopeHeld) sectionScope = PS::Phrase;
        else if (ui.sceneHeld)         sectionScope = PS::Scene;
        else if (ui.morphHeld)        sectionScope = PS::Morph;
        else if (ui.songHeld)       sectionScope = PS::Song;

        const bool isScopedMode = (sectionScope != PS::None);

        // --- Section keys (TRIG / SRC / FILTER / AMP / MOD / FX) ----------
        if (def.role == KeyRole::SectionKey)
        {
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
