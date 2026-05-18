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

            // Verb and utility row (T=PlayStop, Y/U/I=verbs, O=TapTempo)
            { code('T'), B::PlayStop,    -1 },
            { code('Y'), B::VerbRecord,  -1 },
            { code('U'), B::VerbPlay,    -1 },
            { code('I'), B::VerbStop,    -1 },
            { code('O'), B::TapTempo,    -1 },

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
        constexpr std::array<Entry, 14> kFunc = { {
            // Func+2(Rec)=snapshot(Yes), Func+T(Ply)=restore(No)
            { code('2'), B::Yes,               -1 },
            { code('T'), B::No,                -1 },

            // Func+<(W)=No, Func+>(R)=Yes for general confirmations
            { code('W'), B::No,                -1 },
            { code('R'), B::Yes,               -1 },

            // Func+E(down)=stop+reset (temporary home until MD nav-secondary pass)
            { code('E'), B::StopReset,         -1 },

            // Meta sections (keys 4-9)
            { code('4'), B::MetaSection,        0 },
            { code('5'), B::MetaSection,        1 },
            { code('6'), B::MetaSection,        2 },
            { code('7'), B::MetaSection,        3 },
            { code('8'), B::MetaSection,        4 },
            { code('9'), B::MetaSection,        5 },

            // Trig grid mode chords (held = mode active)
            { code('Y'), B::TrigModeKeyboard,  -1 },
            { code('U'), B::TrigModeRetrig,    -1 },
            { code('I'), B::TrigModeSoundPool, -1 },
        } };

        // Track layer — applied when Track (key Q) is held.
        constexpr std::array<Entry, 8> kTrack = { {
            { code('S'), B::SelectTrack, 0 },
            { code('D'), B::SelectTrack, 1 },
            { code('F'), B::SelectTrack, 2 },
            { code('G'), B::SelectTrack, 3 },
            { code('H'), B::SelectTrack, 4 },
            { code('J'), B::SelectTrack, 5 },
            { code('K'), B::SelectTrack, 6 },
            { code('L'), B::SelectTrack, 7 },
        } };

        // Mute layer — applied when Mute (key A) is held.
        constexpr std::array<Entry, 8> kMute = { {
            { code('S'), B::ToggleMute, 0 },
            { code('D'), B::ToggleMute, 1 },
            { code('F'), B::ToggleMute, 2 },
            { code('G'), B::ToggleMute, 3 },
            { code('H'), B::ToggleMute, 4 },
            { code('J'), B::ToggleMute, 5 },
            { code('K'), B::ToggleMute, 6 },
            { code('L'), B::ToggleMute, 7 },
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
