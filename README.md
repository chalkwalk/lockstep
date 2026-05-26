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
  `Part`, `Scene`, `Master`, `Mute`, `Fill` — MHY), plus a **held step**
  (`Trig`) and a **section** key. Two modifiers — one from each column —
  can be held together to combine scopes (the *compound chord*).
  (`Cue` is reserved for the cue bus, MU, but not yet bound to a key.)
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
Project
 └── Bank        (default 8)
      └── Pattern (16 per bank)
           └── references a Part
```

- **Project** — one plugin instance. Owns all banks, the sample pool,
  MIDI mappings, and global settings.
- **Bank** — a namespace of patterns, giving them memorable addresses
  ("Bank A, Pattern 03").
- **Pattern** — the trig grid and everything that varies *with* trigs:
  per-step overrides, P-Locks, per-track length/divider, trig defaults,
  and conditions.
- **Part** — the **kit**: which sound engine each track hosts, its base
  parameters, and sample references. Several patterns can share one
  Part, so you can "swap the pattern but keep the sounds," or give each
  pattern its own Part for a full kit change.

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
| **Part** | The per-track kit: machine identity, base parameters, sample refs. Shared or owned per pattern. |
| **Pattern** | The trig grid and per-step data; references one Part. |
| **Bank** | A group of patterns with addressable slots. |
| **Scope** | A held modifier declaring what the next verb operates on. Eight in the left cluster (`Func`, `Track`, `Pattern`, `Part`, `Scene`, `Master`, `Mute`, `Fill` — MHY), plus a held step and a section key. `Cue` is reserved for the cue bus (MU) but not yet bound to a key. |
| **Compound chord** | Two modifiers (one per column) held together to combine scopes. Cross-column only; never fires on its own — it just narrows the scope until a verb is pressed. `Func` composes with anything. |
| **Verb** | The action applied to the scope (`Record`=copy, `Play`=paste, `Stop`=clear, `Yes`, `No`). |
| **Section** | A grouping of parameters on the section bar (keys `5–0`). Canonical six: TRIG / SRC / FILTER / AMP / MOD / FX. Held scope modifiers reinterpret each key (e.g. `Track+FILTER` = post-machine filter, `Master+FX` = master FX). The Manipulation Zone shows eight parameters (4×2) of the active cell at a time. |
| **Scene / Crossfader** | A per-Part pair of sparse parameter maps (A and B) blended by one continuous fader. The `Scene` modifier assigns slots; `Scene + ^/v` picks endpoint A/B. The fader is mouse/CC/hardware-only (no QWERTY). |
| **Manipulation Zone (MZ)** | The four-parameter editing quadrant. What you are tweaking right now. |
| **Step Grid** | The 2×8 matrix of step keys mirroring the bottom two QWERTY rows. |
| **Focus / focused track** | The currently selected track (or Global). Determines what contextual encoders and selected-track MIDI map to. |
| **Edit context** | The held-step state that routes edits to a step override vs. the track base. |
| **Choke** | A 1–2 ms micro-fade applied before retriggering a monophonic voice, to avoid clicks. |
| **Control-All** | Holding `Track` with no track selected broadcasts the next parameter edit to every track that has a matching control. |
| **Mute** | Suppresses a track's trigs non-destructively. Global mutes survive pattern changes; pattern mutes are saved per pattern. |
| **Fill** | A momentary modifier: while held, fill-conditioned steps fire. Used for live variation. |
| **Trig condition** | A per-step (or per-track) firing rule: probability, iteration (m:n), previous-step dependency, and fill rule. |
| **Checkpoint** | A RAM-only snapshot of the current pattern + kit. Push before a risky idea; pop to revert. Up to 8 deep, not saved to disk. |
| **Chain** | A RAM-only queue of upcoming pattern changes — the closest thing to a song timeline (there is no fixed arrangement). |
| **Sample pool** | The project-wide library of samples, stored as `{path, hash}` references rather than embedded audio. |
| **Sound Pool** *(planned)* | A project-scope library of saved per-track sounds, recallable or P-lockable per step. |
| **Scene / crossfader** *(planned)* | A per-Part pair of parameter snapshots blended by a continuous fader. |
| **Scope colour grammar** *(planned, MHZ.1)* | A canonical palette per scope (`step` = light grey, plus distinct hues for `track / pattern / part / machine / scene / master`) used by key tints, the step-grid scope re-skin, and any badge that needs to say "which scope is held". |
| **Scope re-skin** | When a scope modifier maps to a 1-of-16 selector (Track / Pattern / Part; `Func + Part` = machine picker), the 16 step keys become a non-paginated index for that scope. Unavailable indices dim. Cells tint in the scope's colour. |
| **Top-bar dashboard** *(planned, MHZ.2)* | The top of the editor splits into a persistent performance dashboard (BPM, Bank/Pattern/Part, transport position, chain queue, checkpoint depth) on the left, and a live held-context preview on the right. |
| **Value-label table** *(planned, MHZ.2)* | A `ParamSpec` field carrying textual names for stepped/enum positions (`LP24 / LP12 / HP / BP`, `MONO / PARA`, …). The MZ renders the textual name in place of a number when present. |
| **Step-hold capture window** | The canonical chord-edit path: hold a step → play MIDI → each note-on snapshots all currently-held notes; release commits velocity (highest) and gate. Empty capture = no change. Independent of record-arm and transport. Multi-step: all held steps receive the same chord. |
| **Note-count badge** | 1–4 stacked tick marks on the left edge of each step cell showing `trigOverride.noteCount` — immediately visible without entering any edit mode. |
| **Note-edit mode** | `Func + Section(0) + step` (release step while Func+Trig held) enters a 1-octave chromatic keyboard on the step grid: cells 0–11 = C through B, 12–15 unused. Press a cell to toggle that pitch in the current view octave. Cross-octave instances show small octave-number badges. NavUp/NavDown shift the octave. Staged removals commit on Func release. |
| **P-Lock clear gestures** | `Trig + Func + Stop` clears every P-Lock on the held step(s), leaving trig intact. `Trig + (active MZ slot) + Stop` clears only that one slot. `Func + step` enters P-Lock clear mode: cells re-skin orange showing only the *set* P-locks (packed, not by raw slot index); press a cell to stage it for removal, press again to cancel; release Func to commit all staged removals. |
| **NoteSelection bias** | Per-track bias for chord-note spread when the machine voice count is smaller than the step's note count. `TopBias` (default) includes top + bottom and fills from the top; `BottomBias` fills from the bottom. Set in the TRIG meta-section, slot 3 (Bias = TOP / BOT). |
| **Func+Part machine picker** | Hold Func (1) then tap Part (W) — the Part key relabels to MACH; step cells show available machine names. Press a step to assign that machine to the active track. |

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

Hold **Track** (`Q`) and press a step key to select a track:
`Track + D` selects track 2. Release `Q`. Track 2 is now focused.

Load a snare into the pool and place trigs on steps **5 and 13**
(`H`, `N`) for a backbeat. (If track 2 isn't pointed at the snare yet,
open the **SRC** section — see Step 5 — and set its sample.)

### Step 4 — Make a track polymetric

Focus a track, open the **TRACK** meta section (`Func + 6`), and set its
**length** to something other than 16 — try 7. That track now loops every
7 steps while the others loop every 16, and the two phase against each
other. This is the heart of Lockstep's groove.

### Step 5 — Tweak a sound (and lock it per step)

The **Section Bar** is keys `4`–`9`. Press a section key (e.g. **SRC**,
the sound-source section) to bring its parameters into the
**Manipulation Zone** — the four-control quadrant. Turn the on-screen
encoders (or a mapped MIDI knob) to adjust them.

Now the magic: **hold a step key** and turn the same control. Instead of
changing the track's base value, you've written a **P-Lock** — that
parameter change applies *only* on that step. Hold step 9 and drop the
pitch, for example, and only the third kick is lower. Release the step;
the lock stays.

To remove a lock, hold the step and clear it (push the held encoder, or
use the section-clear gesture).

### Step 6 — Add a conditional trig

Open the **COND** meta section (`Func + 4`). With no step held, the four
controls set the **track's** base condition (probability, iteration
m:n). Set probability to, say, 50% and that track fires stochastically
each loop.

Hold a single step and the same controls now write that **step's**
condition — so you can make just one trig 50%-likely, or fire it only on
every 4th pass (set m:n to 1:4). The grid shows you what will fire before
it happens: certain hits are bright, skips are dim, probabilistic steps
are in between.

### Step 7 — Record a melody live

Press **Record-Arm** (`2`). Now play notes (via the on-screen keyboard
or an attached MIDI keyboard) and they're captured as trigs, quantised
to the focused track's grid. Hold a step while playing a note to write
that note's pitch onto that specific step instead.

Press `T` again to disarm.

### Step 8 — Perform variations

- **Mute a track live:** hold **Mute** (`A`) and press a track's step
  key (`A + S` = mute track 1). It drops out instantly and
  non-destructively. Toggle again to bring it back.
- **Fill:** hold **Fill** (`Z`). Any steps you've marked as fill-only
  fire only while you hold it — instant live variation.
- **Copy a pattern and mutate it:** hold **Pattern** (`Func + 2`) and
  press **Record** to copy; move to another pattern slot and press
  **Play** to paste. Now change it without touching the original.
- **Checkpoint before a risky idea:** press `Func + 2` (release without
  pressing a step) to push a snapshot. Experiment freely. Press
  `Func + O` to revert. Up to 8 levels deep.

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

> **Layout target (MHX).** This section documents the **10×4** surface
> that the MHX milestone delivers (DESIGN §33). The current shipping
> build still runs the prior **9×4** grid; the two differ in the left
> modifier column (one column → two) and therefore in where the step
> keys sit. Everything below describes the 10×4 target.

Lockstep uses a fixed **10×4** grid. The **left two columns** are an
eight-key modifier cluster, all reachable by one hand; the **right
eight columns** are the functional block — function/section keys (top
two rows) and step keys (bottom two rows).

```
 MODIFIERS    │  FUNCTIONAL BLOCK                                 keys
 [FUNC][TRACK]│ [TAP ][ ^  ][TRIG][SRC ][FILTER][AMP][MOD][ FX]  1 2 3 4 5 6 7 8 9 0
 [PATT][PART ]│ [ <  ][ v  ][ >  ][YES ][REC ][PLAY][STOP][ NO]  Q W E R T Y U I O P
 ─────────────┼──────────────────────────────────────────────────────────────────────
 [SCENE][MSTR]│ [ steps 1 - 8 ]                                   A S D F G H J K L ;
 [MUTE][FILL ]│ [ steps 9 - 16 ]                                  Z X C V B N M , . /
```

(The two left columns in each row hold the eight modifiers; the next
two slots on row 0 are `3=TAP` and `4=NavUp`; sections fill `5–0`.
Row 1's right side is `E=NavLeft / R=NavDown / T=NavRight` followed
by the verb cluster `Y U I O P` = `Yes / Rec / Play / Stop / No`.)

The verb keys `Rec` / `Play` / `Stop` on row 1 (`U I O`) are the
**scope verbs** (Copy / Paste / Clear in scope+verb grammar) — *not*
transport. Transport Play/Stop lives on key `0` (top-right corner);
`Yes`/`No` (`Y`/`P`) handle confirmations and snapshots
(`Func+Yes`=push, `Func+No`=pop).

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
| `2` | **Track** | One or more tracks; or, with none selected, Control-All. `Track+section` opens the track-foundation row (post-machine FILTER/AMP, IEffect inserts). |
| `Q` | **Pattern** | A pattern (or several, in chain mode). `Pattern+section` opens pattern-data cells. |
| `W` | **Part** | The kit half of the Project/Bank/Pattern/Part hierarchy. `Func+Part` (W relabels to MACH) opens the machine picker via step-cell re-skin. |
| `A` | **Scene** | Scene assignment; `Scene + ^`/`v` picks endpoint A/B. `Scene+section` opens scene-assign cells per section. |
| `S` | **Master** | Master-bus / FX focus. `Master+FX` opens master FX slots. |
| `Z` | **Mute** | The mute mask (hold and tap several tracks). |
| `X` | **Fill** | "While held, fills fire." `Fill+step` marks step as fill-only. |
| step key (held) | **Trig** | The held step(s). Multi-step holds allowed. |
| `5`–`0` | **Section** | The held section's parameters. Cell meaning depends on which scope (if any) is held alongside. |

(`Cue` is reserved as a scope (for the cue bus / pre-listen feature
landing in MU) but is not bound to a cluster key yet; cue-scene and
cue-routing live under `Func+Scene` and master-scope cells in the
interim.)

**Compound chords.** Hold one modifier from each column to combine
scopes (e.g. `Scene + Mute` = fade a track across the crossfader). The
rule: cross-column only, a two-modifier hold never acts on its own (it
just narrows the scope until you press a verb), and `Func` composes
with anything.

### 5.3 Verb keys

| Key | Verb | Meaning |
|---|---|---|
| `Y` | **Yes** | Affirmative — confirm a prompt; `Func+Yes` = snapshot push. |
| `U` | **Record** | Copy the current scope into the clipboard. |
| `I` | **Play** | Paste the clipboard into the scope. |
| `O` | **Stop** | Clear the scope. |
| `P` | **No** | Negative — dismiss a prompt; `Func+No` = snapshot pop. |

### 5.4 Transport and navigation

| Key | Action |
|---|---|
| `0` | Play / Stop transport. |
| `Func + 0` | Stop and reset to the start. |
| `3` | Tap tempo (`Func + I` = toggle metronome). |
| `9` | Toggle record-arm. |
| `4` | Navigate up (inverted-T above `E R T`). |
| `E` / `R` / `T` | Navigate left / down / right. |

### 5.5 Track selection and focus

Lockstep has 16 tracks. The track header shows 8 at a time; the **"1–8" / "9–16"** page button (top-left of the track row) flips between banks. Selecting a track via keyboard automatically flips to the correct page.

| Gesture | Action |
|---|---|
| `Track (Q) + D–;` | Select / focus track 1–8 (`Q + D` = track 1, … `Q + ;` = track 8). |
| `Track (Q) + C–/` | Select / focus track 9–16 (`Q + C` = track 9, … `Q + /` = track 16). |
| Page button (click) | Flip track header between tracks 1–8 and 9–16. |

Tracks 1–8 default to `SamplerMachine` and tracks 9–16 to `MidiOutMachine` (Digitakt-style default split). Any track can be reassigned to any machine via **`Part + SRC`** (`W + 6` — opens the machine selector popup; replaces the retired `Func+R` gesture). A small **"M"** badge in the top-right corner of a track button identifies MIDI-out tracks at a glance.

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
| `7` | **FILTER** — filter *(post-machine block: planned, ME)* |
| `8` | **AMP** — amplitude envelope *(post-machine block: planned, ME)* |
| `9` | **MOD** — modulation (LFO, matrices, per-op envelopes, voice/portamento) |
| `0` | **FX** — per-track effects *(planned, MV)* |

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
| `Mute (Z) + step key` | Toggle **global** mute on that track (survives pattern changes). |
| `Func + Mute (Z) + step key` | Toggle **pattern** mute (saved with the pattern; applied on Func release). |
| `Func` held + multiple mute toggles | Deferred multi-select — all selected tracks toggle atomically on release ("kill four tracks at once"). |

(`Func + Mute` is legal even though both are column-1 modifiers: `Func`
is the universal qualifier.)

Mutes are non-destructive: trigs are suppressed at the output, no
note-offs are forced. There is no separate solo — "solo" is "mute
everything else," which the multi-select gesture already gives you.

### 5.12 Fills

- Hold **Fill** (`Z`): while held, every step's condition treats "fill"
  as true.
- Each step's condition has a **fill rule**: `Always` (default — ignore
  fill), `OnlyFill` (fire only while Fill is held), `NeverFill` (fire
  only while Fill is *not* held).
- The grid previews fill-only steps in a distinct colour so you can see
  what a fill will do before you trigger it.

### 5.13 Trig conditions

Set in the **COND** section (`Func + 3`). Three condition types, each
valid at track level (no step held) or step level (step held):

- **Probability (1–100%)** — stochastic firing.
- **Iteration (m:n)** — fire on pass *m* of every *n* loops.
- **Previous-step dependency** — fire only if the previous step did (or
  didn't) fire. Step-level only.

All conditions are **deterministic and pre-computable**, so the grid
shows certain-fire / certain-skip / probabilistic states ahead of the
playhead.

### 5.14 Patterns, banks, and chaining

| Gesture | Action |
|---|---|
| `Pattern (A) + step key` | Queue a pattern to switch at the next grid boundary. |
| `Pattern + Stop` | Cancel the queued switch. |
| `Pattern + Record` | Copy the whole pattern. |
| **Fork Part** *(Func-layer; exact key provisional in MHX)* | Give the active pattern its own copy of the kit. |
| Chain mode | Append multiple patterns to a RAM-only play queue (the only song-level surface; not saved). |

Pattern switches are queued, not instant — the swap happens at a musical
boundary. The transport chrome shows the queued pattern.

### 5.15 Checkpoints (live undo)

| Gesture | Action |
|---|---|
| `Pattern (A)` (release without pressing a step) | Push the current pattern + kit onto the checkpoint stack. |
| `Func + O` (No) | Pop and restore the last checkpoint. |

Up to 8 deep, oldest evicted on overflow. **RAM-only** — checkpoints are
a scratch-take tool and do *not* persist across save/reload. The chrome
shows the stack depth (`CK:N`).

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

### 5.17 Keyboard / UI revamp *(planned, MHZ)*

A chrome-and-grammar pass over the 10×4 surface that MHX/MHY froze.
Lands as three sub-milestones (MHZ.1 → MHZ.2 → MHZ.3) before the
remaining MH machine catalogue resumes.

**MHZ.1 — Surface chrome (planned).**

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

**MHZ.2 — Contextual modes, top bar, MZ streamline (planned).**

- **Step-grid scope re-skin.** Hold `Track` and the 16 step keys
  become a 1-of-16 track picker; `Pattern` → pattern picker; `Part`
  → part picker; `Part + SRC` → machine picker showing machine names.
  **Pagination is suppressed in this mode** — only "which key was
  pressed" matters. Unavailable indices dim; cells tint with the
  scope colour.
- **Top-bar dashboard + held-context preview.** The pre-MHZ "mode
  chips" row is replaced by a persistent performance dashboard (BPM,
  Bank / Pattern / Part identity, transport position, chain queue,
  checkpoint depth) on the left, and a live held-context preview on
  the right (e.g. `TRACK 3 + …`, `PART + SRC → machine picker`).
- **MZ streamline.** Each slot collapses to a larger rotary plus a
  single value display; stepped/enum params show textual values
  (`LP24 / LP12 / HP / BP`, `MONO / PARA`) instead of numbers when
  `ParamSpec.valueLabels` is populated.
- **Double-click rotary** resets a slot to its default. (Eventual
  hardware push-encoder-twice maps to the same gesture.)

**MHZ.3 — Note capture, P-Lock clear, machine picker.**

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
- **Func+Part machine picker.** Hold Func (1) and tap Part (W) —
  Part relabels to MACH; step cells show available machine names.
  Press a step to assign that machine to the active track. Release
  Func or Part to exit.

**MHZ.4 — Polyphonic step authoring improvements.**

- **Snapshot chord capture.** Step-hold MIDI capture now uses
  snapshot-currently-held semantics: each note-on writes the full set
  of currently-held MIDI notes to the step (not just the pressed note).
  Note-offs don't change the step's stored notes. Multi-step: all held
  steps receive the same chord in parallel.
- **Realtime chord record.** Transport-time record-arm now aggregates
  notes that quantise to the same step into a chord (up to 4 notes,
  de-duplicated). A new note-on to a different step starts fresh.
- **Note-count badge.** 1–4 stacked tick marks on each step cell show
  `noteCount` at a glance — visible in normal mode without entering any
  edit overlay.
- **Note-edit mode.** `Func + Section(0) + step` (Trig key held, press
  a step then release it) enters a 1-octave chromatic overlay: cells 0–11
  = C through B, cells 12–15 unused. Press a cell to toggle that pitch in
  the current octave. Cross-octave instances of each pitch class show as
  small octave-number badges. NavUp/NavDown shift the view octave. Staged
  removals commit on Func release.
- **NoteSelection in TRIG meta-section.** The TRIG meta-section (hold
  any step and navigate to Section key 5) now shows a "Bias" slot (TOP /
  BOT) that reads and writes the per-track chord-spread bias.
- **VA paraphonic topology.** VA Para-4 mode now routes chord notes to
  oscillators by slot: notes 1 & 3 → osc1+sub, notes 2 & 4 → osc2+sub.
  A single shared noise generator replaces per-voice noise generators.

### 5.18 Trig-grid modes *(selectors present; behaviour planned)*

The trig grid can become a modal surface for non-step roles:

(Mode-chord keys are on the `Func` layer; exact keys provisional in MHX.)

| Gesture | Mode |
|---|---|
| `Func` + mode key | Keyboard mode (16 keys → chromatic notes) |
| `Func` + mode key | Retrig / slice mode |
| `Func` + mode key | Sound Pool mode |

A mode badge (`KEY` / `RETRIG` / `POL`) shows in the chrome. The full
behaviour of these modes lands in a later milestone (see
[§6](#6-implemented-vs-planned)).

---

<a name="6-implemented-vs-planned"></a>
## 6. Implemented vs. planned

Lockstep is under active development. This manual describes both the
shipped behaviour and the design intent. To avoid confusion:

**Working today** (milestones M0–MHZ.4): sample loading and playback;
**sampler trim and loop** — `samp_start` / `samp_length` window into a sample,
`samp_loop_mode` (OFF / SUS / S+R / ALL), `samp_loop_start` / `samp_loop_len`
loop region relative to the playback window; edit-time zero-crossing snap on all
four position slots; **SlicerMachine** (`lockstep.slicer.v1`) — dual-mode SLICE
(note → slice index 0–15, start/length relative to active slice) and SCRUB
(note → pitch rate); up to 16 slices, auto-placed by equal division or
transient detection (5 ms RMS blocks, fast/slow envelope ratio, triangular
centre-weighted search, ZC snap within block); MONO / POLY toggle (V4 voice
pool); per-slice anti-click fade; reverse playback at `slicer_rate < 0`; AHDSR
envelope; choke micro-fade; DC blocker, soft-clip, gain smoothing;
polymetric multi-track sequencing; P-Lock editing; trig conditions
(probability / m:n / prev-dep); MIDI CC ingestion with soft-takeover and
scoped mappings; MIDI clock + sync modes; the full QWERTY overlay
(Manipulation Zone, Section Bar, Step Grid); pattern recording; full
project serialization; the scope+verb grammar; copy/paste/clear for
step / section / track / pattern; global and pattern mutes; the Fill
modifier; Control-All; the checkpoint stack; pattern queueing and chain
mode; trig-grid mode selectors with chrome badges; the **FM synthesizer**
(`FMMachine`) — 4-operator FM with free modulation matrix, per-operator
ADSR, ratio / fine-tune / mix per operator, macro attack / release /
sustain scalars, **Mono / Poly voice modes** (Poly: 4-voice pool with
oldest-voice stealing); machine selection via `Part + SRC` (post-MHY; was `Func + R`); **runtime
polyphony** — each machine reports its live voice count per trig, and
chord steps are clamped via a per-track Top-bias / Bottom-bias
"spread-with-bias" selector (editable in the TRIG meta-section as "Bias =
TOP / BOT") that keeps the top and bottom voices first and spreads
remaining picks evenly between them; **polyphonic trig steps** — steps
carry up to 4 notes (hold step + play keys for snapshot-chord capture;
realtime record aggregates chord notes per step; note-edit overlay for
keyboardless entry; note-count badge on each cell; gate auto-written on
last-note-off); the **VA synthesizer** (`VAMachine`) — 2× PolyBLEP
oscillators (Saw/Pulse/Tri/Sin) + sub + shared noise, state-variable
filter (LP4/LP2/HP/BP + drive), filter ADSR, amp ADSR, LFO (6 shapes, 4
targets: Cutoff/Pitch/PW/Amp), portamento, and Mono / Paraphonic-4 voice
modes (Para: chord notes 1 & 3 → osc1+sub; notes 2 & 4 → osc2+sub). **Note:** the shipping overlay is still the
**9×4** layout (one left modifier column, four-slot Manipulation Zone);
§5 documents the 10×4 target that MHX delivers.

**Planned** (remaining milestones): post-machine
FILTER and AMP blocks with role-tagged sections (ME); the first-class
MIDI-out machine (MF); the full behaviour of the alternate trig modes
— Keyboard / Retrig / Sound Pool (MG); scenes + crossfader (MI);
pattern/part management UI (MJ);
microtiming and swing (ML); 16-levels mode (MM); live sampling and
resampling (MN); audition and cross-track record (MO); UI polish and
the state-colour palette (MP); special trig types (MQ); audio-input
routing and the Thru machine (MR); recorder buffers and looper (MS–MT);
the cue bus (MU); and the insert/master effects system (MV).

See `ROADMAP.md` for the full milestone breakdown and current status.
```
