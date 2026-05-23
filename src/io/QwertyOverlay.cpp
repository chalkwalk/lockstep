#include "QwertyOverlay.h"

#include <array>

namespace lockstep
{
    namespace
    {
        using B = ControllerButton;

        struct Entry
        {
            int keyCode;
            B   button;
            int index = -1;
        };

        constexpr int code(char c) { return static_cast<int>(c); }

        // Primary layer — no special modifier held (Fill is transparent; doesn't alter mappings).
        constexpr std::array<Entry, 32> kPrimary = { {
            // Navigation (2=Rec, 3=Up, W=Left, E=Down, R=Right)
            { code('2'), B::RecordArm,   -1 },
            { code('3'), B::NavUp,       -1 },
            { code('W'), B::NavLeft,     -1 },
            { code('E'), B::NavDown,     -1 },
            { code('R'), B::NavRight,    -1 },

            // Section buttons (keys 4-9)
            { code('4'), B::Section,      0 },
            { code('5'), B::Section,      1 },
            { code('6'), B::Section,      2 },
            { code('7'), B::Section,      3 },
            { code('8'), B::Section,      4 },
            { code('9'), B::Section,      5 },

            // Verb and utility row — new order: T=CPY Y=PST U=CLR I=TAP P=PLY (far right)
            { code('T'), B::VerbRecord,  -1 },
            { code('Y'), B::VerbPlay,    -1 },
            { code('U'), B::VerbStop,    -1 },
            { code('I'), B::TapTempo,    -1 },
            { code('O'), B::PlayStop,    -1 },

            // Step grid row 1 (S-L = steps 0-7)
            { code('S'), B::Step,         0 },
            { code('D'), B::Step,         1 },
            { code('F'), B::Step,         2 },
            { code('G'), B::Step,         3 },
            { code('H'), B::Step,         4 },
            { code('J'), B::Step,         5 },
            { code('K'), B::Step,         6 },
            { code('L'), B::Step,         7 },

            // Step grid row 2 (X-. = steps 8-15)
            { code('X'), B::Step,         8 },
            { code('C'), B::Step,         9 },
            { code('V'), B::Step,        10 },
            { code('B'), B::Step,        11 },
            { code('N'), B::Step,        12 },
            { code('M'), B::Step,        13 },
            { code(','), B::Step,        14 },
            { code('.'), B::Step,        15 },
        } };

        // Func layer — applied when Func (key 1) is held.
        // Keys not listed here fall through to the primary table.
        constexpr std::array<Entry, 15> kFunc = { {
            // Func+2=PatternScope (held: queue pattern via step; released w/o step: Snapshot).
            // Func+P(PLY, far-right)=Restore checkpoint.
            { code('2'), B::PatternScope,      -1 },
            { code('O'), B::Restore,           -1 },

            // Func+E(down)=stop+reset (temporary home until MD nav-secondary pass)
            { code('E'), B::StopReset,         -1 },

            // Func+W = fork active Part (make it unique, break Part sharing). (MD.5)
            { code('W'), B::ForkPart,          -1 },

            // Func+R = open/close machine selector. (MGX.6)
            { code('R'), B::MachineSelect,     -1 },

            // Func+I(TAP) = metronome toggle
            { code('I'), B::MetronomeToggle,   -1 },

            // Meta sections (keys 4-9)
            { code('4'), B::MetaSection,        0 },
            { code('5'), B::MetaSection,        1 },
            { code('6'), B::MetaSection,        2 },
            { code('7'), B::MetaSection,        3 },
            { code('8'), B::MetaSection,        4 },
            { code('9'), B::MetaSection,        5 },

            // Trig grid mode chords (held = mode active)
            { code('T'), B::TrigModeKeyboard,  -1 },
            { code('Y'), B::TrigModeRetrig,    -1 },
            { code('U'), B::TrigModeSoundPool, -1 },
        } };

        // Track layer — applied when Track (key Q) is held.
        // Row 3 (S-L) = tracks 0-7; row 4 (X-.) = tracks 8-15.
        constexpr std::array<Entry, 16> kTrack = { {
            { code('S'), B::SelectTrack,  0 },
            { code('D'), B::SelectTrack,  1 },
            { code('F'), B::SelectTrack,  2 },
            { code('G'), B::SelectTrack,  3 },
            { code('H'), B::SelectTrack,  4 },
            { code('J'), B::SelectTrack,  5 },
            { code('K'), B::SelectTrack,  6 },
            { code('L'), B::SelectTrack,  7 },
            { code('X'), B::SelectTrack,  8 },
            { code('C'), B::SelectTrack,  9 },
            { code('V'), B::SelectTrack, 10 },
            { code('B'), B::SelectTrack, 11 },
            { code('N'), B::SelectTrack, 12 },
            { code('M'), B::SelectTrack, 13 },
            { code(','), B::SelectTrack, 14 },
            { code('.'), B::SelectTrack, 15 },
        } };

        // Mute layer — applied when Mute (key A) is held.
        // Row 3 (S-L) = tracks 0-7; row 4 (X-.) = tracks 8-15.
        constexpr std::array<Entry, 16> kMute = { {
            { code('S'), B::ToggleMute,  0 },
            { code('D'), B::ToggleMute,  1 },
            { code('F'), B::ToggleMute,  2 },
            { code('G'), B::ToggleMute,  3 },
            { code('H'), B::ToggleMute,  4 },
            { code('J'), B::ToggleMute,  5 },
            { code('K'), B::ToggleMute,  6 },
            { code('L'), B::ToggleMute,  7 },
            { code('X'), B::ToggleMute,  8 },
            { code('C'), B::ToggleMute,  9 },
            { code('V'), B::ToggleMute, 10 },
            { code('B'), B::ToggleMute, 11 },
            { code('N'), B::ToggleMute, 12 },
            { code('M'), B::ToggleMute, 13 },
            { code(','), B::ToggleMute, 14 },
            { code('.'), B::ToggleMute, 15 },
        } };

        template <std::size_t N>
        bool lookup(const std::array<Entry, N>& table, int keyCode, ControllerEvent& out)
        {
            for (const auto& e : table)
            {
                if (e.keyCode == keyCode)
                {
                    out = { ControllerEvent::Type::ButtonDown, e.button, e.index, 0 };
                    return true;
                }
            }
            return false;
        }
    }

    bool QwertyOverlay::isEdgeKey(int keyCode) noexcept
    {
        // Keycodes for the decorative keys that sit immediately outside the 9-column
        // grid: ` 0 - = (number-row edges), Tab [ ] (Q-row edges),
        // ; ' (A-row right), / (Z-row right).
        // CapsLock and Shift are OS-level modifiers with no standard KeyPress code.
        static constexpr int kEdge[] = {
            96,          // ` (backtick / grave)
            48, 45, 61,  // 0  -  =
            9,           // Tab
            80, 91, 93,  // P  [  ]   (P is the right-edge Q-row key when O=PLY)
            59, 39,      // ;  '
            47,          // /
        };
        for (int k : kEdge)
            if (k == keyCode)
                return true;
        return false;
    }

    ControllerEvent QwertyOverlay::resolve(int keyCode,
                                           bool funcHeld,
                                           bool trackHeld,
                                           bool muteHeld) const  // NOLINT(readability-convert-member-functions-to-static)
    {
        using T = ControllerEvent::Type;

        // Left-column modifier keys always emit their own identity.
        if (keyCode == code('1')) { return { T::ButtonDown, B::Func,       -1, 0 }; }
        if (keyCode == code('Q')) { return { T::ButtonDown, B::TrackScope, -1, 0 }; }
        if (keyCode == code('A')) { return { T::ButtonDown, B::MuteScope,  -1, 0 }; }
        if (keyCode == code('Z')) { return { T::ButtonDown, B::FillScope,  -1, 0 }; }

        ControllerEvent ev;

        // Track and Mute layers take priority for step-row keys.
        if (trackHeld && lookup(kTrack, keyCode, ev)) { return ev; }
        if (muteHeld  && lookup(kMute,  keyCode, ev)) { return ev; }

        // Func layer for everything else.
        if (funcHeld  && lookup(kFunc,  keyCode, ev)) { return ev; }

        // Primary layer as final fallback.
        if (lookup(kPrimary, keyCode, ev)) { return ev; }

        return {};  // unmapped key
    }
}
