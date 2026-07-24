# Critical User Journey (CUJ) catalogue

The automated equivalent of a manual test sheet a team would run before every
release. Each entry is a whole user task — described the way a person performs it —
plus the first-order outcomes a manual tester would tick off. Journeys are
implemented as end-to-end tests in `lockstep_dispatch_tests` (the only suite that
links the real headless editor + `UiDriver`), and grown in **waves**: this sheet is
the durable artifact that outlives any one arc, and every wave flips rows here.

## How to read a row

Each journey lists an ordered **gesture script** (how a person does it), the
**assertions** (the durable processor-state effect *and* the visible affordance on
`surface()` — reach for a scene golden only for chrome that has no model), and the
**harness deps** it needs. The **status** column is the wave tracker.

**Status legend**

- ☑ — implemented (a passing test exists in `lockstep_dispatch_tests`).
- ~ — partial (some assertions land; gaps noted inline).
- ☐ — planned (documented here, not yet built).

**Harness-dep legend** (Phase-0 enablers, see `UiDriver.h` / `AudioRig.h`)

- `audio` — the live-audio bridge (E1): `runBlocks`/`play`, `lastRms`, `hasNaN`.
- `midi-in` — MIDI-note-in verbs (E2): `noteOn`/`noteOff`/`playNote`.
- `midi-out` — MIDI-out capture (E3): `midiOut()` through the bridge.
- `mz` — semantic MZ param verb (E4): `setParam(slot, value)` via the armed path.
- (no tag) — pure gesture + state/surface; needs none of the above.

## Assertion philosophy

Default for every CUJ: assert the *durable effect* on processor state
(`proc().sequence()...`, `proc().arrangement()`, `getMachineId`, clipboard,
`checkpointDepth`) **and** the *visible affordance* on `surface()` (cell
colour/text/decorations, `activeLayer`, MZ slot `hasOverride`/`valueText`, banners).
A scene golden is a last resort, for chrome with no model (transport readouts,
inspector, pop-overs). Every journey asserts it truly *reached* its precondition
before checking the outcome (the `expectReached` discipline, E5) — a setup that
silently failed must fail loudly, not assert against a wrong start state.

---

## Group A — Trig authoring & the pattern

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| A1 | Two-track drum beat | ☑ | audio |
| A2 | Trig conditions | ☐ | mz |
| A3 | Step editing | ☐ | |
| A4 | Copy/paste/clear across scopes | ☐ | |

**A1 — Two-track drum beat.** ☑ `CujTrigAuthoringTest.cpp`. Focus track 0, clear it
(`Track+CLEAR`, confirmed — clearing a track is a guarded destructive verb); `step`
0,4,8,12 → those `.trig` set, surface cells read `StepTrigCertain`, off-beats
`StepEmpty`. `Func+Phrase+step 6` → `tracks[0].length==7` (polymeter; this is the
phrase-length authoring gesture, DESIGN §34.4 — *not* a TRIG field). Select track 1,
clear, `step` 4,12 → track-1 backbeat, surface confirms. Then `play()` (bridge) →
non-silent peak RMS across the roll, no NaN. Both tracks run FMMachine so the beat
sounds. (No `mz` dep in the end: length uses the gesture, not the MZ.)

**A2 — Trig conditions.** Held-step bare `TRIG` promotes to per-step COND (TRIG
relabels); set probability/iteration via `setParam`; assert the step's condition
override. `Func+TRIG` with no step → track base condition. Assert grid brightness
ladder on `surface()` (certain=bright, prob=mid, skip=dim).

**A3 — Step editing.** Hold step → inspector re-skin (`activeLayer`/surface shows
P-Locks). `hold step + SRC` → note-edit overlay (NOTE relabel, chromatic cells).
`hold step + ←/→` → bubble-swap (trig moves to neighbour). `hold step + Func+←/→` →
microOffset changes. `hold step + P` (QUANT) → microOffset zeroed.

**A4 — Copy/paste/clear across scopes.** `step+U` → clipboard type==step; `Track+U`
→ type==track; `section+U` → type==section (the canonical "same verb, three
scopes"). Paste `I`, clear `O`; assert target state + clipboard badges.

## Group B — Generators (all via the generator hub, `3` held ≥350 ms)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| B1 | Euclid generate + commit/cancel | ☑ | mz |
| B2 | Density overlay | ☐ | mz |
| B3 | Velocity overlay | ☐ | mz |
| B4 | Melodic generator | ☐ | mz |
| B5 | Harmonic voice-mover | ☐ | mz |

**B1 — Euclid.** ☑ `CujGeneratorsTest.cpp`. Hold the 3-key past the long-press +
tick the timer → hub opens; `step 0` picks the EUCLID cell → `euclidHeld`,
`activeOverlay==Euclid`, `resolveMetaBand==Euclidean` (PULSE/OFSET/ACCNT on the
encoders). `setParam` retunes PULSE 4→6 through the armed MZ path → live preview
spreads six onsets. Bare `P` bakes it (six-onset Euclid rhythm, ≠ the seed); a
second run enters and cancels with `Func+P` → the original pattern is restored
exactly. Started from a clustered non-Euclidean seed so the redistribution is
observable.

**B2 — Density overlay.** Hub → DENSITY; thin trigs; assert emitted census drops;
per-track selection detents (Scrub/Re-roll/Exempt) reachable; MOD sub-page cycle.

**B3 — Velocity overlay.** Hub → VEL; AMP re-press cycles Depth/Center/Mode/Blend
sub-pages (assert page dots); assert overlay velocity applied at emit.

**B4 — Melodic generator.** Hub → MELODY; `setParam` DENSE/CORE/SEED; assert live
preview; `P` prints editable steps with pitches in the effective key; **seed
determinism** — same SEED in same place reproduces the same line, different track
differs (assert two tracks diverge).

**B5 — Harmonic voice-mover.** Hub → CHORD; four-voice view (assert surface layout);
edit a voice in-scale; `LEN` grows (clones prev chord); `P` prints one chord per bar
of the in-scope time-sig; no two voices share a pitch.

## Group C — Sound, sections, P-Locks

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| C1 | P-Lock one step | ☐ | mz |
| C2 | Per-step sample swap (Sound Pool) | ☐ | |
| C3 | Machine picker | ☐ | |
| C4 | Section paging & scope colour | ☐ | |
| C5 | Control-All | ☐ | mz |
| C6 | P-Lock clear gestures | ☐ | mz |

**C1 — P-Lock one step.** SRC section; `hold step + setParam(slot, v)` → override set
on that step, **base unchanged** (the OEB invariant), MZ slot shows `hasOverride`.
Clear via `hold step + Func+O`.

**C2 — Per-step sample swap (Sound Pool).** `Fill+SRC` → grid re-skins to Sound
Pool, SRC glows Fill colour. Press cell (no step) → live-swap focus track. `hold
step` + cell → `sound_id` P-Lock baked on that step; release Fill → restore.

**C3 — Machine picker.** `Track + hold(SRC)` → grid re-skins to machine names; press
cell → `getMachineId(focus)` changes; **SRC panel reachable** (guard the
`numSections()` regression — the picked machine's SRC page paints).

**C4 — Section paging & scope colour.** Machine pages read neutral, `Track`-held
pages read cyan; page dots per section; re-press cycles.

**C5 — Control-All.** `Track` held, no track selected → next `setParam` broadcasts to
every track with a matching control (assert all tracks' base moved).

**C6 — P-Lock clear gestures.** `Trig+Func+O` clears all P-Locks (trig intact);
`Trig+slot+O` clears one slot; `Trig+SRC+O` clears note/vel/gate only. Assert the
hint band surfaces these when a locked step is held.

## Group D — Performance overlays

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| D1 | Mute | ☐ | audio |
| D2 | Fill | ☐ | |
| D3 | Morph | ☐ | mz |
| D4 | Cue | ☐ | |
| D5 | Checkpoints | ☐ | |

**D1 — Mute.** `Mute+step` global mute (survives scene change); `Scene+Mute+step`
per-scene mute; assert mask + that a muted track's `play()` RMS drops (bridge).

**D2 — Fill.** `Fill+step` marks fill-only (dim until held); assert those steps fire
only while `Fill` held (census under hold vs release).

**D3 — Morph.** `Morph + setParam` sculpts at fader split; `Morph+↑/↓` pure A/B;
`Morph+Mute` fluid mute; `Morph+O` BAKE, `Func+Morph+O` ERASE. Assert map state +
crossfader value.

**D4 — Cue.** `Func+3` → Cue scope (`3` shows CUE cyan hint); `Cue+Mute` toggles
focus-track cue balance; `Cue+hold(AMP)` → cue console sticky (8 balance slots, AMP
pages bank). Reuse existing `cue-console` scene golden for the chrome.

**D5 — Checkpoints.** Bare `Y` (SNAP) scope-respecting push → `checkpointDepth`
increments for the held scope; `Func+Y` restore tap=pop/hold=floor; `Func+O` undo.
Assert state reverts.

## Group E — Launch / arrangement

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| E1 | Scene | ☐ | |
| E2 | Phrase | ☐ | |
| E3 | Song | ☐ | |
| E4 | Deletion picker | ☐ | |

**E1 — Scene.** `Scene+step` empty → create+launch (`initialised`, queued); occupied
→ carry/launch; active → revert to floor; `Scene+O` SYNC discards deviations;
`Scene+Record` commit-and-bake. Assert selector shows **names + identity colours**
(the WI-3 work — regression-guard it here).

**E2 — Phrase.** `Phrase+step` deviates focus track (`deviated[t]`);
`Scene+Phrase+step` deviates all; diagonal row clears all deviations; `Phrase+U/I/O`
copy/paste/clear.

**E3 — Song.** `Song+step` occupied → switch (full reset, deviations clear); empty →
create-on-select; `Mute+Song+step` → blank; `Song+O` → Panic (voices killed).

**E4 — Deletion picker.** `scope+hold(O)` (Track/Phrase/Scene) → picker re-skin +
status; tap slot → named confirm pop-over ("Delete PHRASE 3? P=CONFIRM"); `P`
confirms, entity reset; sticky-cancel behaviour. Golden the pop-over chrome.

## Group F — Machines & deck (consume the audio bridge)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| F1 | Realtime record | ☐ (seed) | audio, midi-in, mz |
| F2 | Tape record + overdub + punch | ☐ | audio |
| F3 | Two-track audio loop | ☐ | audio |
| F4 | Record machine → pool | ☐ | audio |
| F5 | MIDI-out track | ☐ | midi-out |

**F1 — Realtime record.** `U` arm → `isRecordArmed`; `play()` + `playNote` →
quantised trig written; double-tap `U` → overdub (button amber, up to 4 notes/step
accumulate); `hold step + playNote` → note onto that step; live P-Lock motion (turn
knob while rolling, no step) → P-Locks on crossed steps.

**F2 — Tape record + overdub + punch.** `Track+hold(SRC)` → TapeMachine; drive the
deck verbs; feed audio-in; record a take, wind back, punch a region, `UNDO` pops a
layer. Assert deck state + take length + non-silent capture.

**F3 — Two-track audio loop.** LoopMachine; build a loop across ≥2 sub-tracks via
overdub; assert sub-track count + loop content non-silent + take-group on promote.

**F4 — Record machine → pool.** RecordMachine grabs a volatile REC buffer; `Save…`
promotes it to a durable File pool entry; assert pool group counts.

**F5 — MIDI-out track.** MidiOutMachine; a trig emits a note-on on `midiOut()` (E3);
CC activity flashes the top-right dot; velocity meter is magenta.

## Group G — Capture: the anchor "live-set → stems" flow (audio bridge)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| G1 | Arm → roll → stop → saved | ☐ | audio |
| G2 | Routing = stem grouping | ☐ | audio |

**G1 — Arm → roll → stop → saved.** `Func+Song+U` tap → `capturePhase==Armed`, banner
"ARMED ▸ master + N stems"; `play()` → REC; stop → STOPPING→SAVED; assert a
`Captures/…/` dir with `master.wav` + per-non-empty-track `track-NN.wav` +
`take-sheet.txt` (write to the scratchpad/tmp dir, assert files exist + non-empty).

**G2 — Routing = stem grouping.** Route a track into a Route bus → that bus is one
stem with feeders folded; assert stem file set matches routing.

## Group H — Time & global

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| H1 | TIME page | ☐ | audio |
| H2 | Retrig/ratchet | ☐ | audio |

**H1 — TIME page.** `Song+TRIG`/`Scene+TRIG` opens TIME band (TRIG→TIME); tempo/sig
scope ladder (Set/Song/Scene); CLICK toggle; PreRoll count-in delays record start by
N bars (assert with the bridge). Golden the scope-coloured readout.

**H2 — Retrig/ratchet.** TRIG field 5 "RTG" P-lockable; a step with a rate
re-triggers at that musical rate (assert emitted note count under `play()`).

---

## Out of scope (named, not chased)

- StreamMachine long-form and the in-DAW Aux-output stem path (G3) — deferred until
  F5/G1 prove the pattern; Aux routing needs host-bus plumbing the headless rig does
  not model.
- Function-row hit-test/paint geometry unification (a separate arc).
- Any new product behaviour — the CUJ arc is tests + harness only; a journey that
  reveals a real bug files it, does not fix it inline.

## Test assets

Journeys default to **procedurally generated in-code fixtures** (sine burst, click
train, two-transient blip) — deterministic, tiny, license-free. The seed trio and
every Phase-0 self-test need **no external asset** (bridge liveness uses a synth
machine; realtime record feeds synthesised note-in). Real committed CC0 /
public-domain clips live in `tests/assets/` (with `LICENSES.md`) and are needed only
by later machine waves that depend on real audio *content*: Slice (transient
detection), Sample/Stretch (musical one-shot/loop), Stream (>30 s bed), sample
analysis (key/tempo detection), tape/loop deck (real input).
