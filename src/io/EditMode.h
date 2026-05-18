#pragma once

#include <functional>
#include "ControllerEvent.h"

namespace lockstep
{
    // Tracks the set of scope buttons currently held and derives the active
    // performance grammar context from them.  All input sources route through
    // this state machine before handler dispatch, so scoping is source-agnostic.
    //
    // Scope hierarchy (highest to lowest priority when multiple are held):
    //   Trig (held step) > Section > Track > Pattern > Mute > Fill > Func
    //
    // The primary scope is the last scope button pressed that is still held.
    // Verb keys (VerbRecord, VerbPlay, VerbStop) dispatch through onVerb which
    // carries the current primary scope. Snapshot/Restore bypass EditMode entirely.
    class EditMode
    {
    public:
        // Which scope buttons are currently held.
        struct ScopeState
        {
            bool func    = false;
            bool track   = false;  // key Q: Control-All if no trig held
            bool pattern = false;  // Func+2
            bool mute    = false;  // key A
            bool fill    = false;  // key Z
            bool trig    = false;  // at least one step is held (set externally)
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
            Section,  // a section key is held (set externally when section held)
        };

        [[nodiscard]] const ScopeState&  scopeState()   const { return scope_; }
        [[nodiscard]] PrimaryScope       primaryScope() const { return primary_; }

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
