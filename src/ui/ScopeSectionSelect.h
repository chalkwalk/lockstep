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
        Phrase  = 2,  // (reserved — no section params yet)
        Scene   = 3,  // (reserved)
        Song    = 4,  // (reserved — master FX keeps its own Song+FX access)
        Global  = 5,  // (reserved)
    };

    // One candidate section group for a canonical section key, tagged with the
    // scope layer that owns it. The classifier is a single predicate the caller
    // applies:
    //   origin = (firstSlot >= 0 && firstSlot < numParams(track)) ? Machine : Track
    // Track blocks always live at firstSlot >= numParams(track); extensions too.
    struct SecCandidate
    {
        int sectionIdx = 0;
        int pageCount = 0;
        SecOrigin origin = SecOrigin::Machine;
    };

    // Scope-aware page-list selection (P6, generalized in Part 4).
    //
    // `heldFloor` is the scope the user is holding; SecOrigin::Machine means
    // unqualified (no peel — fill top-down across the whole stack). Holding a
    // scope peels every layer of higher precedence than it:
    //   - Unqualified (Machine): the machine-owned groups win; a section the
    //     machine owns nothing at falls through to the track group (and, later,
    //     lower layers). Never appends the track group as an *extra* page when
    //     the machine does own the section — that double-append is exactly the
    //     "buried track AMP" bug.
    //   - Hold Track: the Machine layer is peeled; the track groups win. A
    //     section with no track block (e.g. SRC/MOD on a synth) yields an empty
    //     list → the key is inert / dim (strict "track sections only").
    //
    // Returns the candidates of the single highest-precedence origin that
    // survives the peel, in the input's display order. Empty ⇒ nothing at/below
    // the held scope owns this section (dim key).
    //
    // `all` is in display order (canonical group first, then extensions).
    inline std::vector<SecCandidate>
    selectScopeSections(const std::vector<SecCandidate>& all, SecOrigin heldFloor)
    {
        const auto pr = [](SecOrigin o) { return static_cast<int>(o); };

        // Best (highest-precedence) origin among candidates not peeled by the floor.
        bool found = false;
        SecOrigin best = SecOrigin::Global;
        for (const auto& c : all)
        {
            if (pr(c.origin) < pr(heldFloor)) continue;  // peeled (above the floor)
            if (!found || pr(c.origin) < pr(best)) { best = c.origin; found = true; }
        }
        if (!found) return {};

        std::vector<SecCandidate> out;
        for (const auto& c : all)
            if (c.origin == best) out.push_back(c);
        return out;
    }
}
