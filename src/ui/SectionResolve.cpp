#include "SectionResolve.h"
#include "mode/SectionStackTable.h"
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

    SectionResolution resolveSectionKey(const LockstepProcessor& proc, int track,
                                        int canonicalKey, SecOrigin floor, bool funcLayer)
    {
        std::vector<SecCandidate> all = buildParamCandidates(proc, track, canonicalKey);

        // The section-stack meta/sticky rows are *scope gestures*, not part of the
        // unqualified top-down browse: a bare section key shows the machine/track
        // param page or nothing, never a scope's meta band. So append the stack
        // rows only when a scope is held (floor != Machine) or the Func layer is
        // active. Below the floor they still fall through (Scene+FX → Song FX);
        // the Func filter inside selectScopeSections then keeps the right hierarchy.
        if (funcLayer || floor != SecOrigin::Machine)
            appendSectionStackCandidates(all, canonicalKey);

        SectionResolution r;
        r.groups = selectScopeSections(all, floor, funcLayer);
        if (r.groups.empty())
            return r;  // hasContent stays false

        r.hasContent = true;
        r.winner = r.groups.front().origin;
        r.action = r.groups.front().action;
        r.metaIndex = r.groups.front().metaIndex;
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
