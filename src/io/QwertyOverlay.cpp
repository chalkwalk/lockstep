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

        // Primary layer — no special modifier held.
        // Fill/Cue/Scene/Master are transparent (they just set held state).
        constexpr std::array<Entry, 34> kPrimary = { {
            // Section buttons (keys 3-8)
            { code('3'), B::Section,     0 },
            { code('4'), B::Section,     1 },
            { code('5'), B::Section,     2 },
            { code('6'), B::Section,     3 },
            { code('7'), B::Section,     4 },
            { code('8'), B::Section,     5 },

            // Transport (row 1 right: 9=Arm, 0=Play/Stop)
            { code('9'), B::RecordArm,  -1 },
            { code('0'), B::PlayStop,   -1 },

            // Navigation row (E=Left, R=Up, T=Down, Y=Right)
            { code('E'), B::NavLeft,    -1 },
            { code('R'), B::NavUp,      -1 },
            { code('T'), B::NavDown,    -1 },
            { code('Y'), B::NavRight,   -1 },

            // Verb row (U=Copy/Record, I=Paste/Play, O=Clear/Stop)
            { code('U'), B::VerbRecord, -1 },
            { code('I'), B::VerbPlay,   -1 },
            { code('O'), B::VerbStop,   -1 },

            // Tap tempo
            { code('P'), B::TapTempo,   -1 },

            // Step grid row 1 (D-; = steps 0-7)
            { code('D'), B::Step,        0 },
            { code('F'), B::Step,        1 },
            { code('G'), B::Step,        2 },
            { code('H'), B::Step,        3 },
            { code('J'), B::Step,        4 },
            { code('K'), B::Step,        5 },
            { code('L'), B::Step,        6 },
            { 59,        B::Step,        7 },  // ; = 59

            // Step grid row 2 (C-/ = steps 8-15)
            { code('C'), B::Step,        8 },
            { code('V'), B::Step,        9 },
            { code('B'), B::Step,       10 },
            { code('N'), B::Step,       11 },
            { code('M'), B::Step,       12 },
            { 44,        B::Step,       13 },  // , = 44
            { 46,        B::Step,       14 },  // . = 46
            { 47,        B::Step,       15 },  // / = 47
        } };

        // Func layer — applied when Func (key 1) is held.
        // Keys not listed here fall through to the primary table.
        constexpr std::array<Entry, 15> kFunc = { {
            // Meta sections (keys 3-8)
            { code('3'), B::MetaSection,       0 },
            { code('4'), B::MetaSection,       1 },
            { code('5'), B::MetaSection,       2 },
            { code('6'), B::MetaSection,       3 },
            { code('7'), B::MetaSection,       4 },
            { code('8'), B::MetaSection,       5 },

            // Func+9 = metronome toggle
            { code('9'), B::MetronomeToggle,  -1 },

            // Navigation layer: Func+nav keys become transport/utility actions.
            { code('E'), B::StopReset,        -1 },  // Func+E(NavLeft) = StopReset
            { code('R'), B::MachineSelect,    -1 },  // Func+R(NavUp)   = open machine selector
            { code('T'), B::Snapshot,         -1 },  // Func+T(NavDown) = Yes / push checkpoint
            { code('Y'), B::ForkPart,         -1 },  // Func+Y(NavRight)= fork active Part

            // Verb layer: Func+verb keys become trig-mode chords + Restore.
            { code('U'), B::TrigModeKeyboard, -1 },  // Func+U(VerbRecord) = keyboard mode
            { code('I'), B::TrigModeRetrig,   -1 },  // Func+I(VerbPlay)   = retrig mode
            { code('O'), B::Restore,          -1 },  // Func+O(VerbStop)   = No / pop checkpoint
            { code('P'), B::TrigModeSoundPool,-1 },  // Func+P(TapTempo)   = sound pool mode
        } };

        // Track layer — applied when Track (key Q) is held.
        // Step rows D-; (0-7) and C-/ (8-15) select tracks.
        constexpr std::array<Entry, 16> kTrack = { {
            { code('D'), B::SelectTrack,  0 },
            { code('F'), B::SelectTrack,  1 },
            { code('G'), B::SelectTrack,  2 },
            { code('H'), B::SelectTrack,  3 },
            { code('J'), B::SelectTrack,  4 },
            { code('K'), B::SelectTrack,  5 },
            { code('L'), B::SelectTrack,  6 },
            { 59,        B::SelectTrack,  7 },  // ;
            { code('C'), B::SelectTrack,  8 },
            { code('V'), B::SelectTrack,  9 },
            { code('B'), B::SelectTrack, 10 },
            { code('N'), B::SelectTrack, 11 },
            { code('M'), B::SelectTrack, 12 },
            { 44,        B::SelectTrack, 13 },  // ,
            { 46,        B::SelectTrack, 14 },  // .
            { 47,        B::SelectTrack, 15 },  // /
        } };

        // Mute layer — applied when Mute (key Z) is held.
        constexpr std::array<Entry, 16> kMute = { {
            { code('D'), B::ToggleMute,  0 },
            { code('F'), B::ToggleMute,  1 },
            { code('G'), B::ToggleMute,  2 },
            { code('H'), B::ToggleMute,  3 },
            { code('J'), B::ToggleMute,  4 },
            { code('K'), B::ToggleMute,  5 },
            { code('L'), B::ToggleMute,  6 },
            { 59,        B::ToggleMute,  7 },  // ;
            { code('C'), B::ToggleMute,  8 },
            { code('V'), B::ToggleMute,  9 },
            { code('B'), B::ToggleMute, 10 },
            { code('N'), B::ToggleMute, 11 },
            { code('M'), B::ToggleMute, 12 },
            { 44,        B::ToggleMute, 13 },  // ,
            { 46,        B::ToggleMute, 14 },  // .
            { 47,        B::ToggleMute, 15 },  // /
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
        // Keys that sit immediately outside the 10-column grid.
        // Row 1 edge: ` (left of 1), - = (right of 0)
        // Row 2 edge: Tab (left of Q), [ ] (right of P)
        // Row 3 edge: ' (right of ;)  — CapsLock is OS-level, no standard code
        // Row 4 edge: none (/ is step 15)
        static constexpr int kEdge[] = {
            96,          // ` (backtick / grave)
            45, 61,      // -  =
            9,           // Tab
            91, 93,      // [  ]
            39,          // '
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

        // Column-1 modifier keys (structural scopes) always emit their own identity.
        if (keyCode == code('1')) { return { T::ButtonDown, B::Func,         -1, 0 }; }
        if (keyCode == code('Q')) { return { T::ButtonDown, B::TrackScope,   -1, 0 }; }
        if (keyCode == code('A')) { return { T::ButtonDown, B::PatternScope, -1, 0 }; }
        if (keyCode == code('Z')) { return { T::ButtonDown, B::MuteScope,    -1, 0 }; }

        // Column-2 modifier keys (performance scopes) emit their own identity.
        if (keyCode == code('2')) { return { T::ButtonDown, B::FillScope,   -1, 0 }; }
        if (keyCode == code('W')) { return { T::ButtonDown, B::CueScope,    -1, 0 }; }
        if (keyCode == code('S')) { return { T::ButtonDown, B::SceneScope,  -1, 0 }; }
        if (keyCode == code('X')) { return { T::ButtonDown, B::MasterScope, -1, 0 }; }

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
