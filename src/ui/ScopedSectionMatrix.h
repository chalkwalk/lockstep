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
        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kTrack = {{
            { "TRIG", false },   // per-track condition defaults (ME)
            { "SRC",  false },   // input_source / Thru assignment (MR)
            { "FLTR", false },   // post-machine FLTR (ME.6)
            { "AMP",  false },   // post-machine AMP + sends (ME.7)
            { "MOD",  false },   // per-track LFO (ME)
            { "FX",   false },   // IEffect inserts 1+2 (MV)
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kPattern = {{
            { "LEN",  false },   // length / scale lock (MC)
            { nullptr,false },   // dim (no content planned)
            { nullptr,false },   // dim
            { "GAIN", false },   // pattern output gain (MC)
            { "TMPO", false },   // tempo + chain queue (MC)
            { nullptr,false },   // dim
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kPart = {{
            { "TRIG", false },   // trig templates (MC)
            { "MACH", false },   // machine select — Part+SRC (MC wires the full flow)
            { "FLTR", false },   // part-base FLTR (MC)
            { "AMP",  false },   // part-base AMP (MC)
            { "MOD",  false },   // part-base MOD (MC)
            { "FX",   false },   // part-base FX (MC)
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kScene = {{
            { "CXFD", false },   // crossfader curve / scene rename (MI)
            { "SRC",  false },   // scene-assign SRC (MI)
            { "FLTR", false },   // scene-assign FLTR (MI)
            { "AMP",  false },   // scene-assign AMP (MI)
            { "MOD",  false },   // scene-assign MOD (MI)
            { "FX",   false },   // scene-assign FX (MI)
        }};

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kMaster = {{
            { nullptr,false },   // dim
            { nullptr,false },   // dim
            { "FLTR", false },   // master FLTR (MV)
            { "AMP",  false },   // master gain + sends (MV)
            { nullptr,false },   // dim
            { "FX",   false },   // master FX 1+2 (MV)
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
