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
            case PS::Pattern: return col(kScopePattern);
            case PS::Part:    return col(kScopePart);
            case PS::Scene:   return col(kScopeScene);
            case PS::Master:  return col(kScopeMaster);
            // Non-section scopes and None use the default step colour.
            case PS::None: case PS::Func: case PS::Trig: case PS::Mute:
            case PS::Fill: case PS::Cue: case PS::Section:
                break;
        }
        return col(kScopeStep);
    }

    // Returns the scope colour that corresponds to the held modifier state in
    // UiState — whichever section-suite scope is currently held, or the step
    // colour if none.
    inline juce::Colour scopeColourFromState(const UiState& ui) noexcept
    {
        using PS = EditMode::PrimaryScope;
        if (ui.trackHeld)        return scopeColour(PS::Track);
        if (ui.patternScopeHeld) return scopeColour(PS::Pattern);
        if (ui.partHeld)         return scopeColour(PS::Part);
        if (ui.sceneHeld)        return scopeColour(PS::Scene);
        if (ui.masterHeld)       return scopeColour(PS::Master);
        return scopeColour(PS::None);
    }

    // -------------------------------------------------------------------------
    // Key label resolver (MHZ.1.3 + MHZ.1.5, DESIGN §6.5)
    //
    // resolveKeyLabel() is the single rule that produces {primary, hint} for
    // every painted key.  The three policies it encodes:
    //
    //  1. Primary label swaps when a modifier reinterprets the key.
    //  2. An always-on hint survives only for genuinely-invariant secondary
    //     meanings (COPY/PASTE/CLEAR on verb keys under any scope).  All
    //     other secondaries appear only when their relevant modifier is held.
    //  3. Empty / disabled is a first-class return (no-content scoped cells).
    // -------------------------------------------------------------------------

    // Role of a key within the performance grammar.
    enum class KeyRole : uint8_t
    {
        Modifier,    // Func, Track, Pattern, Part, Scene, Master, Mute, Fill
        SectionKey,  // TRIG / SRC / FILTER / AMP / MOD / FX (index 0-5)
        VerbCopy,    // U / REC — becomes COPY under any scope or step-hold
        VerbPaste,   // I / PLY — becomes PASTE under any scope or step-hold
        VerbClear,   // O / STP — becomes CLEAR under any scope or step-hold
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
    inline KeyLabel resolveKeyLabel(const KeyDef&     def,
                                    const UiState&    ui,
                                    const EditContext& ec) noexcept
    {
        using PS = EditMode::PrimaryScope;

        // Determine the held section-suite scope modifier (if any).
        PS sectionScope = PS::None;
        if      (ui.trackHeld)        sectionScope = PS::Track;
        else if (ui.patternScopeHeld) sectionScope = PS::Pattern;
        else if (ui.partHeld)         sectionScope = PS::Part;
        else if (ui.sceneHeld)        sectionScope = PS::Scene;
        else if (ui.masterHeld)       sectionScope = PS::Master;

        const bool isScopedMode = (sectionScope != PS::None);
        const bool isStepHeld   = ui.stepHeld || ec.isActiveForEditing();

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

        // --- Verb keys with COPY / PASTE / CLEAR semantics ----------------
        // Policy 2: these hints are invariant — shown in the hint band even
        // with no modifier held.  When a scope or step is held the copy/paste/
        // clear action becomes the primary (the hint is then redundant, so it
        // is suppressed to avoid repetition).
        const char* cpcLabel = nullptr;
        if      (def.role == KeyRole::VerbCopy)  cpcLabel = "COPY";
        else if (def.role == KeyRole::VerbPaste)  cpcLabel = "PASTE";
        else if (def.role == KeyRole::VerbClear)  cpcLabel = "CLEAR";

        if (cpcLabel != nullptr)
        {
            const bool cpcActive = isScopedMode || isStepHeld;
            if (cpcActive)
                return { juce::String(cpcLabel), {}, false };

            // Func held: Func-layer takes over the hint band (brightened).
            if (ui.funcHeld && def.funcLayer[0] != '\0')
                return { juce::String(def.natural), juce::String(def.funcLayer), false };

            // Default: natural primary + always-on COPY/PASTE/CLEAR hint.
            return { juce::String(def.natural), juce::String(cpcLabel), false };
        }

        // --- All other keys -----------------------------------------------
        // Primary is the natural label; hint is the Func-layer secondary
        // (shown at reduced alpha always, full brightness when Func is held).
        return { juce::String(def.natural), juce::String(def.funcLayer), false };
    }
}
