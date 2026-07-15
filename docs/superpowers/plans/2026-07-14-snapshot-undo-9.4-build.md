# 9.4 Snapshot / Undo — Build (items D–H) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the performer-facing snapshot/undo model — independent per-scope
mark stacks, a separate shallow undo stack reached by `Func+O` that also takes back a
restore — with the elaborate cross-scope model left rejected.

**Architecture:** All checkpoint state lives in `src/core/Arrangement.h` (JUCE-free,
value types, RAM-only). Marks already exist as per-scope LIFO stacks; this build adds
a *distinct* undo stack per scope, redirects the ~20 destructive-op auto-captures
(which today wrongly push the mark stacks) onto it, makes `restoreOne` arm undo before
it overwrites, wires `Func+O` → `UNDO` through the existing verb-dispatch seam
(`KeyBindings.cpp` → `CommandCore` → `CommandEffects` → `PluginEditor`), and shows
per-scope mark depth on the scope keys. Redo is designed but not built.

**Tech Stack:** C++20, JUCE 8, CMake. Namespace `lockstep`. Two test binaries:
`lockstep_tests` (includes `CheckpointTest.cpp`) and `lockstep_dispatch_tests`
(golden at `tests/goldens/dispatch.txt`).

## Global Constraints

- Build: `cmake --build build -j$(nproc)`. Strict `-Werror` with `-Wall -Wextra
  -Wpedantic -Wsign-conversion -Wfloat-conversion -Wshadow -Wimplicit-fallthrough`.
- Tests are plain binaries, NOT ctest: `./build/tests/lockstep_tests` and
  `./build/tests/lockstep_dispatch_tests`. `ctest` reports "No tests found".
- Re-bless the dispatch golden with `LOCKSTEP_REGEN_GOLDEN=1 ./build/tests/lockstep_dispatch_tests`
  — **always read the diff before blessing**; it is the enumeration net for verb behaviour.
- Switch hygiene (PRINCIPLES §20): every `case` ends in break/return/throw/`[[fallthrough]]`.
  Exhaustive switches over closed enums omit `default:` so `-Wswitch` flags new values.
- Status strings: authored only in `src/command/StatusText.h` (DESIGN §37.4), never inline.
- No non-ASCII in `juce::String(const char*)` literals; icon glyphs use `u8"…"`.
- Checkpoint payloads are RAM-only and never serialized. They hold *sample references*,
  never PCM. Do not touch `PluginState` serialization for any of this.
- Commit per task boundary. Never `git add -A` / `git add .` / `git commit -a`
  (staging hook blocks it); stage explicit paths. `CLAUDE.md`/`GEMINI.md` stay untracked.
- Spec: **DESIGN §13.6**. Rejected model: `docs/snapshot-undo-rejected-elaborate-model.md`.
  Roadmap checkboxes: `ROADMAP.md` 9.4 items D–H — tick them in the same task.

---

## Current-state facts (read before starting)

- `src/core/Arrangement.h` checkpoint members (~line 568–746):
  - `static constexpr int kMaxCkDepth = 8;`
  - `Song floorSong_{};`
  - `std::vector<Song> songStack_;`
  - `std::map<int, std::vector<Song::SongTrack>> trackStack_;`
  - `std::map<int, std::vector<Scene>> sceneStack_;`
  - `std::map<std::pair<int,int>, std::vector<Phrase>> phraseStack_;`
  - Methods: `snapshot(CheckpointScope, int track)`, `restoreOne(...)->bool`,
    `restoreToFloor(...)`, `checkpointDepth(...)->int`, `seedFloor()`.
- `enum class CheckpointScope { Song, Track, Scene, Phrase }` at ~line 14.
- **The confusion this build fixes:** `snapshot()` is called from *two* kinds of site:
  (a) explicit `Y` marks — `verbs::noScope/track/phrase/scene` VerbSnapshot arms
  (shipped in 9.4 B/C), and `PluginEditor.cpp:2831`/`:5825` no-scope snapshot; and
  (b) auto-captures *before destructive ops* — the ~20 `snapshot(...)` calls in
  `PluginEditor.cpp` (e.g. `:556 :581 :587 :597 :607 :5186 :5199 :5210`, the confirm
  and paste and control-all paths) and `verbs::phrase` VerbDelete
  (`VerbCommands.cpp:258`). Group (a) are **marks** (keep). Group (b) are **undo
  arms** (must move to `armUndo`). The exact classification is Task E, Step 1.
- Restore reaches `Arrangement::restoreOne` two ways, both already unified this
  session onto the effects: `CommandCore` `AId::VerbRestore → fx.restorePop()` /
  `AId::RestoreFloor → fx.restoreFloor()` (`CommandCore.cpp:338-339`), and the keyboard
  `CB::Restore` key-up branch (`PluginEditor.cpp:~6496`) which now calls
  `editorEffects_->restorePop()/restoreFloor()`.
- `Func+O` today: no `kModFunc` row for `VerbClear`, so it falls to the bare
  `kModNone` row → `AId::VerbClear` (CLEAR). The `Func+VerbClear→VerbDelete`
  ButtonLayers remap was retired (9.29), so the seat is free for UNDO.
- Status-lane depth: `InspectorModel.cpp:168-169` prints `"  CK:" + checkpointDepth`,
  fed by `StatusInput::checkpointDepth` (`InspectorModel.h:67`) set at
  `PluginEditor.cpp:2056` from `processor_.checkpointDepth(ckScope(ckTrk), ckTrk)`.
- Scope-key rendering: `src/ui/SurfaceModel.cpp` builds the surface cells; the verb/
  modifier keys are emitted there (grep `KeyRole::` and the modifier cluster). Pips
  are a `SurfaceCell` decoration channel (border/dot/strip/pip) — DESIGN §35.8.

---

## Task D: Per-scope memory budget (replace the fixed depth cap)

**Files:**
- Modify: `src/core/Arrangement.h` (checkpoint block ~568–746)
- Test: `tests/CheckpointTest.cpp` (append cases; registered in its own runner)

**Interfaces:**
- Produces: `Arrangement::checkpointBytes(CheckpointScope, int track) const` (sum of
  payload `sizeof` across that scope's mark stack); eviction inside `snapshot()` keyed
  on a byte budget instead of `kMaxCkDepth`.
- Consumes: nothing new.

- [ ] **Step 1: Write the failing test** — append to `tests/CheckpointTest.cpp`:

```cpp
static void testSongStackEvictsByMemoryNotCount()
{
    auto arr = makeArrangement();
    // A Song payload is ~2.77 MB; the budget is 64 MB, so >23 marks must evict.
    for (int i = 0; i < 40; ++i)
    {
        arr->workingTrack(0).steps[0].trig = (i % 2 == 0);
        arr->snapshot(CheckpointScope::Song, 0);
    }
    // Depth is capped by bytes, not by 8 (the old kMaxCkDepth), and not unbounded.
    const int depth = arr->checkpointDepth(CheckpointScope::Song, 0);
    CHECK(depth > 8,  "Song: memory budget allows far more than the old count cap");
    CHECK(depth <= 40, "Song: still bounded");
    CHECK(arr->checkpointBytes(CheckpointScope::Song, 0) <= 64u * 1024 * 1024,
          "Song: stack stays within the 64 MB budget");
}
```

Add its call into the file's test-runner list (grep the existing `testSong...` calls
in `runCheckpointTests`/`main` and add `testSongStackEvictsByMemoryNotCount();`).

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests`
Expected: FAIL — `checkpointBytes` undefined (compile error), or depth capped at 8.

- [ ] **Step 3: Implement.** In `src/core/Arrangement.h`:
  - Replace `static constexpr int kMaxCkDepth = 8;` with
    `static constexpr std::size_t kCkBudgetBytes = 64ull * 1024 * 1024; // per scope`.
  - Add a private helper (near the stacks):

```cpp
template <class Vec>
static void evictToBudget(Vec& stack)
{
    std::size_t total = 0;
    for (const auto& e : stack) total += sizeof(e);
    while (stack.size() > 1 && total > kCkBudgetBytes)
    {
        total -= sizeof(stack.front());
        stack.erase(stack.begin());   // oldest non-floor
    }
}
```

  - In `snapshot()`, replace each `if (static_cast<int>(stk.size()) > kMaxCkDepth)
    stk.erase(stk.begin());` (all four scopes, and the `songStack_` case) with
    `evictToBudget(<that stack>);`.
  - Add the public accessor:

```cpp
[[nodiscard]] std::size_t checkpointBytes(CheckpointScope scope, int track) const
{
    auto sum = [](const auto& stk) {
        std::size_t t = 0; for (const auto& e : stk) t += sizeof(e); return t; };
    switch (scope)
    {
        case CheckpointScope::Song:  return sum(songStack_);
        case CheckpointScope::Track: { auto it = trackStack_.find(track);
            return it == trackStack_.end() ? 0 : sum(it->second); }
        case CheckpointScope::Scene: { auto it = sceneStack_.find(sceneIdx);
            return it == sceneStack_.end() ? 0 : sum(it->second); }
        case CheckpointScope::Phrase: { if (track < 0 || track >= (int)kNumTracks) return std::size_t{0};
            const int p = activePhraseIdx(track); auto it = phraseStack_.find({track, p});
            return it == phraseStack_.end() ? 0 : sum(it->second); }
    }
    return 0;
}
```

  Note: `sizeof(e)` counts the flat struct (the dominant term); the P-Lock heap tails
  are a minor add and deliberately ignored — this is a pathology backstop, not a
  precise allocator. This matches the DESIGN §13.6 measured-payload rationale.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests`
Expected: PASS, and every pre-existing CheckpointTest case still passes.

- [ ] **Step 5: Tick ROADMAP D and commit**

Tick `- [ ]`→`- [x]` on ROADMAP 9.4 item D.

```bash
git add src/core/Arrangement.h tests/CheckpointTest.cpp ROADMAP.md
git commit -m "Cap checkpoint stacks by memory budget, not a fixed count"
```

---

## Task E: Separate undo stack + `Func+O` = UNDO

**Files:**
- Modify: `src/core/Arrangement.h` (add undo stacks + `armUndo`/`popUndo`/`undoDepth`)
- Modify: `src/PluginProcessor.h` / `.cpp` (thin passthroughs, mirror `snapshot`)
- Modify: `src/PluginEditor.cpp` (redirect group-(b) auto-captures; implement `fx.undo()`)
- Modify: `src/command/VerbCommands.cpp` (redirect the `verbs::phrase` VerbDelete arm)
- Modify: `src/command/CommandEffects.h` (add `virtual void undo() = 0;`)
- Modify: `src/command/KeyBindings.h` (add `AId::VerbUndo`)
- Modify: `src/command/KeyBindings.cpp` (add the `Func+O` row)
- Modify: `src/command/CommandCore.cpp` (dispatch `AId::VerbUndo → fx.undo()`)
- Modify: `src/command/StatusText.h` (add `undid…` / `nothingToUndo`)
- Test: `tests/CheckpointTest.cpp`, `tests/KeyBindingTest.cpp`, `tests/goldens/dispatch.txt`

**Interfaces:**
- Produces: `Arrangement::armUndo(CheckpointScope, int track)` (push current state onto
  the scope's *undo* stack, byte-budgeted), `Arrangement::popUndo(CheckpointScope, int
  track)->bool` (apply newest undo entry; false if empty), `Arrangement::undoDepth(...)
  const->int`; `LockstepProcessor::armUndo/popUndo/undoDepth` passthroughs;
  `CommandEffects::undo()`; `ActionId::VerbUndo`.
- Consumes: Task D's byte-budget helper `evictToBudget`.

- [ ] **Step 1: Classify the auto-capture sites.** Grep every
  `snapshot(CheckpointScope` / `processor_.snapshot(` / `arrangement.snapshot(` call.
  For each, decide: is it an **explicit `Y` mark** (keep as `snapshot`) or an
  **auto-capture before a destructive op** (change to `armUndo`)? The marks are the
  four `verbs::*` VerbSnapshot arms and the two no-scope `PluginEditor` snapshot lines
  in the `VerbSnapshot` key branch. Everything guarding a delete / clear-confirm /
  paste / Control-All / bake is an undo arm. Write the list into the commit body.

- [ ] **Step 2: Write the failing test** — append to `tests/CheckpointTest.cpp`:

```cpp
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
```

Register `testUndoStackIsSeparateFromMarks();` in the runner.

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests`
Expected: FAIL — `armUndo`/`popUndo`/`undoDepth` undefined.

- [ ] **Step 4: Implement the Arrangement undo stacks.** In `src/core/Arrangement.h`,
  mirror the mark stacks with parallel undo members and methods:

```cpp
// Undo stacks — armed automatically before destructive ops; walked by Func+O.
// Separate from the mark stacks so a flurry of Y-marks never buries the pre-mistake
// point (DESIGN §13.6). Same byte budget, same eviction.
std::vector<Song> songUndo_;
std::map<int, std::vector<Song::SongTrack>> trackUndo_;
std::map<int, std::vector<Scene>> sceneUndo_;
std::map<std::pair<int,int>, std::vector<Phrase>> phraseUndo_;
```

  `armUndo(scope, track)` = `writeBackWorkingToActive();` then push the same payload
  `snapshot()` pushes, but onto the `*Undo_` container, then `evictToBudget(...)`.
  `popUndo(scope, track)` = the body of `restoreOne` but reading/popping the `*Undo_`
  container and returning `false` on empty (never falling through to floor).
  `undoDepth(scope, track)` = mirror of `checkpointDepth` over `*Undo_`. Clear all four
  in `seedFloor()`. Keep switches exhaustive, no `default:`.

- [ ] **Step 5: Run to verify the unit test passes**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests`
Expected: PASS.

- [ ] **Step 6: Add processor passthroughs.** In `src/PluginProcessor.h` next to the
  existing `snapshot`/`restoreOne` (~line 496–499):

```cpp
void armUndo(CheckpointScope scope, int track) { arrangement_.armUndo(scope, track); }
bool undo(CheckpointScope scope, int track);            // impl in .cpp, refreshes
[[nodiscard]] int undoDepth(CheckpointScope scope, int track) const
    { return arrangement_.undoDepth(scope, track); }
```

  In `.cpp`, `undo()` mirrors `restoreOne()`'s impl (`PluginProcessor.cpp:7062`):
  `const bool ok = arrangement_.popUndo(scope, track); if (ok) refreshWorkingFromModel(); return ok;`

- [ ] **Step 7: Redirect the group-(b) auto-captures.** Change each classified
  destructive-op site from `…snapshot(scope, track)` to `…armUndo(scope, track)`:
  `verbs::phrase` VerbDelete (`VerbCommands.cpp:258`) and the ~19 `PluginEditor.cpp`
  destructive sites. Leave the four `verbs::*` VerbSnapshot marks and the no-scope
  `VerbSnapshot` key branch untouched.

- [ ] **Step 8: Wire `Func+O`.** Add `AId::VerbUndo` to `KeyBindings.h` (near
  `VerbRestore`). Add the row to `KeyBindings.cpp` in the VerbClear block, above the
  bare `kModNone` CLEAR row:

```cpp
// 9.4 item E: Func+O = UNDO — the counter of Clear. Bare Func (no scope) only; a
// held scope's Func rows (INIT, ERASE) win on popcount, and Trig+Func+Clear (clear
// P-Locks) is handled inside verbs::trig, not here.
{ CB::VerbClear, -1, kModFunc, SL::Base, AId::VerbUndo, u8"UNDO", CS::FuncHeld },
```

  Add `virtual void undo() = 0;` to `CommandEffects.h`. In `CommandCore.cpp`, add
  `case AId::VerbUndo: fx.undo(); return true;` (near `AId::VerbRestore`, line 338).

- [ ] **Step 9: Implement `fx.undo()` in the editor.** In `PluginEditor.cpp`'s effects
  class (near `restorePop()`, ~line 231):

```cpp
void undo() override
{
    int ckTrk = 0;
    const CheckpointScope scp = ed.ckScope(ckTrk);
    if (!ed.processor_.undo(scp, ckTrk))
        ed.setStatus(status::nothingToUndo());
    else
        ed.setStatus(status::undid(scp));   // scope-named, authored in StatusText.h
    ed.refreshSurface();
}
```

  Add `nothingToUndo()` and `undid(CheckpointScope)` to `StatusText.h`. `undid` needs
  a scope→name mapping; keep it ASCII (`"Undid Track"` etc).

- [ ] **Step 10: Add the binding-resolution assertion** to `tests/KeyBindingTest.cpp`
  in `testVerbRow` (near the O rows):

```cpp
CHECK(resolve(CB::VerbClear, kModFunc) == AId::VerbUndo,
      "Func+O (no scope) = UNDO");
```

  (This replaces any prior expectation that `Func+O` fell through to CLEAR.)

- [ ] **Step 11: Build, run all tests, re-bless the golden.**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests && ./build/tests/lockstep_dispatch_tests`
Expected: unit tests PASS; the dispatch golden **fails** on the moved auto-captures
(destructive scenarios now change `ckpt.*`→`undo.*`) and the new `Func+VerbClear`
row. Add `undo.Song/Track/Scene/Phrase` to `verbDigest` in `DispatchGoldenTest.cpp`
(mirror the `ckpt.*` block, ~line 306) so undo is visible. Read the diff, confirm each
change is the intended redirect, then:
`LOCKSTEP_REGEN_GOLDEN=1 ./build/tests/lockstep_dispatch_tests` and re-run to confirm PASS.

- [ ] **Step 12: Tick ROADMAP E and commit** (list the Step-1 classification in the body).

```bash
git add src/core/Arrangement.h src/PluginProcessor.h src/PluginProcessor.cpp \
  src/PluginEditor.cpp src/command/VerbCommands.cpp src/command/CommandEffects.h \
  src/command/KeyBindings.h src/command/KeyBindings.cpp src/command/CommandCore.cpp \
  src/command/StatusText.h tests/CheckpointTest.cpp tests/KeyBindingTest.cpp \
  tests/DispatchGoldenTest.cpp tests/goldens/dispatch.txt ROADMAP.md
git commit -m "Split undo off the mark stacks and put it on Func+O"
```

---

## Task F: A restore arms undo (Func+O takes it back)

**Files:**
- Modify: `src/core/Arrangement.h` (`restoreOne` arms undo before overwriting)
- Test: `tests/CheckpointTest.cpp`

**Interfaces:**
- Consumes: `armUndo` (Task E), `restoreOne` (existing).
- Produces: no new symbol — `restoreOne` gains an `armUndo` call at its top.

- [ ] **Step 1: Write the failing test** — append to `tests/CheckpointTest.cpp`:

```cpp
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
```

Register `testRestoreIsUndoable();`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests`
Expected: FAIL — `undoDepth` is 0 after restore (restore doesn't arm undo yet).

- [ ] **Step 3: Implement.** In `Arrangement::restoreOne`, before the `switch`, arm the
  undo for the same scope so the pre-restore live state is recoverable:

```cpp
bool restoreOne(CheckpointScope scope, int track)
{
    // 9.4 item F: a restore overwrites live state, which is a destructive act — so it
    // arms undo, exactly like a clear or paste. Func+O then takes the restore back.
    // Arm only when there is actually a mark to restore (else it is a no-op and must
    // not leave a spurious undo entry).
    if (checkpointDepth(scope, track) == 0) return false;
    armUndo(scope, track);
    switch (scope) { /* … unchanged … */ }
}
```

  Note: the existing empty-stack guards inside the switch stay (defense in depth); the
  early `checkpointDepth==0` return preserves item A's "empty = no-op, no wipe" and
  ensures no undo is armed on a no-op. `restoreToFloor` is left un-armed for now
  (floor is the deliberate reset; revisit only if a CUJ asks).

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests`
Expected: PASS. Re-run `./build/tests/lockstep_dispatch_tests`; if a restore scenario
now shows an `undo.*` bump, read it, confirm it is the intended arm, and re-bless.

- [ ] **Step 5: Tick ROADMAP F and commit**

```bash
git add src/core/Arrangement.h tests/CheckpointTest.cpp tests/goldens/dispatch.txt ROADMAP.md
git commit -m "Make a restore arm undo, so Func+O takes it back"
```

---

## Task G: Surface — per-scope mark pips; repoint the CK:N chip

**Files:**
- Modify: `src/ui/SurfaceModel.cpp` (pip on each scope key from `checkpointDepth`)
- Modify: `src/ui/InspectorModel.cpp` (`:168`), `src/PluginEditor.cpp` (`:2056`)
- Test: `tests/goldens/dispatch.txt` (surface digest, if the pip is modeled there) or
  an assertion in the surface-model test if one covers scope keys.

**Interfaces:**
- Consumes: `checkpointDepth` (marks) and `undoDepth` (Task E).
- Produces: a pip decoration on the four scope-cluster keys.

- [ ] **Step 1: Find the scope-key emit** in `SurfaceModel.cpp` (grep the modifier
  cluster: `Track/Phrase/Scene/Song` cells). Confirm the `SurfaceModel` build has
  access to per-scope mark depth — thread `checkpointDepth(scope, focusTrack)` for each
  scope into the model input if not already present (mirror how other per-cell state
  arrives). Keep `buildSurfaceModel` pure (PRINCIPLES §19) — pass depths in, don't call
  the processor from inside.

- [ ] **Step 2: Add the pip.** For each scope key, when its mark depth > 0, set the
  cell's `pip` decoration channel to the count (add-only `CellState`/decoration, never
  renumber existing tokens — CLAUDE.md). Undo depth is NOT a mark and is not pipped
  here; it is named in the status lane (Step 3).

- [ ] **Step 3: Repoint CK:N.** The `InspectorModel.cpp:168` `"CK:N"` chip spoke only
  for the held scope's mark depth. Keep it as the *status-lane* summary of the held
  scope (it already reads `si.checkpointDepth`), but add a pending-undo note: when
  `undoDepth(heldScope) > 0`, append `"  UNDO:" + n`. Thread `undoDepth` into
  `StatusInput` next to `checkpointDepth` (`InspectorModel.h:67`, set at
  `PluginEditor.cpp:2056`).

- [ ] **Step 4: Build, smoke-test standalone, re-bless if the surface digest moved.**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests && ./build/tests/lockstep_dispatch_tests`
Expected: PASS (re-bless the golden only after reading a surface-digest diff).
Manual: launch `build/src/Lockstep_artefacts/Debug/Standalone/Lockstep`; hold `Track`,
tap `Y` twice, confirm the Track key shows a pip reading 2 and the status lane shows
`CK:2`; `Func+O` after a clear shows `UNDO:1`.

- [ ] **Step 5: Tick ROADMAP G and commit**

```bash
git add src/ui/SurfaceModel.cpp src/ui/InspectorModel.cpp src/ui/InspectorModel.h \
  src/PluginEditor.cpp tests/goldens/dispatch.txt ROADMAP.md
git commit -m "Show mark depth on each scope key; name the pending undo in the lane"
```

---

## Task H: Consolidated invariant tests + docs tick

**Files:**
- Modify: `tests/CheckpointTest.cpp` (the model's invariants as named cases)
- Modify: `ROADMAP.md` (tick H; flip 9.4's header if A–H all ticked)

**Interfaces:** none new — this task only proves the model.

- [ ] **Step 1: Add the invariant cases** to `tests/CheckpointTest.cpp` (some may
  already exist from D–F; add only the missing ones), then register each:

```cpp
static void testScopesAreIndependent()
{
    auto arr = makeArrangement();
    arr->snapshot(CheckpointScope::Track, 3);          // a Track-3 mark
    arr->snapshot(CheckpointScope::Song, 0);           // a Song mark
    CHECK(arr->restoreOne(CheckpointScope::Song, 0), "restore the Song");
    CHECK(arr->checkpointDepth(CheckpointScope::Track, 3) == 1,
          "a Song restore leaves the Track-3 mark untouched (no cross-scope rule)");
}

static void testUndoIsShallowNotSingle()
{
    auto arr = makeArrangement();
    arr->armUndo(CheckpointScope::Track, 0);           // op 1
    arr->armUndo(CheckpointScope::Track, 0);           // op 2
    CHECK(arr->undoDepth(CheckpointScope::Track, 0) == 2, "two undo levels held");
    CHECK(arr->popUndo(CheckpointScope::Track, 0), "pop op 2");
    CHECK(arr->popUndo(CheckpointScope::Track, 0), "pop op 1");
    CHECK(!arr->popUndo(CheckpointScope::Track, 0), "then empty");
}
```

  (`testRestoreIsUndoable`, `testUndoStackIsSeparateFromMarks`,
  `testSongStackEvictsByMemoryNotCount`, and the existing
  `testEmptyStackRestoreIsANoOp` cover the rest of the invariant set.)

- [ ] **Step 2: Run all tests**

Run: `cmake --build build -j$(nproc) && ./build/tests/lockstep_tests && ./build/tests/lockstep_dispatch_tests`
Expected: all PASS.

- [ ] **Step 3: Tick ROADMAP H; if D–H all ticked, flip the 9.4 header** from
  `*[specced 2026-07-14 — build open]*` to `*[shipped 2026-07-14]*` and compress the
  build-item detail per the ROADMAP compression convention (preamble).

- [ ] **Step 4: Commit**

```bash
git add tests/CheckpointTest.cpp ROADMAP.md
git commit -m "Prove the snapshot/undo invariants; close 9.4"
```

---

## Self-review notes (author)

- **Spec coverage:** D=independent stacks + budget (§13.6 memory-budget para); E=undo
  stack + Func+O (§13.6 "Marks and undo are separate"); F=restore-arms-undo (§13.6
  "Restore is undoable"); G=pips + lane (§13.6 "Depth is shown"); H=invariants. Redo
  and unrestore are explicitly out of scope (§13.6 "designed but deferred") — no task,
  by design.
- **Deferred, on purpose:** `REDO` (no grammar seat yet); `restoreToFloor` arming undo;
  P-Lock heap tails in the byte estimate. Each is called out where it arises.
- **Golden discipline:** E and F move the golden because auto-captures relocate from
  `ckpt.*` to `undo.*`; the plan adds `undo.*` to `verbDigest` so the move is *visible*
  rather than silent, and mandates reading the diff before every re-bless.
- **Type consistency:** `armUndo`/`popUndo`/`undoDepth` are used identically in Tasks
  E/F/H and the processor passthroughs; `checkpointBytes`/`kCkBudgetBytes` only in D.
