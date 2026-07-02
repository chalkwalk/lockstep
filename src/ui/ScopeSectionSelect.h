#pragma once

#include <vector>

namespace lockstep
{
    // One candidate section group for a canonical section key, pre-classified as
    // machine-owned (the machine declares params at this section index) or
    // track-level (a track DSP block: FLTR / AMP / CHANNEL+ENV / FX inserts, or a
    // virtual extension section carrying a relocated track block).
    //
    // The classifier is a single predicate the caller applies:
    //   machineOwned = (firstSlot >= 0 && firstSlot < numParams(track))
    // Track blocks always live at firstSlot >= numParams(track); extensions too.
    struct SecCandidate
    {
        int sectionIdx = 0;
        int pageCount = 0;
        bool machineOwned = false;
    };

    // Scope-aware page-list selection (P6 / grab-bag items 6+7).
    //
    // The two scopes are mirror images with fallback:
    //   Track scope  → the track-level groups; if the section has no track block
    //                  at all (e.g. SRC/MOD on a synth), fall back to the machine
    //                  groups so the key still does something.
    //   Unqualified  → the machine-owned groups; if the machine owns nothing at
    //                  this section (e.g. a bare sampler under FILTER), fall back
    //                  to the track-level group. Never appends the track group as
    //                  an *extra* page when the machine does own the section — that
    //                  double-append is exactly the "buried track AMP" bug.
    //
    // `all` is in display order (canonical group first, then extensions).
    inline std::vector<SecCandidate>
    selectScopeSections(const std::vector<SecCandidate>& all, bool trackScope)
    {
        std::vector<SecCandidate> machine, track;
        for (const auto& c : all)
            (c.machineOwned ? machine : track).push_back(c);

        if (trackScope)
            return track.empty() ? machine : track;
        return machine.empty() ? track : machine;
    }
}
