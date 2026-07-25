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
| A2 | Trig conditions | ☑ | mz |
| A3 | Step editing | ~ | |
| A4 | Copy/paste/clear across scopes | ☑ | |

**A1 — Two-track drum beat.** ☑ `CujTrigAuthoringTest.cpp`. Focus track 0, clear it
(`Track+CLEAR`, confirmed — clearing a track is a guarded destructive verb); `step`
0,4,8,12 → those `.trig` set, surface cells read `StepTrigCertain`, off-beats
`StepEmpty`. `Func+Phrase+step 6` → `tracks[0].length==7` (polymeter; this is the
phrase-length authoring gesture, DESIGN §34.4 — *not* a TRIG field). Select track 1,
clear, `step` 4,12 → track-1 backbeat, surface confirms. Then `play()` (bridge) →
non-silent peak RMS across the roll, no NaN. Both tracks run FMMachine so the beat
sounds. (No `mz` dep in the end: length uses the gesture, not the MZ.)

**A2 — Trig conditions.** ☑ `CujTrigAuthoringTest.cpp`. Held step + `TRIG` opens the
COND band; `setParam` on field 0 lands the probability **on the held step** with the
track's base untouched, and the grid reads `StepTrigProbable` beside a neighbour's
`StepTrigCertain`. The same band with **no** step held writes `baseCond` instead (the
step keeps its own, stronger condition) and every unconditioned step then reads
probable. Field 2 sets the iteration denominator on a held step.

*Gotcha, cost a debug cycle:* **`DispatchProbe::frame()` after the band changes.**
Slider ranges come from the page the MZ is SHOWING, so writing 50 into a slider still
carrying the previous page's range clamps it to that range's top (measured: 10) — the
write looks like it landed in the wrong place when it landed in the wrong *range*.

**A3 — Step editing.** ~ `CujTrigAuthoringTest.cpp`. Holding a step re-skins the grid
to `SurfaceLayer::StepInspector`; bare `←/→` carries the trig to its neighbour and
back; `Func+←/→` nudges `microOffset` late without moving the trig; `P` (QUANT) puts
it back on the grid.

**Gesture collision, filed not fixed (see ROADMAP 9.36):** the microtiming nudge needs
**two** Func presses. The FIRST Func over a held step is the W7 latch, which is
*consumed* — the branch says "no funcHeld" in as many words — so a player who presses
Func once and then `→` gets the **step move**, silently, instead of a nudge. The
journey drives the two-press path and asserts the move separately; the `hold step +
SRC` note-edit leg is left to a later wave (flip to ☑ with it).

**A4 — Copy/paste/clear across scopes.** ☑ `CujClipboardTest.cpp`. The canonical
"same verb, three scopes": held `step`+`U` → clipboard type==step; `Track`+`U` →
type==track; held `section`+`U` → type==section. Typing is asserted behaviourally —
a step clip pasted under `Track` leaves the track alone — then the step paste lands
and the track paste mirrors the source onto another track. `Track`+`O` proves the
confirm gate (nothing clears until `P`). Surface leg: under a held Song the copy key
**dims** (Song has no clipboard — the 9.14 st.5 lie), under a held Track it glows.

*Found by this journey, filed not fixed:* (a) a held step used as a copy/paste
**operand** still authors on release — `verbs::trig` marks the edit context
param-written for Clear but not Record/Play — so the paste is asserted while the
step is held; (b) the copy-key glow is wired for section-**suite** scopes only, so a
held step or section copies without lighting the key (an omission, not a lie).
*Fixed on the spot* (a crash, not a wart): `commandContext()`'s `static`
ProcessorCatalog bound the first editor's processor forever — a second plugin
instance read the first's schema, and outliving it dangled.

## Group B — Generators (all via the generator hub, `3` held ≥350 ms)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| B1 | Euclid generate + commit/cancel | ☑ | mz |
| B2 | Density overlay | ☑ | mz |
| B3 | Velocity overlay | ☑ | mz |
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

**B2 — Density overlay.** ☑ `CujGeneratorsTest.cpp`. Hub cell 1 opens `Overlay::Density`
and the encoders become the density band. The claim under test is that density is
**subtractive**: pushing the master offset to −0.9 and running blocks leaves the stored
onsets byte-identical, and restoring the offset leaves them identical again — the
thinning lives in what is emitted, never in the pattern. Sticky, and escapes on a
double-tap `Func`. Per-track selection detents are left to a later wave.

**B3 — Velocity overlay.** ☑ `CujGeneratorsTest.cpp`. Hub cell 2 opens `Overlay::Vel`.
Four axes hang off ONE key: re-pressing **AMP** moves to the next sub-page, the meta
band follows, and the pages cycle back round within one lap. Sticky; double-tap `Func`
escapes.

*The gotcha worth knowing:* with velocity **off** everywhere the cycle has exactly one
page — MODE, the switch — because it skips disabled axes. A journey that opens the
overlay cold and expects AMP to page is testing an overlay that has nothing to page
to. Enable a track's `velMode` first. Applying the overlay velocity at emit is left to
a later wave.

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
| C1 | P-Lock one step | ☑ | mz |
| C2 | Per-step sample swap (Sound Pool) | ☐ | |
| C3 | Machine picker | ☑ | |
| C4 | Section paging & scope colour | ☑ | |
| C5 | Control-All | ☑ | mz |
| C6 | P-Lock clear gestures | ☑ | mz |

**C1 — P-Lock one step.** ☑ `CujSoundTest.cpp`. SRC section; `hold step +
setParam(slot, v)` → override set on that step, **base unchanged** (the OEB
invariant), MZ slot shows `hasOverride`; `hold step + CLEAR` wipes it; releasing the
step returns the same knob to editing the base, so the scope cannot leak past the
hold. *Correction:* the clear here is bare `O`, not `Func+O` — `Func+O` under a held
step is the staged slot-picker clear mode, and pressing `Func` mid-hold also LATCHES
the step, so that gesture belongs to C6.

**C2 — Per-step sample swap (Sound Pool).** `Fill+SRC` → grid re-skins to Sound
Pool, SRC glows Fill colour. Press cell (no step) → live-swap focus track. `hold
step` + cell → `sound_id` P-Lock baked on that step; release Fill → restore.

**C3 — Machine picker.** ☑ `CujSoundTest.cpp`. `Track + hold(SRC)` → `activeLayer ==
MachinePicker`; a cell press loads that machine on the focused track and closes the
picker (choosing IS the verb). Then the guard that matters: the picked machine's SRC
section reports real slots, its section key is live and the MZ page fills — the
`numSections() == highestSectionIndex + 1` trap, which shipped once as a machine with
an unreachable source panel.

**C4 — Section paging & scope colour.** ☑ `CujSoundTest.cpp`. A bare section key opens
the machine's own page (`mzPageOrigin == SecOrigin::Machine`) and re-pressing it pages
(the MZ's slot offset moves — asserted only after a precondition confirms the section
HAS more than one page). Under a held Track the same key addresses a different
**layer**, not a deeper page: `Track+TRIG` is the track's divider/length band
(`MetaBand::Divider`), and the section cell wears `theme::kScopeTrack` while held.

**C5 — Control-All.** ☑ `CujSoundTest.cpp`. Track held with no track picked arms it
(`controlAllActive`); a `setParam` then lands on **every** track whose schema has the
same slot id (matching is by id, not position); selecting a track ends the mode and
the next write lands on that track alone.

*Two things the gesture actually requires, both learned the hard way:* Track must stay
**down** (releasing it clears the arm), and a **section press in the middle** is not
optional — a bare Track hold puts the SWING band under the knobs, and any non-scope
press dismisses it back to the machine params. Also: read the target slot **after**
the section press settles, because that press pages the MZ, and a slot that is no
longer on the visible page is a write that silently does not happen.

**C6 — P-Lock clear gestures.** `Trig+Func+O` clears all P-Locks (trig intact);
`Trig+slot+O` clears one slot; `Trig+SRC+O` clears note/vel/gate only. Assert the
hint band surfaces these when a locked step is held.

## Group D — Performance overlays

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| D1 | Mute | ☑ | audio |
| D2 | Fill | ☑ | audio |
| D3 | Morph | ☑ | mz |
| D4 | Cue | ☑ | |
| D5 | Checkpoints | ☑ | |

**D1 — Mute.** ☑ `CujPerformanceTest.cpp`. Both behaviours (9.17): **stopped**,
`Mute+step` mutes at once (`getGlobalMute`, mute-view cells read `MuteMuted` /
`MuteAudible`, and the gesture is its own inverse); **rolling**, the same press ARMS
to the launch grid (`hasPendingMute`, global mute still clear — a mute never cuts
mid-bar) and lands at the boundary, after which the peak RMS across a roll drops.
`Scene+Mute+step` is asserted as a separate lane that leaves the global bit alone.
*Harness note:* while rolling, the landed global mute reaches the APVTS through a
`callAsync`, so a headless test sees it in the SOUND, not in `getGlobalMute`.
"survives a scene change" is left to E1, which owns scene switching.

**Every Mute/Track chord in a journey needs a `gap()` first** — two presses of the
same modifier inside the double-tap window LATCH the scope (3.10), and a latched
Track silently turns every step press into a track select.

**D2 — Fill.** ☑ `CujPerformanceTest.cpp`. `Fill+step` marks a step fill-on (cell reads
`StepFillAdd` while Fill is held) **without** authoring an ordinary trig, and pressing
again cycles it to fill-off. The census is the whole point and is done in sound: with
the track carrying nothing but fill steps, a full-bar roll is *silent* at rest and
audible while Fill is held.

**Roll a full bar (`kBarBlocks`, ~400 blocks at 48k/256), not a handful.** A 48-block
roll is ~250 ms and can pass BETWEEN two trigs, reporting silence from a loud pattern —
which quietly guts any "did the mute/fill work" comparison. D2 failed this way first;
A1 and D1 were passing on luck and now roll a bar too.

**D3 — Morph.** ☑ `CujPerformanceTest.cpp`. The authoring loop: hold `Morph`, hold
`↑`/`↓` to name a pole, write it — both poles land, and sweeping the fader moves the
**resolved** value from exactly A to exactly B. `Morph+CLEAR` bakes the blend into the
kit and retires the map; `Func+Morph+CLEAR` erases it.

*Two things the gesture requires:* the pole qualifier is **momentary** (the nav key-up
clears it), so the nav key is HELD across the write, not tapped before it — tap it and
the write becomes a *sculpt* at the current fader position, which at fader 0 lands in A
and looks like the B write silently going to the wrong pole. And read
`morphEffectiveValue`, not `baseParams`: a morph map is resolved on the way out
(P-Lock > morph-lerp > kit base), which is exactly why the fader edits nothing.
`Morph+Mute` fluid mute is left to a later wave.

**D4 — Cue.** ☑ `CujPerformanceTest.cpp`. `Func+3` enters the scope (`cueHeld`), and
releasing the 3-key leaves it; `Cue+Mute` flips the **focused** track's balance into
the cue bus and back, touching no other track. The discoverability leg is the point of
6.4's access pass and is asserted on the surface: under `Func`, key 3 advertises
**CUE**. The console (`Cue + hold(AMP)`) has its own unit coverage
(`runCueConsoleTests`) and is left to a later wave here.

**D5 — Checkpoints.** ~ `CujPerformanceTest.cpp`. `Track+Y` pushes on the Track stack
and *not* the Song's (the stacks are independent); bare `Y` marks the Song; `Func+Y`
tapped pops it and the exact marked pattern comes back; the restore arms an undo.

**This journey found the two gaps that became ROADMAP 9.37, and now proves the
rulings that closed them:** `Func+O` reverts the restore (undo was unreachable —
`clearVerbTap` only routed to the table when `primaryScope()` was neither `None` nor
`Func`, and with only Func held it *is* Func); `Track+Func+Y` walks the **Track's**
stack and pops it, not the Song's (the restore was reserved under any held scope,
which made every per-scope stack write-only); and a **held step no longer retargets**
a checkpoint verb — `Track+Y` with a step down marks the Track, where it used to
silently mark the Song because Trig outranks Track in `primaryScope()`.

## Group E — Launch / arrangement

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| E1 | Scene | ☑ | |
| E2 | Phrase | ☑ | |
| E3 | Song | ~ | |
| E4 | Deletion picker | ☑ | |

**E1 — Scene.** ☑ `CujArrangementTest.cpp`. `Scene+step` on an empty cell creates and
launches (create-on-select); on an occupied one it launches; `Scene+CLEAR` syncs live
deviations away. The identity leg is the WI-3 regression guard: with Scene held, the
named+coloured cell carries its **name** in `primary` and a fill that differs from an
unnamed neighbour's, the playing scene reads `SelectorCurrent` and the other reads
`SelectorOccupied`. *Gotcha:* `Phrase+step` is launch, not create, and is inert on an
un-created row — the deviation leg must target a row some scene brought into being.
`Scene+Record` (commit-and-bake) is left to a later wave.

**E2 — Phrase.** ☑ `CujArrangementTest.cpp`. `Phrase+step` deviates the **focused**
track onto the pressed row and badges it on `surface().trackDeviated`, leaving every
other track alone; `Scene+Phrase+step` takes the whole band along; `Scene+Phrase` on
the scene's own diagonal brings everyone home and the badges out. Phrase rows must
EXIST first (a scene create fills the diagonal) — `Phrase+step` is launch, not create.

*Asymmetry found, filed not asserted:* the per-track `Phrase+<diagonal>` marks the
track deviated onto its own home phrase (`swapPhraseForTrack` sets `deviated = true`
unconditionally), so the badge stays lit while the track plays exactly the scene's
content. `deviateAllToPhrase` gets it right (clears on `N == sceneIdx`).
`Phrase+U/I/O` is left to a later wave.

**E3 — Song.** ~ `CujArrangementTest.cpp`. `Song+step` on an empty slot creates by
copying the song you were on and switches to it (v36 slots); an occupied slot
switches, each song keeping its own pattern; audio rolls **across** the switch (the
frame-race guard) and stays finite.

**Gap found, filed not fixed:** `Mute+Song+step` → blank song **cannot fire, and hits
something else instead**. The Mute LAYER rewrites every step key to `ToggleMute`
(`kLayerRemaps`) before the Step case can read `songHeld`; `resolveBinding` matches on
a *subset* of held mods and no `ToggleMute` row requires Song, so the plain
`{ToggleMute, kModMute}` row wins and the press **mutes the track with that index**
(measured: it arms a pending mute on track 3). Dispatch's own documented "empty +
Mute: blank default song" branch is dead code. (ROADMAP 5.3 claims this gesture; corrected there.) `Song+O` →
Panic is left to a later wave. Flip to ☑ when the blank variant is reachable.

**E4 — Deletion picker.** ☑ `CujArrangementTest.cpp`. `Track + hold(CLEAR)` re-skins
the grid to `SurfaceLayer::DeletePicker`; tapping a slot only ARMS a confirm
(`ConfirmKind::DeleteTrack` at that target, nothing deleted yet) and the status lane
shows a `Confirm` **naming** the target — asserted through `buildInspectorModel`,
since a confirm derived from state cannot fade while armed (9.30). `P` then deletes
that track and only that one, and the prompt clears with it.

*Wrinkle found, not asserted:* the picker calls itself sticky ("releasing the arming
chord doesn't exit"), and for Phrase/Scene it is — but the **Track** picker only
accepts `SelectTrack`, which is what a step key becomes *while Track is held*. Release
the scope first and the tap cancels the picker instead (measured).

## Group F — Machines & deck (consume the audio bridge)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| F1 | Realtime record | ☑ | audio, midi-in |
| F2 | Tape record + overdub + punch | ☐ | audio |
| F3 | Two-track audio loop | ☐ | audio |
| F4 | Record machine → pool | ☐ | audio |
| F5 | MIDI-out track | ☐ | midi-out |

**F1 — Realtime record.** ☑ `CujRecordTest.cpp`. Select track 1 (0→1 syncs the
processor focus track — the note-in routing target), `U` arm → `isRecordArmed`;
`play()` + `playNote(60)` → a quantised trig lands carrying the played pitch (which
only works if the transport is truly rolling, so it doubles as bridge-liveness
proof); double-tap `U` → `isOverdubArmed`; a three-note chord in one block
accumulates onto a single recorded step. *Deferred to a later wave:* `hold step +
playNote` onto a specific step, and live P-Lock motion (knob turned while rolling).

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
