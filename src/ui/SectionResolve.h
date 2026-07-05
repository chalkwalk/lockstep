#pragma once

#include <vector>
#include "ScopeSectionSelect.h"
#include "../io/EditMode.h"  // EditMode::PrimaryScope

namespace lockstep
{
    class LockstepProcessor;
    struct UiState;

    // Item 7 — the single section-key resolver. One function answers "what does
    // this canonical section key do, on this track, under this held scope +
    // Func layer?" for every consumer: the editor dispatch, the section-bar
    // painter, and the manipulation-zone banner. They can no longer disagree.
    //
    // It unions two candidate sources — the machine/track *param* sections
    // derived live from the machine schema (buildParamCandidates, the exact
    // classification KeyboardArea::sectionsForKey uses) and the static
    // section-stack table (meta bands + TIME/KEY sticky) — then picks the layer
    // NEAREST to the held ceiling (ties toward the deeper scope). Params have no
    // per-scope layer (OEB: step-override ELSE track-base), so a held scope only
    // sets the ceiling; it never adds a scoped-param candidate.

    struct SectionResolution
    {
        bool hasContent = false;                 // false ⇒ dim / inert key
        SecOrigin winner = SecOrigin::Machine;   // scope layer that won
        SecAction action = SecAction::ParamSection;
        int metaIndex = -1;                      // masterSection index (MetaSection)
        const char* label = nullptr;             // stack-row label; null ⇒ use section name
        std::vector<SecCandidate> groups;        // winning-origin candidates, display order
    };

    // Machine/Track param candidates for one canonical key, in display order
    // (canonical group first, then matching extensions). Machine-owned iff the
    // first slot sits inside the machine's param range; track blocks / virtual
    // extensions live past numParams(). Shared by sectionsForKey and the resolver
    // so both classify identically.
    std::vector<SecCandidate> buildParamCandidates(const LockstepProcessor& proc,
                                                   int track, int canonicalKey);

    // Full resolution: param candidates + section-stack rows, peeled by floor +
    // func layer. `floor` is the held scope's stack layer (Machine = unqualified).
    SectionResolution resolveSectionKey(const LockstepProcessor& proc, int track,
                                        int canonicalKey, SecOrigin floor, bool funcLayer);

    // Map a section-suite PrimaryScope to its stack floor. None ⇒ Machine
    // (unqualified). Morph has no stack floor (the editor keeps its bespoke
    // branch) and must not be passed here.
    SecOrigin sectionFloorForScope(EditMode::PrimaryScope scope) noexcept;

    // 9.22 — the section-row resolution mode for the current UiState: which stack
    // floor to resolve at, and whether the Func-meta hierarchy (COND/NOTE + the
    // Func+7 TRSP shortcut) or the primary layer is active. One place owns the
    // Func-promotion rule so every consumer (painter, dispatch, MZ banner) agrees:
    //   • Func + Song  → { Global, funcLayer=false }  — Global scope (promoted).
    //   • Func + other/none → { Machine, funcLayer=true } — bare Func step metas.
    //   • no Func → { sectionFloorForScope(held scope), funcLayer=false }.
    // Promotion is Global-only for now (the ladder is a future extension). Morph is
    // bespoke and handled by the caller's own branch before this is consulted.
    struct SectionResolveMode
    {
        SecOrigin floor = SecOrigin::Machine;
        bool funcLayer = false;
    };
    SectionResolveMode sectionResolveMode(const UiState& ui) noexcept;
}
