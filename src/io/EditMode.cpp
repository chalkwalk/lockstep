#include "EditMode.h"
#include "../command/ScopePriority.h"

namespace lockstep
{
    void EditMode::setTrigHeld(bool held)
    {
        scope_.trig = held;
        recomputePrimary();
    }

    void EditMode::setSectionHeld(bool held)
    {
        sectionHeld_ = held;
        recomputePrimary();
    }

    bool EditMode::onScopeEvent(const ControllerEvent& ev)
    {
        const bool isDown = (ev.type == ControllerEvent::Type::ButtonDown);

        switch (ev.button)
        {
            // MHY cluster Col 1: Func / Pattern / Scene / Mute.
            case ControllerButton::Func:
                scope_.func = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::PhraseScope:
                scope_.phrase = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::MorphScope:
                scope_.morph = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::MuteScope:
                scope_.mute = isDown;
                recomputePrimary();
                return true;

            // MHY cluster Col 2: Track / Part / Master / Fill.
            case ControllerButton::TrackScope:
                scope_.track = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::SceneScope:
                scope_.scene = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::SongScope:
                scope_.song = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::FillScope:
                scope_.fill = isDown;
                recomputePrimary();
                return true;

            // No hardware CueScope button exists — the Cue scope ships (DESIGN §31)
            // but is entered as the Func+3 compound in the editor (enterCueScope),
            // which sets ui.cueHeld, not this ControllerButton. Accept the event
            // anyway so a future controller that does bind a Cue key just works.
            case ControllerButton::CueScope:
                scope_.cue = isDown;
                recomputePrimary();
                return true;

            // All other buttons are not scope modifiers; caller handles them.
            case ControllerButton::VerbSnapshot:
            case ControllerButton::VerbRecord:
            case ControllerButton::VerbPlay:
            case ControllerButton::VerbStopLegacy:
            case ControllerButton::VerbConfirm:
            case ControllerButton::Snapshot:
            case ControllerButton::Restore:
            case ControllerButton::NavUp:
            case ControllerButton::NavLeft:
            case ControllerButton::NavDown:
            case ControllerButton::NavRight:
            case ControllerButton::Section:
            case ControllerButton::MetaSection:
            case ControllerButton::Step:
            case ControllerButton::SelectTrack:
            case ControllerButton::ToggleMute:
            case ControllerButton::ForkPart:
            case ControllerButton::RecordArm:
            case ControllerButton::TapTempo:
            case ControllerButton::MetronomeToggle:
            case ControllerButton::PlayStop:
            case ControllerButton::StopReset:
            case ControllerButton::VerbClear:
            case ControllerButton::VerbDelete:
            case ControllerButton::VerbPanic:
            case ControllerButton::None:
                return false;
        }
        return false;
    }

    void EditMode::onVerb(ControllerButton verb)
    {
        if (onVerbDispatched) { onVerbDispatched(primary_, verb); }
    }

    bool EditMode::hasCompoundScope() const noexcept
    {
        // MHY columns:
        //   Col 1 (non-Func): pattern, scene, mute.
        //   Col 2:            track, part, master, fill.
        // Func is the universal qualifier and pairs with anything.
        const bool col1NonFunc = scope_.phrase || scope_.morph || scope_.mute;
        const bool col2 = scope_.track || scope_.scene || scope_.song || scope_.fill;
        if (scope_.func && (col1NonFunc || col2)) { return true; }
        if (col1NonFunc && col2) { return true; }
        return false;
    }

    bool EditMode::hasSameColumnConflict() const noexcept
    {
        // MHY columns. Two of the same column non-Func held = no-op conflict.
        const int col1Count = (scope_.phrase ? 1 : 0) + (scope_.morph ? 1 : 0) + (scope_.mute ? 1 : 0);
        const int col2Count = (scope_.track ? 1 : 0) + (scope_.scene ? 1 : 0) + (scope_.song ? 1 : 0) + (scope_.fill ? 1 : 0);
        return (col1Count >= 2) || (col2Count >= 2);
    }

    void EditMode::recomputePrimary()
    {
        // Walk kScopePriority (ScopePriority.h) — the one SSOT for priority order.
        for (auto s : kScopePriority)
        {
            if (isScopeHeld(s))
            {
                primary_ = s;
                return;
            }
        }
        // Cue is reserved (no QWERTY binding); checked after the walk so it does
        // not displace ranked scopes but remains reachable for future input sources.
        if (scope_.cue)
        {
            primary_ = PrimaryScope::Cue;
            return;
        }
        primary_ = PrimaryScope::None;
    }

    bool EditMode::isScopeHeld(PrimaryScope s) const noexcept
    {
        using PS = PrimaryScope;
        switch (s)
        {
            case PS::Trig:    return scope_.trig;
            case PS::Section: return sectionHeld_;
            case PS::Track:   return scope_.track;
            case PS::Phrase:  return scope_.phrase;
            case PS::Scene:   return scope_.scene;
            case PS::Mute:    return scope_.mute;
            case PS::Morph:   return scope_.morph;
            case PS::Song:    return scope_.song;
            case PS::Fill:    return scope_.fill;
            case PS::Func:    return scope_.func;
            case PS::Cue:     return scope_.cue;
            case PS::None:    return false;
        }
        return false;
    }
}
