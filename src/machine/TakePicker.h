#pragma once

#include "SamplePool.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace lockstep
{
    // The 6/5 take-group picker rule (DESIGN §40.7).
    //
    // A promoted deck take is N sub-track WAVs + a downmix, all sharing one
    // takeGroupId (§40.7 / promoteDeckTake). How a picker PRESENTS them depends on
    // the picking machine's class:
    //
    //   * A sample-class picker (Sampler, Slicer, Player, Stream) has one place to
    //     put audio, so it sees the MEMBERS only — each non-empty sub-track plus the
    //     downmix. A four-track take reads as up to FIVE ordinary samples.
    //   * A deck-class picker (Loop, Tape) can load the whole take onto its
    //     sub-tracks, so it sees the group as ONE ENTITY plus its members — up to
    //     SIX. Picking the group loads all sub-tracks; picking a member loads one.
    //
    // Plain (non-group) pool entries are a single Sample row either way. This
    // function is pure over the pool + an acceptance predicate, so it is unit-tested
    // and shared by every picker surface (the live menu, a controller display).

    struct TakePickerRow
    {
        enum class Kind { Sample, TakeGroup };
        Kind kind = Kind::Sample;
        int poolIndex = -1;        // Sample: the entry. TakeGroup: its downmix (the
                                   // representative the pick loads from / labels by).
        std::uint32_t groupId = 0; // 0 for a plain sample
        juce::String label;
    };

    // `accepted(i)` = the machine's sample-class filter (sampleAcceptedByTrack).
    // `deckClass` = the picking machine's isDeckClass().
    [[nodiscard]] inline std::vector<TakePickerRow>
    buildTakePickerRows(const SamplePool& pool,
                        const std::function<bool(int)>& accepted,
                        bool deckClass)
    {
        std::vector<TakePickerRow> rows;
        const int n = pool.size();

        // Track which groups we have already emitted the group-entity row for, so a
        // group contributes exactly one entity (deck-class) and then its members.
        std::vector<std::uint32_t> groupsSeen;
        const auto groupEmitted = [&](std::uint32_t g) {
            return std::find(groupsSeen.begin(), groupsSeen.end(), g) != groupsSeen.end();
        };

        for (int i = 0; i < n; ++i)
        {
            if (! accepted(i)) continue;
            const auto* s = pool.get(i);
            if (s == nullptr) continue;

            const std::uint32_t g = s->takeGroupId;
            if (g == 0)
            {
                // A plain sample.
                rows.push_back({ TakePickerRow::Kind::Sample, i, 0,
                                 pool.displayName(i) });
                continue;
            }

            // A take-group member. On the first member of a group seen, a deck-class
            // picker emits the group entity first (represented by the group's
            // downmix if we can find it, else this member).
            if (deckClass && ! groupEmitted(g))
            {
                groupsSeen.push_back(g);
                int rep = i;
                for (int j = 0; j < n; ++j)
                {
                    const auto* c = pool.get(j);
                    if (c != nullptr && c->takeGroupId == g && c->takeMember == 0) { rep = j; break; }
                }
                rows.push_back({ TakePickerRow::Kind::TakeGroup, rep, g,
                                 "Take " + juce::String(g) });
            }

            // The member itself (both classes). Label by member: 0 = mix, k = sub k.
            const juce::String member = s->takeMember == 0
                                            ? juce::String("mix")
                                            : ("sub " + juce::String(s->takeMember));
            rows.push_back({ TakePickerRow::Kind::Sample, i, g,
                             "Take " + juce::String(g) + " " + member });
        }
        return rows;
    }
}
