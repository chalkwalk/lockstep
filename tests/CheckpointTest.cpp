// CheckpointTest — scope-respecting Checkpoints (DESIGN §13.6).
// Covers snapshot / restoreOne / restoreToFloor / floor-idempotence /
// depth-cap eviction per scope; and Scene-scope deviation-clear behaviour.

#include "TestHarness.h"
#include "../src/core/Arrangement.h"

namespace lockstep
{
    // Heap-allocated to avoid blowing the test stack (Arrangement holds tens of
    // MB of Song data by value). Seeded with observable phrase lengths.
    static std::unique_ptr<Arrangement> makeArrangement()
    {
        auto arr = std::make_unique<Arrangement>();
        for (auto& song : arr->songs)
            for (auto& st : song.tracks)
            {
                st.kit.baseParams.assign(2, 0.0f);
                for (auto& phr : st.phrases)
                    phr.initialised = true;
            }
        arr->songs[0].tracks[0].phrases[0].length = 16;
        arr->songs[0].tracks[0].phrases[1].length = 8;
        // Diagonal: scene 0 plays row 0 (len 16), scene 1 plays row 1 (len 8).
        arr->syncWorkingFromActive();
        arr->seedFloor();   // establish the floor at this known-good state
        return arr;
    }

    // ── Song scope ────────────────────────────────────────────────────────────

    static void testSongSnapshotRestoreOne()
    {
        auto arr = makeArrangement();

        // Make a live edit, snapshot, make another edit.
        arr->workingTrack(0).steps[3].trig = true;
        arr->snapshot(CheckpointScope::Song, 0);
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 1,
              "Song: depth 1 after first snapshot");

        arr->workingTrack(0).steps[5].trig = true;

        // restoreOne pops back to the first snapshot.
        arr->restoreOne(CheckpointScope::Song, 0);
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 0,
              "Song: depth 0 after restoreOne");
        CHECK(arr->workingTrack(0).steps[3].trig,
              "Song restoreOne: step 3 trig preserved (from snapshot)");
        CHECK(!arr->workingTrack(0).steps[5].trig,
              "Song restoreOne: step 5 trig removed (made after snapshot)");
    }

    static void testSongRestoreToFloor()
    {
        auto arr = makeArrangement();

        arr->workingTrack(0).steps[3].trig = true;
        arr->snapshot(CheckpointScope::Song, 0);
        arr->workingTrack(0).steps[5].trig = true;
        arr->snapshot(CheckpointScope::Song, 0);
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 2,
              "Song: depth 2 after two snapshots");

        arr->restoreToFloor(CheckpointScope::Song, 0);
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 0,
              "Song: depth 0 after restoreToFloor");
        CHECK(!arr->workingTrack(0).steps[3].trig,
              "Song restoreToFloor: step 3 cleared (floor is clean)");
        CHECK(!arr->workingTrack(0).steps[5].trig,
              "Song restoreToFloor: step 5 cleared");
    }

    static void testSongFloorIdempotent()
    {
        auto arr = makeArrangement();

        // 9.4 item A: this test used to assert that restoreOne on an EMPTY stack
        // "returns to floor" — i.e. it encoded the silent wipe as the contract. It is
        // the bug, written down. The floor is reached by ASKING for it (restoreToFloor,
        // the deliberate hold); a restore that ran out of stack must change nothing.
        arr->workingTrack(0).steps[2].trig = true;
        CHECK(!arr->restoreOne(CheckpointScope::Song, 0),
              "Song: restoreOne on an empty stack refuses");
        CHECK(arr->workingTrack(0).steps[2].trig,
              "Song: and leaves the work alone — it does NOT fall through to the floor");

        // restoreToFloor is the deliberate gesture, and it IS idempotent.
        arr->restoreToFloor(CheckpointScope::Song, 0);
        CHECK(!arr->workingTrack(0).steps[2].trig,
              "Song floor: the deliberate gesture reaches it");
        arr->restoreToFloor(CheckpointScope::Song, 0);
        CHECK(!arr->workingTrack(0).steps[2].trig,
              "Song floor: and is idempotent once there");
    }

    // ── Song scope: memory-budget eviction ────────────────────────────────────
    //
    // 9.4 item D: the stack is bounded by a per-scope memory budget (kCkBudgetBytes),
    // not a fixed count. A Song payload is ~2.77 MB, so the 64 MB budget holds ~20
    // Song marks — far more than the old count cap of 8, but still bounded so a
    // pathological mark-spam can't grow without limit.
    static void testSongStackEvictsByMemoryNotCount()
    {
        auto arr = makeArrangement();
        for (int i = 0; i < 40; ++i)
        {
            arr->workingTrack(0).steps[0].trig = (i % 2 == 0);
            arr->snapshot(CheckpointScope::Song, 0);
        }
        const int depth = arr->checkpointDepth(CheckpointScope::Song, 0);
        CHECK(depth > 8,  "Song: memory budget allows far more than the old count cap");
        CHECK(depth <= 40, "Song: still bounded");
        CHECK(arr->checkpointBytes(CheckpointScope::Song, 0) <= 64u * 1024 * 1024,
              "Song: stack stays within the 64 MB budget");
    }

    // ── Track scope ───────────────────────────────────────────────────────────

    static void testTrackSnapshotRestoreOne()
    {
        auto arr = makeArrangement();

        arr->workingTrack(0).steps[4].trig = true;
        arr->snapshot(CheckpointScope::Track, 0);
        CHECK(arr->checkpointDepth(CheckpointScope::Track, 0) == 1,
              "Track: depth 1 after snapshot");

        arr->workingTrack(0).steps[7].trig = true;
        arr->restoreOne(CheckpointScope::Track, 0);
        CHECK(arr->workingTrack(0).steps[4].trig,
              "Track restoreOne: step 4 trig preserved");
        CHECK(!arr->workingTrack(0).steps[7].trig,
              "Track restoreOne: step 7 removed");
    }

    static void testTrackScopeIsolated()
    {
        auto arr = makeArrangement();

        // Set step 0 on track 0, snapshot, then make a further edit (step 1).
        // Also set step 0 on track 1.  Restoring track 0 should revert step 1
        // (made after snapshot) but leave track 1 untouched.
        arr->workingTrack(0).steps[0].trig = true;
        arr->snapshot(CheckpointScope::Track, 0);
        arr->workingTrack(0).steps[1].trig = true;  // edit AFTER snapshot
        arr->workingTrack(1).steps[0].trig = true;
        arr->restoreOne(CheckpointScope::Track, 0);

        CHECK(arr->workingTrack(0).steps[0].trig,
              "Track isolation: track 0 step 0 restored from snapshot (was true)");
        CHECK(!arr->workingTrack(0).steps[1].trig,
              "Track isolation: track 0 step 1 reverted (was set after snapshot)");
        CHECK(arr->workingTrack(1).steps[0].trig,
              "Track isolation: track 1 unaffected by track 0 restore");
    }

    // ── Scene scope ───────────────────────────────────────────────────────────

    static void testSceneSnapshotRestoreOne()
    {
        auto arr = makeArrangement();

        // Save the scene state, change the coreTime, then restore.
        arr->snapshot(CheckpointScope::Scene, 0);
        arr->scene().coreTime.numerator = 7;
        arr->restoreOne(CheckpointScope::Scene, 0);
        CHECK(arr->scene().coreTime.numerator == 4,
              "Scene restoreOne: coreTime restored to snapshot value");
    }

    static void testSceneRestoreClearsDeviation()
    {
        auto arr = makeArrangement();

        arr->swapPhraseForTrack(0, 3);
        CHECK(arr->deviated[0], "pre: track 0 deviated");

        arr->snapshot(CheckpointScope::Scene, 0);
        arr->restoreOne(CheckpointScope::Scene, 0);
        CHECK(!arr->deviated[0],
              "Scene restore: deviation cleared (restore is exact/clobbering)");
    }

    // ── Phrase scope ──────────────────────────────────────────────────────────

    static void testPhraseSnapshotRestoreOne()
    {
        auto arr = makeArrangement();

        arr->workingTrack(0).steps[6].trig = true;
        arr->snapshot(CheckpointScope::Phrase, 0);
        arr->workingTrack(0).steps[9].trig = true;

        arr->restoreOne(CheckpointScope::Phrase, 0);
        CHECK(arr->workingTrack(0).steps[6].trig,
              "Phrase restoreOne: step 6 preserved");
        CHECK(!arr->workingTrack(0).steps[9].trig,
              "Phrase restoreOne: step 9 removed");
    }

    static void testPhraseRestoreToFloor()
    {
        auto arr = makeArrangement();

        arr->workingTrack(0).steps[1].trig = true;
        arr->snapshot(CheckpointScope::Phrase, 0);
        arr->workingTrack(0).steps[2].trig = true;

        arr->restoreToFloor(CheckpointScope::Phrase, 0);
        CHECK(!arr->workingTrack(0).steps[1].trig,
              "Phrase restoreToFloor: step 1 cleared");
        CHECK(!arr->workingTrack(0).steps[2].trig,
              "Phrase restoreToFloor: step 2 cleared");
        CHECK(arr->checkpointDepth(CheckpointScope::Phrase, 0) == 0,
              "Phrase restoreToFloor: stack cleared");
    }

    static void testPhraseScopeIsolatedFromKit()
    {
        auto arr = makeArrangement();

        // Kit change on track 0 should not affect Phrase-scope stack.
        arr->snapshot(CheckpointScope::Phrase, 0);
        arr->kit(0).baseParams[0] = 0.99f;
        // The app keeps the working buffer in step with active; a sound edit lands in
        // working and is flushed to active, never left diverged. Sync here so the state
        // is realistic — otherwise the writeBack that restore-arms-undo now performs
        // (item F) would flush a stale working base over this direct active poke. The
        // invariant under test is unchanged: a Phrase mark stores only the phrase, so
        // restoring it cannot revert the kit.
        arr->syncWorkingTrackFromActive(0);
        arr->restoreOne(CheckpointScope::Phrase, 0);
        // Kit edit is NOT reverted by a Phrase-scope restore.
        CHECK(feq(arr->kit(0).baseParams[0], 0.99f),
              "Phrase scope: kit edit survives Phrase-scope restore");
    }

    // ── seedFloor / song-switch reseeds floor ─────────────────────────────────

    static void testSeedFloorClearsStacks()
    {
        auto arr = makeArrangement();

        arr->workingTrack(0).steps[3].trig = true;
        arr->snapshot(CheckpointScope::Song, 0);
        arr->snapshot(CheckpointScope::Track, 0);
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 1, "pre: Song stack");

        arr->seedFloor();
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 0,
              "seedFloor: Song stack cleared");
        CHECK(arr->checkpointDepth(CheckpointScope::Track, 0) == 0,
              "seedFloor: Track stack cleared");
    }

    static void testSongSwitchReseeds()
    {
        auto arr = makeArrangement();

        arr->workingTrack(0).steps[3].trig = true;
        arr->snapshot(CheckpointScope::Song, 0);
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 1, "pre-switch");

        arr->setActiveSong(1);
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 0,
              "Song switch: checkpoint stack cleared (floor reseeded to song 1)");
        CHECK(arr->songIdx == 1, "Song switch: songIdx updated");
    }

    // ── 9.4 item A: an empty stack is a no-op, not a wipe ─────────────────────
    //
    // Every scope used to fall through to the floor when its stack ran out, so one
    // Func+Y too many silently reverted that scope to the state it had when the
    // project LOADED — discarding everything since, with no confirm and no message.
    // Track and Phrase were the worst of it: their snapshot gesture dispatched to
    // nothing, so those stacks could never be pushed to by hand and a single restore
    // press was ALWAYS the wipe.
    //
    // Restore must now leave the state untouched and report that it did nothing. The
    // floor stays reachable — via restoreToFloor (the deliberate hold), never via a
    // tap that ran out of stack.
    static void testEmptyStackRestoreIsANoOp()
    {
        auto arr = makeArrangement();

        // Work done since the floor was seeded, on every scope, with nothing snapshotted.
        arr->workingTrack(0).steps[3].trig = true;
        arr->workingTrack(1).steps[7].trig = true;
        arr->scene().activeMask[2] = !arr->scene().activeMask[2];
        const bool maskAfterEdit = arr->scene().activeMask[2];

        CHECK(arr->checkpointDepth(CheckpointScope::Track, 0) == 0, "precondition: no track marks");
        CHECK(arr->checkpointDepth(CheckpointScope::Song, 0) == 0, "precondition: no song marks");

        // Each restore must REFUSE (false) and change nothing.
        CHECK(!arr->restoreOne(CheckpointScope::Track, 0), "Track: empty stack restore refuses");
        CHECK(arr->workingTrack(0).steps[3].trig,
              "Track: the refused restore did NOT wipe the track to the project baseline");

        CHECK(!arr->restoreOne(CheckpointScope::Phrase, 1), "Phrase: empty stack restore refuses");
        CHECK(arr->workingTrack(1).steps[7].trig,
              "Phrase: the refused restore did NOT wipe the phrase");

        CHECK(!arr->restoreOne(CheckpointScope::Scene, 0), "Scene: empty stack restore refuses");
        CHECK(arr->scene().activeMask[2] == maskAfterEdit,
              "Scene: the refused restore did NOT wipe the scene");

        CHECK(!arr->restoreOne(CheckpointScope::Song, 0), "Song: empty stack restore refuses");
        CHECK(arr->workingTrack(0).steps[3].trig && arr->workingTrack(1).steps[7].trig,
              "Song: the refused restore did NOT wipe the song to the project baseline");

        // The floor is still reachable — but only by asking for it.
        arr->restoreToFloor(CheckpointScope::Song, 0);
        CHECK(!arr->workingTrack(0).steps[3].trig,
              "the floor is still reachable through the deliberate gesture");
    }

    // ── 9.4 item E: undo stack is separate from the mark stacks ───────────────
    //
    // Marks are the performer's (Y / Func+Y). Undo is the system's shallow safety net,
    // armed automatically before a destructive op and walked by Func+O. They must not
    // share a stack, or a flurry of Y-marks would bury the pre-mistake point.
    static void testUndoStackIsSeparateFromMarks()
    {
        auto arr = makeArrangement();
        arr->workingTrack(0).steps[1].trig = true;
        arr->snapshot(CheckpointScope::Track, 0);            // a MARK
        CHECK(arr->checkpointDepth(CheckpointScope::Track, 0) == 1, "one mark");
        CHECK(arr->undoDepth(CheckpointScope::Track, 0) == 0,       "no undo yet");

        arr->armUndo(CheckpointScope::Track, 0);             // a destructive op fires
        arr->workingTrack(0).steps[1].trig = false;          // ...and mutates
        CHECK(arr->checkpointDepth(CheckpointScope::Track, 0) == 1, "mark count unchanged");
        CHECK(arr->undoDepth(CheckpointScope::Track, 0) == 1,       "undo armed");

        CHECK(arr->popUndo(CheckpointScope::Track, 0), "undo applies");
        CHECK(arr->workingTrack(0).steps[1].trig, "undo reverted the destructive edit");
        CHECK(arr->checkpointDepth(CheckpointScope::Track, 0) == 1, "mark still there after undo");
        CHECK(!arr->popUndo(CheckpointScope::Track, 0), "empty undo is a no-op");
    }

    // ── 9.4 item F: a restore is itself a destructive op, so it arms undo ─────
    //
    // Restoring a mark overwrites live state. That is destructive exactly like a clear
    // or paste, so it arms the undo stack — Func+O then takes a mis-fired restore back.
    static void testRestoreIsUndoable()
    {
        auto arr = makeArrangement();
        arr->workingTrack(0).steps[2].trig = true;   // state A
        arr->snapshot(CheckpointScope::Track, 0);    // mark A
        arr->workingTrack(0).steps[2].trig = false;  // live is now B (diverged)

        CHECK(arr->restoreOne(CheckpointScope::Track, 0), "restore to mark A");
        CHECK(arr->workingTrack(0).steps[2].trig, "live is A after restore");
        CHECK(arr->undoDepth(CheckpointScope::Track, 0) == 1, "restore armed an undo (was B)");

        CHECK(arr->popUndo(CheckpointScope::Track, 0), "undo the restore");
        CHECK(!arr->workingTrack(0).steps[2].trig, "live is B again — the restore was taken back");
    }

    // An empty-stack restore is a no-op (item A) and must NOT arm a spurious undo.
    static void testEmptyRestoreArmsNoUndo()
    {
        auto arr = makeArrangement();
        arr->workingTrack(0).steps[2].trig = true;
        CHECK(!arr->restoreOne(CheckpointScope::Track, 0), "empty restore refuses");
        CHECK(arr->undoDepth(CheckpointScope::Track, 0) == 0, "and arms no undo");
    }

    void runCheckpointTests()
    {
        testSongSnapshotRestoreOne();
        testSongRestoreToFloor();
        testSongFloorIdempotent();
        testSongStackEvictsByMemoryNotCount();
        testTrackSnapshotRestoreOne();
        testTrackScopeIsolated();
        testSceneSnapshotRestoreOne();
        testSceneRestoreClearsDeviation();
        testPhraseSnapshotRestoreOne();
        testPhraseRestoreToFloor();
        testPhraseScopeIsolatedFromKit();
        testSeedFloorClearsStacks();
        testSongSwitchReseeds();
        testEmptyStackRestoreIsANoOp();
        testUndoStackIsSeparateFromMarks();
        testRestoreIsUndoable();
        testEmptyRestoreArmsNoUndo();
    }
}

