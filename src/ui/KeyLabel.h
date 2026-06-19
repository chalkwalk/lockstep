#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ScopedSectionMatrix.h"
#include "UITheme.h"
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
            // In density sticky mode the MOD key (index 4) acts as sub-page cycle.
            // Label shows the next destination so the user knows what one press will do.
            if (def.sectionIdx == 4 && ui.densityStickyMode)
            {
                using SP = UiState::DensitySubPage;
                const char* nextLabel =
                    ui.densitySubPage == SP::Amount     ? "MUSIC" :
                    ui.densitySubPage == SP::Musicality ? "SELECT" :
                    /* Selection */                       "AMOUNT";
                return { juce::String(nextLabel), {}, false };
            }
            // In velocity sticky mode the AMP key (index 3) acts as sub-page cycle.
            if (def.sectionIdx == 3 && ui.velStickyMode)
            {
                using VP = UiState::VelSubPage;
                const char* nextLabel =
                    ui.velSubPage == VP::Depth  ? "CENTER" :
                    ui.velSubPage == VP::Center ? "MODE"   :
                    ui.velSubPage == VP::Mode   ? "BLEND"  :
                    /* Blend */                   "DEPTH";
                return { juce::String(nextLabel), {}, false };
            }
            // Tempo and time-sig sticky modes relabel the TRIG key (index 0) to signal the
            // active band. This mirrors the density/vel pattern above.
            if (def.sectionIdx == 0 && ui.tempoStickyMode)
                return { "TEMPO", {}, false };
            if (def.sectionIdx == 0 && ui.timeSigStickyMode)
                return { "TIME", {}, false };
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
