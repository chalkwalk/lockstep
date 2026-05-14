#pragma once

#include <cstdint>

namespace lockstep
{
    // The QWERTY overlay maps the upper-left 8x4 block of the keyboard onto
    // the Lockstep grid, mirroring the eventual hardware layout so muscle
    // memory transfers when users move to the dedicated controller.
    //
    //   1  2  3  4  5  6  7  8       1     = Shift
    //    Q  W  E  R  T  Y  U  I      2/Q/W/E = nav up/left/down/right
    //     A  S  D  F  G  H  J  K     A-K   = steps 1-8
    //      Z  X  C  V  B  N  M  ,    Z-,   = steps 9-16
    //
    //   3-8 = SelectSection 0-5; Shift+3-8 = meta (COND/TRIG/TRACK/—/—/GLOBAL)
    //   R   = Record arm          T = Tap tempo
    //   Y/U/I = Copy / Paste / Clear
    //   Space = Play/Stop
    //
    // Shift (key 1) + section key = master section select.
    class QwertyOverlay
    {
    public:
        enum class Action : std::uint8_t
        {
            None,
            Shift,

            NavUp,
            NavDown,
            NavLeft,
            NavRight,

            Step,                // valid stepIndex in 0..15

            SelectSection,       // stepIndex holds machine section index 0..5
            SelectMetaSection,   // stepIndex holds meta section index 0..5 (Shift held)

            SelectTrack,         // stepIndex holds track index 0..7 (Shift + step row 1)

            RecordArm,
            TapTempo,
            Copy,
            Paste,
            Clear,

            PlayStop
        };

        struct Mapping
        {
            Action action = Action::None;
            int stepIndex = -1;
        };

        // Accepts a JUCE KeyPress key code (uppercase ASCII for letter and
        // digit keys, 0x20 for space) and the current shift state.
        // Physical-position stability across non-QWERTY layouts is a
        // known limitation; the mapping assumes a standard QWERTY layout.
        [[nodiscard]] Mapping resolve(int keyCode, bool shiftHeld) const;
    };
}
