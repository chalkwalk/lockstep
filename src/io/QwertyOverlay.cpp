#include "QwertyOverlay.h"
#include "../command/ButtonLayers.h"

#include <algorithm>
#include <array>

namespace lockstep
{
    namespace
    {
        using B = ControllerButton;

        struct Entry
        {
            int keyCode;
            B button;
            int index = -1;
        };

        constexpr int code(char c) { return static_cast<int>(c); }

        // Primary layer — base mapping before any layer remap.
        // Modifier-cluster keys (1/Q/A/Z, 2/W/S/X) are handled by resolve()
        // directly; only non-modifier keys appear here.
        // Layer remaps (Track/Mute/Func) are applied via resolveLayer()
        // from ButtonLayers.h — kFunc/kTrack/kMute tables are no longer needed.
        constexpr std::array<Entry, 32> kPrimary = { {
            // Row 1 utilities: TAP(3), NavUp(4)
            { code('3'), B::TapTempo, -1 },
            { code('4'), B::NavUp, -1 },

            // Section buttons (keys 5-0: TRIG/SRC/FLTR/AMP/MOD/FX — MHY rename)
            { code('5'), B::Section, 0 },
            { code('6'), B::Section, 1 },
            { code('7'), B::Section, 2 },
            { code('8'), B::Section, 3 },
            { code('9'), B::Section, 4 },
            { code('0'), B::Section, 5 },

            // Navigation (inverted-T: 4=Up above E=Left, R=Down, T=Right)
            { code('E'), B::NavLeft, -1 },
            { code('R'), B::NavDown, -1 },
            { code('T'), B::NavRight, -1 },

            // Right-utility verbs (MHY.4): Snapshot / Rec / Play / Clear / Yes
            // (Func layer remaps Y→Restore, O→VerbDelete via kLayerRemaps)
            { code('Y'), B::VerbYes, -1 },
            { code('U'), B::VerbRecord, -1 },
            { code('I'), B::VerbPlay, -1 },
            { code('O'), B::VerbClear, -1 },
            { code('P'), B::VerbNo, -1 },

            // Step grid row 1 (D-; = steps 0-7)
            // (Track layer → SelectTrack; Mute layer → ToggleMute via kLayerRemaps)
            { code('D'), B::Step, 0 },
            { code('F'), B::Step, 1 },
            { code('G'), B::Step, 2 },
            { code('H'), B::Step, 3 },
            { code('J'), B::Step, 4 },
            { code('K'), B::Step, 5 },
            { code('L'), B::Step, 6 },
            { 59, B::Step, 7 },  // ; = 59

            // Step grid row 2 (C-/ = steps 8-15)
            { code('C'), B::Step, 8 },
            { code('V'), B::Step, 9 },
            { code('B'), B::Step, 10 },
            { code('N'), B::Step, 11 },
            { code('M'), B::Step, 12 },
            { 44, B::Step, 13 },  // , = 44
            { 46, B::Step, 14 },  // . = 46
            { 47, B::Step, 15 },  // / = 47
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
        static constexpr std::array<int, 7> kEdge = {
            96,          // ` (backtick / grave)
            45,
            61,      // -  =
            9,           // Tab
            91,
            93,      // [  ]
            39,          // '
        };
        return std::any_of(kEdge.begin(), kEdge.end(),
                           [keyCode](int k) { return k == keyCode; });
    }

    ControllerEvent QwertyOverlay::resolve(int keyCode,  // NOLINT(readability-convert-member-functions-to-static)
                                           bool funcHeld,
                                           bool trackHeld,
                                           bool muteHeld) const
    {
        using T = ControllerEvent::Type;

        // MHY cluster identities always return their scope button unchanged.
        // Col 1 = Func / Phrase / Morph / Mute.
        if (keyCode == code('1')) { return { T::ButtonDown, B::Func, -1, 0 }; }
        if (keyCode == code('Q')) { return { T::ButtonDown, B::PhraseScope, -1, 0 }; }
        if (keyCode == code('A')) { return { T::ButtonDown, B::MorphScope, -1, 0 }; }
        if (keyCode == code('Z')) { return { T::ButtonDown, B::MuteScope, -1, 0 }; }

        // Col 2 = Track / Scene / Song / Fill.
        if (keyCode == code('2')) { return { T::ButtonDown, B::TrackScope, -1, 0 }; }
        if (keyCode == code('W')) { return { T::ButtonDown, B::SceneScope, -1, 0 }; }
        if (keyCode == code('S')) { return { T::ButtonDown, B::SongScope, -1, 0 }; }
        if (keyCode == code('X')) { return { T::ButtonDown, B::FillScope, -1, 0 }; }

        // Primary lookup, then a single resolveLayer() call handles all three
        // modifier layers (Track > Mute > Func priority is encoded in kLayerRemaps).
        ControllerEvent ev;
        if (!lookup(kPrimary, keyCode, ev)) { return {}; }  // unmapped key

        return resolveLayer(ev, { funcHeld, trackHeld, muteHeld });
    }
}
