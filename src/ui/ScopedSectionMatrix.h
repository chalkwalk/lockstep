#pragma once

#include <array>
#include "../io/EditMode.h"
#include "../machine/IMachine.h"

namespace lockstep
{
    // Static lookup table for the scope-indexed section matrix (DESIGN §6.1.2, MHY).
    //
    // Each (scope, section) cell specifies:
    //   label      — the section-bar key label shown while that scope is held
    //   hasContent — whether anything is wired to this cell in the current build
    //
    // hasContent=false cells are shown dimmed (label visible but key disabled).
    // Content accretes per-milestone: ME → Track FLTR/AMP, MC → Part cells,
    // MI → Scene cells, MV → Track FX + Master FX. MHY only scaffolds the table.
    //
    // Scopes without a section suite (Mute, Fill, Trig, Section, Cue) return the
    // machine's own primary labels unchanged (no scope-override needed).

    struct ScopedCellInfo
    {
        const char* label      = nullptr;  // nullptr = use machine/canonical label
        bool        hasContent = false;
    };

    // Returns the cell info for the given (scope, section) pair.
    // For scopes without a section suite this returns {nullptr, true} so the
    // caller falls through to machine-label rendering.
    inline ScopedCellInfo scopedCell(EditMode::PrimaryScope scope, int section) noexcept
    {
        using PS = EditMode::PrimaryScope;

        // Section-scope matrix.  Rows = scope, columns = section index (0-5).
        // Labels match the plan §1 cell map; hasContent reflects current wiring.
        //
        //  idx: 0=TRIG  1=SRC        2=FLTR       3=AMP        4=MOD        5=FX
        // hasContent=true marks cells with planned content (will be wired per milestone);
        // hasContent=false marks cells intentionally empty (no planned content).
        // The section bar dims false cells and highlights true cells as available.

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kTrack = {{
            { "TRIG",   true  },   // per-track condition defaults (ME)
            { "SRC",    true  },   // input_source / Thru assignment (MR)
            { "FILTER", true  },   // post-machine FLTR (ME.6)
            { "AMP",    true  },   // post-machine AMP + sends (ME.7)
            { "MOD",    true  },   // per-track LFO (ME)
            { "FX",     true  },   // IEffect inserts 1+2 (MV)
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kPattern = {{
            { "LEN",  true  },   // length / scale lock → routes to TRACK meta (Length/Divider)
            { nullptr,false },   // dim — no content planned
            { nullptr,false },   // dim — no content planned
            { nullptr,false },   // GAIN — pattern output gain not yet implemented
            { nullptr,false },   // TMPO — per-pattern tempo not yet implemented
            { nullptr,false },   // dim — no content planned
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kPart = {{
            { "TRIG",   true  },   // trig templates (MC)
            { "MACH",   true  },   // machine select — Part+SRC (MC wires the full flow)
            { "FILTER", true  },   // part-base FLTR (MC)
            { "AMP",    true  },   // part-base AMP (MC)
            { "MOD",    true  },   // part-base MOD (MC)
            { "FX",     true  },   // part-base FX (MC)
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kScene = {{
            { nullptr,false },   // CXFD — crossfader curve (MI, not yet implemented)
            { nullptr,false },   // SRC — scene-assign SRC (MI, not yet implemented)
            { nullptr,false },   // FLTR — scene-assign FLTR (MI, not yet implemented)
            { nullptr,false },   // AMP — scene-assign AMP (MI, not yet implemented)
            { nullptr,false },   // MOD — scene-assign MOD (MI, not yet implemented)
            { nullptr,false },   // FX — scene-assign FX (MI, not yet implemented)
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kMaster = {{
            { nullptr,false },   // dim — no content planned
            { nullptr,false },   // dim — no content planned
            { nullptr,false },   // FLTR — master FLTR (MV, not yet implemented)
            { nullptr,false },   // AMP — master gain + sends (MV, not yet implemented)
            { nullptr,false },   // dim — no content planned
            { nullptr,false },   // FX — master FX 1+2 (MV, not yet implemented)
        }};

        if (section < 0 || section >= IMachine::kMaxSections)
            return {};

        switch (scope)
        {
            case PS::Track:   return kTrack  [static_cast<std::size_t>(section)];
            case PS::Pattern: return kPattern[static_cast<std::size_t>(section)];
            case PS::Part:    return kPart   [static_cast<std::size_t>(section)];
            case PS::Scene:   return kScene  [static_cast<std::size_t>(section)];
            case PS::Master:  return kMaster [static_cast<std::size_t>(section)];

            // Performance specialists and non-section scopes: no label override.
            case PS::None:
            case PS::Func:
            case PS::Trig:
            case PS::Mute:
            case PS::Fill:
            case PS::Cue:
            case PS::Section:
                break;
        }
        return { nullptr, true };  // use machine label, treat as available
    }
}
