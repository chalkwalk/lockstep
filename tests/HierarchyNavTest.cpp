// HierarchyNavTest — the Phrase/Kit ⇄ working-Track projection seam
// (src/core/HierarchyNav.h). These tests pin the source-of-truth contract that
// the legacy one-way syncSequenceFromCurrentScene() violated: edits to the
// working Track must be reversible back into the Phrase/Kit, or a scene/phrase
// switch silently drops them. The "go direct" consolidation routes the audio
// path through this seam; these tests are its regression net.

#include "TestHarness.h"
#include "../src/core/HierarchyNav.h"

namespace lockstep
{
    static void testResolveActivePhraseIdx()
    {
        // Non-deviated: returns the scene index (diagonal).
        CHECK(resolveActivePhraseIdx(3, false, 7, 0) == 3,
              "diagonal: non-deviated returns scene index");
        // Deviated: take the deviation index, ignore the scene index.
        CHECK(resolveActivePhraseIdx(3, true, 7, 0) == 7,
              "diagonal: deviated uses deviation index");
        // Out-of-range clamps into [0, kPhrasesPerTrack-1].
        CHECK(resolveActivePhraseIdx(0, true, 999, 0) == kPhrasesPerTrack - 1,
              "diagonal: over-range deviation clamps high");
        CHECK(resolveActivePhraseIdx(0, true, -5, 0) == 0,
              "diagonal: negative deviation clamps low");
    }

    static void testProjectionSplit()
    {
        Song::SongTrack st;
        st.kit.baseParams.assign(3, 0.0f);
        st.kit.baseParams[0] = 0.25f;
        st.kit.subdivIndex = 12; // D1_4 Straight = 1.0 PPQ (old divider=4 equivalent)
        st.phrases[0].length = 12;
        st.phrases[0].steps[2].trig = true;
        st.phrases[0].trigDefaults.note = 67;

        const Track t = projectPhraseToTrack(st.phrases[0], st.kit);

        // Phrase owns content/timing.
        CHECK(t.length == 12, "project: length comes from Phrase");
        CHECK(t.steps[2].trig, "project: steps come from Phrase");
        CHECK(t.trigDefaults.note == 67, "project: trigDefaults come from Phrase");
        // Kit owns sound + subdivision.
        CHECK(t.subdivIndex == 12, "project: subdivIndex comes from Kit");
        CHECK(t.baseParams.size() == 3 && feq(t.baseParams[0], 0.25f),
              "project: baseParams come from Kit");
    }

    // The headline regression: an edit to the working Track survives a re-project
    // (and therefore a scene/phrase switch) IF AND ONLY IF it is written back.
    static void testEditSurvivesSwitchOnlyWithWriteBack()
    {
        Song::SongTrack st;
        st.kit.baseParams.assign(3, 0.0f);
        st.phrases[0].length = 16;
        st.phrases[1].length = 8;     // a second phrase to switch to and back

        const auto idx = static_cast<std::size_t>(
            resolveActivePhraseIdx(0, false, -1, 0));
        Track work = projectPhraseToTrack(st.phrases[idx], st.kit);

        // Live edits on the working buffer (a trig, a length change, a base param).
        work.steps[5].trig = true;
        work.length = 11;
        work.baseParams[1] = 0.6f;

        // Without write-back, re-projecting from the (stale) Phrase/Kit loses the
        // edit — this is exactly the legacy lost-edits bug, asserted explicitly.
        const Track stale = projectPhraseToTrack(st.phrases[idx], st.kit);
        CHECK(!stale.steps[5].trig, "no write-back: trig edit is lost on re-project");
        CHECK(stale.length == 16, "no write-back: length edit is lost on re-project");
        CHECK(feq(stale.baseParams[1], 0.0f), "no write-back: base edit is lost on re-project");

        // Write the working Track back into its Phrase (content) and Kit (sound).
        applyTrackEditsToPhrase(work, st.phrases[idx]);
        applyTrackBaseToKit(work, st.kit);

        // Now a re-project preserves every edit.
        const Track kept = projectPhraseToTrack(st.phrases[idx], st.kit);
        CHECK(kept.steps[5].trig, "write-back: trig edit preserved");
        CHECK(kept.length == 11, "write-back: length edit preserved");
        CHECK(feq(kept.baseParams[1], 0.6f), "write-back: base edit preserved");

        // Switch to phrase 1 (different content) and back to 0: edit still there,
        // because it lives in the Phrase, not the transient working buffer.
        const Track other = projectPhraseToTrack(st.phrases[1], st.kit);
        CHECK(other.length == 8 && !other.steps[5].trig,
              "switch: phrase 1 has its own content, unaffected by phrase 0's edit");
        const Track back = projectPhraseToTrack(st.phrases[0], st.kit);
        CHECK(back.steps[5].trig && back.length == 11,
              "switch back: phrase 0's written-back edit survives the round trip");
    }

    void runHierarchyNavTests()
    {
        testResolveActivePhraseIdx();
        testProjectionSplit();
        testEditSurvivesSwitchOnlyWithWriteBack();
    }
}
