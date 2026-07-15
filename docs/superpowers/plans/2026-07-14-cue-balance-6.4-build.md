# 6.4 Cue balance — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans
> to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Build the per-track cue-balance crossfade (DESIGN §31 / spec
`docs/superpowers/specs/2026-07-14-cue-balance-6.4-design.md`) — the buildable
core of ROADMAP 6.4.

**Architecture:** Cue balance `b∈[0,1]` is a per-track **APVTS param**
(persistent performance overlay, mirroring `trackMute`). It is applied as a
**fan-out gain at the deposit stage** (never in place on `trackBuffers_`, so
direct track taps bypass cue by construction): route/sends scale by `(1−b)`, a
new `depositRoutedToCue` adds `×b` into cue bus 1. A per-track smoothed value +
scratch buffer give a per-sample declick ramp for the quantized flip. Surfaces:
a direct `Cue + focused-track` gesture, a two-page cue overlay (step-grid
quantized flip + encoder param page), an AMP-page param cell, and a surface
indicator.

**Tech Stack:** C++20, JUCE 8, namespace `lockstep`. Tests are two binaries
(`./build/tests/lockstep_tests`, `./build/tests/lockstep_dispatch_tests`) — NOT
ctest.

## Global Constraints (copied from project rules — apply to every task)

- Strict warnings, `-Werror`: `-Wall -Wextra -Wpedantic -Wsign-conversion
  -Wfloat-conversion -Wshadow -Wimplicit-fallthrough`. Switch over a closed enum
  omits `default:` (so `-Wswitch` catches a new value); every code-carrying
  `case` ends in break/return/throw/`[[fallthrough]]`.
- **Never `git add -A` / `git add .` / `git commit -a`** — stage explicit paths.
  Never stage `CLAUDE.md` / `GEMINI.md` / `AGENTS.md` / `.claude/` (staging hook
  blocks it). Commit per task.
- Status strings only in `src/command/StatusText.h`. No non-ASCII in
  `juce::String(const char*)` — use `u8"…"`.
- New `ActionId` values **append at the enum end** (above `Count`) — the dispatch
  golden records actions by ordinal; a mid-enum insert renumbers everything.
- **Audio-path tests must call** `proc.setRateAndBufferSizeDetails(44100, 512)`
  before the first block, or machines go silent (getSampleRate()==0).
- `CellState` is add-only; `SurfaceCell` frozen prefix is offset-bound — new
  fields append to the non-frozen extension.
- Build: `cmake --build build -j$(nproc)`. Golden re-bless (if needed):
  `LOCKSTEP_REGEN_GOLDEN=1 ./build/tests/lockstep_dispatch_tests`, then read
  `git diff tests/goldens/dispatch.txt` before accepting.

## Build vs. deferred

**This plan builds:** state + serialization, the audio crossfade, tap
verification, the direct gesture, the two-page overlay, the quantized flip, the
AMP param cell, the surface indicator, tests.

**Deferred to follow-on ROADMAP items (NOT here):**
- **Morph-cue integration.** Morph is per-scene and `(track,slot)`-keyed
  (`scene.morphA/morphB`, `morphBlend`), but cue balance is a persistent
  non-scene overlay. Driving a persistent overlay from per-scene morph endpoints
  is a real design question, not wiring — it needs its own session. The quantized
  flip covers "bring in N cued tracks at once" without morph.
- `Cue + Scene` double-resolve pre-listen.
- `Cue + MIDI-out` copy to a cue MIDI destination.

---

## Task 1: Cue-balance state (APVTS param + accessors)

**Files:**
- Modify: `src/ParameterIDs.h` (add `cueBalance(int)` id)
- Modify: `src/Parameters.cpp:45-53` (add the param next to `trackMute`/`trackSolo`)
- Modify: `src/PluginProcessor.h` (accessor decls + `cueBalanceParams_` cache,
  near `trackMuteParams_` at ~1712 and the mute accessors at ~548)
- Modify: `src/PluginProcessor.cpp` (accessor defs; cache the atomic pointers
  where `trackMuteParams_` is populated)
- Test: `tests/CueBalanceTest.cpp` (new; register in `tests/CMakeLists.txt`
  alongside the other `lockstep_tests` sources)

**Interfaces produced (used by later tasks):**
- `ParamIDs::cueBalance(int t) -> std::string` = `"track_" + t + "_cue"`
- `float LockstepProcessor::getCueBalance(int track) const` (0 out of range)
- `void LockstepProcessor::setCueBalance(int track, float b)` (clamps [0,1],
  writes through APVTS via `setValueNotifyingHost`-equivalent used by
  `setGlobalMute`)
- `void LockstepProcessor::toggleCueBalance(int track)` (snaps 0↔1)

- [ ] **Step 1 — failing test.** In `tests/CueBalanceTest.cpp`: construct a
  `LockstepProcessor`, assert `getCueBalance(0) == 0.0f` (default), then
  `setCueBalance(0, 0.75f)` and assert `getCueBalance(0) == 0.75f`, and
  `toggleCueBalance(1)` flips `0→1→0`. Round-trip: set balances, call
  `getStateInformation`/`setStateInformation` into a fresh processor, assert they
  survive (APVTS carries them for free — this proves it).
- [ ] **Step 2 — run, expect fail** (`getCueBalance` undefined):
  `cmake --build build -j && ./build/tests/lockstep_tests`.
- [ ] **Step 3 — implement.** Add the ID; add the param (mirror the `trackMute`
  block, `AudioParameterFloat` range `{0,1}` default `0`, name
  `"Track N Cue"`); cache `cueBalanceParams_[t]` where `trackMuteParams_` is
  filled; implement the three accessors mirroring `getGlobalMute`/`setGlobalMute`.
- [ ] **Step 4 — run, expect pass.**
- [ ] **Step 5 — commit** (`src/ParameterIDs.h src/Parameters.cpp
  src/PluginProcessor.h src/PluginProcessor.cpp tests/CueBalanceTest.cpp
  tests/CMakeLists.txt`).

---

## Task 2: Distribution-stage crossfade + declick

**Files:**
- Modify: `src/PluginProcessor.h` (declare `depositRoutedToCue`; add
  `cueGain_` smoothed-gain array + `cueSplitScratch_` buffer, mirroring
  `muteGain_`/`cutRampScratch_`)
- Modify: `src/PluginProcessor.cpp` — send deposit (~1167-1184), `depositToBus`
  (~1403), `sumRoutedToMaster` (~1414), `depositRoutedToAux` (~1427), the two
  processBlock call sites (~2562-2570, ~3582-3590), `prepareToPlay` (allocate
  scratch; set smoothing ramp)
- Test: `tests/CueBalanceTest.cpp` (audio-path cases)

**Interfaces produced:**
- `void depositRoutedToCue(juce::AudioBuffer<float>& fullBuffer, int n)` — for
  every track with effective `b>0`, adds its cue-scaled signal into cue bus 1.

**Key constraint:** cue gain is applied to the **deposited copies**, never in
place on `trackBuffers_[i]` (mute uses `applyGain` in place at ~2561 — cue must
NOT, or direct taps stop bypassing cue). Per-sample ramp via `cueSplitScratch_`.

- [ ] **Step 1 — failing tests** (audio-path; each calls
  `setRateAndBufferSizeDetails(44100,512)` first, then processes blocks):
  - **Crossfade:** a Master-routed track producing a known signal. `b=0` →
    mainOut has full signal, cue bus silent. `b=1` → mainOut silent, cue bus has
    full signal. `b=0.5` → each ≈ half (allow ramp settle over a few blocks).
  - **Send fade:** with Send A > 0, `b=1` → send bus contribution →0 (assert the
    send bus magnitude drops as `b→1`).
- [ ] **Step 2 — run, expect fail.**
- [ ] **Step 3 — implement.**
  - Add `cueGain_[kNumTracks]` (a per-track smoothed gain like `muteGain_`),
    `cueSplitScratch_` (stereo, block-sized). In `prepareToPlay` size the scratch
    and set the smoothing time (~5 ms) and reset targets to `getCueBalance`.
  - Per block, per track: set `cueGain_[i]` target = `getCueBalance(i)`.
  - **Cue deposit** (`depositRoutedToCue`, called right after
    `depositRoutedToAux` at both sites): for each track, if target or current
    gain > 0, copy `trackBuffers_[i]` into `cueSplitScratch_`, apply the per-
    sample `b` ramp, add into `getBusBuffer(fullBuffer, false, 1)` when cue bus
    enabled.
  - **Main-path `(1−b)`:** scale the route/send deposits by `(1−b)`. Sends
    (~1174/1181): multiply the existing `trackSendA/B` scalar by
    `(1 − cueGain_[i].getCurrentValue())` (block-rate is consistent with sends'
    existing scalar gain). Route sums (`sumRoutedToMaster`, `depositToBus`,
    `depositRoutedToAux`): pass a per-sample `(1−b)` ramp — apply it via a scaled
    copy through `cueSplitScratch_` (reuse the scratch: fill with
    `trackBuffers_[i]`, ramp by `(1−b)`, then `addFrom` the scratch) so the
    declick is per-sample on the audible path too. Keep `trackBuffers_[i]`
    itself untouched.
  - Advance the `cueGain_[i]` ramp once per block (skip-to-target when
    `b==target` to avoid cost when nothing is cued).
- [ ] **Step 4 — run, expect pass.**
- [ ] **Step 5 — commit.**

---

## Task 3: Tap semantics + feedback-guard invariance (tests)

**Files:** Test only — `tests/CueBalanceTest.cpp`. (If a test fails, the fix
belongs in Task 2 — cue must be deposit-stage, not in place.)

- [ ] **Step 1 — failing/using tests:**
  - **Direct track tap bypasses cue.** Track B's `input_source = Track A`; A is
    Master-routed and cued (`b=1`). Assert B still receives A's full signal
    (B reads `trackBuffers_[A]`, upstream of the split). Drive one block, inspect
    B's input via an existing tap harness or the resulting output.
  - **Master tap reflects cue.** Track C's `input_source = Master`; a Master-
    routed track D is cued (`b=1`). Assert D is absent from what C taps (Master
    sum excludes the cued portion).
  - **`outputReachesMaster` stays static.** A Master-routed track at `b=1` still
    returns `outputReachesMaster(track) == true`.
- [ ] **Step 2 — run.** If bypass/reflect fails, correct Task 2 (ensure cue is
  applied only at deposit; `trackBuffers_` untouched; Master tap reads the
  post-deposit master, not pre-cue).
- [ ] **Step 3 — commit** (test file; plus any Task-2 correction).

---

## Task 4: Direct `Cue + focused track` gesture

**Files:**
- Modify: `src/PluginEditor.cpp` (Cue-scope dispatch near `enterCueScope`/
  `auditionStepDown`, ~4302-4360; the down-handler `dispatchDown` ~4390+)
- Modify: `src/command/StatusText.h` (a `cueToggled`/`cueBalance` status line)
- Test: `tests/KeyBindingTest.cpp` or a new gesture test — assert the gesture on
  the focused track calls `toggleCueBalance(focused)` and lands on the right
  track under the Cue scope (scope-routing).

**Binding:** while the `Cue` scope is held (`cueHeld`), the gesture toggles the
**focused** track's cue balance (`keyboardArea_.getActiveTrack()`), snapping
0↔1. Must NOT disturb the momentary audition (`Cue+step` / `Cue`-held remain).
Pick a free key under Cue that does not collide with audition steps — reuse the
existing `ToggleMute` (`Z`) chord read (`Cue + Mute` = "cue focused track"),
mirroring how Mute overloads per-track cells; confirm no existing `Cue+Mute`
binding first (`grep kModMute src/command/KeyBindings.cpp`). If taken, fall back
to a dedicated verb-free key and record the choice in the spec.

- [ ] **Step 1 — failing test** asserting the chord routes to
  `toggleCueBalance(activeTrack)`.
- [ ] **Step 2 — run, expect fail.**
- [ ] **Step 3 — implement** the dispatch + status text.
- [ ] **Step 4 — run, expect pass; build.**
- [ ] **Step 5 — commit.**

---

## Task 5: Cue overlay — two pages

**Files:**
- Modify: `src/state/UiState.h` (add `Overlay::Cue` enum value)
- Modify: `src/ui/mode/ModeReducer.cpp` (add one `kOverlays` descriptor row —
  mirror `Overlay::Density` at ~58; bump the `std::array<..., 6>` to 7; add the
  `escapeOverlay`/`activeOverlay` cases the `-Wswitch` demands)
- Modify: `src/PluginEditor.cpp` (open gesture; render the two pages; Nav pages
  between them)
- Test: `tests/` mode test — entry/exit + the two-page toggle (mirror an
  existing overlay test).

**Pages:** page 0 = **step-grid quantized flip** (16 step keys = 16 tracks; a
tap arms that track's pending cue flip — store in a
`std::array<bool,kNumTracks>` pending mask, mirror `pendingPatternMuteToggle` in
UiState.h:219). Page 1 = **encoder param page**: the 8-slot MZ band edits
continuous `cueBalance` for a window of tracks (reuse the meta-band param
plumbing; each encoder → `setCueBalance(track, v)`).

- [ ] **Step 1 — failing test:** entering `Overlay::Cue`, `activeOverlay` returns
  it; Nav toggles page; escape resets. (Mirror the Density overlay test.)
- [ ] **Step 2 — run, expect fail.**
- [ ] **Step 3 — implement** enum + descriptor + open gesture + two-page render.
- [ ] **Step 4 — run, expect pass; build (the `-Wswitch` exhaustiveness will
  force the escape/active cases).**
- [ ] **Step 5 — commit.**

---

## Task 6: Quantized flip + declick

**Files:**
- Modify: `src/PluginProcessor.{h,cpp}` (a `queueCueFlip(mask)` that, on the next
  launch quantum, applies the pending flips by `setCueBalance` — the Task-2 ramp
  provides the ~5 ms declick automatically)
- Modify: `src/PluginEditor.cpp` (fire the overlay's pending mask through
  `queueCueFlip`)
- Test: `tests/` — arming a mask then reaching the quantum flips those tracks'
  balances (drive the launch-quantize clock as existing launch tests do).

**Reuse:** the 9.17 `LaunchQuant` authority (same path scene launches use). The
flip toggles each armed track (`b -> 1-b`, or force in/out — match the overlay's
arm semantics). Declick is inherited from Task 2's `cueGain_` smoothing; no new
ramp.

- [ ] Steps 1–5 as above (failing quantum test → implement → pass → commit).

---

## Task 7: AMP/CHANNEL page cue-balance param cell

**Files:**
- Modify: the channel/AMP section param assembly (find via `grep -n "channelState\|chanOff\|AMP\|kChanSecIdx" src/PluginProcessor.cpp src/ui/*.cpp`)
  — surface `cueBalance` as a read/write cell in the AMP/CHANNEL page, edited
  like Level (write via `setCueBalance`).
- Test: editing the cell writes `getCueBalance`.

**Note:** this cell edits the *overlay* param, not a scene slot — it reads/writes
`getCueBalance`/`setCueBalance` directly, not `channelState.setSlot`.

- [ ] Steps 1–5 (failing cell test → implement → pass → commit).

---

## Task 8: Surface cue indicator

**Files:**
- Modify: `src/ui/SurfaceModel.h` (add a cue decoration — a non-frozen field or a
  new add-only `CellState` token; do NOT renumber existing tokens)
- Modify: `src/ui/SurfaceModel.cpp` (set the indicator on cued track cells from
  `getCueBalance > 0`)
- Modify: `src/ui/KeyButton.cpp` (paint it)
- Test: `tests/SurfaceModelTest.cpp` — a cued track's cell carries the indicator;
  an uncued one does not.

- [ ] Steps 1–5 (failing model test → implement → pass → commit).

---

## Task 9: Docs + roadmap + sweep

**Files:**
- Modify: `README.md` — move cue from "planned (6.4)" to implemented for the
  balance feature; update the shortcut table with the `Cue + track` gesture and
  the cue overlay; keep `Cue + Scene` / `Cue + MIDI-out` / morph-cue marked
  planned. Verify the shortcut table matches the real binding chosen in Task 4.
- Modify: `ROADMAP.md` — tick the 6.4 balance-core boxes; leave morph-cue,
  `Cue+Scene`, `Cue+MIDI-out` as open follow-on items; if the balance core fully
  closes the milestone's *core*, mark 6.4 accordingly and file the three
  deferrals as their own entries.
- Modify: memory (`project_...` cue file + MEMORY.md index) noting the shipped
  core and the deferred morph-cue design question.

- [ ] **Step 1 — full build + both test binaries green.**
- [ ] **Step 2 — standalone smoke:** cue a track (gesture), confirm it leaves the
  main mix and appears on cue (ch 3–4); the quantized flip brings armed tracks in
  on the quantum without clicks.
- [ ] **Step 3 — commit** docs + roadmap. (Memory files are untracked-safe to
  write; they are not committed.)

---

## Self-review notes

- Type consistency: `getCueBalance`/`setCueBalance`/`toggleCueBalance`,
  `depositRoutedToCue`, `cueGain_`, `cueSplitScratch_`, `ParamIDs::cueBalance`,
  `Overlay::Cue`, `queueCueFlip` — used consistently across tasks.
- The one risk area is Task 2's per-sample `(1−b)` on the route sums via the
  shared scratch: if it proves fiddly, a first cut may use a block-rate scalar
  `(1−b)` on the route sums (matching sends) and keep the per-sample ramp only on
  the cue deposit — clicks on the audible path during a flip would then be a
  known follow-up, but sends already behave that way. Prefer the per-sample path;
  fall back only if it blocks.
