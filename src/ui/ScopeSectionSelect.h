#pragma once

#include <cstdint>
#include <vector>

namespace lockstep
{
    // Ordered scope layer stack for section-parameter resolution (Part 4 uniform
    // overlay). Lower value = higher precedence. A section key resolves top-down:
    // the highest-precedence layer that owns params at that section wins; holding
    // a scope "peels" every layer above it (higher precedence / lower value).
    //
    // Only Machine and Track own per-section parameters today; the lower layers
    // are wired into the model so future per-scope section params slot in with no
    // rearchitecture (they currently produce no candidates → those keys read dim).
    enum class SecOrigin : std::uint8_t
    {
        Machine = 0,  // the machine engine's params (SRC/MOD/... it declares)
        Track   = 1,  // track DSP block: FLTR / AMP / CHANNEL+ENV / FX inserts
        Phrase  = 2,  // phrase length (LEN meta)
        Scene   = 3,  // scene TIME sticky
        Song    = 4,  // master FX (Song+FX) + song TIME sticky
        Global  = 5,  // transport globals (Func+7 TRSP meta)
    };

    // What a resolved section key *does* when pressed. Machine/Track param pages
    // page the machine schema; the section-stack table (SectionStackTable.h)
    // carries the non-param content that a held scope (or Func) maps a key onto.
    // Closed enum — every switch over it is exhaustive (no default:), so a new
    // action is a compile error until wired (PRINCIPLES §20).
    enum class SecAction : std::uint8_t
    {
        ParamSection = 0,  // page the machine/track param schema (selectSection)
        MetaSection  = 1,  // latch a masterSection meta page (selectMetaSection)
        TimeSticky   = 2,  // toggle the TIME/KEY sticky (applyTimeEntry)
    };

    // One candidate section group for a canonical section key, tagged with the
    // scope layer that owns it. Machine/Track param candidates are classified by
    // a single predicate the caller applies:
    //   origin = (firstSlot >= 0 && firstSlot < numParams(track)) ? Machine : Track
    // Track blocks always live at firstSlot >= numParams(track); extensions too.
    // Section-stack (meta/sticky) candidates carry action != ParamSection and a
    // metaIndex; they may be funcQualified (only visible while Func is held).
    struct SecCandidate
    {
        int sectionIdx = 0;
        int pageCount = 0;
        SecOrigin origin = SecOrigin::Machine;
        SecAction action = SecAction::ParamSection;
        int metaIndex = -1;          // masterSection content index (MetaSection only)
        bool funcQualified = false;  // true = only present while the Func layer is held
        const char* label = nullptr; // stack-row label (DIV/LEN/…); null ⇒ use section name
    };

    // Scope-aware page-list selection (P6, generalized in Part 4 / Item 7).
    //
    // `heldFloor` is the scope the user is holding; SecOrigin::Machine means
    // unqualified (no peel — fill top-down across the whole stack). Holding a
    // scope is a *floor*: peel every layer of higher precedence (lower value),
    // then take the highest-precedence origin that still has content, falling
    // through below the floor when the floor's own layer is empty:
    //   - Unqualified (Machine): the machine-owned groups win; a section the
    //     machine owns nothing at falls through to the track group (and, later,
    //     lower layers). Never appends the track group as an *extra* page when
    //     the machine does own the section — that double-append is exactly the
    //     "buried track AMP" bug.
    //   - Hold Track: the Machine layer is peeled; the track group wins if it
    //     has content, else the resolver falls through to Phrase/Scene/Song
    //     below it. A key is dim only when nothing *at or below* the floor has
    //     content (replaces the old "strict Track peel = always dim" rule).
    //
    // `funcLayer` selects the parallel Func hierarchy: candidates whose
    // funcQualified flag differs from `funcLayer` are removed first, so Func held
    // shows only func-qualified rows (COND/NOTE/TRSP) and Func released shows only
    // the primary stack. The peel then runs over what survives.
    //
    // Returns the candidates of the single highest-precedence origin that
    // survives filter+peel, in the input's display order. Empty ⇒ nothing
    // at/below the held scope (in the selected layer) owns this section (dim key).
    //
    // `all` is in display order (canonical group first, then extensions).
    inline std::vector<SecCandidate>
    selectScopeSections(const std::vector<SecCandidate>& all, SecOrigin heldFloor,
                        bool funcLayer = false)
    {
        const auto pr = [](SecOrigin o) { return static_cast<int>(o); };

        // Best (highest-precedence) origin among candidates that survive the
        // Func filter and are not peeled by the floor.
        bool found = false;
        SecOrigin best = SecOrigin::Global;
        for (const auto& c : all)
        {
            if (c.funcQualified != funcLayer) continue;  // wrong hierarchy
            if (pr(c.origin) < pr(heldFloor)) continue;  // peeled (above the floor)
            if (!found || pr(c.origin) < pr(best)) { best = c.origin; found = true; }
        }
        if (!found) return {};

        std::vector<SecCandidate> out;
        for (const auto& c : all)
            if (c.funcQualified == funcLayer && c.origin == best) out.push_back(c);
        return out;
    }
}
