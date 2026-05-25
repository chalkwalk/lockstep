#pragma once

#include <cstdint>

namespace lockstep
{
    // Logical button identifiers spanning QWERTY, MIDI, and future hardware sources.
    // Buttons that carry a positional index (Step, Section, MetaSection, SelectTrack)
    // store the index in ControllerEvent::index; all other buttons have index == -1.
    //
    // 10x4 layout (MHX shape, MHY identities; DESIGN §33, §5.5):
    //   Left two columns = eight modifier cluster (MHY frequency-of-use order):
    //     Col 1 (1/Q/A/Z): Func / Pattern / Scene / Mute
    //     Col 2 (2/W/S/X): Track / Part / Master / Fill
    //   Right 8x4 functional block:
    //     Row 1 (3-0): TAP(3), NavUp(4), six canonical sections(5-0)
    //     Row 2 (E-P): NavLeft(E), NavDown(R), NavRight(T), MachineSelect(Y),
    //                  Snapshot(U), RecordArm(I), PlayStop(O), StopReset(P)
    //     Row 3 (D-;):       steps 0-7
    //     Row 4 (C-/):       steps 8-15
    //   Cue is reserved as a scope (§31) but is not bound to a cluster key
    //   until MU; the CueScope enum value remains for future reactivation.
    enum class ControllerButton : std::uint8_t
    {
        // Column-1 modifiers (MHY): Func / Pattern / Scene / Mute.
        Func,         // key 1: universal qualifier (secondary functions via Func+key)
        PatternScope, // key Q: pattern-level scope (MHY: moved from A)
        SceneScope,   // key A: scene assignment scope (§17, MHY: moved from S); Scene+^/v picks endpoint A/B
        MuteScope,    // key Z: mute scope (Z+step=toggle track mute)

        // Column-2 modifiers (MHY): Track / Part / Master / Fill.
        TrackScope,   // key 2: track scope (MHY: moved from Q); Track+step=SelectTrack, Track alone=Control-All
        PartScope,    // key W: part scope (MHY new — kit identity, machine select via Part+SRC, §4.7)
        MasterScope,  // key S: master-bus / FX focus (§32.3, MHY: moved from X)
        FillScope,    // key X: fill modifier (MHY: moved from 2; held=fill conditions evaluate true)

        // Cue scope: reserved for MU reactivation, currently not bound to any key.
        CueScope,

        // Verb keys (meaning changes based on the active scope from EditMode).
        VerbRecord,       // Func+I: copy / capture scope into clipboard
        VerbPlay,         // Func+O: paste / apply clipboard to scope
        VerbStop,         // Func+P: clear scope

        // Checkpoint operations — direct actions, not scope-qualified verbs.
        Snapshot,         // Func+T: push checkpoint (Yes)
        Restore,          // Func+O: pop checkpoint (No)

        // Trig-grid mode chords: held = mode active, exit on release.
        TrigModeKeyboard, // Func+U: 16 trig keys -> chromatic keyboard
        TrigModeRetrig,   // Func+I: 16 trig keys -> retrigger pads
        TrigModeSoundPool,// Func+P: 16 trig keys -> sound pool browser

        // Navigation (E=Left, R=Up, T=Down, Y=Right).
        NavUp, NavLeft, NavDown, NavRight,

        // Section / meta-section buttons; index carries the section index (0-5).
        Section,          // keys 3-8 (Func not held)
        MetaSection,      // keys 3-8 (Func held)

        // Trig grid step; index carries the step index (0-15).
        Step,

        // Track select; index carries the track index (0-7).
        SelectTrack,

        // Mute toggle for a specific track; index carries the track index (0-7).
        ToggleMute,

        // Fork the active Part (make it unique). Gesture: Func+Y. (MD.5)
        ForkPart,

        // Open / close the machine selector overlay. Gesture: Func+R. (MGX.6)
        MachineSelect,

        // Transport / utility.
        RecordArm,       // key 9 (Func not held)
        TapTempo,        // key P (Func not held)
        MetronomeToggle, // Func+9
        PlayStop,        // key 0 (Func not held)
        StopReset,       // Func+E

        // Sentinel: returned by QwertyOverlay::resolve() for unmapped keys.
        // The default ControllerEvent::button must be this value so that the
        // editor's switch statement falls through to its default: return false
        // branch rather than firing a real action.
        None,
    };

    // A single normalised input event from any source (QWERTY, MIDI CC, UI encoder,
    // hardware controller). Handlers downstream are fully source-agnostic.
    struct ControllerEvent
    {
        enum class Type : std::uint8_t { ButtonDown, ButtonUp, EncoderDelta };

        Type             type   = Type::ButtonDown;
        ControllerButton button = ControllerButton::None;
        int              index  = -1;  // step / section / track index
        int              delta  = 0;   // for EncoderDelta only
    };
}
