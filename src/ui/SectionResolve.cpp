#include "SectionResolve.h"
#include "mode/SectionStackTable.h"
#include "ScopedSectionMatrix.h"
#include "../PluginProcessor.h"
#include "../machine/IMachine.h"

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

    // The section stack is a true underlay (per the intended model): imagine each
    // scope's sections stacked from Global at the bottom up to the held scope on
    // top (Machine/unqualified is the topmost layer). A key shows the *topmost
    // non-empty layer at or below the held ceiling* — so unqualified TRIG, empty at
    // the Machine layer, falls through to the Track layer's DIV meta and shows DIV.
    // `selectScopeSections(all, floor)` does exactly this peel (drop layers above
    // the floor, take the shallowest that remains).
    //
    // Two content kinds populate the layers, and they fall through differently:
    //   • Meta/sticky/func rows (DIV/LEN/TIME/master-FX; COND/NOTE/TRSP) are
    //     self-targeting, so they fall through the whole stack freely.
    //   • Machine + track *param* pages fall through between themselves (bare
    //     FILTER on a sampler → the track filter block).
    //   • A scope-scoped machine-param edit (Scene+FILTER = the machine's filter,
    //     scene override) is PINNED to its held scope. The write scope is driven by
    //     the held modifier, so letting it fall through to a different scope would
    //     misroute the write (holding Phrase must not edit filter). It appears only
    //     when its own scope is the ceiling and the per-scope policy permits it.
    SectionResolution resolveSectionKey(const LockstepProcessor& proc, int track,
                                        int canonicalKey, SecOrigin floor, bool funcLayer)
    {
        std::vector<SecCandidate> all;

        // Every stack meta/sticky/func row on this key. selectScopeSections filters
        // the Func hierarchy (funcQualified == funcLayer) and peels by ceiling.
        appendSectionStackCandidates(all, canonicalKey);

        if (!funcLayer)
        {
            // Machine + track param pages (fall through Machine → Track).
            const auto params = buildParamCandidates(proc, track, canonicalKey);
            for (const auto& c : params)
                all.push_back(c);

            // Scope-scoped machine-param edit, pinned to the held scope so the
            // held-modifier-driven write can't misroute (Phrase can't edit filter).
            if (!params.empty())
            {
                using PS = EditMode::PrimaryScope;
                PS scope = PS::None;
                if (floor == SecOrigin::Phrase)     scope = PS::Phrase;
                else if (floor == SecOrigin::Scene) scope = PS::Scene;
                else if (floor == SecOrigin::Song)  scope = PS::Song;
                if (scope != PS::None && scopedCell(scope, canonicalKey).hasContent)
                    all.push_back({ canonicalKey, params.front().pageCount, floor,
                                    SecAction::ParamSection, -1, false, nullptr });
            }
        }

        SectionResolution r;
        r.groups = selectScopeSections(all, floor, funcLayer);
        if (r.groups.empty())
            return r;  // nothing at/below the ceiling on this key → dim

        r.hasContent = true;
        r.winner = r.groups.front().origin;
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
}
