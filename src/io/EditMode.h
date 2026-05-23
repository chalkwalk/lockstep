#pragma once

#include <functional>
#include "ControllerEvent.h"

namespace lockstep
{
    // Tracks the set of scope buttons currently held and derives the active
    // performance grammar context from them.  All input sources route through
    // this state machine before handler dispatch, so scoping is source-agnostic.
    //
    // Compound-chord rule (MHX §13, §33.3):
    //   Two modifiers may be held together only if they come from different columns
    //   (col-1 structural vs col-2 performance).  A modifier+modifier compound never
    //   fires on its own — it sets a compound scope waiting for a verb or encoder.
    //   Func is the universal qualifier and composes with any other modifier.
    //   Two col-1 modifiers or two col-2 modifiers are ignored (same-column chords
    //   are not defined; the secondary press is a no-op in this context).
    //
    // Scope hierarchy (highest to lowest priority when multiple are held):
    //   Trig (held step) > Section > Track > Pattern > Mute > Cue > Scene > Master > Fill > Func
    class EditMode
    {
    public:
        // Which scope buttons are currently held (MHX 10x4 layout).
        struct ScopeState
        {
            // Column 1 (structural):
            bool func    = false;  // key 1
            bool track   = false;  // key Q: Control-All if no trig held
            bool pattern = false;  // key A (dedicated in MHX)
            bool mute    = false;  // key Z
            // Column 2 (performance):
            bool fill    = false;  // key 2
            bool cue     = false;  // key W (§31)
            bool scene   = false;  // key S (§17)
            bool master  = false;  // key X (§32.3)
            // Set externally from EditContext / section holds:
            bool trig    = false;  // at least one step is held
        };

        // The primary scope determines what the next verb operates on.
        enum class PrimaryScope : std::uint8_t
        {
            None,
            Func,     // Func held without another scope
            Trig,     // one or more steps held
            Track,    // Track scope (Control-All if trig not also held)
            Pattern,  // Pattern scope
            Mute,     // Mute scope
            Fill,     // Fill scope (momentary; verb is less common here)
            Cue,      // Cue/monitor scope (§31)
            Scene,    // Scene assignment scope (§17)
            Master,   // Master-bus / FX focus (§32.3)
            Section,  // a section key is held (set externally when section held)
        };

        [[nodiscard]] const ScopeState&  scopeState()   const { return scope_; }
        [[nodiscard]] PrimaryScope       primaryScope() const { return primary_; }

        // Returns true when a cross-column compound scope is active (one col-1 modifier
        // AND one col-2 modifier held simultaneously, excluding Func which is universal).
        // Func+col2 counts as a compound; col1+col2 (non-Func) also counts.
        [[nodiscard]] bool hasCompoundScope() const noexcept;

        // Returns true when any two same-column non-Func modifiers are both held
        // (which is a no-op per the compound-chord rule).
        [[nodiscard]] bool hasSameColumnConflict() const noexcept;

        // Sync the trig-held and section-held sub-states from outside
        // (EditContext and SectionBar).
        void setTrigHeld(bool held);
        void setSectionHeld(bool held);

        // Process a button-down or button-up event for scope modifier keys.
        // Returns true if the event was consumed as a scope-modifier change.
        bool onScopeEvent(const ControllerEvent& ev);

        // Verb dispatch: called when a verb button (VerbRecord/Play/Stop/Yes/No)
        // fires. The provided callbacks are invoked based on the current primary
        // scope. Stubs for all verbs fire here; the real handler implementations
        // land in MB.3 and later milestones.
        void onVerb(ControllerButton verb);

        // Callbacks wired by the editor.  All are optional.
        std::function<void(PrimaryScope, ControllerButton)> onVerbDispatched;

    private:
        ScopeState   scope_;
        PrimaryScope primary_ = PrimaryScope::None;
        bool         sectionHeld_ = false;

        void recomputePrimary();
    };
}
