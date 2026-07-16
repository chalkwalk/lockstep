// PhraseShareTest — SHR:N share-count core for the 5.3 Browser (DESIGN §23.2,
// src/core/PhraseShare.h). Pins the diagonal + deviation tally: how many Scenes
// play each phrase row of a track. Pure, so testable without a processor.

#include "TestHarness.h"
#include "../src/core/PhraseShare.h"

namespace lockstep
{
    static void testAllDiagonal()
    {
        // No deviations: every scene plays its own diagonal row, so each row is
        // shared by exactly one scene (SHR:1) and each scene lands on its own row.
        std::array<bool, kScenesPerSong> deviated{};        // all false
        std::array<int, kScenesPerSong>  deviationRow{};    // ignored when !deviated

        const auto sh = computePhraseShare(deviated, deviationRow);
        for (int r = 0; r < kPhrasesPerTrack; ++r)
            CHECK(sh.count[static_cast<std::size_t>(r)] == 1,
                  "all-diagonal: every phrase row is shared by exactly one scene");
        for (int s = 0; s < kScenesPerSong; ++s)
            CHECK(sh.effectiveRow[static_cast<std::size_t>(s)] == s,
                  "all-diagonal: scene s lands on row s");
    }

    static void testDeviationMovesShare()
    {
        // Scene 5 deviates the track onto row 2. Row 2 is now shared by scenes 2
        // and 5 (SHR:2); row 5 is played by nobody (SHR:0).
        std::array<bool, kScenesPerSong> deviated{};
        std::array<int, kScenesPerSong>  deviationRow{};
        deviated[5] = true;
        deviationRow[5] = 2;

        const auto sh = computePhraseShare(deviated, deviationRow);
        CHECK(sh.count[2] == 2, "deviation: row 2 gains a sharer (scenes 2 and 5)");
        CHECK(sh.count[5] == 0, "deviation: row 5 loses its only player");
        CHECK(sh.effectiveRow[5] == 2, "deviation: scene 5's effective row is 2");
        CHECK(sh.effectiveRow[2] == 2, "deviation: scene 2 still on its diagonal");
    }

    static void testMultipleSharersAndTotal()
    {
        // Pile scenes 1, 3, 7 all onto row 0 (which scene 0 also plays): SHR:4.
        std::array<bool, kScenesPerSong> deviated{};
        std::array<int, kScenesPerSong>  deviationRow{};
        for (int s : {1, 3, 7}) { deviated[static_cast<std::size_t>(s)] = true;
                                  deviationRow[static_cast<std::size_t>(s)] = 0; }

        const auto sh = computePhraseShare(deviated, deviationRow);
        CHECK(sh.count[0] == 4, "multi: row 0 shared by scenes 0,1,3,7");
        CHECK(sh.count[1] == 0 && sh.count[3] == 0 && sh.count[7] == 0,
              "multi: the departed scenes' diagonal rows are now empty");

        // The tally is a partition of the scenes: total across rows == scene count.
        int total = 0;
        for (int r = 0; r < kPhrasesPerTrack; ++r) total += sh.count[static_cast<std::size_t>(r)];
        CHECK(total == kScenesPerSong, "tally sums to the scene count (a partition)");
    }

    static void testDeviationClamps()
    {
        // An out-of-range deviation is clamped by resolveActivePhraseIdx, so the
        // tally still lands on a valid row (no OOB write).
        std::array<bool, kScenesPerSong> deviated{};
        std::array<int, kScenesPerSong>  deviationRow{};
        deviated[4] = true;
        deviationRow[4] = 999;

        const auto sh = computePhraseShare(deviated, deviationRow);
        CHECK(sh.effectiveRow[4] == kPhrasesPerTrack - 1, "clamp: over-range deviation clamps high");
        CHECK(sh.count[kPhrasesPerTrack - 1] == 2,
              "clamp: last row shared by its own scene and the clamped deviator");
    }

    void runPhraseShareTests()
    {
        testAllDiagonal();
        testDeviationMovesShare();
        testMultipleSharersAndTotal();
        testDeviationClamps();
    }
}
