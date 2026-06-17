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
7. [Gesture tree (every action, by press order)](#7-gesture-tree)

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
  content hash), never as embedded audio. Saves are instant, whether to a
  standalone project file or a DAW host save, and auto-saves stay cheap.

---

<a name="2-the-paradigm"></a>
## 2. The paradigm: how to reason about the system

If you understand five ideas, you can predict how almost everything in
Lockstep behaves.

### 2.1 Scope + verb

Every editing and performance action is built from two halves:

- A **scope** key declares *what you're about to operate on*. Scopes are
  held modifiers: the eight-key left cluster (`Func`, `Track`, `Phrase`,
  `Scene`, `Morph`, `Song`, `Mute`, `Fill` — 7.9 naming), plus a **held
  step** (`Trig`) and a **section** key. Two modifiers — one from each
  column — can be held together to combine scopes (the *compound chord*).
  (`Cue` is reserved for the cue bus, 6.4, but not yet bound to a key.)
- A **verb** key declares *what to do*. The verb set is small and
  fixed (`Y U I O P`): `Snapshot` (push checkpoint), `Record` (= copy),
  `Play` (= paste), `Clear`, `Yes` — the `Func` layer gives `Restore /
  Panic / Delete / No` — plus turning an encoder for live value tweaks.

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
musical subdivision (1/64 to 4/1, straight / dotted / triplet), so a
7-step track and a 16-step track phase against each other naturally with
no master-bar concept.

### 2.5 The sound path: what the names mean and where they sit

The container hierarchy above says where *musical content* lives. This
section is the other axis — what a **track** *is*, and the order sound
flows through it. Every name here recurs throughout the manual:

```
Phrase ──trigs──▶ Machine ──▶ Foundation (FILTER → AMP) ──▶ Inserts ×2 ─┬─ (×sendA) → Send bus A
                                                                           ├─ (×sendB) → Send bus B
                                                                           └────────────────────▶ track sum
                                    Send bus A ──▶ Send Return A ─┐
                                    Send bus B ──▶ Send Return B ──┤
                                    track sum ────────────────────▶ Σ ──▶ Master Insert 1 ──▶ 2 ──▶ output gain
```

- **Machine** — the sound engine a track hosts (sampler, FM, VA, drum
  synth, MIDI-out…). Each machine declares its own parameters; the
  section keys (`5–0`) page through them. The machine is part of the
  **Kit**, so swapping Kits swaps machines.
- **Foundation** — the post-machine FILTER and AMP blocks that the
  *sequencer* owns, identical on every track regardless of machine.
  This is what "track-foundation row" (`Track + section`) refers to: a
  guaranteed filter and amplitude envelope that exist even when the
  machine has none of its own. A machine's *own* filter, if it has one,
  still lives behind the canonical FILTER key — the foundation filter
  is in addition, downstream.
- **Inserts** — two per-track `IEffect` slots after the foundation.
  Loaded via the `Func+FX` picker, edited on the FX section,
  momentarily bypassed with the Animate gesture (`FX` + step).
  MIDI-out tracks have none (no audio).
- **Send buses** — post-insert taps from each track. Set Send A / B
  levels on AMP page 2. Send returns are processed before master inserts.
- **Master FX** — four Song-scope units (2 inserts + 2 send returns)
  over the summed output. Loaded via `Func+Song+FX`; cycle with
  repeated press (FX1→FX2→Snd A→Snd B). HQ-only effects are hidden
  from the track picker.

And the *state* that feeds this path is layered, finest layer winning
(this is "more specific scope wins" applied to values):

- **Base** — the Kit's stored value for a parameter. What you edit with
  no step held.
- **Morph** — a per-Scene A/B pair of sparse parameter maps blended by
  the crossfader, sitting *over* the base. Continuous, performed.
- **P-Lock / trig override** — a per-step override, sitting over both.
  Discrete, authored. A step that has one ignores Morph and base for
  that parameter.

So: a **Phrase** decides *when* notes happen, the **Kit** (machine +
foundation + inserts) decides *what they sound like*, **Morph** bends
the sound continuously across a Scene, and **P-Locks** pin exact values
to exact steps. The **Scene** chooses which Phrase each track plays and
who is audible; the **Song** holds it all; the **Set** is the plugin.

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
| **Verb** | The action applied to the scope. Verb row `Y U I O P` = `Snapshot / Record (=copy) / Play (=paste) / Clear / Yes`; under `Func` the same keys give `Restore / Panic / Delete / No` (7.12). |
| **Section** | A grouping of parameters on the section bar (keys `5–0`). Canonical six: TRIG / SRC / FILTER / AMP / MOD / FX. Held scope modifiers reinterpret each key (e.g. `Track+FILTER` = post-machine filter, `Song+FX` = master FX). The Manipulation Zone shows eight parameters (4×2) of the active cell at a time. |
| **Morph / Crossfader** *(5.2)* | A per-Scene pair of sparse parameter maps (A and B) blended by one continuous fader. Hold/latch `Morph` + encoder sculpts both poles at the normalised fader-split (scene-layer selector, symmetric with holding a step for P-Lock). `Morph + ^/v` forces pure A/B writes (QWERTY path). Absent pole mirrors the set pole — fader is inert until A ≠ B. Stepped params snap at f=0.5. Right-click the on-screen fader for MIDI-learn. |
| **Manipulation Zone (MZ)** | The eight-parameter (4×2) editing band. What you are tweaking right now. |
| **Step Grid** | The 2×8 matrix of step keys mirroring the bottom two QWERTY rows. |
| **Focus / focused track** | The currently selected track (or Global). Determines what contextual encoders and selected-track MIDI map to. |
| **Edit context** | The held-step state that routes edits to a step override vs. the track base. |
| **Choke** | A 1–2 ms micro-fade applied before retriggering a monophonic voice, to avoid clicks. |
| **Foundation** | The post-machine FILTER + AMP blocks the *sequencer* owns on every track, identical regardless of machine. Reached via `Track + section` (the track-foundation row). See §2.5. |
| **Insert (FX)** | One of a track's two post-foundation `IEffect` slots. Loaded via the `Func+FX` picker, edited on the FX section, momentarily bypassed via Animate (`FX` + step). MIDI-out tracks have none. |
| **Master FX** | Four Song-scope FX units on the master bus: 2 inserts (post-sum) + 2 send returns (post send-bus). Loaded via `Func+Song+FX`; cycle units with repeated press. |
| **Send A / Send B** | Per-track post-insert level tap into shared send buses (AMP page 2, slots 8–9). Each send bus has a return effect before the master inserts. |
| **Animate** | The momentary insert punch-in: hold `FX` + step to bypass (or enable) an insert for exactly the hold duration. Performance-only — never written to the pattern. Under Song+FX focus the step grid targets the four master units: steps 0-3 = master FX1, 4-7 = FX2, 8-11 = Send A, 12-15 = Send B. |
| **Density** | Live, subtractive trig-thinning overlay. `Func`-held → transient per-track Density band. **Double-tap Func** → sticky DENSITY mode; nav keys page between tracks 1-8 and 9-16; `Song`-held (encoder or on-screen drag) → master offset (visible as arc baseline shift); FX key cycles Amount/Mode sub-page (Musicality + Selection). `Song`-alone = swing (unchanged). Only silences would-fire trigs. Ephemeral amounts; durable Musicality + Selection per track. Selection has three detents: **Scrub** (deterministic), **Re-roll** (stochastic), **Exempt** (track bypasses density entirely — amount and Musicality cells greyed). |
| **Velocity overlay** | Per-track live velocity modulation computed at emit time, not baked. **Func+AMP** → sticky VEL OVERLAY mode; AMP re-press cycles sub-pages (Depth → Center → Mode → Blend); press any other section key to exit; nav keys page between tracks 1-8 and 9-16. Mode: Off (no overlay) / Bar (metric weight against coreTime bar). Blend: Replace (overlay supersedes authored velocity) / Mix (overlay delta added on top; Euclidean-baked accents stay active). Durable per-track, serialized (v20). |
| **Retrig / ratchet** | Per-step re-triggering at a musical rate (`/4 … /32T`). `Fill+TRIG` opens the rate picker: press a rate for a live stutter on the focused track, or hold a step first to bake the rate as a per-step P-Lock. Slicer tracks show a slice picker instead. |
| **Euclidean generator** | `Phrase+Fill` held: encoders shape `PULSE / OFSET / ACCNT` against the phrase length, audible live; the mode is latched until **P** (commit) or **Func+P** (cancel). Y is inert in this mode. |
| **Control-All** | Holding `Track` with no track selected broadcasts the next parameter edit to every track that has a matching control. |
| **Mute** | Suppresses a track's trigs non-destructively. `Mute+step` = global mute (survives scene/song changes); `Scene+Mute+step` = per-scene mute (the scene's active-mask). |
| **Fill** | A momentary modifier: while held, fill-conditioned steps fire. Used for live variation. |
| **Trig condition** | A per-step (or per-track) firing rule: probability, iteration (m:n), previous-step dependency, and fill rule. |
| **Checkpoint** | A RAM-only snapshot for live undo. Bare `Y` (SNAP) pushes before a risky idea; `Func+Y` (RESTORE) tap=pop/hold=floor. The `Y` key owns both halves. **Scope-respecting:** the snapshot captures whichever scope is held (none=Song, Track, Scene, Phrase). Up to 8 deep per scope; floor = saved state. |
| **Launch model** | Performance is launch-based, not arrangement-based: queue a **Scene** (`Scene+step`) to fire at the next core-time boundary, or switch **Songs** (`Song+step`). There is no written timeline or pattern chain. |
| **Sample pool** | The project-wide library of samples, stored as `{path, hash}` references rather than embedded audio. |
| **Sound Pool** | A project-scope library of saved per-track sounds (machine + base params + sample refs). `Fill+SRC` re-skins the step grid to the pool for live-swap audition; with a step held the swap bakes as a `sound_id` P-Lock (5.7). |
| **Scope colour grammar** *(3.3)* | A canonical palette per scope (`step` = light grey, plus distinct hues for `track / phrase / scene / machine / morph / song`) used by key tints, the step-grid scope re-skin, and any badge that needs to say "which scope is held". In-scope keys (the section keys and verbs the scope rebinds) light fill+border in the scope colour; ambient keys stay neutral; reserved keys dim. |
| **Scope re-skin** | When a scope modifier maps to a 1-of-16 selector (Track / Phrase / Scene; `Func+Track` = machine/Kit picker), the 16 step keys become a non-paginated index for that scope. Unavailable indices dim. Cells tint in the scope's colour. |
| **Top-bar dashboard** *(3.4)* | The top of the editor splits into a persistent performance dashboard (BPM, Song/Scene/Phrase, transport position, pending Scene, checkpoint depth) on the left, and a live held-context preview on the right. |
| **Value-label table** *(3.4)* | A `ParamSpec` field carrying textual names for stepped/enum positions (`LP24 / LP12 / HP / BP`, `MONO / PARA`, …). The MZ renders the textual name in place of a number when present. |
| **Step-hold capture window** | The canonical chord-edit path: hold a step → play MIDI → each note-on snapshots all currently-held notes; release commits velocity (highest) and gate. Empty capture = no change. Independent of record-arm and transport. Multi-step: all held steps receive the same chord. |
| **Note-count badge** | 1–4 stacked tick marks on the left edge of each step cell showing `trigOverride.noteCount` — immediately visible without entering any edit mode. |
| **Note-edit mode** | `Func + Src + step` (the SRC key relabels NOTE; release the step while Func+Src held) enters a 1-octave chromatic keyboard on the step grid: cells 0–11 = C through B, 12–15 unused. Press a cell to toggle that pitch in the current view octave. Cross-octave instances show small octave-number badges. NavUp/NavDown shift the octave. Staged removals commit on Func release. |
| **P-Lock clear gestures** | `Trig + Func + Clear` (`Trig + 1 + O`) clears every P-Lock on the held step(s), leaving trig and condition intact. `Trig + (active MZ slot) + Clear` clears only that one slot. `Func + step` enters P-Lock clear mode: cells re-skin orange showing only the *set* P-Locks (packed, not by raw slot index); press a cell to stage it for removal, press again to cancel; release Func to commit. `Trig + (section key) + Clear` clears just that section's overrides on the held step(s); since **SRC** owns the note payload, `Trig + SRC + Clear` clears note / velocity / gate overrides only, leaving trig and P-Locks intact. The hint band shows these gestures automatically when a step with P-Locks or note overrides is held. |
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
- **Copy a phrase and mutate it:** hold **Phrase** (`Q`) and press
  **Record** (`U`) to copy; move to another phrase slot and press
  **Play** (`I`) to paste. Now change it without touching the original.
- **Checkpoint before a risky idea:** press `Y` (SNAP) to push a
  snapshot. Experiment freely. Press `Func + Y` (RESTORE) to revert
  (tap = pop one, hold = jump to the saved floor). Up to 8 levels deep.

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
 [PHRASE][SCENE]│[ < ][ v ][ > ][SNAP][REC ][PLAY][CLEAR][YES]   Q W E R T Y U I O P
 ──────────────┼──────────────────────────────────────────────────────────────────────
 [MORPH][SONG ]│ [ steps 1 - 8 ]                                  A S D F G H J K L ;
 [MUTE ][FILL ]│ [ steps 9 - 16 ]                                 Z X C V B N M , . /
```

(The two left columns in each row hold the eight modifiers; the next
two slots on row 0 are `3=TAP` and `4=NavUp`; sections fill `5–0`.
Row 1's right side is `E=NavLeft / R=NavDown / T=NavRight` followed
by the verb cluster `Y U I O P`, whose **on-screen legends** are
`SNAP / REC / PLAY / CLEAR / YES`. Each verb key carries a `Func`-layer
secondary legend: `Func+Y`=RESTORE, `Func+O`=DEL, `Func+P`=CANCEL (the `U`
key has no Func legend — `Func+U` is omni copy). The `I` key still paints
a stale `PANIC` legend, but `Func+I` is **unqualified paste** — Panic
moved to `Song+Clear`.)

The verb keys are **context-sensitive** — they read three layers:

- **No scope held** — transport / confirm: `Y`(SNAP) pushes a checkpoint,
  `U`(REC) toggles record-arm (double-tap = overdub), `I`(PLAY)
  starts/stops the transport (double-tap = stop-to-top), `O`(CLEAR) clears
  the active P-Lock slot, `P`(CONFIRM) confirms a pending prompt.
- **A scope held** — the scope verbs: `U`=Copy, `I`=Paste, `O`=Clear.
  `Y` is the scope's snapshot (reserved/dim on most scopes); `P`=**QUANT**
  under Trig/Track/Phrase (zero microOffset), dim on Scene/Morph/Song/Mute/Fill.
- **`Func` qualifier** — `Func+Y`=Restore (pop/floor), `Func+U`=omni copy,
  `Func+I`=unqualified paste, `Func+O`=delete entity, `Func+P`=cancel a
  prompt.

There is no separate transport key — the verb row does double duty, which
is why the surface needs no extra buttons. The full action set is indexed
by press-order in [§7](#7-gesture-tree).

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
| `Q` | **Phrase** | A per-track musical phrase (pure note content). `Phrase+step` (or `Track+Phrase+step`) deviates the focused track to that phrase. `Scene+Phrase+step` deviates all tracks; landing on the diagonal row clears all deviations. To clear all deviations: `Scene+Phrase+step` on diagonal, re-launch active Scene, or `Func+Scene+step`. |
| `W` | **Scene** | A launchable cross-track row (diagonal phrase row + active-mask + core time). Scene N always plays phrase row N. `Scene+step` occupied = carry overlay; on active = revert to floor; on **empty** = baked-copy create + launch. `Func+Scene+empty` = baseline-copy create; `Mute+Scene+empty` = blank create. `Func+Scene+occupied` = floor launch. `Scene+Record` = commit-and-bake. `Func+Scene+Record/Play` = copy/paste. |
| `A` | **Morph** | The A/B crossfader scope. Hold/latch + encoder sculpts overlay at current fader split; `Morph+^`/`v` forces pure A/B writes; `Morph+Mute` = fluid mute a track. |
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

The five verb keys carry an on-screen primary legend, a `Func`-layer
secondary legend, and a scope-compound meaning. (Note: the key the README
historically called "Yes" is the `Y`/SNAP key; the confirm action lives on
`P`, now labelled **CONFIRM** / **CANCEL**.)

| Key | Legend | No scope | Under a scope | `Func + key` |
|---|---|---|---|---|
| `Y` | **SNAP** | Push a checkpoint (scope-respecting) | Scope snapshot (`Scene`=re-sync; most scopes reserved/dim) | **Restore** — tap = pop one, hold = jump to floor |
| `U` | **REC** | Toggle record-arm (double-tap = overdub) | **Copy** scope → clipboard | **Omni copy** (scene + track + phrase) |
| `I` | **PLAY** | Play / Stop transport (double-tap = stop-to-top) | **Paste** clipboard → scope | **Unqualified paste** (stamp the one captured layer) |
| `O` | **CLEAR** | Clear active P-Lock slot (`Scene`/`Phrase` held = cancel queued scene; `Song` held = Panic) | **Clear** scope contents | **Delete** — opens deletion picker (see §5.4a) |
| `P` | **CONFIRM** | Confirm a pending prompt (green = CONFIRM / red under Func = CANCEL) | **Quantize** (Trig/Track/Phrase scope: zero microOffset on held steps / whole track / all tracks); scope confirm (dims on Scene/Morph/Song/Mute/Fill) | **Cancel** a pending prompt |

> **Checkpoint push/restore.** Push is the bare `Y`(SNAP) key; restore is
> `Func+Y`(RESTORE). Both are *scope-respecting*: with no scope held the
> snapshot is the Song; with `Track` / `Scene` / `Phrase` held it captures
> that scope. While a section-suite scope is held, `Y` is that scope's
> snapshot rather than a global one. See [§5.15](#515-checkpoints-live-undo).

### 5.4 Transport and navigation

Transport and record-arm ride the verb row (no scope held — see §5.1):

| Key | Action |
|---|---|
| `I` (Play) | Play / Stop transport (no scope held); `Func + I` = **unqualified paste** (stamps the single captured layer by type; rejects omni grab with "Paste: pick a scope"). |
| `O` (Clear) | Clear active P-Lock slot (no scope held); `Func + O` = delete entity (opens deletion picker). |
| `U` (Rec) | Toggle record-arm (overwrite). Double-tap = overdub (append); `Func + U` = **omni copy** (captures scene + active track + pattern in one grab; badge `CPY:ALL`). |
| `Func + Song + U` | **Capture** — arm or disarm WAV capture of the master output (see §5.20). |
| `3` | Tap tempo; `Func + 3` = toggle metronome. |
| `4` | Navigate up (inverted-T above `E R T`). |
| `E` / `R` / `T` | Navigate left / down / right. |

### 5.4a Deletion picker and named confirms

Holding a scope+Func+Clear chord (`Track/Phrase/Scene + Func + O`) enters the
**deletion picker** modality — the step grid repaints as a slot-selector for
that scope. Status reads "Delete which PHRASE?" (or TRACK / SCENE). The
currently playing/focused slot is highlighted.

- **Sticky prompt.** Releasing the arming chord does **not** cancel; the picker
  persists until you tap a slot or press a non-Func key (shows "Cancelled").
- **Tap a slot.** The picker exits and a named confirm replaces it: "Delete
  PHRASE 3?  P=CONFIRM  Func+P=CANCEL". The `P` key shows **CONFIRM (green)** with Func
  up and **CANCEL (red)** with Func held; the live colour is the signal.
- **Confirm stickiness.** The confirm prompt is also sticky — releasing Func or
  any held modifier does not cancel. Any key press other than `P` or `Func`
  cancels (status "Cancelled"; press swallowed).
- **Scope coverage.** Track / Phrase / Scene deletions use the picker.
  `Song+Func+O` is inert (no entity to delete). `Morph+Func+O` = morph
  **erase** (no picker; Morph maps are not entities).
- **Delete semantics.** Delete Phrase N = reset slot N of the **focused track**
  to uninitialised. Delete Scene N = clear scene slot N (falls back to scene 0
  if the active scene is deleted). Delete Track = mark the track as empty.

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
| `VAMachine` | VA | Virtual-analog dual-osc synth. PolyBLEP oscillators + sub + shared noise. State-variable filter (LP24/LP12/HP/BP + drive). Filter ADSR + amp ADSR. LFO (6 shapes). Mono / Paraphonic-4 voice modes. Para topology: chord notes 1 & 3 → osc1+sub; notes 2 & 4 → osc2+sub. |
| `DrumSynthMachine` | DR | Rytm-style per-track drum synthesis. One stepped `Type` param selects the variant; each has dedicated DSP. Eight types ship: KICK, SNARE, HAT, TOM, CLAP, COWBELL, CYMBAL, RIMSHOT. |
| `MidiOutMachine` | M | MIDI CC / note output to external gear. Configurable destination, channel, program, 16 CC slots with user-assignable numbers and labels. |

**Per-track DSP chain (universal, 8.28).** Every audio track runs the same
post-machine signal chain regardless of machine type:

```
machine → FLTR (LP/HP/BP/Notch/OFF) → [ENVELOPE] → CHANNEL → inserts → sends
```

- **FLTR** — always present; default mode is **OFF** (bit-exact passthrough, no CPU cost).
  Shared with the machine's FILTER section key on machines that don't own one natively;
  VA/FM/DrumSynth (which have internal filters) get a second FLTR page appended.
- **ENVELOPE** (AHDSR + gate source) — present only for machines that don't provide
  their own amplitude envelope (`SamplerMachine`, `SlicerMachine`, `MidiOutMachine`).
  VA/FM/DrumSynth handle amplitude internally and bypass this block.
- **CHANNEL** (level, pan, sendA, sendB) — always present for all machines including
  VA/FM/DrumSynth. P-locking `lockstep.amp.level` on any track audibly scales output.
  Pan is a track-level operation applied once here; machines output dual-mono and do not
  apply their own pan. (VA's internal pan slot is inert; the CHANNEL pan is canonical.)

**Stepped (enum) parameter values.** These are the closed value sets the
Manipulation Zone shows as text instead of numbers (from each machine's
`ParamSpec.valueLabels`; exhaustive as of Phase 4):

| Machine | Parameter | Values |
|---|---|---|
| *(all audio tracks)* | FLTR mode | `LP` · `HP` · `BP` · `NO` · `OFF` |
| | FLTR slope | `12dB` · `24dB` |
| | ENV gate src | `Envelope` · `Held-open` |
| `SamplerMachine` | Loop mode | `OFF` · `SUS` · `S+R` · `ALL` |
| | Retrig | `LEGATO` · `RETRIG` |
| | Vel>Amp | continuous 0–100 % (default 0 = velocity-independent; increase to scale level by note velocity) |
| `SlicerMachine` | Mode | `SLICE` · `SCRUB` |
| | Slice source | `EQUAL` · `TRANS` |
| | Loop mode | `OFF` · `SUS` · `S+R` · `ALL` |
| | Voice | `MONO` · `POLY` |
| `FMMachine` | Voice mode | `MONO` · `POLY` |
| | Retrig | `LEGATO` · `RETRIG` |
| `VAMachine` | Voice mode | `MONO` · `PARA` |
| | Filter type | `LP24` · `LP12` · `HP` · `BP` |
| | Osc 1 wave | `SAW` · `TRI` · `SQR` · `SIN` |
| | Osc 2 wave | `SAW` · `TRI` · `SQR` · `SIN` · `OFF` |
| | LFO shape | `SIN` · `TRI` · `SAW` · `SQR` · `S&H` · `RND` |
| | LFO target | `CUT` · `PITCH` · `PW` · `AMP` |
| | LFO sync | `FREE` · `SYNC` |
| | Retrig | `LEGATO` · `RETRIG` |
| `DrumSynthMachine` | Type | `KICK` · `SNARE` · `HAT` · `TOM` · `CLAP` · `COWBELL` · `CYMBAL` · `RIMSHOT` |

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
| `0` | **FX** — per-track effects (2 insert slots per track) |

Press a section key repeatedly to page through its parameters (the MZ
shows eight at a time, in two rows of four — `kMZSlots`). Holding
`Func` flips to each machine's **secondary** page (deep-dive
parameters like FM mod matrices); holding any other scope opens that
scope's row in the **scope-section matrix**. Like a scope-hold, holding
`Func` repaints the whole section row in colour: the cells that carry a
wired secondary glow (orange), and the cells that don't dim out — so the
row always shows which secondaries are actually reachable.

| Held scope | What `+5` (TRIG) means | …`+7` (FILTER) | …`+0` (FX) |
|---|---|---|---|
| `Track` | Kit **subdivision** (labelled `DIV`; two fields: note value + flavour Straight/Dotted/Triplet) | Post-machine (foundation) FILTER | Track inserts |
| `Phrase` | Phrase **length** (labelled `LEN`, per active phrase) | (dim) | (dim) |
| `Scene` | Trig templates | Scene-assign FILTER | Scene-assign FX |
| `Morph` | (dim — Morph never affects trigs) | Morph-assign FLTR | Morph-assign FX |
| `Song` | (dim) | (dim — master FILTER reserved) | **Master FX 1+2 + Send A/B** (4 units, cycled by re-press) |
| `Func` (over any of the above) | The secondary variant of the cell (e.g. `Func+Scene+FILTER` = the other scene's filter assignments). |

Each scope's `TRIG` cell opens the parameter owned by that hierarchy level.
Three metas sit on `Func`: **COND** (probability, m:n, prev-dep) on
`Func+TRIG`, **NOTE** (explicit note / velocity / gate step entry) on
`Func+SRC`, and the **transport globals** (output gain, sync mode, channel
mode) on `Func+7`. Trig defaults remain on bare `TRIG`. (Pre-6.5 the
transport globals sat under `Song+FX`; that cell now carries the master
insert parameters.)

**FX inserts and the effect picker.** Each track has two insert slots (slot
0 / slot 1). The `FX` key shows a dim **"PICK FX"** secondary hint at rest; holding
`Func` promotes it to the primary label. `Func+FX` opens the effect picker — the
step grid re-skins to the available effects catalogue; press a step to load that
effect into the focused slot (the other slot's loaded effect shows a dim cross-slot
hint). Re-press the active effect to toggle bypass. Re-press `Func+FX` while the
picker is open to cycle the targeted insert slot (0 → 1 → 0). Press `FX` (alone)
to navigate the insert's params in the MZ; hold `FX + step` momentarily to
**animate bypass** (bypass on press, restore on release). MIDI-out tracks show no
inserts. Under **Song+FX focus**, the same gesture targets the master units —
steps 0-3 bypass FX1, 4-7 bypass FX2, 8-11 bypass Send A, 12-15 bypass Send B —
and is suppressed when the master picker is open.

**Master bus: 2 inserts + 2 send returns.** The master bus has four FX units at
Song scope (DESIGN §32.3):

| Unit | Key | Role | Signal flow |
|---|---|---|---|
| Insert 1 | `Song+FX` (cycle 1) | Post-sum insert | track sum → Ins 1 → Ins 2 → out |
| Insert 2 | `Song+FX` (cycle 2) | Post-sum insert | (chained after Ins 1) |
| Send A | `Song+FX` (cycle 3) | Send return | accumulated send bus A → return FX → sum |
| Send B | `Song+FX` (cycle 4) | Send return | accumulated send bus B → return FX → sum |

Sends are post-fader, post-insert taps from each track. Set **Send A** / **Send B**
on **AMP page 2** (hold `AMP`, repeat to page-turn). `Func+Song+FX` opens the
master picker for the currently focused unit (re-press to cycle all four slots
regardless of whether they are loaded; re-pick the active effect to toggle bypass;
the other units' loaded effects show a dim cross-slot hint). `Song+FX` re-press
cycles through **loaded** units only, skipping empty ones (falls back to Insert 1 if
none are loaded); exit back to track params by pressing any bare section key.
Send return effects are typically loaded with Mix=1.0 (wet-only); insert effects
apply across the whole mix. MIDI-out tracks have no sends.

**Available effects:**

*Track inserts (any slot):*
| Badge | Name | Key params |
|---|---|---|
| `DLY` | Delay | Time, Feedbk, Mix, LPF |
| `REV` | Reverb | Size, Decay, Damp, Mix |
| `DRV` | Distortion | Drive, Mix |
| `CHR` | Chorus | Rate, Depth, Mix |
| `TLT` | Tilt EQ | Tilt (−1..+1), Gain (dB) |
| `CMP` | Compressor | Thresh, Ratio, Atk, Rel, Mkup |
| `BIT` | Bitcrush | Bits, Rate, Mix |
| `FLG` | Flanger | Rate, Depth, Feedbk, Mix |
| `PHA` | Phaser | Rate, Depth, Centre, Feedbk, Mix |

*Master inserts + send returns only (`masterOnly`):*
| Badge | Name | Key params |
|---|---|---|
| `RVH` | HQ Reverb | PreDly, Size, Decay, Damp, LoCut, Mod, Mix |
| `DLH` | HQ Delay | Time (tempo div), Feedbk, Color, Width, Mix |
| `BUS` | Bus Compressor | Thresh, Ratio, Atk, Rel (Auto), SC HPF, Mkup, Mix |
| `UTL` | Master Utility | Tilt, Width (M/S), Trim (dB) |

State round-trips in serializer v18 (v19 when Density ships).

**Density overlay.** While `Func` is held, the Manipulation Zone shows a transient
**Density** band: 8 rotaries for the 8 tracks in the current bank (bank follows
focused track). **`Func+FX`** pins sticky DENSITY mode; any nav key (↑↓←→)
pages between banks 1-8 and 9-16. While in the band (transient or sticky),
`Song`-held (encoder or on-screen drag) adjusts the master offset additively to all tracks;
the arc on each rotary shifts to show the offset and a tick marks the effective (audible) value.
`Func` double-tap (universal escape) or a foreign scope key to exit sticky mode.

Pressing **Track, Phrase, Scene, Morph, Mute, or Fill** while sticky exits the mode
before the scope's normal handler runs — so holding Track to pick a track then
releasing returns to Base, not back into Density. `Song` (master offset), nav keys
(bank flip), and the FX section key (Amount/Mode sub-page) remain density's own
controls and do not exit sticky mode.

Inside sticky DENSITY mode, press the **FX** section key to toggle between:
- **Amount** sub-page: per-track rotaries; Song-held (encoder or drag) = master offset.
- **Mode** sub-page: per-track Musicality (Uniform / Mixed / Metric) and Selection
  (Scrub / Re-roll) — these are durable (saved per song per track).

When sticky mode is off, `Song`-alone still opens the swing editor as normal.

Density is strictly downstream of the probability/condition system and only
silences trigs — it never re-enables a step and never touches the pattern.
Per-track amounts and master offset are **ephemeral** (reset on song/project
change; ride the scene sticky/floor launch).

| Mode | Behaviour |
|---|---|
| **Uniform** | Pure random thinning — all steps equally likely to survive. |
| **Mixed** | Blends metric weighting and random (default). |
| **Metric** | Importance-weighted using Lerdahl–Jackendoff dot-counts: downbeats stay, finest offbeats drop first. Works correctly in any time signature (3/4, 6/8, 7/8, 9/8, …). Gradual thinning — no abrupt whole-tier cliff. |
| **Scrub** | Fully deterministic, loop-stable, hash-free. Each track gets a fixed rotation offset so same-density tracks land on different steps. **Uniform**: even Euclidean spread across the whole loop length; exact same pattern every play-through. **Metric**: clean tier-by-tier Euclid thinning over the bar — downbeats outlast backbeats outlast offbeats. **Mixed**: metric core always kept, remainder filled with an even Euclidean spread. Recallable; turning the knob reshuffles. |
| **Re-roll** | Stochastic (r < p formula). Uniform: fresh roll each step firing. Mixed/Metric: one roll per step-in-bar drawn at the bar boundary, shifting bar-to-bar. Not recallable. |

**Swing by held scope.** Holding a scope key shows a single `Swing` rotary
whose value is the **cumulative groove at that scope level** — what you hear:

| Held scope | Rotary value | Tick(s) |
|---|---|---|
| **Song** (S) | Song-all (absolute root) | none |
| **Scene** (W) | Song-all + Scene-all | gold tick at the song floor |
| **Track** (T) | Song-all + Scene-all + Track (= effective) | faint gold (song) + green (scene) |

The ticks are scope-coloured radial marks showing the inherited floor from
higher layers. At track scope you can see at a glance whether the track is
pushing above or pulling below the scene floor: turn the rotary toward the
green tick to match it, or overshoot for extra push/pull.

The swing band is a **transient default**: pressing any section, verb, nav
key, or step while a scope is held collapses back to the normal machine-param
view so you can reach the sections you need. Release and re-hold the scope
key to re-open the swing display. (DIV / PHRASELEN / GLOBAL bands are sticky —
they stay open until you select a different section or change track.)
The swing band is also accessible on hardware controllers (Push1, X-Touch):
the single encoder edits the cumulative swing level.
Swing lives in musical state, not APVTS — it is not host-automatable.

<a name="59-copy-paste-clear"></a>
### 5.9 Copy / paste / clear

A single uniform grammar — **hold scope, press verb**:

| Scope held | + Copy (`U`/Rec) | + Paste (`I`/Play) | + Clear (`O`/Clear) |
|---|---|---|---|
| **Trig** (1+ steps) | Copy steps (trigs + conditions + P-Locks) | Paste onto held steps | Full clear: trig off + condition reset + all P-Locks. `Func+Clear` = P-Locks only (keep trig). `SRC+Clear` = note/velocity/gate only (keep trig + P-Locks); any other `section+Clear` = that section's P-Locks. |
| **Section** key | Copy that section's params | Paste section to current track | Reset section to default |
| **Track** (specific) | Copy whole track | Paste track | Clear track steps (keeps length/divider/base) |
| **Phrase** (`Q`) | Copy whole phrase (all tracks' steps) | Paste phrase | Clear phrase |

(The clipboard's whole-sequence type is still named `Pattern` internally;
the scope key that copies it is **Phrase**, `Q`.) Multi-step copies preserve
relative offsets. Pasting a 1-step clipboard over many held steps replicates;
pasting many over one unrolls forward. The clipboard is in-memory only and
**typed** — a step clipboard can't be pasted into a phrase scope, etc. (An
`All` omni grab, `Func+U`, can be pasted into any matching scope.)

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

A fourth per-step firing rule, the **fill rule** (`Always` / `OnlyFill` /
`NeverFill`), is set with the Fill modifier and documented separately in
[§5.12](#512-fills).

All conditions are **deterministic and pre-computable**, so the grid
shows certain-fire / certain-skip / probabilistic states ahead of the
playhead.

### 5.14 Scenes, phrases, and songs (the launch model)

Performance is **launch-based**, not arrangement-based (Phase 7). There is
no bank dimension, no pattern queue, and no written chain — you launch
Scenes and switch Songs live.

| Gesture | Action |
|---|---|
| `Scene (W) + step key` | Launch a Scene (quantized when playing). On a *different* occupied Scene: carries the live overlay. On the *active* Scene: reverts to floor. On an **empty** slot: **baked-copy create** + launch (current effective content, including deviations). Conflict-gated when target phrase row has content; no-op skip when identical. |
| `Func + Scene + step key` | On an occupied Scene: **floor launch**. On an **empty** slot: **baseline-copy create** (floor diagonal row only, no deviations) + launch. Conflict-gated. |
| `Mute + Scene + empty-step` | **Blank create** — a fresh empty Scene, no content copied. |
| `Scene + Clear` | Revert the active Scene to its saved floor (same as re-launching it). |
| `Scene + Record` | **Commit-and-bake** (confirm-gated): for each deviated track, copy its effective phrase content into the Scene's diagonal row (`sceneIdx`), then clear the deviation. If no deviations, no-op. |
| `Func + Scene + Record` | Copy the active Scene (floor + all effective phrases) to the typed clipboard. Badge: `CPY:SCN`. |
| `Func + Scene + Play` | Paste clipboard Scene onto the active Scene (baked layout). Conflict-gated. |
| `Mute + Func + Scene + Play` | Paste **floor only** (strip deviations). The `Mute` qualifier reads as "strip the content overlay; apply floor metadata only". |
| `Song (S) + Clear (O)` | **Panic** — kill all voices immediately. |
| `Phrase (Q) + step key` | **Deviate focused track** to that phrase. Same as `Track+Phrase+step`. |
| `Track + Phrase (Q) + step key` | Deviate the focused track to that phrase. |
| `Scene + Phrase (Q) + step key` | **Deviate all tracks** to that phrase. Landing on the Scene's diagonal row (row N for Scene N) clears all deviations. |
| `Song (S) + step key` | Switch Songs (quantized) — a full reset; live deviations clear. |

### 5.15 Checkpoints (live undo)

| Gesture | Action |
|---|---|
| `Yes` (`Y`, SNAP) | Push the **currently-held scope** onto its checkpoint stack. |
| `Func + Yes` (`Func + Y`, RESTORE — tap) | Pop one entry from the scoped stack (restore last snapshot). |
| `Func + Yes` (`Func + Y`, RESTORE — hold+release) | Jump straight to the floor (= the saved state at last load). |

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

<a name="517-keyboard-ui-revamp"></a>
### 5.17 Keyboard / UI revamp *(shipped — Phase 3)*

The chrome-and-grammar pass over the 10×4 surface that 3.1/3.2 froze. It
shipped across 3.3–3.10; the detail below documents the behaviour now in
the build. (The old 3.11 pattern-length authoring was absorbed into Phase 7
Stage E / 7.5 and has shipped — see *Phrase-length authoring* below.)

**3.3 — Surface chrome.**

- Larger key cells with bigger primary text; **6-character** key label
  ceiling (was 3–4); longer abbreviations like `FILTER`, `ATTACK`,
  `RETRIG`, `COPY`, `PASTE`, `CLEAR`.
- A unified **label-resolution rule**: when a modifier is held the key
  re-skins to its contextual meaning; universally-invariant secondary
  meanings (verb keys' COPY/PASTE/CLEAR under any scope) keep an
  always-on hint.
- **Scope colour grammar**: a canonical palette per scope (light grey
  for step, distinct hues for `track / phrase / scene / machine /
  morph / song`) so the surface visibly says *which scope is held*.

**3.4 — Contextual modes, top bar, MZ streamline.**

- **Step-grid scope re-skin.** Hold `Track` and the 16 step keys
  become a 1-of-16 track picker; `Phrase` → phrase picker; `Scene`
  → scene picker; `Func + Track` → machine/Kit picker showing machine
  names. **Pagination is suppressed in this mode** — only "which key
  was pressed" matters. Unavailable indices dim; cells tint with the
  scope colour.
- **Top-bar dashboard + held-context preview.** The pre-Phase 3 "mode
  chips" row is replaced by a persistent performance dashboard (BPM,
  Song / Scene / Phrase identity, transport position, pending scene,
  checkpoint depth) on the left, and a live held-context preview on
  the right (e.g. `TRACK 3 + …`, `FUNC + TRACK → machine picker`).
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
- **P-Lock clear gestures.** `Trig + Func + Clear` clears all
  P-Locks on the held step(s), trig left intact. `Trig + (active MZ
  slot) + Clear` clears only that one slot's P-Lock. Both use the
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
  (`Track`, `Phrase`, `Scene`, `Morph`, `Song`, `Mute`, `Fill` — `Func` never
  latches) latches it hands-free — exactly as if the key stayed physically held.
  Double-tap the same modifier again to release. Column exclusivity is enforced:
  at most one latch in each column ({`Phrase`, `Morph`, `Mute`} and {`Track`,
  `Scene`, `Song`, `Fill`}); latching a second key in the same column releases
  the first. **Latch is the recommended workflow for `Morph+Mute` track
  editing** — latch one of the pair and physically hold the other; keyboards
  without N-key rollover may not register step keys when both are physically
  held simultaneously.
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

**Phrase-length authoring (old 3.11 → Phase 7 / 7.5, DESIGN §34.4).**

- **Set length by step.** Hold `Phrase` (key `Q`) + `Func` and press a step: the
  focused track's length becomes that absolute, page-aware step index + 1. Hold
  `Morph` (key `A`) + `Func` instead to broadcast the same length to **all**
  tracks. While held, the grid re-skins — body cells `LengthInRun`, the last step
  `LengthBoundary`, beyond-length cells `LengthOutRun`.
- **Double/halve.** `Func + ↑` doubles the focused track's length (duplicating the
  step data into the new tail); `Func + ↓` halves it.
- **The `LEN` encoder** in the `Track+TRIG` meta-layer writes the same underlying
  per-track length — no divergence.
- **Scroll past the end.** At the last in-length page a single `NavRight` is a
  no-op; **double-tap `NavRight`** unlocks one empty page beyond the length so a
  longer length can be set out there. The unlock auto-clears once the visible page
  is back within range; the nav row reveals the empty page in its count.

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

The step grid can be re-skinned into non-step roles. Three paths exist:

- **Per-track input modes (shipped, 3.9).** `Track + NavUp/Down` cycles
  the focused track between `PLAY ↔ CHROMATIC ↔ LEVELS`. In **CHROMATIC**
  the 16 step cells become a one-octave keyboard (NavUp/Down shift the
  octave) — this is the shipped form of the old "keyboard mode." In
  **LEVELS** the cells become quantised velocity buckets.
- **Retrig / ratchet overlay (shipped, 5.7).** `Fill + TRIG` re-skins the
  step grid to an 8-rate ratchet picker (`/4`, `/4T`, `/8`, `/8T`, `/16`,
  `/16T`, `/32`, `/32T`). While the overlay is open (Fill held), pressing a
  rate cell starts a live stutter on the focused track at that rate. If a
  step is held while you press a rate, the rate is **baked** as a per-step
  P-Lock (`hasRetrig`/`retrigRate`) that fires automatically during playback.
  On slicer tracks `Fill+TRIG` shows the slice-point picker instead: each
  cell addresses a slice; pressing one auditions that slice and bakes
  the `note = sliceIdx` override onto any held steps. The TRIG key glows in
  Fill colour while Fill is held to announce the overlay.
- **Sound Pool overlay (shipped, 5.7 / 5.7c).** `Fill + SRC` re-skins the step
  grid to the project's Sound Pool (up to 16 saved sounds). Pressing a cell
  live-swaps the focused track to that sound for audition. If a step is held
  the swap is **baked** as a `sound_id` P-Lock. Release Fill to restore the
  track's original sound. The SRC key glows in Fill colour while Fill is held.

  The **Sound Bank overlay** (the "Sound Bank" button in the header) exposes the
  full pool as a list for management outside performance mode:
  - **Single-click a row** (anywhere other than the label) — recalls the sound
    onto the focused track (machine must match; a mismatch shows a status message
    instead of silently failing).
  - **Recall button** — same as single-click.
  - **Double-click the name label** — edits the name inline; press Enter to confirm.
  - **Del button** — removes the entry, remaps any `sound_id` P-Locks that
    pointed at it (decrements indices for all higher entries).
  - **Save current track** button — saves the focused track's current machine + params
    under an auto-generated name (`<Engine> T<n>`, e.g. `Va T1`), uniquified if
    a clash exists.
  - The Sound Pool is now **fully serialized** (v16). Saved sounds survive save/reload
    and DAW session round-trips. (Prior to v16, the pool was ephemeral.)
- **Euclidean generator (shipped, 5.9).** `Phrase + Fill` held together
  enters Euclidean generator mode on the **focused track**. The Manipulation
  Zone switches to three encoders:
  - **PULSE** — number of onsets (0 … phrase length).
  - **OFSET** — rotation in steps (signed, shifts the pattern forward/back).
  - **ACCNT** — number of accented onsets (Euclidean-distributed over pulses;
    accented trigs get velocity 100, unaccented get velocity 64).

  Once armed, the step grid shows the live Euclidean pattern (not the
  phrase-select banner), and encoders continue to update the preview even
  after releasing the chord keys (the mode is latched until committed or
  cancelled). The status bar reads **EUCLID  pulses / offset / accent  |
  P = commit  Func+P = cancel**.

  **Commit (bare P):** saves the original phrase as an undo checkpoint
  (always, even if the phrase was empty), then bakes the live pattern in
  place. **Cancel (Func+P):** restores the original phrase. **Y is inert**
  in this mode. Output is ordinary hand-editable trig data.

- **Accent velocity generator (shipped, §39.10).** `Func + Fill` held together
  enters accent-print mode on the **focused track**. The Manipulation Zone
  switches to two encoders:
  - **DEPTH** — swing depth (0–100 %). At 0 every trig gets exactly Center.
  - **CENTR** — center velocity (1–127; default 90).

  Velocities are computed as:
  `vel = Center + Depth × range × (2w − 1)`, where `w` is the Lerdahl–Jackendoff
  metric weight [0,1] of the step's position in the bar (`w=1` = downbeat,
  `w=0` = finest offbeat). Works correctly in any time signature.
  Only trig steps are written; rest steps are untouched.

  Live preview fires on every encoder change. The status bar reads
  **ACCENT  depth / center  |  P = commit  Func+P = cancel**.

  **Commit (bare P):** checkpoint then bakes velocities. **Cancel (Func+P or
  Func double-tap):** restores the original phrase.

### 5.19 Standalone project files *(shipped — Phase 9)*

In standalone mode, a **File bar** appears below the tempo bar with four
buttons: **New**, **Open**, **Save**, and **Save As…**

- **File format:** `.lockstep` files are plain UTF-8 XML — human-readable and
  git-diffable. They use the same versioned serializer as DAW session state
  (currently v18), so the full upgrade chain applies on load.
- **New** — resets the project to the pristine default (one sampler track, no
  samples, no P-Locks). If the current project has unsaved changes a
  three-way **Save / Discard / Cancel** dialog appears first.
- **Open** — shows an OS file picker filtered to `*.lockstep`. Same dirty guard
  as New.
- **Save** — writes to the current file directly; falls through to Save As if
  no file is open yet.
- **Save As…** — shows a save-mode OS file picker; adds `.lockstep` extension
  automatically if omitted.
- **Project name** — displayed to the right of the buttons; shows the file stem
  (without extension) or `(unsaved)` when no file is open.
- **Last-project persistence** — the last opened or saved file is stored in the
  application preferences. On the next launch the file is automatically loaded,
  giving a "reopen-last" workflow with no extra steps.
- **Quit guard** — the standalone wrapper saves its own session on quit, so no
  data is lost across a clean restart. A dedicated quit-confirmation dialog
  (requiring a custom standalone app) is deferred to a future phase.

### 5.20 Performance capture *(shipped — Phase 8.26)*

`Func + Song + U` (Func + Song + Rec) arms or disarms a live recording of
the master output to a 32-bit-float WAV file.

- **Tap point:** post master gain, DC blocker, and soft-clip — exactly
  what appears at the physical outputs. Capture runs continuously across
  transport stop and start; a performance recording has no gaps.
- **File location:** `Captures/capture-YYYYMMDD-HHMMSS.wav` next to the
  current project file (standalone) or in `~/Music/Lockstep/Captures/`
  (plugin / no project open). The directory is created automatically.
- **Status:** arm → `REC capture-….wav`; disarm → `Captured m:ss ->
  filename`. A failed arm (unwritable directory) shows an error in the
  status band — the audio thread never panics.
- **Format:** 32-bit float WAV, stereo, device sample rate. Open in any
  audio editor; Reaper/Audacity/DAWs read 32-bit float WAV natively.
- **Stem export** is not yet available (one file per track + master); the
  recorder is architecturally N-stream-shaped for a future milestone.

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
  SVF + LFO, Mono/Para-4), `DrumSynthMachine` (Rytm-style, eight voices —
  KICK/SNARE/HAT/TOM/CLAP/COWBELL/CYMBAL/RIMSHOT, each with dedicated DSP),
  plus the `StubMachine` fallback. Shared post-machine FILTER (SVF) + AMP.
- The **surface-model foundation** for external controllers (6.6.5a):
  one pure `buildSurfaceModel()` the screen renders from.
- **Microtiming, Swing & Quantize** (`5.1`): per-step `microOffset` (±50% of step
  length, P-lockable via TRIG meta section); hierarchical additive swing (song-all +
  song-track + scene-all, ±50%, `effectiveSwing = clamp(sum, ±50%)`); morph-style
  qualifier editing (hold Song or Scene while TRACK meta is open to retarget the
  Swing encoder); sample-accurate look-ahead scheduler; live-record residual capture;
  `Quantize` verb (`scope + Yes` zeros microOffset — see §5.3); amber/cyan step-grid nudge ticks;
  TRACK band effective-swing readout.

- **Retrig / ratchet overlay** (`Fill+TRIG` rate picker + per-step bake) and
  **Sound Pool overlay** (`Fill+SRC` live-swap + `sound_id` P-Lock bake), plus
  the **Slice-point picker** on slicer tracks (`Fill+TRIG`).
- **Sound Bank** (5.7c): the Sound Bank overlay provides full management of the
  project sound pool — save, recall (with machine-mismatch guard), delete (with
  `sound_id` P-Lock remap), inline rename, and auto-naming. Pool is serialized
  at v16 and survives save/reload.
- **Standalone project files** (9.1): `.lockstep` XML files, shared serializer +
  upgrade chain, New/Open/Save/Save As with dirty guard, last-project auto-open.
- **Per-track FX inserts** (2 slots, `Func+FX` picker, `FX+step` animate-bypass).
- **Master FX bus** (2 post-sum slots, `Func+Song+FX` picker, MZ params under `Song+FX`, serializer v14).
- **Density overlay** (`Func`-held → transient per-track Density band; `Func+FX` = sticky DENSITY mode; nav = bank 1-8/9-16; `Song`-held = master offset (encoder or drag); FX key = Amount/Mode/Selection sub-page; section key 0-4 or foreign scope = exit sticky mode; `Song`-alone = swing).

**Planned** — the rest of the
machine catalogue (`4.5` Static, `4.6` Percussion, `4.7` Digital); **Phase 5**
performance depth (scenes + crossfader `5.2`; pattern/part management
UI `5.3`; sampling + resampling `5.4`; audition + cross-track record `5.5`;
special trig types `5.6`; UI polish + state-colour palette `5.8`); and **Phase 6**
routing, FX & platform (audio-input boundary + Thru `6.1`; recorder buffers `6.2`;
looper `6.3`; cue bus `6.4`; master FX bus `6.5b`; external
controller surfaces `6.6`, in progress; the Machine Module ABI `6.7`;
beta polish `6.8`).

See `ROADMAP.md` for the authoritative milestone breakdown and current
status — it is the single source of truth for what ships when.

---

<a name="7-gesture-tree"></a>
## 7. Gesture tree (every action, by press order)

This appendix indexes **every live gesture** the surface interprets,
organised the way your hands actually move. It is the *by-press-order*
view of the grammar; `DESIGN.md` §13.0 is the **same set ordered by
gesture cost** (how many keys a chord holds). When a gesture is added or
changed, update both.

### How to read a chord

You **hold a chord of scope keys, then strike one operand last.** The
operand is the only key you "press and release" to fire the action — a
verb, a step, a nav arrow, or an encoder turn. Everything to its left is
held down while you strike it.

Chords are written in **press order**, left to right:

```
[ primary scope ] [ secondary scope ] [ Func ] → operand
```

- The **primary scope** is the one that most directly names what you're
  operating on; a **secondary scope** (always from the *other* cluster
  column) narrows it further. Two scopes from the *same* column never
  combine.
- **`Func`** is the universal qualifier — it flips a chord to its
  secondary meaning. It is written last among the held keys (right before
  the operand), matching how it reads as "…but the Func variant." (Note:
  DESIGN §13.0 writes `Func` *first* for its cost-ladder bookkeeping; same
  chord, different reading convention.)
- The **operand** is struck last: a verb (`Y U I O P`), a step, a nav
  arrow (`4 E R T`), or an encoder turn.

**A step key plays two roles.** When you *hold* it first it is a **scope**
(`Trig`) — e.g. `step + U` copies that step. When you *strike* it last it
is an **operand/target** — e.g. `Scene + step` launches that step's scene.
So "step last" is only true when the step is the target.

The tree below roots each gesture under its **highest-priority held
scope** (the resolution order is `Trig > Section > Track > Phrase > Scene >
Mute > Morph > Song > Fill > Func`); `Func`-only gestures live under
**Func**. `Cue` is a reserved scope with no key bound yet (6.4), and
`Morph` is a fully live scope (5.2) with encoder, nav-qualifier, Clear,
and Mute gestures; `Cue` is reserved with no key bound yet (6.4).

Legends in parentheses are the on-screen key labels (see
[§5.3](#53-verb-keys)).

### No scope — strike a key alone

```
(nothing held)
├─ step (tap)        → toggle a trig on the focused track — §5.6
├─ step (double-tap) → latch the step into the edit context (hands-free) — §5.17
├─ Y (SNAP)          → push a checkpoint on the held scope (Song if none) — §5.15
├─ U (REC)           → toggle record-arm; double-tap = overdub — §5.4
├─ I (PLAY)          → play / stop transport; double-tap = stop-to-top — §5.4
├─ O (CLEAR)         → clear the active P-Lock slot — §5.4
├─ P (CONFIRM)       → confirm a pending prompt — §5.3
├─ 3 (TAP)           → tap tempo — §5.4
└─ 4 / E / R / T     → navigate up / left / down / right (track + step page) — §5.4
```

When the focused track is in **CHROMATIC** or **LEVELS** input mode, the
step keys instead play notes / set velocity buckets — see
[§5.18](#518-modal-trig-grid-surfaces). The **mini-sequencer timeline** in the
nav row remains visible in all three modes; the mode badge in the top context
band identifies the active mode.

Links: [§5.6](#56-step-editing) · [§5.17](#517-keyboard-ui-revamp) ·
[§5.15](#515-checkpoints-live-undo) · [§5.4](#54-transport-and-navigation) ·
[§5.3](#53-verb-keys)

### Func — the qualifier, held alone

```
Func (1)
├─ Func + Y (RESTORE) → restore checkpoint: tap = pop one, hold = jump to floor — §5.15
├─ Func + U           → omni copy (scene + track + phrase; badge CPY:ALL) — §5.4
├─ Func + I           → unqualified paste (stamp the one captured layer) — §5.4
├─ Func + O           → deletion picker (bare Func+O: inert; needs a scope) — §5.4a
├─ Func + P           → cancel a pending prompt — §5.3
├─ Func + 3           → toggle the metronome — §5.4
├─ Func + 5…0         → secondary section page (machine deep params; COND/NOTE meta; Func+7 = transport globals) — §5.8
│   ├─ Func + FX (0)          → effect picker: step grid re-skins to effect catalogue; press step to load; re-pick active = toggle bypass — §5.8
│   └─ Func + Song + FX (0)  → master FX picker (same catalogue; loads into Song-scope master unit; re-press to cycle all 4 slots; re-pick active = toggle bypass) — §5.8
├─ Func + ← / →       → rotate the focused track's steps −1 / +1 — §5.17
├─ Func + ↑ / ↓       → double / halve the focused track length — §5.17
├─ Func + step        → P-Lock clear mode (cells show set P-Locks; stage removals, release to commit) — §5.17
├─ Func (hold)        → transient Density band (8 tracks, bank follows focus); Song-held = master offset (encoder or drag) — §39
└─ Func double-tap    → sticky DENSITY mode: nav = bank 1-8/9-16; Song-held = master (encoder or drag); FX key = Amount/Mode — §39
```

Links: [§5.15](#515-checkpoints-live-undo) ·
[§5.4](#54-transport-and-navigation) · [§5.3](#53-verb-keys) ·
[§5.8](#58-sections-and-the-manipulation-zone) ·
[§5.17](#517-keyboard-ui-revamp)

### Trig — one or more held steps

```
step(s) held
├─ + U               → copy held step(s) (trigs + conditions + P-Locks) — §5.9
├─ + I               → paste clipboard onto held step(s) — §5.9
├─ + O               → clear held step(s) (full: trig + condition + P-Locks) — §5.9
├─ + Func + O        → clear all P-Locks on held step(s), keep the trig — §5.17
├─ + (MZ slot) + O   → clear only that one slot's P-Lock on held step(s) — §5.17
├─ + (section) + O   → clear that section's overrides on held step(s); SRC+O = clear notes/velocity/gate — §5.17
├─ + P (QUANT)       → Quantize: zero microOffset on held step(s) — §5.1
├─ + section key     → edit that section's field as a step override (P-Lock / trig override) — §5.7
└─ + encoder turn    → write a P-Lock on the held step(s) — §5.7
```

Links: [§5.9](#59-copy-paste-clear) · [§5.17](#517-keyboard-ui-revamp) ·
[§5.7](#57-parameter-editing-p-locks)

### Section — a held section key (5–0: TRIG/SRC/FILTER/AMP/MOD/FX)

```
section (5–0)
├─ (tap)             → select / page that section's params into the MZ (8 at a time) — §5.8
├─ + U               → copy that section's params (all steps) — §5.9
├─ + I               → paste that section onto the current track — §5.9
├─ + O               → reset that section to default — §5.9
├─ + step            → animate bypass (FX section only: bypasses insert slot 0–7/8–15; restores on release) — §5.8
├─ Track + section   → track-foundation row (post-machine FILTER/AMP, inserts) — §5.8
├─ Func + section    → the machine's secondary page / meta layer — §5.8
├─ Func + FX (0)     → effect picker (step grid re-skins to catalogue; press step to load) — §5.8
└─ Func + SRC + step → note-edit mode (1-octave chromatic overlay on the step grid) — §5.17
```

(Section contents vary by machine — see the catalogue and value tables in
[§5.5](#55-track-selection-and-focus).)

Links: [§5.8](#58-sections-and-the-manipulation-zone) ·
[§5.9](#59-copy-paste-clear) · [§5.17](#517-keyboard-ui-revamp)

### Track (2)

```
Track (2)
├─ + step (D–; / C–/) → select & focus track 1–8 / 9–16 — §5.5
├─ + step (empty trk) → clone the active track's machine + params there, then focus it — §5.5
├─ + param edit (no track selected) → Control-All: broadcast the edit to every matching track — §5.10
├─ + ↑ / ↓ (no track selected) → cycle the focused track's input mode PLAY ↔ CHROMATIC ↔ LEVELS — §5.17
├─ + track-key + Nav → set the input mode on that specific track — §5.18
├─ + U / I / O       → copy / paste / clear the whole track — §5.9
├─ + Func + O        → deletion picker: step grid shows tracks; tap to choose → named confirm (P=CONFIRM, Func+P=CANCEL) — §5.4a
├─ + P (QUANT)       → Quantize: zero microOffset on every step of the track — §5.1
├─ + Scene           → re-sync the focused track to the active scene — §5.14
├─ + TRIG → kit divider (DIV meta) — §5.8
├─ + (held) → shows song-track delta swing in band (SwTrk + (D)) — §5.8
└─ Func + Track      → machine / Kit picker (Track→KIT; press a step to assign) — §5.5
```

Links: [§5.5](#55-track-selection-and-focus) ·
[§5.10](#510-control-all) · [§5.17](#517-keyboard-ui-revamp) ·
[§5.18](#518-modal-trig-grid-surfaces) · [§5.9](#59-copy-paste-clear) ·
[§5.14](#514-scenes-phrases-and-songs-the-launch-model)

### Phrase (Q)

```
Phrase (Q)
├─ + step            → deviate focused track to that phrase (same as Track+Phrase+step) — §5.14
├─ Track + Phrase + step → deviate the focused track to that phrase — §5.14
├─ Scene + Phrase + step → deviate all tracks; diagonal row = clear all deviations — §5.14
├─ + U / I / O       → copy / paste / clear the whole phrase (all tracks) — §5.9
├─ + Func + O        → deletion picker: step grid shows phrase slots on the focused track; tap to choose → named confirm — §5.4a
├─ + P (QUANT)       → Quantize: zero microOffset across every step on every track — §5.1
├─ + O (queued scene pending) → cancel the queued scene — §5.14
└─ + Fill (X, held together) → Euclidean generator on the focused track:
                       encoders = PULSE / OFSET / ACCNT; release prints the rhythm — §5.18
```

Links: [§5.14](#514-scenes-phrases-and-songs-the-launch-model) ·
[§5.9](#59-copy-paste-clear)

### Scene (W)

```
Scene (W)
├─ + step (occupied)        → launch (carries the live overlay); double-tap = floor launch — §5.14
├─ + step (active scene)    → revert to the saved floor — §5.14
├─ + step (empty slot)      → baked-copy create + launch (conflict-gated) — §5.14
├─ Func + Scene + step (occupied) → floor launch (arrive at saved floor) — §5.14
├─ Func + Scene + step (empty)    → default-create (blank) + launch — §5.14
├─ + U (REC)                → **commit-and-bake** deviations (confirm-gated) — §5.14
├─ + O (CLEAR)              → revert the active scene to its floor / cancel a queued scene — §5.14
├─ + Y (SNAP)               → re-sync all tracks to the active scene — §5.14
├─ Func + Scene + U         → copy the active scene to the clipboard (CPY:SCN) — §5.14
├─ Func + Scene + I         → paste the clipboard scene (baked; conflict-gated) — §5.14
├─ Func + Scene + O         → deletion picker: step grid shows scene slots; tap to choose → named confirm — §5.4a
├─ Mute + Func + Scene + I  → paste floor only (strip the content overlay) — §5.14
├─ + (held)                 → shows scene-all delta swing in band (SwScn + (D)) — §5.8
└─ Scene + Mute + step      → per-scene mute (this track's active-mask) — §5.11
```

Links: [§5.14](#514-scenes-phrases-and-songs-the-launch-model) ·
[§5.11](#511-mutes)

### Song (S)

```
Song (S)
├─ + step            → switch Songs (quantized; a full reset, live deviations clear) — §5.14
├─ + O (CLEAR)       → Panic — kill all voices immediately — §5.14
├─ + (held) → shows song-all swing in band (Swing, absolute root) — §5.8
├─ + FX (0)          → master FX params in MZ (FX cell, dim "PICK FX" hint); re-press = cycle loaded master units — §5.8
├─ Func + Song + FX (0) → master FX picker (catalogue overlay; re-press cycles all 4 units; re-pick active = toggle bypass) — §5.8
└─ Func + Song       → Global / master-bus focus — §5.2
```

Links: [§5.14](#514-scenes-phrases-and-songs-the-launch-model) ·
[§5.2](#52-modifier-scope-keys)

### Morph (A) — *implemented (5.2)*

```
Morph (A)   hold/latch = scene-layer selector (symmetric with held step → P-Lock)
├─ + encoder            → sculpt morph at fader split (da/db normalised 1:1) — §5.2
├─ + ↑ + encoder        → pure A write (fader ignored) — §5.2
├─ + ↓ + encoder        → pure B write (fader ignored) — §5.2
├─ + Clear on slot       → clear slot from both maps — §5.2
├─ + Mute on track       → fluid mute (AMP Level → silence into near pole) — §5.2
├─ + O (CLEAR)           → **BAKE** — commit fader-split to both poles; hint "ERASE" — §5.2
└─ Func + Morph + O      → **ERASE** — wipe both Morph maps for this scene — §5.2
```

Links: [§5.2](#52-modifier-scope-keys)

### Mute (Z)

```
Mute (Z)
├─ + step            → toggle global mute on that track (hold and tap several) — §5.11
├─ Scene + Mute + step → toggle scene mute (active-mask for this scene) — §5.11
└─ Func + Mute + step  → solo that track (additive) — §5.11
```

Links: [§5.11](#511-mutes)

### Fill (X)

```
Fill (X)
├─ (hold)            → while held, fill-conditioned steps fire; TRIG and SRC keys glow — §5.12
├─ + step            → mark that step fill-only — §5.12
├─ + TRIG (5)        → Retrig overlay: step grid → ratchet-rate picker (/4…/32T) — §5.18
│   └─ press rate    → start live stutter at that rate; hold a step first to bake per-step P-Lock
├─ + TRIG (5) on slicer track → Slice-point picker: step cells = slice indices — §5.18
├─ + SRC (6)         → Sound Pool overlay: step grid → saved-sound selector — §5.18
│   └─ press sound   → live-swap track to that sound; hold a step first to bake sound_id P-Lock
└─ Func + Fill       → Accent velocity generator on the focused track (§39.10):
                       encoders = DEPTH (0-100%) / CENTR (1-127); live preview; P = commit, Func+P = cancel
```

Links: [§5.12](#512-fills)

### Encoders — turn a control (any source)

```
encoder
├─ turn (no step held)  → edit the track base parameter — §5.7
├─ turn (step held)     → write / update a P-Lock on the held step — §5.7
├─ reset (double-click) → restore the slot default (or clear the P-Lock if a step is held) — §5.7
├─ tempo encoder        → ± BPM — §5.16
└─ master encoder       → ± output gain — §5.16
```

The same edit-context rule governs encoders, mapped MIDI CCs, and QWERTY
edits alike ([§5.7](#57-parameter-editing-p-locks),
[§5.16](#516-midi-input)).

> **Keeping this in sync.** This tree and `DESIGN.md` §13.0 are two views
> of one gesture set (press-order vs. cost-rung). A new or changed gesture
> must be reflected in both, and any new step-grid appearance must be a
> `CellState` token (DESIGN §35.8), never ad-hoc paint.
```
