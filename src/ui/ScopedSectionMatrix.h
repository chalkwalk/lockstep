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
        const char* label = nullptr;  // nullptr = use machine/canonical label
        bool hasContent = false;
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

        using N = IMachine;  // alias for terse kCanonicalSectionNames access
        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kTrack = { {
            { "DIV", true },   // Track+TRIG: kit divider (DESIGN §6.2)
            { N::kCanonicalSectionNames[1], true },   // SRC
            { N::kCanonicalSectionNames[2], true },   // FILTER
            { N::kCanonicalSectionNames[3], true },   // AMP
            { N::kCanonicalSectionNames[4], true },   // MOD
            { N::kCanonicalSectionNames[5], true },   // FX
        } };

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kPhrase = { {
            { "LEN", true },   // Phrase+TRIG: phrase length (per active phrase)
            { nullptr, false },   // dim — no content planned
            { nullptr, false },   // dim — no content planned
            { nullptr, false },   // GAIN — pattern output gain not yet implemented
            { nullptr, false },   // TMPO — per-pattern tempo not yet implemented
            { nullptr, false },   // dim — no content planned
        } };

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kScene = { {
            { N::kCanonicalSectionNames[0], true },   // TRIG templates (MC)
            { nullptr, true },   // SRC — picker lives on Func+Part (MHZ.3.5)
            { N::kCanonicalSectionNames[2], true },   // FILTER
            { N::kCanonicalSectionNames[3], true },   // AMP
            { N::kCanonicalSectionNames[4], true },   // MOD
            { N::kCanonicalSectionNames[5], true },   // FX
        } };

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kMorph = { {
            { nullptr, false },   // TRIG — Morph never affects trigs (DESIGN §17.2)
            { N::kCanonicalSectionNames[1], true },   // SRC
            { "FLTR", true },   // morph-assign FLTR (abbreviated, 5.2)
            { N::kCanonicalSectionNames[3], true },   // AMP
            { N::kCanonicalSectionNames[4], true },   // MOD
            { N::kCanonicalSectionNames[5], true },   // FX
        } };

        static constexpr std::array<ScopedCellInfo, IMachine::kMaxSections> kSong = { {
            { nullptr, false },   // dim — no content planned
            { nullptr, false },   // dim — no content planned
            { nullptr, false },   // FLTR — master FLTR (MV, not yet implemented)
            { nullptr, false },   // AMP — master gain + sends (MV, not yet implemented)
            { nullptr, false },   // dim — no content planned
            { "GLBL", true },   // GLOBAL meta: output gain / sync / clock (DESIGN §6.2)
        } };

        if (section < 0 || section >= IMachine::kMaxSections)
            return {};

        switch (scope)
        {
            case PS::Track:  return kTrack[static_cast<std::size_t>(section)];
            case PS::Phrase: return kPhrase[static_cast<std::size_t>(section)];
            case PS::Scene:  return kScene[static_cast<std::size_t>(section)];
            case PS::Morph:  return kMorph[static_cast<std::size_t>(section)];
            case PS::Song:   return kSong[static_cast<std::size_t>(section)];

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
