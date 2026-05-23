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
            // Column-1 structural scope modifiers.
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

            // Column-2 performance scope modifiers.
            case ControllerButton::FillScope:
                scope_.fill = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::CueScope:
                scope_.cue = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::SceneScope:
                scope_.scene = isDown;
                recomputePrimary();
                return true;
            case ControllerButton::MasterScope:
                scope_.master = isDown;
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
        // Column 1 (structural, non-Func): track, pattern, mute.
        const bool col1NonFunc = scope_.track || scope_.pattern || scope_.mute;
        // Column 2 (performance): fill, cue, scene, master.
        const bool col2 = scope_.fill || scope_.cue || scope_.scene || scope_.master;
        // Compound = (col1-non-Func AND col2) OR (Func AND col2) OR (Func AND col1-non-Func).
        // Func is the universal qualifier so it pairs with anything.
        if (scope_.func && (col1NonFunc || col2)) { return true; }
        if (col1NonFunc && col2) { return true; }
        return false;
    }

    bool EditMode::hasSameColumnConflict() const noexcept
    {
        // Two col-1 non-Func modifiers held together is a same-column conflict.
        const int col1Count = (scope_.track ? 1 : 0)
                            + (scope_.pattern ? 1 : 0)
                            + (scope_.mute ? 1 : 0);
        // Two col-2 modifiers held together is a same-column conflict.
        const int col2Count = (scope_.fill ? 1 : 0)
                            + (scope_.cue ? 1 : 0)
                            + (scope_.scene ? 1 : 0)
                            + (scope_.master ? 1 : 0);
        return (col1Count >= 2) || (col2Count >= 2);
    }

    void EditMode::recomputePrimary()
    {
        // Priority: Trig > Section > Track > Pattern > Mute > Cue > Scene > Master > Fill > Func > None
        if (scope_.trig)    { primary_ = PrimaryScope::Trig;    return; }
        if (sectionHeld_)   { primary_ = PrimaryScope::Section; return; }
        if (scope_.track)   { primary_ = PrimaryScope::Track;   return; }
        if (scope_.pattern) { primary_ = PrimaryScope::Pattern; return; }
        if (scope_.mute)    { primary_ = PrimaryScope::Mute;    return; }
        if (scope_.cue)     { primary_ = PrimaryScope::Cue;     return; }
        if (scope_.scene)   { primary_ = PrimaryScope::Scene;   return; }
        if (scope_.master)  { primary_ = PrimaryScope::Master;  return; }
        if (scope_.fill)    { primary_ = PrimaryScope::Fill;    return; }
        if (scope_.func)    { primary_ = PrimaryScope::Func;    return; }
        primary_ = PrimaryScope::None;
    }
}
