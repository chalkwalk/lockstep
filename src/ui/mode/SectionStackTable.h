#pragma once

#include <array>
#include "../ScopeSectionSelect.h"  // SecOrigin, SecAction

namespace lockstep
{
    // Item 7 — the static half of the section stack.
    //
    // Machine/Track *param* sections are derived live from each machine's schema
    // (SectionResolve / KeyboardArea::sectionsForKey classify a section as
    // Machine- or Track-owned by whether its first slot sits inside the machine's
    // param range). This table carries the rest: the meta bands and TIME/KEY
    // sticky that a *held scope* (or the Func layer) maps a canonical section key
    // onto. Together they are one hierarchy; the resolver appends these rows to
    // the schema-derived candidates and runs the same scope peel over all of them.
    //
    // Row semantics:
    //   origin        — which scope layer owns this chord (peel precedence).
    //   key           — canonical section index 0-5 (TRIG/SRC/FILTER/AMP/MOD/FX).
    //   label         — section-bar label shown while the chord resolves here.
    //   action        — MetaSection latches masterSection=metaIndex; TimeSticky
    //                   toggles the TIME/KEY page (metaIndex unused, -1).
    //   funcQualified — true rows exist only while the Func layer is held; they
    //                   are the parallel Func hierarchy (COND/NOTE/TRSP).
    //
    // masterSection content indices (see resolveMetaBand / metaContentExists):
    //   0 Cond  1 Trig/NOTE  2 Transport  3 Divider  4 PhraseLen  5 Global(FX)
    struct SectionStackRow
    {
        SecOrigin origin;
        int key;
        const char* label;
        SecAction action;
        int metaIndex;
        bool funcQualified;
    };

    // Display/scan order is precedence-agnostic; the resolver sorts by peel.
    inline constexpr std::array<SectionStackRow, 8> kSectionStackTable = { {
        // Primary hierarchy (Func not held).
        { SecOrigin::Track,  0, "DIV",  SecAction::MetaSection, 3, false },
        { SecOrigin::Phrase, 0, "LEN",  SecAction::MetaSection, 4, false },
        { SecOrigin::Scene,  0, "TIME", SecAction::TimeSticky, -1, false },
        { SecOrigin::Song,   0, "TIME", SecAction::TimeSticky, -1, false },
        { SecOrigin::Song,   5, "FX",   SecAction::MetaSection, 5, false },
        // Parallel Func hierarchy (Func held): COND/NOTE on the step/note pages,
        // TRSP = transport globals (Func+7 in README §5.8).
        { SecOrigin::Machine, 0, "COND", SecAction::MetaSection, 0, true },
        { SecOrigin::Machine, 1, "NOTE", SecAction::MetaSection, 1, true },
        { SecOrigin::Global,  2, "TRSP", SecAction::MetaSection, 2, true },
    } };

    // Append this table's candidates for one canonical key onto `out` (a
    // SecCandidate vector already holding the schema-derived Machine/Track
    // groups). pageCount is 1 — meta/sticky pages are single-view. The resolver
    // then runs selectScopeSections(out, floor, funcLayer) over the union.
    inline void appendSectionStackCandidates(std::vector<SecCandidate>& out, int key)
    {
        for (const auto& row : kSectionStackTable)
            if (row.key == key)
                out.push_back({ row.key, 1, row.origin, row.action, row.metaIndex,
                                row.funcQualified });
    }
}
