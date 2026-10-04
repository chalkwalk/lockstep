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
| A3 | Step editing | ☑ | |
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

**A3 — Step editing.** ☑ `CujTrigAuthoringTest.cpp`. Holding a step re-skins the grid
to `SurfaceLayer::StepInspector`; bare `←/→` carries the trig to its neighbour and
back; `Func+←/→` nudges `microOffset` late without moving the trig, on **one** Func
press; `P` (QUANT) puts it back on the grid. `hold step + SRC` opens the **note
editor** on that step (`activeLayer == NoteEdit`, pointed at the held step) and a grid
cell writes the note onto it; double-tap `Func` leaves.

The journey asserts **both halves of the Func split** side by side, because they are
one key and only the context tells them apart: a Func that qualified the nav does *not*
latch, and a bare Func tap over the same held step does.

*Found here, fixed by 9.38:* the nudge used to need **two** Func presses. The first
Func over a held step was the W7 latch and was *consumed* — the branch said "no
funcHeld" in as many words — so pressing Func once and then `→` performed the **step
move**, silently, instead of the documented nudge. It cost far more than the nudge:
`heldModsFromUiState` and the Func layer of `kLayerRemaps` both derive from that flag,
so the entire Func layer was dark while a step was held. The latch now fires on Func's
key-**up**, and only if Func qualified nothing.

*Sequencing note that survives the fix:* a latch **survives the key-up by design**, so
the note-edit leg still drops it (double-tap `Func`) or its "press step 4" lands on a
step that is already held and the editor never sees a fresh hold.

**A4 — Copy/paste/clear across scopes.** ☑ `CujClipboardTest.cpp`. The canonical
"same verb, three scopes": held `step`+`U` → clipboard type==step; `Track`+`U` →
type==track; held `section`+`U` → type==section. Typing is asserted behaviourally —
a step clip pasted under `Track` leaves the track alone — then the step paste lands
and the track paste mirrors the source onto another track. `Track`+`O` proves the
confirm gate (nothing clears until `P`). The operand **survives being one**: copying a
step leaves its trig alone and a pasted step stays pasted, asserted *after* the
release. Surface leg: under a held Song the copy key **dims** (Song has no clipboard —
the 9.14 st.5 lie), under a held Track it glows, and under a held **step** it glows
too.

*Found by this journey, both fixed in 9.38:* (a) a held step used as a copy/paste
**operand** still authored on release — `verbs::trig` marked the edit context
param-written for Clear but not Record/Play, so copying a step turned its trig off and
pasting onto one inverted what had just landed. The mark moved to
`CommandCore::handleVerb`'s `PS::Trig` case, which every Trig verb passes through.
(b) the copy-key glow asked `firstHeldSectionSuiteScope`, which knows only the five
suite scopes, where dispatch asks `primaryScope()` — in which **Trig is rank 0, the
highest**. So a held step copied, the status lane said `REC=COPY`, and only the key
stayed dark. A held **section** is still not covered: `UiState` carries no
section-held flag, so the surface model cannot see it.

*The glow assertion is on the TINT, not on `!disabled`* — with no scope recognised the
whole block is skipped and the key is not dimmed either, so `!disabled` passed in both
worlds and asserted nothing. Caught by breaking it.

*Fixed on the spot* (a crash, not a wart): `commandContext()`'s `static`
ProcessorCatalog bound the first editor's processor forever — a second plugin
instance read the first's schema, and outliving it dangled.

## Group B — Generators (all via the generator hub, `3` held ≥350 ms)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| B1 | Euclid generate + commit/cancel | ☑ | mz |
| B2 | Density overlay | ☑ | mz |
| B3 | Velocity overlay | ☑ | mz |
| B4 | Melodic generator | ☑ | mz |
| B5 | Harmonic voice-mover | ☑ | mz |

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
double-tap `Func`. Per-track **selection detents** are covered too: a track thins by scrubbing until told
otherwise, and an `Exempt` track keeps its own answer to the master gesture — the
selection lives on the kit, so it outlives the overlay.

**B3 — Velocity overlay.** ☑ `CujGeneratorsTest.cpp`. Hub cell 2 opens `Overlay::Vel`.
Four axes hang off ONE key: re-pressing **AMP** moves to the next sub-page, the meta
band follows, and the pages cycle back round within one lap. Sticky; double-tap `Func`
escapes.

*The gotcha worth knowing:* with velocity **off** everywhere the cycle has exactly one
page — MODE, the switch — because it skips disabled axes. A journey that opens the
overlay cold and expects AMP to page is testing an overlay that has nothing to page
to. Enable a track's `velMode` first. Applying the overlay velocity at emit is left to
a later wave.

**B4 — Melodic generator.** ☑ `CujGeneratorsTest.cpp`. Hub cell 3 arms it and the
encoders become the melody band. Two properties make it performable rather than a
novelty, and both are asserted: it **previews live** (a knob turn changes the grid
before anything is committed) and bare `P` prints exactly what was previewed. Then
**determinism** — re-arming and driving the same field to the same value reproduces the
same line, so a take can be repeated. `Func+P` cancels and restores what was there.

**B5 — Harmonic voice-mover.** ☑ `CujGeneratorsTest.cpp`. Hub cell 4 arms it. The tool
is voice-leading, not a chord palette: one voice moves when its knob turns and the rest
stay put, and the invariant that keeps it sounding like harmony rather than a cluster —
**no two voices on the same pitch** — holds before and after the move. `P` prints the
progression. `LEN` grows the progression by **cloning** the chord before it, not by inventing one
— growing a progression is an edit, never a blank page.

## Group C — Sound, sections, P-Locks

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| C1 | P-Lock one step | ☑ | mz |
| C2 | Per-step sample swap (Sound Pool) | ☑ | |
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

**C2 — Per-step sample swap (Sound Pool).** ☑ `CujSoundTest.cpp`. `Fill+SRC` re-skins
the grid to the pool (`trigGridMode == SoundPool`; cells read `SoundPoolOccupied` /
`SoundPoolEmpty`); a cell press with a step held bakes that sound onto the step
(`trigOverride.hasSoundId` / `soundId`); releasing Fill restores the step grid, so the
re-skin is momentary and cannot be got stuck in.

A cell press with **no** step held is a live **audition** — the track's working
baseParams swap under your hands and nothing is written to any step. (The two fixture
sounds must genuinely differ, or the swap is a no-op and the leg asserts nothing; and
the bake leg auditions on its way through, so the audition leg has to press the *other*
cell.) Read the **working sequence** for it: a live swap writes there directly and
never touches the kit, which is what makes walking away undo it.

*Order matters:* hold the step **before** entering the re-skin. Once the grid is the
pool, every step key is a pool cell — there is no way to grab a step from inside it.
(The catalogue said "hold step + cell", which reads as if either order works.)
The pool must also be seeded: it is a Project-scope library, and a journey that starts
with an empty one asserts nothing.

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
`Morph+Mute` is **fluid mute** — not a mute bit but level-to-silence captured as a
morph, so the fader fades the track out instead of cutting it; pressing it again clears
the map. **Both** modifiers stay down: the row requires Morph AND Mute, and the layer
that carries it is defined by the pair, so Morph alone just makes the step an ordinary
mute.

**D4 — Cue.** ☑ `CujPerformanceTest.cpp`. `Func+3` enters the scope (`cueHeld`), and
releasing the 3-key leaves it; `Cue+Mute` flips the **focused** track's balance into
the cue bus and back, touching no other track. The discoverability leg is the point of
6.4's access pass and is asserted on the surface: under `Func`, key 3 advertises
**CUE**. `Cue + hold(AMP)` opens the **cue console** — the MIXER twin, since AMP is the mixer
key — and it is sticky: the scope keys are free again once it is up, and a double-tap
`Func` closes it.

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
| E3 | Song | ☑ | |
| E4 | Deletion picker | ☑ | |

**E1 — Scene.** ☑ `CujArrangementTest.cpp`. `Scene+step` on an empty cell creates and
launches (create-on-select); on an occupied one it launches; `Scene+CLEAR` syncs live
deviations away. The identity leg is the WI-3 regression guard: with Scene held, the
named+coloured cell carries its **name** in `primary` and a fill that differs from an
unnamed neighbour's, the playing scene reads `SelectorCurrent` and the other reads
`SelectorOccupied`. *Gotcha:* `Phrase+step` is launch, not create, and is inert on an
un-created row — the deviation leg must target a row some scene brought into being.
`Scene+RECORD` is the other half of the pair: it arms a **bake** confirm, bakes nothing
until confirmed, and then the deviation is folded into the scene — the track reads home
because home moved. Commit and discard, on the two verbs that already mean commit and
discard.

**E2 — Phrase.** ☑ `CujArrangementTest.cpp`. `Phrase+step` deviates the **focused**
track onto the pressed row and badges it on `surface().trackDeviated`, leaving every
other track alone; `Scene+Phrase+step` takes the whole band along; `Scene+Phrase` on
the scene's own diagonal brings everyone home and the badges out. Phrase rows must
EXIST first (a scene create fills the diagonal) — `Phrase+step` is launch, not create.

Both spellings of "come home" are asserted, because they answer the same question and
must agree: `Scene+Phrase` on the diagonal brings the band back, and per-track
`Phrase+<own diagonal>` brings one player back rather than badging them as away.

*Asymmetry found here, fixed by 9.38:* `swapPhraseForTrack` and `applyDeviation` set
`deviated = true` unconditionally while `deviateAllToPhrase` cleared it on
`N == sceneIdx`, so sending one track to its own home row badged it as deviating while
it played exactly the scene's content. The flag was always **derived** — the tell being
that two readers recomputed it defensively (`cur != home`) rather than trusting it.
`Arrangement::setDeviation` derives it now and both defensive recomputations are gone,
so a wrong flag shows rather than being papered over.
`Phrase+U/I/O` is covered too: `Phrase+RECORD` copies the focused track's phrase
(clipboard type `Pattern`), `Phrase+PLAY` pastes it onto another track trig for trig,
and `Phrase+CLEAR` arms a confirm before wiping every track's phrase.

**E3 — Song.** ☑ `CujArrangementTest.cpp`. `Song+step` on an empty slot creates by
copying the song you were on and switches to it (v36 slots); an occupied slot
switches, each song keeping its own pattern; audio rolls **across** the switch (the
frame-race guard) and stays finite. `Mute+Song+step` on an empty slot creates a
**blank** one instead — asserted on all three claims (created, blank, and *nothing
muted*), because of how it used to fail. `Song+CLEAR` is **PANIC**, asserted the way a
player would notice it: a ringing voice stops and not one trig moves.

*Found here, fixed by 9.38:* `Mute+Song+step` could not fire, and hit something else
instead. The Mute LAYER rewrites every step key to `ToggleMute` (`kLayerRemaps`) before
the Step case can read `songHeld`; `resolveBinding` matches a *subset* of held mods and
no `ToggleMute` row requires Song, so the plain `{ToggleMute, kModMute}` row won and the
press **muted the track with that index** (measured: it armed a pending mute on track
3). The rule now lives in `handleSongSlotPress`, reached from both buttons, and
`LayerRemapReachabilityTest` fails the build if any remap swallows a compound again.

**E4 — Deletion picker.** ☑ `CujArrangementTest.cpp`. `Track + hold(CLEAR)` re-skins
the grid to `SurfaceLayer::DeletePicker`; tapping a slot only ARMS a confirm
(`ConfirmKind::DeleteTrack` at that target, nothing deleted yet) and the status lane
shows a `Confirm` **naming** the target — asserted through `buildInspectorModel`,
since a confirm derived from state cannot fade while armed (9.30). `P` then deletes
that track and only that one, and the prompt clears with it.

The arming chord is **released before** the slot is tapped, which is how a player does
it and what PRINCIPLES §16 requires ("confirming must not require re-holding the arming
chord"); the leg asserts the picker survives that release.

*Found here, fixed by 9.38:* it did not, for Track. The picker matched a Track slot tap
on `SelectTrack` — the name a step key wears *while Track is held* — so releasing first
made the tap arrive as `Step`, match nothing, and cancel the picker. Phrase and Scene
were sticky; Track only looked it. Both encodings are now accepted for every scope: the
scope decides *what* is deleted, the key only says *which* slot.

## Group F — Machines & deck (consume the audio bridge)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| F1 | Realtime record | ☑ | audio, midi-in |
| F2 | Tape record + overdub + punch | ☑ | audio |
| F3 | Two-track audio loop | ☑ | audio |
| F4 | Record machine → pool | ☑ | audio |
| F5 | MIDI-out track | ☑ | midi-out |

**F1 — Realtime record.** ☑ `CujRecordTest.cpp`. Select track 1 (0→1 syncs the
processor focus track — the note-in routing target), `U` arm → `isRecordArmed`;
`play()` + `playNote(60)` → a quantised trig lands carrying the played pitch (which
only works if the transport is truly rolling, so it doubles as bridge-liveness
proof); double-tap `U` → `isOverdubArmed`; a three-note chord in one block
accumulates onto a single recorded step. *Deferred to a later wave:* `hold step +
playNote` onto a specific step, and live P-Lock motion (knob turned while rolling).

**F2 — Tape record + punch.** ☑ `CujDeckTest.cpp`. Real audio in, punch in on a rolling
transport (instantly — no quantize, which is the tape's difference from the Loop),
record a span, punch out: the reel holds the span and the punch is undoable, because a
tape edit is never destructive.

*Load-bearing:* the two deck faces share a console and a state machine but **not a
command door**. `sendLooperCommand` does a `dynamic_cast<LoopMachine*>` and simply
misses a tape — drive a tape through it and it sits in Playing while every command
falls on the floor. The tape's door is `tapeApplyVerb`. The **marker family** is covered too: markers drop where the head is, and a cue is a
**LOCATE** — it moves the head and fires nothing (§40.4), which is exactly why they can
be dropped freely mid-take.

**F2b — Wind and scrub the reel.** ☑ `CujDeckTest.cpp`. The leg that makes the tape a
*reel* rather than a buffer, and the last row in this catalogue to land. Park the song
and the head detaches: `>>`/`<<` wind it, **audibly** (a wind plays the reel under a
moving head — a locate jumps silently, which is the whole distinction), the release
ends the wind so it can never stick, winding back parks at the leader rather than
running into negative tape, and **reel-is-truth** — the transport follows the head, so
Play resumes where the ear stopped. The MZ's slot 0 becomes the reel you rock: a drag
jogs the head and pointedly does *not* write the Source param it would otherwise edit;
the moment the song rolls it is the Source picker again.

*"Standalone only" is really "whenever Lockstep owns the transport"*
(`transportWindable() == !hostedLocked()`), so the journey drives the gate through the
real `syncMode` parameter rather than faking a wrapper type. Both halves are asserted:
hosted-locked the console offers **no wind cells at all**, and — separately — the
setter itself refuses, because a cell that is not drawn is unreachable by finger but a
**controller can still send the button**. Suppress, don't half-work, has to hold below
the surface too.

*Precondition, and it cost a debug cycle:* **both transports must stop.** Clearing the
in-plugin one parks the sequencer, but the rig's stub playhead keeps advancing ppq on
its own — and a parked tape republishes `reelPosAtBlockStart()` as its head, so the
head crawls forward at exactly 1× with no scrub running at all. Measured, it looks
precisely like a wind that will not stop, *including after an explicit
`setScrubRate(0)`*, which is what makes it worth writing down. Use
`d.audioRig().playHead().setPlaying(false)` as well.

**F3 — Two-track audio loop.** ☑ `CujDeckTest.cpp`. REC defines the loop on the first
pass and the take closes into one with a length (`looperHasLoop`); with the input then
cut, the loop plays back the audio it captured — which is the claim that separates a
looper from a recorder. Drive it with `immediate=true`: a quantized edge sits *Armed*
waiting for a bar line, and the journey would be timing the grid rather than the deck.
Widening is covered: raising `subtrack_count` gives the deck **four sub-tracks in one
slot** — still one track, which is the whole storage decision (§40.7) — with sub 0
armed by default, and the take promotes as a **group**, one file per non-empty sub.

**F4 — Record machine → pool.** ☑ `CujDeckTest.cpp`. A real drum loop is fed in
(`feedAudio`), the recorder trig fires, and the REC slot ends up holding audio: non-zero
peak, a real used length, and no louder than what went in. `Save…` **promotion** completes it: the volatile take becomes a durable file entry on
disk with the audio in it — the promote-or-lose contract, since a REC slot is RAM and
dies with the session.

**Three ordering rules this journey had to discover, all invisible from outside:**
1. **Stand the audio rig up BEFORE installing a capture machine.** Attaching it
   re-prepares the processor at 48k/256, which re-allocates the pool's volatile
   buffers — and a capture machine binds its medium to those buffers when *it* is
   prepared. Install first and `startCapture` bails to Idle on an unbound medium.
2. **The pool slot fills only when a capture CLOSES.** Until then the audio is in the
   machine's own reel, so `volatileUsedLength` reads 0 mid-take.
3. **A recorder trig RESTARTS the capture every time it comes round**, so a take as
   long as the bar can be re-armed forever and never land. Set a short `rec_length`
   (0.5 s) and it closes well inside one pass.

**F5 — MIDI-out track.** ☑ `CujDeckTest.cpp`. Its trigs leave as note-ons carrying real
velocities, and — the half that makes it a *destination* rather than a machine — it
puts nothing on the audio bus at all. Needs no asset.

## Group G — Capture: the anchor "live-set → stems" flow (audio bridge)

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| G1 | Arm → roll → stop → saved | ☑ | audio |
| G2 | Routing = stem grouping | ☑ | audio |

**G1 — Arm → roll → stop → saved.** ☑ `CujCaptureTest.cpp`. The anchor flow, asserted
on what is **on disk** rather than what the state machine believed: `Func+Song+Record`
arms, rolling starts the take, stems are written alongside the master, and after the
stop there is a `master.wav` with audio in it, one `track-NN.wav` per audible track
(each non-empty), and a `take-sheet.txt`. The **alignment invariant** (11.11) is
asserted too — every stem is the same length as the master, which is what lets the set
drop onto a DAW timeline without nudging.

*Two things to respect:* **arming is not recording** — the tap says "capture the next
thing I play" and the take begins when the transport does, so a journey that asserts
`isCapturing()` straight after the gesture fails against working code. And the capture
path is derived from the loaded project file: the journey **saves a project into a temp
dir first**, because otherwise a gesture-driven capture writes into the user's real
`~/Music/Lockstep/Captures`.

**G2 — Routing = stem grouping.** ☑ `CujCaptureTest.cpp`. Three sources, one routed
into a Route track: the capture then produces **two** stem files, not three — the fed
track folded into its bus, and nothing about capture was configured to make that
happen. No stem picker, no export dialog: what lands is whatever reaches the master as
a terminal.

*Gotcha:* a track can only be routed into one that **accepts inbound audio**. An
ordinary synth track is not a destination — `validOutTargets` offers only Off / Master
/ the Aux buses — so the bus in this journey is a `RouteMachine`, whose whole job is
being somewhere to route to.

## Group H — Time & global

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| H1 | TIME page | ☑ | |
| H2 | Retrig/ratchet | ☑ | midi-out |

**H1 — TIME page.** ☑ `CujTimeTest.cpp`. `Song+TRIG` opens `Overlay::Time` and the
encoders become the TIME band; re-pressing TRIG cycles TIME ↔ KEY (two signature pages,
one key); the CLICK field toggles the metronome (9.10 moved it here from `Func+3`).
The scope-ladder leg is the substance: the **same page** opened under `Song` writes the
Song rung and under `Scene` writes the Scene rung, each leaving the other set, and
`effectiveTimeSig` resolves to the nearest rung that is set. The **PreRoll count-in** is covered separately: record-armed with a non-zero pre-roll,
Play starts a count-in of the configured length that ends on its own.

*Two preconditions it needs:* stand the audio rig up **before** parking the transport
(attaching it starts the in-plugin transport, which runs the count-in over), and set
**Auto** sync — the count-in is Lockstep's own transport behaviour by design, because
hosted-and-locked the DAW owns the downbeat and Play must never delay it.

**H2 — Retrig/ratchet.** ☑ `CujTimeTest.cpp`. A step carrying an RTG rate fires *more
often* over a bar than a plain one — counted, on a **MIDI-out track**, because audio
tells you something sounded and not how many times.

*This leg doubles as the Func-layer proof (9.38).* The TRIG band is a **meta page**
(`Func` + the SRC key), and the journey now reaches it in the order a player would —
hold the step, *then* `Func+SRC`. That did not work before: the Func press was consumed
by the W7 latch, so the band never opened and the band had to be opened first. It is
the sharpest evidence that the whole Func layer is reachable over a held step again,
not just the one nudge that got filed. The field then lands on the held step as a
P-Lock and the trig it decorates is untouched.

---

## Group I — The modal sweep (6.9)

Not a journey. Every other group walks one task the way a person performs it;
this one walks **every modal state** and asks the two questions a journey does
not: can a real gesture *reach* it, and can any gesture *leave* it.

| ID | Journey | Status | Deps |
|----|---------|--------|------|
| I1 | Modal sweep: entry, interruption, escape | ☑ | |

**I1 — Modal sweep.** ☑ `ModalSweepTest.cpp`. For each value of `Modal`, drive the
documented entry gesture, assert it arrived, then apply a battery of ten
interruptions — Func double-tap, a tap of each of the eight scope modifiers, and
transport start/stop. After each, two assertions:

- the surface must not show a layer that ranks *below* the active modal (the
  enum is in priority order, so something outranking it is legitimate; something
  beneath it means the modal is active and invisible), and
- a short fixed escape sequence must reach rest, where **rest means every modal
  flag clear**, not merely `activeModal() == None`. That distinction is the
  point: the accessor returns one value by priority and can never report two, so
  "exactly one is active" is true by construction and proves nothing. What goes
  wrong is a modal left half-shut — field cleared, parameters still set.

**Coverage is 15 of 16 and the test says so on every run.** The table is
exhaustive over `Modal` and fails if a value is missing from it, so a new modal
cannot be added without someone deciding how it is reached. `SampleProps` is the
one undriven value: it opens from a Props button on a pool row, so it needs a
populated pool — a fixture, not a gesture.

**What it found.** The melodic generator could not be escaped. `kOverlays` had
descriptors for Euclid and Harmony and none for Melodic, which shipped between
them; with no descriptor `handleOverlayEvent` answered `NotConsumed` to every
event, so the universal escape did nothing and only a Section press got you out.
A `constexpr` check now requires every `Overlay` except `None` to have a row.

*Harness notes, paid for once:* the generator hub needs the editor's timer ticked
or a long hold is invisible; a held chord must not be released, and `GeneratorHub`
correctly outranks the generator it launches while still down; the step inspector
fires mid-hold from the timer, so the step stays pressed.

This is a **standing net, not an arc**: run it, and add a row when a new modal
ships.

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
