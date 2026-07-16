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
    //   Col 1 (1/Q/A/Z): Func, Phrase, Morph, Mute.
    //   Col 2 (2/W/S/X): Track, Scene, Song,  Fill.
    //
    // Scope hierarchy (highest to lowest priority when multiple are held):
    //   Trig (held step) > Section > Track > Phrase > Scene > Mute > Morph >
    //   Song > Fill > Func.
    //   (Cue is reserved as a PrimaryScope but is not bound to a key
    //   post-MHY; left in for MU reactivation.)
    class EditMode
    {
    public:
        // Which scope buttons are currently held (MHX shape, MHY identities).
        struct ScopeState
        {
            // Column 1 (1/Q/A/Z):
            bool func = false;  // key 1
            bool phrase = false;  // key Q
            bool morph = false;  // key A (§17)
            bool mute = false;  // key Z
            // Column 2 (2/W/S/X):
            bool track = false;  // key 2: Control-All if no trig held
            bool scene = false;  // key W (§4.7) — Scene launch / re-sync
            bool song = false;  // key S (§32.3; Func+Song = Global)
            bool fill = false;  // key X
            // Reserved for MU (Cue bus) — no key bound:
            bool cue = false;
            // Set externally from EditContext / section holds:
            bool trig = false;  // at least one step is held
        };

        // The primary scope determines what the next verb operates on.
        //
        // Two CATEGORIES of scope, and the difference is load-bearing:
        //   • Section-SUITE scopes — {Track, Phrase, Scene, Morph, Song}. Held, they
        //     re-skin the 5-0 section row and the step grid to a layer of the musical
        //     container stack (Track DIV/LEN, Song master-FX, Func+Song=Global TRSP).
        //     firstHeldSectionSuiteScope() enumerates exactly these; sectionResolveMode
        //     turns them into a SecOrigin floor. They are 1:1 with a held modifier key.
        //   • QUALIFIER / monitoring scopes — {Cue, Mute, Fill, Trig, Section, Func}.
        //     They qualify what a verb/gesture DOES, but do NOT re-skin the section row,
        //     so they are deliberately absent from firstHeldSectionSuiteScope and map to
        //     the Machine floor in sectionFloorForScope (exhaustive; not a fall-through).
        //
        // CUE is a qualifier/monitoring scope (DESIGN §31), NOT a suite scope. It is
        // also the one scope entered by a COMPOUND (Func+3) rather than a bare modifier
        // key — hardware has no dedicated Cue button (§21). Because it is neither a
        // modifier nor a suite scope, its surface signals can't ride the declarative
        // binding table or the section resolver the way Track/Global do; they are wired
        // imperatively from the ui.cueHeld flag. The full cue-scope touchpoint map:
        //   • entry + release ......... LockstepEditor::enterCueScope / key-up (Func+3)
        //   • scope colour ............ theme::kScopeCue → scopeColour(PS::Cue) (KeyLabel.h)
        //   • entry-key label ......... KeyBindings "CUE" row (Func+3) + StatusText::cueScope
        //   • console-entry affordance  applyCueScopeAffordance() (SurfaceModel.cpp)
        //   • console (sticky) ........ Overlay::Cue (ModeReducer) + MetaBand::Cue
        //   • direct gestures ......... Cue+Mute (toggle), Cue+step (audition), Cue+hold(AMP)
        //   • cue balance model ....... DESIGN §31 / §31.5 (P-Lock ▷ morph ▷ base tiers)
        enum class PrimaryScope : std::uint8_t
        {
            None,
            Func,     // Func held without another scope
            Trig,     // one or more steps held
            Track,    // Track scope (Control-All if trig not also held)
            Phrase,   // Phrase scope (key Q)
            Scene,    // Scene scope (§4.7; launch / re-sync) — key W
            Mute,     // Mute scope
            Fill,     // Fill scope (momentary; verb is less common here)
            Cue,      // Cue/monitor qualifier scope (§31); entered Func+3, see map above
            Morph,    // Morph (A/B crossfader) assignment scope (§17) — key A
            Song,     // Song select; Func+Song = Global/master-bus (§32.3) — key S
            Section,  // a section key is held (set externally when section held)
        };

        [[nodiscard]] const ScopeState& scopeState() const { return scope_; }
        [[nodiscard]] PrimaryScope primaryScope() const { return primary_; }

        // True while a section key (5-0) is physically held. Distinct from "a
        // section is merely displayed": only a held key drives section-scoped
        // verbs. Trig outranks Section, so under a step hold this stays readable
        // even though primaryScope() reports Trig.
        [[nodiscard]] bool sectionHeld() const noexcept { return sectionHeld_; }

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
        ScopeState scope_;
        PrimaryScope primary_ = PrimaryScope::None;
        bool sectionHeld_ = false;

        void recomputePrimary();
        // Maps a PrimaryScope value to the corresponding held flag.
        [[nodiscard]] bool isScopeHeld(PrimaryScope s) const noexcept;
    };
}
