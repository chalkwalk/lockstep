# Structural debt arc — execution plan (9.15 close-out + 9.12 stages 5–8)

Self-contained plan for the arc chosen in the 2026-07-12 alignment review.
Written against the code as of `263cf9e`. Two items, in order: close `9.15`
(small; makes PRINCIPLES §22 honest), then the `9.12` dispatch migration (large).

---

## Part A — 9.15 close-out: make §22 true

### Finding: the stages are all ticked, but the invariant is not enforced

All four 9.15 stages are `[x]`, and the *mechanism* is genuinely in place: one
`SurfaceDispatcher`, one `renderSurfaceFrame()`, the per-mode polling timers are
gone. But the principle §22 claims ("one channel; a redraw never depends on a
human remembering to call it") is still only a convention. Residue found by
audit:

1. **`KeyboardArea` self-repaints on surface-changing state** —
   `setActiveTrack` (l.134), `setPage` (153), `setDisplayMode` (160),
   `clampPage` (191), section changes (420, 436), `syncToActiveTrack` (443),
   mouse-up (725) all call the component's own `repaint()`. That redraws the
   grid but **never reaches the dispatcher**, so open controllers are refreshed
   only if whatever called it *also* happened to call `refreshSurface()`. The
   active-track path (`onActiveTrackChanged` → `syncToActiveTrack`) is exactly
   that shape: LEDs are rescued by the caller, not by the change.
2. **Bare `repaint()` at surface-changing sites in the editor** —
   `PluginEditor.cpp:538` (mouse step-trig toggle: mutates `s.trig`, repaints
   chrome only), `:1270` (generator-hub long-press sets `generatorHubHeld`),
   `:1595` (release sweep after `dispatchUp`), `:567` (track-page flip).
   Legitimately chrome-only and staying as-is: `:1259` (missing-sample banner),
   `:1358` (capture strip timer), `:2313` (file-drag overlay).
3. **A comment describing a deleted mechanism** — `PluginEditor.cpp:764`
   explains the extra grid repaint as compensating for "the KeyboardArea timer
   [which] only repaints on playhead movement". `KeyboardArea` has had **no
   timer** since Stage 3. The comment now teaches a false model.

### Stage 5 — enforce the channel, sweep the residue

- **`KeyboardArea` loses the right to repaint itself.** It gets an injected
  `onSurfaceDirty` callback (wired by the editor to `refreshSurface()`); every
  state-changing setter calls that instead of `repaint()`. The component still
  repaints — but *via the one channel*, so the controllers come with it. Paint
  handlers and pure-visual hover/press state keep their local repaints.
- **Editor residue** repointed to `refreshSurface()` (the four sites above);
  the three chrome-only sites keep the bare call **and gain a one-line reason**,
  so "bare repaint" always means "deliberately chrome-only".
- **Structural guard** (the §20 bar: structure, not memory). Add
  `tests/SurfaceInvalidationGuardTest.cpp` — a source-scanning test that walks
  `src/ui/*.cpp` + `src/PluginEditor.cpp`, flags any `repaint()` /
  `.repaint()` outside an explicit allowlist (paint-path/self-visual sites and
  `renderSurfaceFrame`), and fails with the offending file:line. A new bare
  repaint at a surface-changing site then fails the build, which is what §22
  has been asserting all along.
- Delete the stale timer comment; correct §35.9 if it repeats the claim.
- Then: mark 9.15 **shipped**, and drop the "not yet true" hedge from
  PRINCIPLES §22 — it becomes descriptive, as intended.

*Deliverable: 1–2 commits (docs tick + code). Low risk, no behaviour change
except controllers no longer going stale on the paths in (1).*

---

## Part B — 9.12 stages 5–8: the dispatch migration

### Finding: the ROADMAP's Stage 5 cannot be built as written

Stage 5 says: *"`RecordingEffects`-driven `tests/DispatchGoldenTest.cpp`
captures current `dispatchDown`/`dispatchUp` output across all families **before
any dispatch rewrite**."* That premise is broken, and 9.13 already proved it
empirically — its Stage 7/8 note records that an editor-dispatch harness was
**"unviable headless (component-teardown segfault)"** and had to pivot to a pure
seam.

The measurements say the same thing:

| | |
|---|---|
| `dispatchDown` | **2,079 lines** (`PluginEditor.cpp` 3659–5737) |
| `dispatchUp` | **~657 lines** (5763–6420) |
| `processor_` references inside `dispatchDown` | 181 |
| `uiState_` references | 160 |
| **component references** (`keyboardArea_`, `manipulationZone_`, `repaint()`, buttons) | **133** |
| `resolveBinding` calls | **1** |
| `handleAction` calls | **1** |

Two things follow. First, dispatch is ~2,700 lines of imperative branching that
lives *inside a `juce::Component`*, and those 133 component references are what
make it untestable headlessly — so the golden net can't be written until the
code moves. Second, the grammar table that Stage 2 made authoritative **for
display** is used exactly *once* by dispatch: display and behaviour are still
two hand-synced descriptions of the same grammar, which is the drift 9.12
exists to kill.

So the chicken-and-egg is real: the golden test that protects the rewrite is
blocked by the same coupling the rewrite removes. Resolve it by **extracting
first, mechanically** — a pure move is reviewable by eye and needs no golden net
to be trusted, in a way that a logic rewrite never is.

### Decision (2026-07-13): prove it, don't assume it

The extraction below is **not** approved on the strength of the argument above.
9.13's "unviable headless" note is second-hand evidence, and the conclusion is
expensive enough to deserve a first-hand attempt: **Stage 5 begins by seriously
trying to golden-test dispatch in place.** Avenues, cheapest first:

1. `ScopedJuceInitialiser_GUI` + a real `MessageManager` in the test binary, with
   the editor constructed and destroyed in the right order (editor before
   processor) — the classic cause of a component-teardown crash.
2. Heap-allocate the editor and **deliberately leak** it (never destruct) — the
   golden net only needs `dispatchDown`/`dispatchUp` output, not a clean exit.
3. Reproduce the 9.13 segfault under a debugger and fix the actual cause; it may
   be a harness bug, not a law of nature.

Only if all three genuinely fail — reproduced and understood, not merely
believed — does the mechanical extraction become the prerequisite it is
described as. If one succeeds, the golden net is written **first** (against
today's un-rewritten dispatch, exactly as the original ROADMAP intended) and the
extraction becomes an optional cleanup that the goldens now protect, rather than
a leap of faith taken to enable them.

### Revised staging (applies only if golden-testing in place is proven impossible)

- **Stage 5 — Dispatch seam (mechanical extraction, zero behaviour change).**
  `dispatchDown`/`dispatchUp` move to `src/command/Dispatch.{h,cpp}` as free
  functions over `(UiState&, DispatchCtx, DispatchEffects&)`. `DispatchEffects`
  extends `CommandEffects` with the editor-only effects the 133 component refs
  need (set active track, set page/section, open/close overlays, refresh bands,
  invalidate surface, audition). The editor keeps a thin
  `EditorDispatchHost : DispatchEffects` that implements them with the calls
  that are inline today. **No branch is rewritten, none deleted** — only moved
  and re-expressed through the seam. Green build + existing tests + a standalone
  smoke are the acceptance bar.
  *Largest, riskiest stage; likely its own branch, landed as several
  family-sized commits (modifiers → verbs → nav → sections → steps).*
- **Stage 6 — Golden net.** `tests/DispatchGoldenTest.cpp` drives the extracted
  core with `RecordingEffects` across every family × gesture × held-modifier
  combination, snapshotting the emitted effect stream. This is the safety net
  Stage 5 of the old plan wanted, now actually buildable. **It must be written
  against the *extracted but not yet rewritten* code** — that is the whole point:
  it pins today's behaviour before any of it changes.
- **Stage 7 — New ActionIds + handlers** (was Stage 6). Append-only `ActionId`s
  for latch, escape, restore pop/floor, rec-arm overdub, play-stop, step latch,
  nav unlock, overlay-entry opens; `DispatchEffects` methods; guard test.
- **Stage 8 — Table migration, family by family** (was Stage 7). Each sub-step
  routes via `resolve(..., gesture).action → handleAction`, deletes the
  imperative branch, and keeps the goldens green. A family whose goldens change
  is a *bug found*, not a test to update — the golden diff is the review artifact.
- **Stage 9 — Exhaustiveness guard + cleanup** (was Stage 8). `handleAction`
  switch exhaustive over `ActionId` with **no `default:`** (so `-Werror=switch`
  catches a new unhandled action); test that every `ActionId` is reachable;
  remove `KeyBinding::hint`.

### What this buys (why it is worth the size)

- Display and dispatch stop being two descriptions of one grammar — the drift
  that 9.12 was filed to kill is only *half* dead today (display derives; dispatch
  doesn't).
- ~2,700 lines of behaviour become testable headlessly, which is the precondition
  for every future surface change (5.3 Song/Scene UI, 9.4 snapshot semantics, 6.4
  Cue) landing with a test instead of a hand-check.
- `PluginEditor.cpp` drops from 7,177 lines to roughly 4,400.

### Risks / mitigations

- **A mechanical move that isn't** — the temptation to "fix" a branch while
  moving it. Mitigation: Stage 5 commits are diff-reviewed for *moves only*;
  any behaviour change is split into its own follow-up commit with a note.
- **Effects-interface sprawl** — 133 refs could become 133 methods. Mitigation:
  group by family; prefer a handful of coarse effects (`refreshBands()`,
  `setGridFocus(...)`) over one method per call site.
- **Gap in the golden net** = a silently changed gesture. Mitigation: Stage 6
  enumerates from the binding table itself (every row × every gesture), not from
  a hand-written list, so a family cannot be forgotten.

---

## Suggested order

A (9.15, small, closes a principle) → B stage 5 → B stages 6–9.
Part A is independently shippable and does not block Part B.
