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
    //     Col 1 (1/Q/A/Z): Func / Phrase / Morph / Mute
    //     Col 2 (2/W/S/X): Track / Scene / Song / Fill
    //   Right 8x4 functional block:
    //     Row 1 (3-0): TAP(3), NavUp(4), six canonical sections(5-0)
    //     Row 2 (E-P): NavLeft(E), NavDown(R), NavRight(T),
    //                  Snapshot(Y), Rec(U), Play(I), Clear(O), Yes(P)
    //     Row 3 (D-;):       steps 0-7
    //     Row 4 (C-/):       steps 8-15
    //   Cue is reserved as a scope (§31) but is not bound to a cluster key
    //   until MU; the CueScope enum value remains for future reactivation.
    enum class ControllerButton : std::uint8_t
    {
        // Column-1 modifiers: Func / Phrase / Morph / Mute.
        Func,         // key 1: universal qualifier (secondary functions via Func+key)
        PhraseScope,  // key Q: Phrase scope (per-track phrase select / swap)
        MorphScope,   // key A: Morph (A/B crossfader) assignment scope (§17); Morph+^/v picks endpoint A/B
        MuteScope,    // key Z: mute scope (Z+step=toggle track mute; Func+Z=scene mute)

        // Column-2 modifiers: Track / Scene / Song / Fill.
        TrackScope,   // key 2: track scope; Track+step=SelectTrack, Track alone=Control-All; Func+Track=machine/Kit picker
        SceneScope,   // key W: Scene scope (§4.7 — launch / re-sync / commit a Scene)
        SongScope,    // key S: Song select; Func+Song = Global / master-bus / FX focus (§32.3)
        FillScope,    // key X: fill modifier (MHY: moved from 2; held=fill conditions evaluate true)

        // Cue scope: reserved for MU reactivation, currently not bound to any key.
        CueScope,

        // Verb keys (MHY: primary layer Y/U/I/O/P; meaning changes with active scope).
        VerbYes,          // key Y: Snapshot (push checkpoint); under scope = scope snapshot; Func+Y = Restore
        VerbRecord,       // key U: Record / arm overdub; under scope = Copy
        VerbPlay,         // key I: Play/Pause (double-tap = Stop-to-top); under scope = Paste; Func+I = Panic
        VerbStop,         // key O: (legacy — superseded by VerbClear; kept for ABI stability)
        VerbNo,           // key P: Yes / confirm prompt; Func+P = No / cancel

        // Checkpoint operations — triggered via Func+verb layer.
        Snapshot,         // Func+Y: push checkpoint (now primary Y)
        Restore,          // Func+Y: pop checkpoint (Func+Y)

        // New verb keys appended for colour-rethink (ABI: add-only, never reorder).
        VerbClear,        // key O: Clear active operand; under scope = clear scope contents; Func+O = Delete
        VerbDelete,       // Func+O: delete the active entity (requires confirm via VerbYes/VerbNo)
        VerbPanic,        // Func+I: kill all voices + hard stop

        // Navigation (4=Up, E=Left, R=Down, T=Right).
        NavUp,
        NavLeft,
        NavDown,
        NavRight,

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
        enum class Type : std::uint8_t
        {
            ButtonDown,
            ButtonUp,
            EncoderDelta
        };

        Type type = Type::ButtonDown;
        ControllerButton button = ControllerButton::None;
        int index = -1;  // step / section / track index
        int delta = 0;   // for EncoderDelta only
        int velocity = 0; // 0 = none/unknown; >0 = source-supplied
                                       // note velocity (Push pads). QWERTY/mouse
                                       // leave 0 → callers use the default.
    };
}
