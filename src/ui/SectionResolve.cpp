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

    // The section stack is one hierarchy, but the scope layers do not all mean
    // the same thing, so a single "peel everything above the floor" rule is wrong
    // (it silently dims Scene/Song scope-scoped param editing — the plan's audit
    // point). The layers resolve by kind:
    //   • Func      — parallel hierarchy: only the func stack rows (COND/NOTE/TRSP).
    //   • unqualified (Machine floor) — machine param page, else fall through to a
    //     track-DSP block (so bare FILTER on a sampler reads as the track filter);
    //     stack meta rows never surface unheld.
    //   • Track     — the P6 peel: the machine layer is hidden, track-DSP blocks
    //     win; Track+TRIG is the DIV meta.
    //   • Phrase/Scene/Song — write-target overlays over the *same* machine params:
    //     a stack meta/sticky row on the pressed key wins (LEN/TIME/FX); otherwise
    //     the machine param page shows where the per-scope policy permits it
    //     (ScopedSectionMatrix), coloured by the held scope (scene-scoped edit).
    SectionResolution resolveSectionKey(const LockstepProcessor& proc, int track,
                                        int canonicalKey, SecOrigin floor, bool funcLayer)
    {
        SectionResolution r;

        // Parallel Func hierarchy: the func stack row for this key, or dim.
        if (funcLayer)
        {
            for (const auto& row : kSectionStackTable)
                if (row.funcQualified && row.key == canonicalKey)
                {
                    r.hasContent = true;
                    r.winner = row.origin;
                    r.action = row.action;
                    r.metaIndex = row.metaIndex;
                    r.label = row.label;
                    return r;
                }
            return r;  // dim
        }

        // A held non-Machine scope with a stack meta/sticky row on this key: the
        // row wins (Track DIV, Phrase LEN, Scene/Song TIME, Song master FX).
        if (floor != SecOrigin::Machine)
        {
            for (const auto& row : kSectionStackTable)
                if (!row.funcQualified && row.origin == floor && row.key == canonicalKey)
                {
                    r.hasContent = true;
                    r.winner = row.origin;
                    r.action = row.action;
                    r.metaIndex = row.metaIndex;
                    r.label = row.label;
                    return r;
                }
        }

        const auto params = buildParamCandidates(proc, track, canonicalKey);

        if (floor == SecOrigin::Track)
        {
            // P6 peel: machine layer hidden, track-DSP blocks win; dim if none.
            for (const auto& c : params)
                if (c.origin == SecOrigin::Track)
                    r.groups.push_back(c);
            if (!r.groups.empty())
            {
                r.hasContent = true;
                r.winner = SecOrigin::Track;
                r.action = SecAction::ParamSection;
            }
            return r;
        }

        if (floor == SecOrigin::Machine)
        {
            // Unqualified: highest-precedence param wins (Machine, else track block).
            r.groups = selectScopeSections(params, SecOrigin::Machine, false);
            if (!r.groups.empty())
            {
                r.hasContent = true;
                r.winner = r.groups.front().origin;
                r.action = SecAction::ParamSection;
            }
            return r;
        }

        // Phrase / Scene / Song: machine params as a scope-scoped edit, but only
        // where the per-scope policy allows it. Coloured by the held scope.
        using PS = EditMode::PrimaryScope;
        const PS scope = (floor == SecOrigin::Phrase) ? PS::Phrase
                       : (floor == SecOrigin::Scene)  ? PS::Scene
                                                      : PS::Song;
        if (!params.empty() && scopedCell(scope, canonicalKey).hasContent)
        {
            r.groups = params;
            r.hasContent = true;
            r.winner = floor;   // colour the key/page by the write scope
            r.action = SecAction::ParamSection;
        }
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
