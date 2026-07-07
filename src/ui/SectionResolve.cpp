#include "SectionResolve.h"
#include "mode/SectionStackTable.h"
#include "../PluginProcessor.h"
#include "../machine/IMachine.h"
#include "../state/UiState.h"
#include "../command/ScopePriority.h"

#include <algorithm>

namespace lockstep
{
    std::vector<SecCandidate> buildParamCandidates(const LockstepProcessor& proc,
                                                   int track, int canonicalKey)
    {
        std::vector<SecCandidate> candidates;
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return candidates;

        const int mnp = proc.numParams(track);
        const auto originOf = [mnp](const SectionInfo& info) {
            return (info.firstSlot >= 0 && info.firstSlot < mnp) ? SecOrigin::Machine
                                                                 : SecOrigin::Track;
        };

        // Canonical section first.
        {
            const auto info = proc.section(track, canonicalKey);
            if (info.firstSlot >= 0)
                candidates.push_back({ canonicalKey, std::max(1, info.pageCount),
                                       originOf(info), SecAction::ParamSection, -1, false });
        }

        // Extension sections: indices >= kMaxSections whose parentCanonical matches.
        const int total = proc.numSections(track);
        for (int s = IMachine::kMaxSections; s < total; ++s)
        {
            const auto info = proc.section(track, s);
            if (info.parentCanonical == canonicalKey && info.firstSlot >= 0)
                candidates.push_back({ s, std::max(1, info.pageCount), originOf(info),
                                       SecAction::ParamSection, -1, false });
        }
        return candidates;
    }

    // The section stack is a true underlay: each scope owns a layer of the section
    // row, stacked Machine (top) → Track → Phrase → Scene → Song → Global (bottom).
    // Holding a scope sets a *ceiling*; pressing a section resolves to the content
    // NEAREST to that ceiling — the canonical, taught access. Empty at the held
    // scope, a key falls through to the nearest populated layer (up OR down) as a
    // convenience + colour-teaching aid; every key is coloured by the scope its
    // content TRULY comes from.
    //
    // NEAREST, ties toward deeper. The ceiling's own layer is distance 0, so a
    // canonical chord (Track+TRIG=DIV, Song+FX=masterFX) is always exactly right.
    // Off-ceiling keys fall to min |origin - floor|; a tie (equidistant above and
    // below) breaks toward the deeper scope (larger SecOrigin value).
    //
    // Load-bearing data-model fact (see plan / CLAUDE.md OEB rule): params have
    // only two layers — step-override ELSE track-base. Holding a scope NEVER
    // changes the write target, so there is no per-scope param layer. The
    // candidate set therefore carries only real content: the machine param page,
    // the track-DSP param page, and the meta/sticky/func stack rows. No
    // scoped-param pinning, no ScopedSectionMatrix — a "scene filter" would paint a
    // colour for an edit that does not exist.
    SectionResolution resolveSectionKey(const LockstepProcessor& proc, int track,
                                        int canonicalKey, SecOrigin floor, bool funcLayer,
                                        bool stepHeld)
    {
        std::vector<SecCandidate> all;

        // Every stack meta/sticky/func row on this key (self-targeting content).
        appendSectionStackCandidates(all, canonicalKey);

        // Machine + track param pages — the only real param content (Machine iff
        // the first slot is inside the machine's range, else Track).
        if (!funcLayer)
            for (const auto& c : buildParamCandidates(proc, track, canonicalKey))
                all.push_back(c);

        const auto pr = [](SecOrigin o) { return static_cast<int>(o); };
        const auto dist = [&](SecOrigin o) {
            const int d = pr(o) - pr(floor);
            return d < 0 ? -d : d;
        };

        // Candidate eligibility (9.22). A candidate must match the Func hierarchy,
        // and Global content is *floor-only* in the primary layer: the master-bus /
        // transport-globals layer (TRSP, reached by Func+Song = Global) is a
        // destination you land on AT the Global ceiling, never something a shallower
        // scope bleeds up into. Without this, Scene/Song+FILTER would fall *down* to
        // the Global TRSP row on the FILTER key (its legacy Func+7 slot) and hijack
        // the machine filter — flipping the 9.21 Scene+FILTER behaviour. Global stays
        // reachable via the Func+7 shortcut through the func-meta layer (funcLayer),
        // where this floor gate does not apply.
        const auto eligible = [&](const SecCandidate& c) {
            // Held-step promotion (9.26): the stepQualified COND row exists only in
            // the primary layer while a step is held. It has funcQualified=false, so
            // it also passes the Func filter below when funcLayer is false.
            if (c.stepQualified && !(stepHeld && !funcLayer)) return false;
            if (c.funcQualified != funcLayer) return false;  // wrong hierarchy (Func)
            if (!funcLayer && c.origin == SecOrigin::Global && floor != SecOrigin::Global)
                return false;  // Global is floor-only in the primary layer
            return true;
        };

        // Winner = nearest origin to the ceiling; ties toward the deeper scope.
        bool found = false;
        SecOrigin best = SecOrigin::Machine;
        int bestDist = 0;
        for (const auto& c : all)
        {
            if (!eligible(c)) continue;
            const int d = dist(c.origin);
            if (!found || d < bestDist || (d == bestDist && pr(c.origin) > pr(best)))
            {
                best = c.origin;
                bestDist = d;
                found = true;
            }
        }

        SectionResolution r;
        if (!found)
            return r;  // no layer anywhere owns this key → dim (rare)

        r.hasContent = true;
        r.winner = best;
        for (const auto& c : all)
            if (eligible(c) && c.origin == best)
                r.groups.push_back(c);

        r.action = r.groups.front().action;
        r.metaIndex = r.groups.front().metaIndex;
        r.label = r.groups.front().label;
        return r;
    }

    SecOrigin sectionFloorForScope(EditMode::PrimaryScope scope) noexcept
    {
        using PS = EditMode::PrimaryScope;
        switch (scope)
        {
            case PS::Track:  return SecOrigin::Track;
            case PS::Phrase: return SecOrigin::Phrase;
            case PS::Scene:  return SecOrigin::Scene;
            case PS::Song:   return SecOrigin::Song;

            // Unqualified and every non-stack scope resolve top-down from Machine.
            // Morph is bespoke (caller handles it) but map to Machine defensively.
            case PS::None:
            case PS::Func:
            case PS::Morph:
            case PS::Trig:
            case PS::Mute:
            case PS::Fill:
            case PS::Cue:
            case PS::Section:
                break;
        }
        return SecOrigin::Machine;
    }

    SectionResolveMode sectionResolveMode(const UiState& ui) noexcept
    {
        // Func-promotion rule (9.22), single owner. Global-only for now: Func+Song
        // promotes to the Global scope (primary layer, floor = Global). Func over
        // any other scope (or none) is the bare Func-meta hierarchy. No Func is the
        // plain held scope. Morph is bespoke — the caller gates it out first.
        // Held-step promotion (9.26) rides the primary layer only; the resolver
        // gates it further (stepQualified && !funcLayer), so it is inert under Func.
        if (ui.funcHeld && ui.songHeld && !ui.morphHeld)
            return { SecOrigin::Global, /*funcLayer*/ false, ui.stepHeld };
        if (ui.funcHeld)
            return { SecOrigin::Machine, /*funcLayer*/ true, ui.stepHeld };
        return { sectionFloorForScope(firstHeldSectionSuiteScope(ui)),
                 /*funcLayer*/ false, ui.stepHeld };
    }
}
