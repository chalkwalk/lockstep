#include "QwertyOverlay.h"

#include <array>

namespace lockstep
{
    namespace
    {
        using A = QwertyOverlay::Action;

        struct Entry
        {
            int keyCode;
            A action;
            int stepIndex;
        };

        constexpr int code(char c) { return static_cast<int>(c); }

        constexpr std::array<Entry, 33> kTable = { {
            { code('1'), A::Shift,           -1 },

            { code('2'), A::NavUp,           -1 },
            { code('Q'), A::NavLeft,         -1 },
            { code('W'), A::NavDown,         -1 },
            { code('E'), A::NavRight,        -1 },

            { code('A'), A::Step,             0 },
            { code('S'), A::Step,             1 },
            { code('D'), A::Step,             2 },
            { code('F'), A::Step,             3 },
            { code('G'), A::Step,             4 },
            { code('H'), A::Step,             5 },
            { code('J'), A::Step,             6 },
            { code('K'), A::Step,             7 },

            { code('Z'), A::Step,             8 },
            { code('X'), A::Step,             9 },
            { code('C'), A::Step,            10 },
            { code('V'), A::Step,            11 },
            { code('B'), A::Step,            12 },
            { code('N'), A::Step,            13 },
            { code('M'), A::Step,            14 },
            { code(','), A::Step,            15 },

            { code('3'), A::SelectSection,    0 },
            { code('4'), A::SelectSection,    1 },
            { code('5'), A::SelectSection,    2 },
            { code('6'), A::SelectSection,    3 },
            { code('7'), A::SelectSection,    4 },
            { code('8'), A::SelectSection,    5 },

            { code('R'), A::RecordArm,       -1 },
            { code('T'), A::TapTempo,        -1 },
            { code('Y'), A::Copy,            -1 },
            { code('U'), A::Paste,           -1 },
            { code('I'), A::Clear,           -1 },

            { code(' '), A::PlayStop,        -1 },
        } };
    }

    // Kept as a non-static member so user-defined remapping state can land
    // here in M6 without churning call sites.
    QwertyOverlay::Mapping QwertyOverlay::resolve(int keyCode) const  // NOLINT(readability-convert-member-functions-to-static)
    {
        for (const auto& e : kTable)
        {
            if (e.keyCode == keyCode)
            {
                return { e.action, e.stepIndex };
            }
        }
        return {};
    }
}
