// ArrangementTest — the scene/song/phrase switching + write-back logic that the
// processor delegates to (src/core/Arrangement.h). This is the real regression
// net for the legacy lost-edits-on-switch bug: every switch writes the working
// buffer back into its Phrase/Kit before re-projecting, so live edits survive.

#include <memory>
#include "TestHarness.h"
#include "../src/core/Arrangement.h"

namespace lockstep
{
    // Seed an Arrangement with two songs, each track's kit carrying a 2-slot
    // baseParams frame so projection produces non-empty working frames.
    // Heap-allocated: Arrangement embeds std::array<Song, 16> by value (tens of
    // MB) — fine as a processor member, but it would blow a test's stack.
    static std::unique_ptr<Arrangement> makeSeededArrangement()
    {
        auto arr = std::make_unique<Arrangement>();
        for (auto& song : arr->songs)
            for (auto& st : song.tracks)
            {
                st.kit.baseParams.assign(2, 0.0f);
                for (auto& phr : st.phrases)
                    phr.initialised = true;
            }
        // Distinguish phrase lengths so a phrase switch is observable.
        arr->songs[0].tracks[0].phrases[0].length = 16;
        arr->songs[0].tracks[0].phrases[1].length = 8;
        arr->songs[0].scenes[0].phraseIdx[0] = 0;
        arr->songs[0].scenes[1].phraseIdx[0] = 1;
        arr->syncWorkingFromActive();
        return arr;
    }

    static void testSceneSwitchPreservesEdit()
    {
        auto arr = makeSeededArrangement();

        // At scene 0, track 0 plays phrase 0 (length 16). Make a live edit.
        CHECK(arr->workingTrack(0).length == 16, "scene0: track0 projects phrase 0 (len 16)");
        arr->workingTrack(0).steps[4].trig = true;

        // Switch to scene 1: track 0 now plays phrase 1 (length 8, no edit).
        arr->setActiveScene(1);
        CHECK(arr->workingTrack(0).length == 8,   "scene1: track0 projects phrase 1 (len 8)");
        CHECK(!arr->workingTrack(0).steps[4].trig, "scene1: phrase 1 has no phrase-0 edit");

        // Switch back to scene 0: the edit on phrase 0 must have survived.
        arr->setActiveScene(0);
        CHECK(arr->workingTrack(0).length == 16,  "back to scene0: phrase 0 length restored");
        CHECK(arr->workingTrack(0).steps[4].trig,
              "back to scene0: live edit survived the round trip (write-back works)");
    }

    static void testBaseParamEditSurvivesViaKit()
    {
        auto arr = makeSeededArrangement();
        arr->workingTrack(0).baseParams[1] = 0.7f;   // a sound-base edit

        arr->setActiveScene(1);
        arr->setActiveScene(0);
        // baseParams live on the Kit (shared across the song's scenes); the edit
        // must be written back and re-projected intact.
        CHECK(feq(arr->workingTrack(0).baseParams[1], 0.7f),
              "scene round-trip: baseParams edit preserved via Kit");
    }

    static void testDeviationSwapAndResync()
    {
        auto arr = makeSeededArrangement();

        // Track 0 deviates to phrase 2; edit it.
        arr->swapPhraseForTrack(0, 2);
        CHECK(arr->deviated[0],                      "swap: track 0 marked deviated");
        CHECK(arr->activePhraseIdx(0) == 2,          "swap: active phrase is the deviation");
        arr->workingTrack(0).steps[7].trig = true;

        // Re-sync clears the deviation back to the scene's assignment (phrase 0).
        arr->resyncTrackToScene(0);
        CHECK(!arr->deviated[0],                     "resync: deviation cleared");
        CHECK(arr->activePhraseIdx(0) == 0,          "resync: back to scene assignment");
        CHECK(!arr->workingTrack(0).steps[7].trig,   "resync: phrase 0 has no phrase-2 edit");

        // Deviate to phrase 2 again: the earlier edit must still be there.
        arr->swapPhraseForTrack(0, 2);
        CHECK(arr->workingTrack(0).steps[7].trig,
              "re-deviate: phrase-2 edit survived (per-track write-back works)");
    }

    static void testSongSwitchClearsDeviationAndSwapsKit()
    {
        auto arr = makeSeededArrangement();
        arr->songs[1].tracks[0].kit.baseParams[0] = 0.9f;  // song 1 has a distinct kit

        arr->swapPhraseForTrack(0, 3);
        CHECK(arr->deviated[0], "pre: track 0 deviated in song 0");

        arr->setActiveSong(1);
        CHECK(arr->songIdx == 1,    "song switch: songIdx updated");
        CHECK(arr->sceneIdx == 0,   "song switch: scene reset to 0");
        CHECK(!arr->deviated[0],    "song switch: deviation cleared (full reset)");
        CHECK(feq(arr->workingTrack(0).baseParams[0], 0.9f),
              "song switch: working reflects song 1's kit");
    }

    void runArrangementTests()
    {
        testSceneSwitchPreservesEdit();
        testBaseParamEditSurvivesViaKit();
        testDeviationSwapAndResync();
        testSongSwitchClearsDeviationAndSwapsKit();
    }
}
