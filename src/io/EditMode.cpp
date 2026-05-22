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
            // Scope modifier buttons — update scope state and recompute primary.
            case ControllerButton::Func:
                scope_.func = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::TrackScope:
                scope_.track = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::PatternScope:
                scope_.pattern = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::MuteScope:
                scope_.mute = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::FillScope:
                scope_.fill = isDown;
                recomputePrimary();
                return true;

            // All other buttons are not scope modifiers; caller handles them.
            case ControllerButton::VerbRecord:
            case ControllerButton::VerbPlay:
            case ControllerButton::VerbStop:
            case ControllerButton::Snapshot:
            case ControllerButton::Restore:
            case ControllerButton::TrigModeKeyboard:
            case ControllerButton::TrigModeRetrig:
            case ControllerButton::TrigModeSoundPool:
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
            case ControllerButton::None:
                return false;
        }
        return false;
    }

    void EditMode::onVerb(ControllerButton verb)
    {
        if (onVerbDispatched) { onVerbDispatched(primary_, verb); }
    }

    void EditMode::recomputePrimary()
    {
        // Priority: Trig > Section > Track > Pattern > Mute > Fill > Func > None
        if (scope_.trig)    { primary_ = PrimaryScope::Trig;    return; }
        if (sectionHeld_)   { primary_ = PrimaryScope::Section; return; }
        if (scope_.track)   { primary_ = PrimaryScope::Track;   return; }
        if (scope_.pattern) { primary_ = PrimaryScope::Pattern; return; }
        if (scope_.mute)    { primary_ = PrimaryScope::Mute;    return; }
        if (scope_.fill)    { primary_ = PrimaryScope::Fill;    return; }
        if (scope_.func)    { primary_ = PrimaryScope::Func;    return; }
        primary_ = PrimaryScope::None;
    }
}
