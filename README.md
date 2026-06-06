# Lockstep

> **Lockstep** is the working codename for a performance-oriented step
> sequencer plugin (VST3 / CLAP / Standalone; AU on macOS). The final
> product name is not yet chosen.

Lockstep is a step sequencer you play like an instrument. Trigs,
conditional logic, and per-step parameter locks replace the
continuous click-and-drag automation of a traditional DAW. It runs as
a plugin inside your DAW or as a standalone application, and the entire
editing workflow is reachable from the computer keyboard — no mouse
required.

This document is the user manual. For the architecture and the reasoning
behind the design, see `DESIGN.md`, `PRINCIPLES.md`, and `ROADMAP.md`.

---

## Table of contents

1. [What Lockstep is, and what makes it different](#1-what-lockstep-is)
2. [The paradigm: how to reason about the system](#2-the-paradigm)
3. [Glossary](#3-glossary)
4. [Tutorial: your first piece of music](#4-tutorial)
5. [Feature reference (appendix)](#5-feature-reference)
6. [Implemented vs. planned](#6-implemented-vs-planned)

---

<a name="1-what-lockstep-is"></a>
## 1. What Lockstep is, and what makes it different

Lockstep treats sequencing as a **live performance**, not a passive
piece of automation. Its design draws three explicit influences:

- **Digitakt** — the feel of the core: the trig grid, P-Locks, trig
  conditions, fills, performance mutes, copy/paste/clear, temporary
  save-and-revert, and the "control-all" gesture that broadcasts one
  parameter edit across every track at once.
- **Octatrack** — the *Project → Bank → Pattern → Part* hierarchy, and
  the idea that **every track chooses its own sound engine**. A track
  can host a sampler, a synth, or a MIDI-out adapter; the sequencer
  treats them identically.
- **Squarp Pyramid** — external gear is a first-class citizen. A track
  that drives an outboard synth over MIDI has the *same* trig grid,
  the *same* P-Locks, and the *same* performance modifiers as a track
  driving Lockstep's internal sampler.

What makes it distinctive:

- **One grammar, learned once.** Almost every action is a **scope**
  (what am I operating on?) plus a **verb** (what do I do to it?). Hold
  a scope key, press a verb key. The verbs never change meaning; only
  the scope changes. (See [§2](#2-the-paradigm).)
- **No "edit mode" vs. "perform mode."** The gestures you use to build
  a pattern are the gestures you use to perform it. You can load a
  finished project and improvise on it, or open an empty project and
  author from nothing — the workflow is the same.
- **Keyboard-first, mouse-optional.** The full editor is driven from a
  fixed QWERTY layout. The eventual hardware controller is literally
  the same key map in a denser package — it adds *no* new features,
  only ergonomics.
- **Internal audio and external MIDI are equal.** Any performance
  feature that works on a sampler track works identically on a track
  sequencing your hardware.
- **Tiny project files.** Samples are stored as references (path +
  content hash), never as embedded audio. Saves are instant and DAW
  auto-saves stay cheap.

---

<a name="2-the-paradigm"></a>
## 2. The paradigm: how to reason about the system

If you understand four ideas, you can predict how almost everything in
Lockstep behaves.

### 2.1 Scope + verb

Every editing and performance action is built from two halves:

- A **scope** key declares *what you're about to operate on*. Scopes are
  held modifiers: the eight-key left cluster (`Func`, `Track`, `Pattern`,
  `Part`, `Scene`, `Master`, `Mute`, `Fill` — 3.2), plus a **held step**
  (`Trig`) and a **section** key. Two modifiers — one from each column —
  can be held together to combine scopes (the *compound chord*).
  (`Cue` is reserved for the cue bus, 6.4, but not yet bound to a key.)
- A **verb** key declares *what to do*. The verb set is small and
  fixed: `Record` (= copy), `Play` (= paste), `Stop` (= clear), `Yes`,
  `No`, plus turning an encoder for live value tweaks.

You **hold the scope, then press the verb.** For example:

- Hold a step + `Record` → copy that step.
- Hold a track + `Record` → copy that whole track.
- Hold a section + `Record` → copy that section's parameters.

The verb meant "copy" in all three; only the scope changed. Once you
internalise this, new features stop being new shortcuts to memorise —
they are old verbs applied to new scopes.

### 2.2 Override-ELSE-Base

Every value that can vary per step follows exactly one rule:

```
Effective value = step override (if present) ELSE track base
```

There is no "reset" value to manage and no precedence flags. A value is
either overridden on this step, or it falls through to the track's base
setting. This single rule governs:

- **Machine parameters** (P-Locks): the per-step override of a sound
  engine's controls.
- **Trig fields**: note, velocity, gate length, and trig condition can
  each be overridden per step or left to the track default.

This is why "holding a step and tweaking" always means the same thing —
it writes a step override. Tweaking with no step held writes the track
base.

### 2.3 The edit context (held step = step scope)

Whether your edit lands on a **step** or on the **track** depends on one
piece of state: *is a step currently held?*

- **No step held** → parameter writes go to the **track base**.
- **A step held** → the same writes go to that **step's override**
  (a P-Lock, or a per-step trig override, depending on which section is
  active).

This is true regardless of the *source* of the edit — a QWERTY key, a
MIDI CC from a hardware knob, or the on-screen encoder all resolve
through the same gate. Holding a step and turning a knob P-Locks it;
holding a step and playing a note records that note onto the step.

### 2.4 The container hierarchy

```
Set
 └── Song ×16
      ├── per Track: Kit (the sound) + Phrase ×16 (note content)
      └── Scene ×16 (a launchable row: each track's phrase + active-mask + core time)
```

- **Set** — one plugin instance. Owns all Songs, the sample pool, MIDI
  mappings, and global settings. (Matches an Ableton "Set".)
- **Song** — a self-contained song: per-track Kits and Phrase pools plus a
  set of Scenes. The bank-sized unit; switching Songs is a full performer
  reset (clears live deviations).
- **Phrase** — pure per-track musical content: the trig grid, per-step
  overrides, P-Locks, length/divider, trig defaults, conditions. **No
  sound.** Each track has a pool of 16.
- **Kit** — the per-(track, Song) **sound**: which engine the track hosts,
  its base parameters, post-machine FILTER/AMP, and sample references.
  Recalled live via `Func+Track`. (The dissolved Octatrack "Part".)
- **Scene** — a launchable cross-track row: the global phrase index (which
  row all tracks default to), the active-mask (who sounds), core time, and
  the Morph snapshot. Per-track phrase deviations are live/RAM-only — never
  saved. Launched live (`Scene+step`), not chained into a written arrangement.

Tracks are **polymetric**: each has its own length (1–64 steps) and
clock divider, so a 7-step track and a 16-step track phase against each
other naturally with no master-bar concept.

---

<a name="3-glossary"></a>
## 3. Glossary

| Term | Meaning |
|---|---|
| **Trig** | A step that fires. The sequencer turns it into a MIDI note-on (and a scheduled note-off after the gate length). |
| **P-Lock** (parameter lock) | A per-step override of one or more of a sound engine's parameters. Hold a step, turn a control. |
| **Trig override** | A per-step override of a sequencer field — note, velocity, gate, or condition — as opposed to an engine parameter. |
| **Override-ELSE-Base** | The one resolution rule: effective value = step override if present, else track base. |
| **Machine** | A sound engine. Each track hosts one. Lockstep ships seven: `SamplerMachine` (monophonic sample playback with trim, loop region, ZC-snap), `SlicerMachine` (slice/scrub dual-mode with transient detection and poly), `FMMachine` (4-op FM synthesizer, mono/poly), `VAMachine` (virtual-analog dual-osc + SVF synth, mono/para), `DrumSynthMachine` (Rytm-style drum synth — kick, snare, hat, tom via one stepped param), `MidiOutMachine` (MIDI CC/note output to external gear), and `StubMachine` (silent fallback for unknown IDs). |
| **Machine module** *(planned, 6.7)* | A machine shipped as a loadable native module behind Lockstep's stable C ABI, rather than compiled into the core. First-party machines are statically linked; third-party machines are authored against the SDK and installed into a per-platform folder. Bespoke contract for purpose-built machines — not a VST3/CLAP host. See DESIGN §36. |
| **Kit** | The per-(track, Song) sound: machine identity, base parameters, post-machine FILTER/AMP, sample refs. Recalled via `Func+Track`. |
| **Phrase** | A track's pure note content — the trig grid and per-step data. Each track has a pool of 16; Scenes reference them by index. |
| **Scene** | A launchable cross-track row: a global phrase index (all tracks default to that row) + active-mask + core time + Morph snapshot. Per-track phrase deviations are live/RAM-only and never saved. |
| **Song** | A self-contained song (Kits + Phrase pools + Scenes). The bank-sized unit. |
| **Scope** | A held modifier declaring what the next verb operates on. Eight in the left cluster (`Func`, `Track`, `Phrase`, `Scene`, `Morph`, `Song`, `Mute`, `Fill`), plus a held step and a section key. `Cue` is reserved for the cue bus (6.4) but not yet bound to a key. |
| **Compound chord** | Two modifiers (one per column) held together to combine scopes. Cross-column only; never fires on its own — it just narrows the scope until a verb is pressed. `Func` composes with anything. |
| **Verb** | The action applied to the scope (`Record`=copy, `Play`=paste, `Stop`=clear, `Yes`, `No`). |
| **Section** | A grouping of parameters on the section bar (keys `5–0`). Canonical six: TRIG / SRC / FILTER / AMP / MOD / FX. Held scope modifiers reinterpret each key (e.g. `Track+FILTER` = post-machine filter, `Song+FX` = master FX). The Manipulation Zone shows eight parameters (4×2) of the active cell at a time. |
| **Morph / Crossfader** *(planned, 5.2)* | A per-Scene pair of sparse parameter maps (A and B) blended by one continuous fader. The `Morph` modifier assigns slots; `Morph + ^/v` picks endpoint A/B. The fader is mouse/CC/hardware-only (no QWERTY). |
| **Manipulation Zone (MZ)** | The eight-parameter (4×2) editing band. What you are tweaking right now. |
| **Step Grid** | The 2×8 matrix of step keys mirroring the bottom two QWERTY rows. |
| **Focus / focused track** | The currently selected track (or Global). Determines what contextual encoders and selected-track MIDI map to. |
| **Edit context** | The held-step state that routes edits to a step override vs. the track base. |
| **Choke** | A 1–2 ms micro-fade applied before retriggering a monophonic voice, to avoid clicks. |
| **Control-All** | Holding `Track` with no track selected broadcasts the next parameter edit to every track that has a matching control. |
| **Mute** | Suppresses a track's trigs non-destructively. `Mute+step` = global mute (survives scene/song changes); `Scene+Mute+step` = per-scene mute (the scene's active-mask). |
| **Fill** | A momentary modifier: while held, fill-conditioned steps fire. Used for live variation. |
| **Trig condition** | A per-step (or per-track) firing rule: probability, iteration (m:n), previous-step dependency, and fill rule. |
| **Checkpoint** | A RAM-only snapshot for live undo. `Func+Yes` pushes before a risky idea; `Func+No` tap=pop/hold=floor. **Scope-respecting:** the snapshot captures whichever scope is held (none=Song, Track, Scene, Phrase). Up to 8 deep per scope; floor = saved state. |
| **Launch model** | Performance is launch-based, not arrangement-based: queue a **Scene** (`Scene+step`) to fire at the next core-time boundary, or switch **Songs** (`Song+step`). There is no written timeline or pattern chain. |
| **Sample pool** | The project-wide library of samples, stored as `{path, hash}` references rather than embedded audio. |
| **Sound Pool** *(partial — data model + overlay shipped; trig-grid recall mode planned, 5.7)* | A project-scope library of saved per-track sounds, recallable or P-lockable per step. |
| **Scope colour grammar** *(3.3)* | A canonical palette per scope (`step` = light grey, plus distinct hues for `track / pattern / part / machine / scene / master`) used by key tints, the step-grid scope re-skin, and any badge that needs to say "which scope is held". In-scope keys (the section keys and verbs the scope rebinds) light fill+border in the scope colour; ambient keys stay neutral; reserved keys dim. |
| **Scope re-skin** | When a scope modifier maps to a 1-of-16 selector (Track / Phrase / Scene; `Func+Track` = machine/Kit picker), the 16 step keys become a non-paginated index for that scope. Unavailable indices dim. Cells tint in the scope's colour. |
| **Top-bar dashboard** *(3.4)* | The top of the editor splits into a persistent performance dashboard (BPM, Song/Scene/Phrase, transport position, pending Scene, checkpoint depth) on the left, and a live held-context preview on the right. |
| **Value-label table** *(3.4)* | A `ParamSpec` field carrying textual names for stepped/enum positions (`LP24 / LP12 / HP / BP`, `MONO / PARA`, …). The MZ renders the textual name in place of a number when present. |
| **Step-hold capture window** | The canonical chord-edit path: hold a step → play MIDI → each note-on snapshots all currently-held notes; release commits velocity (highest) and gate. Empty capture = no change. Independent of record-arm and transport. Multi-step: all held steps receive the same chord. |
| **Note-count badge** | 1–4 stacked tick marks on the left edge of each step cell showing `trigOverride.noteCount` — immediately visible without entering any edit mode. |
| **Note-edit mode** | `Func + Src + step` (the SRC key relabels NOTE; release the step while Func+Src held) enters a 1-octave chromatic keyboard on the step grid: cells 0–11 = C through B, 12–15 unused. Press a cell to toggle that pitch in the current view octave. Cross-octave instances show small octave-number badges. NavUp/NavDown shift the octave. Staged removals commit on Func release. |
| **P-Lock clear gestures** | `Trig + Func + Stop` clears every P-Lock on the held step(s), leaving trig intact. `Trig + (active MZ slot) + Stop` clears only that one slot. `Func + step` enters P-Lock clear mode: cells re-skin orange showing only the *set* P-locks (packed, not by raw slot index); press a cell to stage it for removal, press again to cancel; release Func to commit all staged removals. |
| **NoteSelection bias** | Per-track bias for chord-note spread when the machine voice count is smaller than the step's note count. `TopBias` (default) includes top + bottom and fills from the top; `BottomBias` fills from the bottom. Set in the TRIG meta-section, slot 3 (Bias = TOP / BOT). |
| **Func+Track machine/Kit picker** | Hold Func (1) + Track (2) — the Track key relabels to KIT; step cells show available machine names. Press a step to assign that machine to the focused track. |

---

<a name="4-tutorial"></a>
## 4. Tutorial: your first piece of music

This walkthrough builds a simple beat from an empty project. It uses the
**standalone** application; the workflow is identical inside a DAW. The
key letters refer to the QWERTY layout described in
[§5.1](#51-the-keyboard-layout) — keep that diagram handy.

> Throughout: **step keys** are the bottom two QWERTY rows
> (`S D F G H J K L` = steps 1–8, `X C V B N M , .` = steps 9–16).

### Step 1 — Load a sample

Drag an audio file (a kick drum, say) onto the sample pool area of the
window, or use the file dialog. The sample is decoded and added to the
pool. Track 1 hosts the sampler by default and will use the first pool
entry.

### Step 2 — Place some trigs

Press step keys to toggle trigs on the focused track. For a
four-on-the-floor kick, toggle steps **1, 5, 9, 13** (`S`, `H`, `X`, `N`).

Press **Play** (key `O`) to start the transport. You should hear the
kick on every beat. Press `O` again to stop.

### Step 3 — Add a second track

Hold **Track** (`2`) and press a step key to select a track:
`2 + F` selects track 2 (the step keys `D F G H J K L ;` are tracks
1–8). Release `2`. Track 2 is now focused.

Load a snare into the pool and place trigs on steps **5 and 13**
(`H`, `N`) for a backbeat. (If track 2 isn't pointed at the snare yet,
open the **SRC** section — see Step 5 — and set its sample.)

### Step 4 — Make a track polymetric

Focus a track, then hold **Track** and tap the **TRIG** section key
(`2 + 5`) to reach the track-meta layer, and set its **length** to
something other than 16 — try 7. That track now loops every
7 steps while the others loop every 16, and the two phase against each
other. This is the heart of Lockstep's groove.

### Step 5 — Tweak a sound (and lock it per step)

The **Section Bar** is keys `5`–`0` (TRIG / SRC / FILTER / AMP / MOD /
FX). Press a section key (e.g. **SRC**, the sound-source section) to
bring its parameters into the **Manipulation Zone**. Turn the on-screen
encoders (or a mapped MIDI knob) to adjust them.

Now the magic: **hold a step key** and turn the same control. Instead of
changing the track's base value, you've written a **P-Lock** — that
parameter change applies *only* on that step. Hold step 9 and drop the
pitch, for example, and only the third kick is lower. Release the step;
the lock stays.

To remove a lock, hold the step and clear it (push the held encoder, or
use the section-clear gesture).

### Step 6 — Add a conditional trig

Open the **COND** layer (`Func + TRIG`, i.e. `1 + 5`). With no step held,
the four controls set the **track's** base condition (probability, iteration
m:n). Set probability to, say, 50% and that track fires stochastically
each loop.

Hold a single step and the same controls now write that **step's**
condition — so you can make just one trig 50%-likely, or fire it only on
every 4th pass (set m:n to 1:4). The grid shows you what will fire before
it happens: certain hits are bright, skips are dim, probabilistic steps
are in between.

### Step 7 — Record a melody live

Press **Record** (`U`) with no scope held to arm recording. Now play
notes (via the on-screen keyboard or an attached MIDI keyboard) and
they're captured as trigs, quantised to the focused track's grid. Hold
a step while playing a note to write that note's pitch onto that
specific step instead.

Each pass through the pattern **overwrites** whatever notes were on a
step (default). Double-tap **Record** to enter **overdub mode** (button
turns amber, labelled "Overdub") — notes accumulate across passes up to
4 per step. Single-tap to return to overwrite mode.

Press `U` again to disarm.

### Step 8 — Perform variations

- **Mute a track live:** hold **Mute** (`Z`) and press a track's step
  key (`Z + D` = mute track 1). It drops out instantly and
  non-destructively. Toggle again to bring it back.
- **Fill:** hold **Fill** (`X`). Any steps you've marked as fill-only
  fire only while you hold it — instant live variation.
- **Copy a pattern and mutate it:** hold **Pattern** (`Q`) and press
  **Record** (`U`) to copy; move to another pattern slot and press
  **Play** (`I`) to paste. Now change it without touching the original.
- **Checkpoint before a risky idea:** press `Func + Yes` (`1 + Y`) to
  push a snapshot. Experiment freely. Press `Func + No` (`1 + P`) to
  revert. Up to 8 levels deep.

### Step 9 — Save

Save the project (host save inside a DAW, or the standalone's save). The
project stores your sequence, P-Locks, conditions, and sample
*references* — not the audio itself, so keep your sample files in place.

That's a complete loop: load → trig → polymeter → P-Lock → condition →
record → perform → save. Everything past this point is more of the same
grammar applied to more scopes.

---

<a name="5-feature-reference"></a>
## 5. Feature reference (appendix)

This section documents the gestures and what they do, in what context.
Key letters are the default QWERTY mapping.

<a name="51-the-keyboard-layout"></a>
### 5.1 The keyboard layout

Lockstep uses a fixed **10×4** grid (shipped in 3.1, DESIGN §33). The **left two columns** are an
eight-key modifier cluster, all reachable by one hand; the **right
eight columns** are the functional block — function/section keys (top
two rows) and step keys (bottom two rows).

```
 MODIFIERS    │  FUNCTIONAL BLOCK                                  keys
 [FUNC ][TRACK]│ [TAP][ ^ ][TRIG][SRC][FILTER][AMP][MOD][FX]      1 2 3 4 5 6 7 8 9 0
 [PHRASE][SCENE]│[ < ][ v ][ > ][YES][RECORD][PLAY][PANIC][NO]    Q W E R T Y U I O P
 ──────────────┼──────────────────────────────────────────────────────────────────────
 [MORPH][SONG ]│ [ steps 1 - 8 ]                                  A S D F G H J K L ;
 [MUTE ][FILL ]│ [ steps 9 - 16 ]                                 Z X C V B N M , . /
```

(The two left columns in each row hold the eight modifiers; the next
two slots on row 0 are `3=TAP` and `4=NavUp`; sections fill `5–0`.
Row 1's right side is `E=NavLeft / R=NavDown / T=NavRight` followed
by the verb cluster `Y U I O P` = `Yes / Rec / Play / PANIC / No`
(the `O` clear/stop verb is labelled **PANIC**).)

The verb keys `Yes / Rec / Play / Stop / No` on row 1 (`Y U I O P`) are
**context-sensitive**: with **no scope held** they default to
transport/confirmation — `Play` (`I`) starts/stops the transport, `Rec`
(`U`) toggles record-arm, `Yes`/`No` confirm prompts. With a **scope
held** the same keys become the scope verbs Copy / Paste / Clear
(`Rec`/`Play`/`Stop`). Snapshots ride the bare-`Func` layer
(`Func+Yes`=push, `Func+No`=pop). There is no separate transport key —
the verb row does double duty, which is why the surface needs no extra
buttons.

In **Ortholinear** and **Staggered** display modes the keys immediately
outside the 10-column block — `` ` ``, `Tab`, `CapsLock`, `Shift` on
the left and `-`/`=`, `[`/`]`, `'` on the right — are shown as dimmed
decorative anchors. They have no sequencer function, but pressing one
briefly lights it so you can reorient if you overshoot a key.

### 5.2 Modifier (scope) keys

The eight modifiers form the left two columns, ordered by
frequency-of-use (most-touched at row 0, performance specialists on
row 3). Six of the modifiers are **section scopes** (each owns a row
in the scope-section matrix); two are **performance specialists**
(no section row).

| Key | Scope | Selects |
|---|---|---|
| `1` | **Func** | Universal qualifier — composes with any other scope to flip to its "secondary variant." Also the modifier layer for snapshots, verbs, and machine secondaries. |
| `2` | **Track** | One or more tracks; or, with none selected, Control-All. `Track+section` opens the track-foundation row (post-machine FILTER/AMP, IEffect inserts). **`Func+Track`** opens the machine/Kit picker (step cells show machines; press one to assign it to the focused track). |
| `Q` | **Phrase** | A per-track musical phrase (pure note content). `Phrase+step` swaps all non-deviated tracks to that phrase (unison); `Track+Phrase+step` deviates just the focused track. To clear all deviations, re-launch the active Scene or use `Func+Scene+step`. |
| `W` | **Scene** | A launchable cross-track row (global phrase row + active-mask + core time). `Scene+step` on a different Scene = carry overlay; on the active Scene = revert to floor. `Func+Scene+step` = baseline launch (floor only). `Scene+Record` commits global-pattern/mask changes; `Func+Scene+Record/Play/Stop` copy/paste/clear a whole Scene. |
| `A` | **Morph** | The A/B crossfader scope. `Morph + ^`/`v` picks endpoint A/B; `Morph+section` assigns slots to the morph. |
| `S` | **Song** | Song select (`Song+step`). `Func+Song` = Global / master-bus focus. |
| `Z` | **Mute** | Global mute mask (hold and tap several tracks). `Scene+Mute+step` = per-scene mute. |
| `X` | **Fill** | "While held, fills fire." `Fill+step` marks step as fill-only. |
| step key (held) | **Trig** | The held step(s). Multi-step holds allowed. |
| `5`–`0` | **Section** | The held section's parameters. Cell meaning depends on which scope (if any) is held alongside. |

(`Cue` is reserved as a scope (for the cue bus / pre-listen feature
landing in 6.4) but is not bound to a cluster key yet; cue-scene and
cue-routing live under `Func+Scene` and master-scope cells in the
interim.)

**Compound chords.** Hold one modifier from each column to combine
scopes (e.g. `Scene + Mute` = fade a track across the crossfader). The
rule: cross-column only, a two-modifier hold never acts on its own (it
just narrows the scope until you press a verb), and `Func` composes
with anything as the cheapest qualifier.

**Gesture cost is graduated** (PRINCIPLES §15). Cheaper chords carry the
most common actions; cost rises with held-modifier count, in this order:
`key` < `Func+key` < `mod+key` < `Func+mod+key` < `mod+mod+key` <
`Func+mod+mod+key`. The ceiling is four simultaneous keys; five is
forbidden. Counting is by held scopes — holding many steps or tapping
many mutes is one operand, not many keys. Every live gesture's rung is
catalogued in DESIGN §13.0.

### 5.3 Verb keys

| Key | Verb | Meaning |
|---|---|---|
| `Y` | **Yes** | Affirmative — confirm a prompt; `Func+Yes` = snapshot push (bare `Func` only — see note). |
| `U` | **Record** | Copy the current scope into the clipboard. |
| `I` | **Play** | Paste the clipboard into the scope. |
| `O` | **Stop** | Clear the scope. |
| `P` | **No** | Negative — dismiss a prompt; `Func+No` = snapshot pop (bare `Func` only — see note). |

> **Snapshot/restore are bare-`Func` ops.** While a section-suite scope
> (`Track` / `Pattern` / `Part` / `Scene` / `Master`) is also held,
> `Func+scope+Yes/No` is that scope's secondary variant, **not** a global
> checkpoint — so snapshot/restore are reserved (inert) until you release
> the scope. `Yes` itself is reserved (dim) under a scope; the other verbs
> show their scoped op (COPY/PASTE/CLEAR, and DEL on `Track+No`) and light
> in the scope colour.

### 5.4 Transport and navigation

Transport and record-arm ride the verb row (no scope held — see §5.1):

| Key | Action |
|---|---|
| `I` (Play) | Play / Stop transport (no scope held). |
| `O` (Stop) | Stop transport; `Func + O` = stop and reset to the start. |
| `U` (Rec) | Toggle record-arm (overwrite). Double-tap = overdub (append). |
| `3` | Tap tempo (`Func + I` = toggle metronome). |
| `4` | Navigate up (inverted-T above `E R T`). |
| `E` / `R` / `T` | Navigate left / down / right. |

### 5.5 Track selection and focus

Lockstep has 16 tracks. The track header shows 8 at a time; the **"1–8" / "9–16"** page button (top-left of the track row) flips between banks. Selecting a track via keyboard automatically flips to the correct page.

| Gesture | Action |
|---|---|
| `Track (2) + D–;` | Select / focus track 1–8 (`2 + D` = track 1, … `2 + ;` = track 8). |
| `Track (2) + C–/` | Select / focus track 9–16 (`2 + C` = track 9, … `2 + /` = track 16). |
| Page button (click) | Flip track header between tracks 1–8 and 9–16. |

Tracks 1–8 default to `SamplerMachine` and tracks 9–16 to `MidiOutMachine` (Digitakt-style default split). Any track can be reassigned to any machine via **`Func + Part`** (hold `1`, tap `W` — the step grid re-skins to machine names; press a step to assign; replaces the retired `Func+R` gesture). A small **"M"** badge in the top-right corner of a track button identifies MIDI-out tracks at a glance.

#### Machine catalogue

| Machine | Badge | Description |
|---|---|---|
| `SamplerMachine` | SP | Monophonic sample playback. SRC section: sample, pitch, trim window (`samp_start` / `samp_length`), loop mode (OFF / SUS / S+R / ALL), loop region (`samp_loop_start` / `samp_loop_len`). All position slots snap to zero-crossings on write. AMP section: level + AHDSR. |
| `SlicerMachine` | SL | Slice/scrub sample playback. SLICE mode: incoming MIDI note selects slice 0–15; `slicer_start` / `slicer_length` are relative to the active slice. SCRUB mode: note drives playback rate vs. root 60 (identical to Sampler semantics). `slicer_rate` P-lockable for per-step rate; negative rate = reverse playback. `slicer_slice_src` (EQUAL / TRANS) and `slicer_slice_count` auto-recompute slices on change; transient detection uses 5 ms RMS blocks with fast/slow envelope ratio and centre-weighted search. VOICE section: MONO / POLY toggle (V4). |
| `FMMachine` | FM | 4-operator FM synthesis. Free 4×4 modulation matrix. Per-operator ADSR, ratio, fine-tune, mix. Macro attack/release/sustain scalars. MONO / POLY voice modes (V4 pool). |
| `VAMachine` | VA | Virtual-analog dual-osc synth. Saw/Pulse/Tri/Sin PolyBLEP oscillators + sub + shared noise. State-variable filter (LP4/LP2/HP/BP + drive). Filter ADSR + amp ADSR. LFO (6 shapes). Mono / Paraphonic-4 voice modes. Para topology: chord notes 1 & 3 → osc1+sub; notes 2 & 4 → osc2+sub. |
| `DrumSynthMachine` | DR | Rytm-style per-track drum synthesis. Type param selects KICK / SNARE / HAT / TOM variant; each has dedicated DSP (exponential pitch sweep + waveshaper / bandpass noise / hipass noise / sine + tom body). |
| `MidiOutMachine` | M | MIDI CC / note output to external gear. Configurable destination, channel, program, 16 CC slots with user-assignable numbers and labels. |

Focus determines what the contextual encoders edit and what selected-track MIDI mappings drive.

**Third-party machine modules** *(planned, 6.7)* — beyond the built-in
machines above, the catalogue will also list machines installed as
loadable modules. A machine module is native code authored against the
Lockstep SDK and dropped into a per-platform machines folder; a
drag-and-drop install flow copies it there and rescans. Installing or
removing a module is an out-of-grammar administrative action (like
managing sample files), not a scope+verb gesture. A project that
references a module you don't have installed loads safely: the track
shows a stub you can relink, and the missing module's settings are
preserved on re-save. See DESIGN §36.

### 5.6 Step editing

| Gesture | Action |
|---|---|
| step key (tap) | Toggle a trig on/off on the focused track. |
| step key (hold) | Enter step (Trig) scope — subsequent parameter edits become P-Locks / trig overrides on that step. |
| multiple step keys (hold) | Hold several steps at once; edits and copies apply to all of them. |

### 5.7 Parameter editing (P-Locks)

| Context | Where the edit lands |
|---|---|
| No step held, machine section active | Track **base** parameter. |
| Step held, machine section active | Step **P-Lock**. |
| No step held, TRIG/COND meta section | Track defaults / base condition. |
| Step held, TRIG/COND meta section | Step trig override / step condition. |

The rule is identical whether the edit comes from an on-screen encoder,
a mapped MIDI CC, or a QWERTY action.

### 5.8 Sections and the Manipulation Zone

| Key | Section (machine layer) |
|---|---|
| `5` | **TRIG** — note, velocity, gate defaults |
| `6` | **SRC** — sound source (sampler / oscillator controls) |
| `7` | **FILTER** — filter (post-machine SVF block; machines may opt out) |
| `8` | **AMP** — amplitude envelope (post-machine AHDSR + level/pan) |
| `9` | **MOD** — modulation (LFO, matrices, per-op envelopes, voice/portamento) |
| `0` | **FX** — per-track effects *(planned, 6.5)* |

Press a section key repeatedly to page through its parameters (the MZ
shows eight at a time, in two rows of four — `kMZSlots`). Holding
`Func` flips to each machine's **secondary** page (deep-dive
parameters like FM mod matrices); holding any other scope opens that
scope's row in the **scope-section matrix**:

| Held scope | What `Track+5` (TRIG) means | …`Track+7` (FILTER) | …`Master+0` (FX) |
|---|---|---|---|
| `Track` | Per-track condition defaults (length / divider via `Track+TRIG`) | Post-machine FILTER | (no track scope on FX) |
| `Pattern` | Length / scale lock | (dim) | Pattern-FX snapshot |
| `Part` | Trig templates | Part-base FILTER | Part-base FX |
| `Scene` | (rename `CXFD` — crossfader curve) | Scene-assign FILTER | Scene-assign FX |
| `Master` | (dim) | Master FILTER (if any) | **Master FX 1+2** |
| `Func` (over any of the above) | The secondary variant of the cell (e.g. `Func+Scene+FILTER` = the other scene's filter assignments). |

Track-meta content folds into `Func+TRIG` (COND: probability, m:n,
prev-dep) and `TRIG` itself (default note / velocity / gate). Track
length and clock divider live under `Track+TRIG`. Output gain, sync
mode, and clock settings live under master-scope cells.

### 5.9 Copy / paste / clear

A single uniform grammar — **hold scope, press verb**:

| Scope held | + Copy (`T`) | + Paste (`Y`) | + Clear (`U`) |
|---|---|---|---|
| **Trig** (1+ steps) | Copy steps (trigs + conditions + P-Locks) | Paste onto held steps | Clear held steps' overrides |
| **Section** key | Copy that section's params | Paste section to current track | Reset section to default |
| **Track** (specific) | Copy whole track | Paste track | Clear track |
| **Pattern** | Copy whole pattern | Paste pattern | Clear pattern |

Multi-step copies preserve relative offsets. Pasting a 1-step clipboard
over many held steps replicates; pasting many over one unrolls forward.
The clipboard is in-memory only and **typed** — a step clipboard can't be
pasted into a pattern scope, etc.

### 5.10 Control-All

Hold **Track** (`Q`) with **no** specific track selected, then make a
parameter edit. The change broadcasts to **every** track that exposes a
matching control (matched by parameter id first, by role tag second).
Obeys the edit context: if a step is held, it writes a P-Lock on that
step on every matching track; otherwise it updates each base. Use it for
sweeping a filter or tightening every decay across the kit at once.

### 5.11 Mutes

| Gesture | Action |
|---|---|
| `Mute (Z) + step key` | Toggle **global** mute on that track (survives scene/song changes); hold Mute and tap many. |
| `Scene (W) + Mute (Z) + step key` | Toggle **scene** mute (this track's active-mask in the current scene). |
| `Func (1) + Mute (Z) + step key` | **Solo** that track (additive toggle). Solo is the secondary/advanced layer of mute; `Func` is the cheapest qualifier (PRINCIPLES §15). |

Mutes are non-destructive: trigs are suppressed at the output, no
note-offs are forced.

*Planned (not yet implemented):* a deferred **atomic** multi-mute — flag
several tracks while a qualifier is held and commit them all on release,
so a group drops in on the same beat. Its gesture is TBD: it cannot reuse
`Func + Mute` (now solo). Today, plain `Mute + step` hold-tap-many is the
immediate, one-track-at-a-time equivalent.

### 5.12 Fills

- Hold **Fill** (`X`): while held, every step's condition treats "fill"
  as true.
- Each step's condition has a **fill rule**: `Always` (default — ignore
  fill), `OnlyFill` (fire only while Fill is held), `NeverFill` (fire
  only while Fill is *not* held).
- The grid previews fill-only steps in a distinct colour so you can see
  what a fill will do before you trigger it.

### 5.13 Trig conditions

Set in the **COND** layer (`Func + TRIG`, i.e. `Func + 5`). Three
condition types, each valid at track level (no step held) or step level
(step held):

- **Probability (1–100%)** — stochastic firing.
- **Iteration (m:n)** — fire on pass *m* of every *n* loops.
- **Previous-step dependency** — fire only if the previous step did (or
  didn't) fire. Step-level only.

All conditions are **deterministic and pre-computable**, so the grid
shows certain-fire / certain-skip / probabilistic states ahead of the
playhead.

### 5.14 Scenes, phrases, and songs (the launch model)

Performance is **launch-based**, not arrangement-based (Phase 7). There is
no bank dimension, no pattern queue, and no written chain — you launch
Scenes and switch Songs live.

| Gesture | Action |
|---|---|
| `Scene (W) + step key` | Launch a Scene — quantized to the next core-time boundary while playing, immediate when stopped. On a *different* Scene: carries the current live overlay. On the *active* Scene: reverts to its saved floor. |
| `Func + Scene + step key` | **Baseline launch** — switch to any Scene at its clean saved floor, discarding all live deviations. |
| `Scene + Stop` | Revert the active Scene to its saved floor (same as re-launching it). |
| `Scene + Record` | Commit live global-pattern and mask changes into the Scene's floor. Per-track phrase deviations are **not** committed — they are always live/RAM-only. |
| `Func + Scene + Record / Play / Stop` | Copy / paste / clear a whole Scene (floor state only). |
| `Phrase (Q) + step key` | Unison phrase swap: all non-deviated tracks switch to that phrase. |
| `Track + Phrase (Q) + step key` | Sticky per-track deviation: only the focused track switches. |
| `Song (S) + step key` | Switch Songs (quantized) — a full reset; live deviations clear. |

### 5.15 Checkpoints (live undo)

| Gesture | Action |
|---|---|
| `Func + Yes` (`Func + Y`) | Push the **currently-held scope** onto its checkpoint stack. |
| `Func + No` (tap, `Func + P`) | Pop one entry from the scoped stack (restore last snapshot). |
| `Func + No` (hold+release) | Jump straight to the floor (= the saved state at last load). |

**Scope-respecting:** the snapshot captures whichever modifier is held — none = Song,
`Track` = that track's Kit + current Phrase, `Scene` = that Scene's floor,
`Phrase` = that Phrase's steps + P-Locks. Each scope has its own LIFO up to 8 deep;
the floor (= on-disk saved state) is always present and can never be popped.

**RAM-only** — scratch pushes do *not* survive save/reload; the floor is re-seeded
from disk so "reload saved" always works. The `CK:N` badge shows the scoped depth.

### 5.16 MIDI input

- **CC mapping** with two modes: absolute (with soft-takeover, so knobs
  don't jump) and relative (endless encoders).
- Each mapping has a **scope**: Global, a fixed Track[N] slot, or
  Selected-Track (follows focus).
- **Channel modes:** Omni→Selected (all channels route to the focused
  track) or Per-Track (channel N drives track N).
- **Eight contextual encoders** mirror the Manipulation Zone (4×2) and
  always drive the current focus.
- **MIDI clock** can drive the timeline in standalone, with Locked and
  Auto sync modes (Auto freewheels on clock dropout, freezes on explicit
  stop).
- Transport controls bind to MIDI realtime / MMC, not to CC.

### 5.17 Keyboard / UI revamp *(shipped — Phase 3)*

The chrome-and-grammar pass over the 10×4 surface that 3.1/3.2 froze. It
shipped across 3.3–3.10; the detail below documents the behaviour now in
the build. (Only 3.11, pattern-length authoring, remains open.)

**3.3 — Surface chrome.**

- Larger key cells with bigger primary text; **6-character** key label
  ceiling (was 3–4); longer abbreviations like `FILTER`, `ATTACK`,
  `RETRIG`, `COPY`, `PASTE`, `CLEAR`.
- A unified **label-resolution rule**: when a modifier is held the key
  re-skins to its contextual meaning; universally-invariant secondary
  meanings (verb keys' COPY/PASTE/CLEAR under any scope) keep an
  always-on hint.
- **Scope colour grammar**: a canonical palette per scope (light grey
  for step, distinct hues for `track / pattern / part / machine /
  scene / master`) so the surface visibly says *which scope is held*.

**3.4 — Contextual modes, top bar, MZ streamline.**

- **Step-grid scope re-skin.** Hold `Track` and the 16 step keys
  become a 1-of-16 track picker; `Pattern` → pattern picker; `Part`
  → part picker; `Func + Part` → machine picker showing machine names.
  **Pagination is suppressed in this mode** — only "which key was
  pressed" matters. Unavailable indices dim; cells tint with the
  scope colour.
- **Top-bar dashboard + held-context preview.** The pre-Phase 3 "mode
  chips" row is replaced by a persistent performance dashboard (BPM,
  Bank / Pattern / Part identity, transport position, chain queue,
  checkpoint depth) on the left, and a live held-context preview on
  the right (e.g. `TRACK 3 + …`, `FUNC + PART → machine picker`).
- **MZ streamline.** Each slot collapses to a larger rotary plus a
  single value display; stepped/enum params show textual values
  (`LP24 / LP12 / HP / BP`, `MONO / PARA`) instead of numbers when
  `ParamSpec.valueLabels` is populated.
- **Double-click rotary** resets a slot to its default. (Eventual
  hardware push-encoder-twice maps to the same gesture.)

**3.5 — Note capture, P-Lock clear, machine picker.**

- **Step-hold MIDI capture.** Hold a step key, play MIDI notes from
  a connected keyboard, release — notes write to that step (up to 4,
  replace-on-hold). Velocity = highest; gate = span if all notes
  released before step, else track default. Works with transport
  stopped and record-arm off; an empty buffer is a no-op (non-destructive).
- **P-Lock clear gestures.** `Trig + Func + Stop` clears all
  P-Locks on the held step(s), trig left intact. `Trig + (active MZ
  slot) + Stop` clears only that one slot's P-Lock. Both use the
  existing scope+verb grammar.
- **Step-driven P-Lock clear mode.** Hold Func (1), then press a
  step → step cells re-skin orange: cells map to the *packed* list
  of set P-locks only (not by raw slot index). Press a cell to stage
  it for removal; press again to cancel. Release Func to commit all
  staged removals.
- **Func+Track machine/Kit picker.** Hold Func (1) and Track (2) —
  Track relabels to KIT; step cells show available machine names.
  Press a step to assign that machine to the focused track. Release
  Func or Track to exit.

**3.10 — Latch (hands-free virtual-hold) + Track+Nav mode cycle.**

- **Double-tap = virtual-hold (latch).** Double-tapping any latchable modifier
  (`Track`, `Part`, `Pattern`, `Scene`, `Master`, `Mute`, `Fill`) latches it
  hands-free — exactly as if the key stayed physically held. Double-tap the
  same modifier again to release. Column exclusivity is enforced: at most one
  latch in each column ({`Pattern`, `Scene`, `Mute`} and {`Track`, `Part`,
  `Master`, `Fill`}); latching a second key in the same column releases the first.
- **Latched step operands.** Double-tapping a step virtual-holds it into the
  edit context, so encoder edits land on it hands-free. A single tap on any
  step still toggles its trig as normal. Net trig change on latch-in is zero
  (the first-tap trig toggle is reverted on double-tap detection).
- **Func double-tap = universal escape.** When any latch is active, double-tap
  `Func` (key `1`) clears every latched modifier and every latched step in one
  gesture. When no latches are active, Func double-tap is a no-op.
- **Latch pip chrome.** A small scope-coloured dot appears at the bottom-left
  of each latched modifier key and step cell.
- **Track + NavUp/Down = input mode cycle.** Supersedes the 3.9.2 verb
  radio (`Track + I/O/Y`). Hold `Track` (key `2`) without selecting a specific
  track, then press `NavUp` (↑) or `NavDown` (↓) to cycle the focused track's
  input mode: `PLAY ↔ CHROMATIC ↔ LEVELS`. Mode switches call escape-all-latches
  per DESIGN §13.7 ("entering a new modality exits the current one").

**3.6 — Polyphonic step authoring improvements.**

- **Snapshot chord capture.** Step-hold MIDI capture now uses
  snapshot-currently-held semantics: each note-on writes the full set
  of currently-held MIDI notes to the step (not just the pressed note).
  Note-offs don't change the step's stored notes. Multi-step: all held
  steps receive the same chord in parallel.
- **Realtime chord record.** Transport-time record-arm quantises
  incoming notes to the nearest step. Notes landing on the same step
  within a single pass aggregate into a chord (up to 4 notes,
  de-duplicated). On the next pass through that step the chord is
  **replaced** (overwrite mode, default). Double-tap Record to arm
  **overdub mode** (amber button, "OD" on the QWERTY key) — subsequent
  passes append rather than replace, accumulating chords up to the 4-note
  cap. Single-tap Record disarms overdub and returns to overwrite.
- **Note-count badge.** 1–4 stacked tick marks on each step cell show
  `noteCount` at a glance — visible in normal mode without entering any
  edit overlay.
- **Note-edit mode.** `Func + Src + step` (the SRC key relabels NOTE;
  press a step then release it) enters a 1-octave chromatic overlay:
  cells 0–11 = C through B, cells 12–15 unused. Press a cell to toggle that pitch in
  the current octave. Cross-octave instances of each pitch class show as
  small octave-number badges. NavUp/NavDown shift the view octave. Staged
  removals commit on Func release.
- **NoteSelection in TRIG meta-section.** The TRIG meta-section (hold
  any step and navigate to Section key 5) now shows a "Bias" slot (TOP /
  BOT) that reads and writes the per-track chord-spread bias.
- **VA paraphonic topology.** VA Para-4 mode now routes chord notes to
  oscillators by slot: notes 1 & 3 → osc1+sub, notes 2 & 4 → osc2+sub.
  A single shared noise generator replaces per-voice noise generators.

### 5.18 Modal trig-grid surfaces

The step grid can be re-skinned into non-step roles. Two paths exist:

- **Per-track input modes (shipped, 3.9).** `Track + NavUp/Down` cycles
  the focused track between `PLAY ↔ CHROMATIC ↔ LEVELS`. In **CHROMATIC**
  the 16 step cells become a one-octave keyboard (NavUp/Down shift the
  octave) — this is the shipped form of the old "keyboard mode." In
  **LEVELS** the cells become quantised velocity buckets.
- **Retrig / ratchet and Sound Pool modes (planned, 5.7).** The
  remaining modal surfaces — retrig/slice and the sound-pool live-swap —
  are deferred. The `SoundPool` data model and the sound-bank overlay
  already exist in the build; the trig-grid mode that drives them, and
  the ratchet redesign, land in 5.7. (The earlier `TrigGridMode`
  selector was removed; modes now ride the per-track input enum above.)

---

<a name="6-implemented-vs-planned"></a>
## 6. Implemented vs. planned

Lockstep is under active development. This manual describes both the
shipped behaviour and the design intent. To avoid confusion:

**Working today** — all of **Phase 1** (core sequencer), **Phase 2**
(performance grammar), **Phase 3** (the 10×4 control surface), and
**Phase 4 machines 4.1–4.4**. Concretely, that means:

- The full **10×4 QWERTY overlay** (the surface §5 documents) — cluster,
  Manipulation Zone, Section Bar, Step Grid, scope colour grammar, top-bar
  dashboard + held-context preview, contextual scope re-skin, and latch
  (hands-free virtual-hold).
- **Polymetric multi-track sequencing** (16 tracks); **P-Lock editing**;
  **trig conditions** (probability / m:n / prev-dep); the **Override-ELSE-Base**
  resolver; **polyphonic trig steps** (≤4 notes/step, snapshot-chord +
  realtime overwrite/overdub capture, note-edit overlay, note-count badge,
  musical-gate + per-note-velocity capture); **runtime polyphony** with the
  per-track Top/Bottom-bias spread selector.
- The **scope+verb grammar** — copy/paste/clear for step / section /
  track / pattern; global and pattern mutes; the Fill modifier;
  Control-All; the checkpoint stack; pattern queueing and chain mode;
  per-track input modes (CHROMATIC / LEVELS).
- **Canonical sections + post-machine FILTER/AMP** with role tags; the
  first-class **MIDI-out machine** (per-track CC banks, device presets);
  machine reassignment via `Func + Part`.
- **MIDI** CC ingestion (soft-takeover + scoped mappings), MIDI clock +
  sync modes; full **project serialization** (samples as `{path, hash}`
  refs).
- **Machines:** `SamplerMachine` (trim + four loop modes + ZC-snap),
  `SlicerMachine` (SLICE / SCRUB, transient detection, MONO/POLY, reverse),
  `FMMachine` (4-op, free matrix, Mono/Poly), `VAMachine` (dual PolyBLEP +
  SVF + LFO, Mono/Para-4), `DrumSynthMachine` (Rytm-style KICK/SNARE/HAT/TOM),
  plus the `StubMachine` fallback. Shared post-machine FILTER (SVF) + AMP.
- The **surface-model foundation** for external controllers (6.6.5a):
  one pure `buildSurfaceModel()` the screen renders from.

**Planned** — `3.11` pattern-length authoring (active); the rest of the
machine catalogue (`4.5` Static, `4.6` Percussion, `4.7` Digital, `4.8`
DrumSynth voice expansion); **Phase 5** performance depth (microtiming +
swing + quantize `5.1`; scenes + crossfader `5.2`; pattern/part management
UI `5.3`; sampling + resampling `5.4`; audition + cross-track record `5.5`;
special trig types `5.6`; the retrig/ratchet + Sound Pool trig-grid modes
`5.7`; UI polish + state-colour palette `5.8`); and **Phase 6** routing,
FX & platform (audio-input boundary + Thru `6.1`; recorder buffers `6.2`;
looper `6.3`; cue bus `6.4`; insert/master effects `6.5`; external
controller surfaces `6.6`, in progress; the Machine Module ABI `6.7`;
beta polish `6.8`).

See `ROADMAP.md` for the authoritative milestone breakdown and current
status — it is the single source of truth for what ships when.
```
