#pragma once

#include <array>
#include "Song.h"          // kScenesPerSong, kPhrasesPerTrack
#include "HierarchyNav.h"  // resolveActivePhraseIdx

namespace lockstep
{
    // SHR:N share counts for the 5.3 Browser (DESIGN §23.2). For ONE track, how
    // many Scenes in a Song play each phrase row — the share relationship that
    // survived the Part→Kit dissolve.
    //
    // Scene s plays its diagonal row (row s is owned by Scene s) unless it deviates
    // the track to another row. Baked content is written onto the diagonal and the
    // deviation cleared, so it needs no special case here — it is simply the
    // diagonal row of its Scene. Pure, header-only, JUCE-free: unit-testable
    // without a processor (mirrors HierarchyNav.h).
    struct PhraseShare
    {
        std::array<int, kPhrasesPerTrack> count{};        // count[r] = scenes on row r
        std::array<int, kScenesPerSong>   effectiveRow{}; // effectiveRow[s] = row scene s plays
    };

    // Tally shares from each Scene's per-track deviation state. deviated[s] /
    // deviationRow[s] describe Scene s's deviation for the track in question (a
    // non-deviating scene plays its diagonal, so deviated[s] == false).
    [[nodiscard]] inline PhraseShare computePhraseShare(
        const std::array<bool, kScenesPerSong>& deviated,
        const std::array<int, kScenesPerSong>&  deviationRow) noexcept
    {
        PhraseShare out;
        for (int s = 0; s < kScenesPerSong; ++s)
        {
            const int r = resolveActivePhraseIdx(
                s, deviated[static_cast<std::size_t>(s)],
                deviationRow[static_cast<std::size_t>(s)], /*track*/ 0);
            out.effectiveRow[static_cast<std::size_t>(s)] = r;
            out.count[static_cast<std::size_t>(r)] += 1;
        }
        return out;
    }
}
