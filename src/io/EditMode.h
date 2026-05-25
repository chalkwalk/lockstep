#pragma once

#include <functional>
#include "ControllerEvent.h"

namespace lockstep
{
    // Tracks the set of scope buttons currently held and derives the active
    // performance grammar context from them.  All input sources route through
    // this state machine before handler dispatch, so scoping is source-agnostic.
    //
    // Compound-chord rule (DESIGN §13, §33.3; MHY cluster):
    //   Two modifiers may be held together only if they come from different
    //   columns. A modifier+modifier compound never fires on its own — it sets
    //   a compound scope waiting for a verb or encoder. Func is the universal
    //   qualifier and composes with any other modifier. Two same-column
    //   modifiers are ignored.
    //
    //   Col 1 (1/Q/A/Z): Func, Pattern, Scene, Mute.
    //   Col 2 (2/W/S/X): Track, Part,    Master, Fill.
    //
    // Scope hierarchy (highest to lowest priority when multiple are held):
    //   Trig (held step) > Section > Track > Pattern > Part > Mute > Scene >
    //   Master > Fill > Func.
    //   (Cue is reserved as a PrimaryScope but is not bound to a key
    //   post-MHY; left in for MU reactivation.)
    class EditMode
    {
    public:
        // Which scope buttons are currently held (MHX shape, MHY identities).
        struct ScopeState
        {
            // Column 1 (1/Q/A/Z):
            bool func    = false;  // key 1
            bool pattern = false;  // key Q
            bool scene   = false;  // key A (§17)
            bool mute    = false;  // key Z
            // Column 2 (2/W/S/X):
            bool track   = false;  // key 2: Control-All if no trig held
            bool part    = false;  // key W (§4.7) — kit identity, Part+SRC = machine select
            bool master  = false;  // key S (§32.3)
            bool fill    = false;  // key X
            // Reserved for MU (Cue bus) — no key bound:
            bool cue     = false;
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
            Part,     // Part scope (§4.7) — MHY
            Mute,     // Mute scope
            Fill,     // Fill scope (momentary; verb is less common here)
            Cue,      // Cue/monitor scope (§31; reserved until MU)
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
