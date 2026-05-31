#include "EditMode.h"

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

            // Cue scope is reserved (MU); no key emits it post-MHY, but accept
            // the event in case future input sources do.
            case ControllerButton::CueScope:
                scope_.cue = isDown;
                recomputePrimary();
                return true;

            // All other buttons are not scope modifiers; caller handles them.
            case ControllerButton::VerbYes:
            case ControllerButton::VerbRecord:
            case ControllerButton::VerbPlay:
            case ControllerButton::VerbStop:
            case ControllerButton::VerbNo:
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
            case ControllerButton::MachineSelect:
            case ControllerButton::RecordArm:
            case ControllerButton::TapTempo:
            case ControllerButton::MetronomeToggle:
            case ControllerButton::PlayStop:
            case ControllerButton::StopReset:
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
        const bool col2        = scope_.track   || scope_.scene  || scope_.song || scope_.fill;
        if (scope_.func && (col1NonFunc || col2)) { return true; }
        if (col1NonFunc && col2) { return true; }
        return false;
    }

    bool EditMode::hasSameColumnConflict() const noexcept
    {
        // MHY columns. Two of the same column non-Func held = no-op conflict.
        const int col1Count = (scope_.phrase ? 1 : 0)
                            + (scope_.morph   ? 1 : 0)
                            + (scope_.mute    ? 1 : 0);
        const int col2Count = (scope_.track   ? 1 : 0)
                            + (scope_.scene    ? 1 : 0)
                            + (scope_.song  ? 1 : 0)
                            + (scope_.fill    ? 1 : 0);
        return (col1Count >= 2) || (col2Count >= 2);
    }

    void EditMode::recomputePrimary()
    {
        // Priority (MHY): Trig > Section > Track > Pattern > Part > Mute >
        // Scene > Master > Fill > Func > None.
        // (Cue is reserved but currently unreachable from QWERTY.)
        if (scope_.trig)    { primary_ = PrimaryScope::Trig;    return; }
        if (sectionHeld_)   { primary_ = PrimaryScope::Section; return; }
        if (scope_.track)   { primary_ = PrimaryScope::Track;   return; }
        if (scope_.phrase) { primary_ = PrimaryScope::Phrase; return; }
        if (scope_.scene)    { primary_ = PrimaryScope::Scene;    return; }
        if (scope_.mute)    { primary_ = PrimaryScope::Mute;    return; }
        if (scope_.cue)     { primary_ = PrimaryScope::Cue;     return; }
        if (scope_.morph)   { primary_ = PrimaryScope::Morph;   return; }
        if (scope_.song)  { primary_ = PrimaryScope::Song;  return; }
        if (scope_.fill)    { primary_ = PrimaryScope::Fill;    return; }
        if (scope_.func)    { primary_ = PrimaryScope::Func;    return; }
        primary_ = PrimaryScope::None;
    }
}
