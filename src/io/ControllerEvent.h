#pragma once

#include <cstdint>

namespace lockstep
{
    // Logical button identifiers spanning QWERTY, MIDI, and future hardware sources.
    // Buttons that carry a positional index (Step, Section, MetaSection, SelectTrack)
    // store the index in ControllerEvent::index; all other buttons have index == -1.
    //
    // 9x4 layout (columns 1-9, rows QWERTY/ASDF/ZXCV):
    //   Left column (1/Q/A/Z) = dedicated modifier strip.
    //   Right 8x4 block top half = function/nav/verb keys.
    //   Right 8x4 block bottom half = 16 sequencer steps (S-L, X-.).
    enum class ControllerButton : std::uint8_t
    {
        // Left-column modifiers — dedicated physical keys, always reachable.
        Func,       // key 1: primary modifier (secondary functions via Func+key)
        TrackScope, // key Q: track scope (Q+step=SelectTrack, Q alone=Control-All)
        MuteScope,  // key A: mute scope (A+step=toggle track mute)
        FillScope,  // key Z: fill modifier (held=fill conditions evaluate true)

        // Secondary scope buttons reachable via the Func layer.
        PatternScope,     // Func+2: pattern-level scope

        // Verb keys (meaning changes based on the active scope from EditMode).
        VerbRecord,       // Y: copy / capture scope into clipboard
        VerbPlay,         // U: paste / apply clipboard to scope
        VerbStop,         // I: clear scope

        // Checkpoint operations — direct actions, not scope-qualified verbs.
        Snapshot,         // Func+2(Rec): push checkpoint
        Restore,          // Func+T(Ply): pop checkpoint

        // Trig-grid mode chords: held = mode active, exit on release.
        TrigModeKeyboard, // Func+Y: 16 trig keys -> chromatic keyboard
        TrigModeRetrig,   // Func+U: 16 trig keys -> retrigger pads
        TrigModeSoundPool,// Func+I: 16 trig keys -> sound pool browser

        // Navigation (2=Up, W=Left, E=Down, R=Right).
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

        // Transport / utility.
        RecordArm,  // key 2 (Func not held)
        TapTempo,   // O (Func not held)
        PlayStop,   // T (Func not held)
        StopReset,  // Func+E
    };

    // A single normalised input event from any source (QWERTY, MIDI CC, UI encoder,
    // hardware controller). Handlers downstream are fully source-agnostic.
    struct ControllerEvent
    {
        enum class Type : std::uint8_t { ButtonDown, ButtonUp, EncoderDelta };

        Type             type   = Type::ButtonDown;
        ControllerButton button = ControllerButton::NavUp;
        int              index  = -1;  // step / section / track index
        int              delta  = 0;   // for EncoderDelta only
    };
}
