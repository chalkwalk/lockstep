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
    //   3,4 = Param page  prev/next      R = Record arm
    //   5,6 = Pattern     prev/next      T = Tap tempo
    //   7,8 = Mute / Solo (hold)         Y/U/I = Copy / Paste / Clear
    //                                    Space = Play/Stop
    //
    // Keypress handlers and editor wiring land alongside the Manipulation
    // Zone in M6; this header declares the seam and the action vocabulary.
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

            Step,             // valid stepIndex in 0..15

            ParamPagePrev,
            ParamPageNext,
            PatternPrev,
            PatternNext,
            MuteHold,
            SoloHold,

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
        // digit keys, 0x20 for space). Physical-position stability across
        // non-QWERTY layouts is an M6 concern.
        [[nodiscard]] Mapping resolve(int keyCode) const;
    };
}
