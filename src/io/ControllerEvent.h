#pragma once

#include <cstdint>

namespace lockstep
{
    // Logical button identifiers spanning QWERTY, MIDI, and future hardware sources.
    // Buttons that carry a positional index (Step, Section, MetaSection, SelectTrack)
    // store the index in ControllerEvent::index; all other buttons have index == -1.
    //
    // 10x4 layout (MHX §33, §5.5):
    //   Left two columns = eight modifier cluster:
    //     Col 1 (1/Q/A/Z): Func / Track / Pattern / Mute
    //     Col 2 (2/W/S/X): Fill / Cue / Scene / Master
    //   Right 8x4 functional block:
    //     Row 1 (3-8, 9, 0): six canonical sections, RecordArm, PlayStop
    //     Row 2 (E-P):       nav L/U/D/R, verbs Record/Play/Stop, TapTempo
    //     Row 3 (D-;):       steps 0-7
    //     Row 4 (C-/):       steps 8-15
    enum class ControllerButton : std::uint8_t
    {
        // Column-1 modifiers (structural / edit scopes).
        Func,         // key 1: primary modifier (secondary functions via Func+key)
        TrackScope,   // key Q: track scope (Q+step=SelectTrack, Q alone=Control-All)
        PatternScope, // key A: pattern-level scope (dedicated key in MHX)
        MuteScope,    // key Z: mute scope (Z+step=toggle track mute)

        // Column-2 modifiers (performance scopes).
        FillScope,    // key 2: fill modifier (held=fill conditions evaluate true)
        CueScope,     // key W: cue/monitor scope (§31)
        SceneScope,   // key S: scene assignment scope (§17); Scene+^/v picks endpoint A/B
        MasterScope,  // key X: master-bus / FX focus (§32.3)

        // Verb keys (meaning changes based on the active scope from EditMode).
        VerbRecord,       // Y: copy / capture scope into clipboard
        VerbPlay,         // U: paste / apply clipboard to scope
        VerbStop,         // I: clear scope

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
