---
sidebar_position: 10
title: "The paradigm"
---

# The paradigm

If you understand five ideas, you can predict how almost everything in
Lockstep behaves.

### 2.1 Scope + verb

Every editing and performance action is built from two halves:

- A **scope** key declares *what you're about to operate on*. Scopes are
  held modifiers: the eight-key left cluster (`Func`, `Track`, `Phrase`,
  `Scene`, `Morph`, `Song`, `Mute`, `Fill` — 7.9 naming), plus a **held
  step** (`Trig`) and a **section** key. Two modifiers — one from each
  column — can be held together to combine scopes (the *compound chord*).
  (`Cue` is the audition scope, entered as the compound `Func+3`: hold it to
  pre-listen the focused track, `Cue+step` to audition a step — writes nothing.)
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
  Chosen via `Track + hold(SRC)`; the sound itself is the operand of the
  **Machine** scope (`Func+Track`). (The dissolved Octatrack "Part".)
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

- **Machine** — the sound engine a track hosts (sampler, FM, Analog, drum
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
- **Scope overlay / colour** — section params resolve through an ordered
  layer stack (machine → track → …). Unqualified, each key shows the
  machine's page, falling through to the track page where the machine
  owns nothing; **hold `Track`** to peel the machine layer and see the
  track pages only (machine-only sections go dim). Section keys **and**
  the MZ are coloured by the scope a page comes from — machine pages read
  neutral, track pages read **cyan** — so you always know which layer you
  are editing.
- **Inserts** — two per-track `IEffect` slots after the foundation.
  Loaded via the `hold FX` picker (or `Song + hold FX` for master), edited on the FX section,
  momentarily bypassed with the Animate gesture (`FX` + step).
  MIDI-out tracks have none (no audio).
- **Send buses** — post-insert taps from each track. Set Send A / B
  levels on AMP page 2. Send returns are processed before master inserts.
- **Master FX** — four Song-scope units (2 inserts + 2 send returns)
  over the summed output. Loaded via `Func+Song+FX`; cycle with
  repeated press (FX1→FX2→Snd A→Snd B). Master-only effects are
  **compacted out** of the track picker (they simply don't appear —
  no greyed gaps), and the `External` send only appears on the two
  send units. When a catalogue spans more than one page of 16 cells,
  the picker pages with **Nav ←/→** (a `P1/2` indicator shows in the
  picker's status line).
- **Output routing & buses** — each track's finished signal has **one**
  destination, the **Out** slot in the CHANNEL block: `Master` (default),
  `Track N`, `Aux N`, or `Off`. Out is *single-destination* by construction
  (out-degree ≤ 1) — choosing a destination **replaces** the previous one;
  a track is never on Master *and* an aux at once. Routing a track to
  `Track N` removes it from the
  master sum and feeds it into track *N*; if *N* hosts a **Route** machine
  it becomes an **aux/sub-bus** that reads the sum of everything routed
  into it, processes it through its own FILTER/AMP/FX, and sends *that*
  onward. `Aux N` sends the track to one of the plugin's **host Aux output
  buses** instead of the master sum — the DAW mixes it (the exclusive tap
  is deliberate: the host, not Lockstep, decides how the aux returns). A
  track on an Aux whose host bus is disabled (e.g. standalone, or the DAW
  left the bus unconnected) **folds back to Master** so no audio is lost.
  Ordering is solved automatically (per-block topological sort);
  a routing that would form a feedback loop is refused. This is how you
  build drum buses, parallel chains, and resampling. The **Out** rotary
  only steps through valid destinations (Off / Master / current buses) and
  shows the target live as you turn — you never jog through unusable
  tracks. On the track/VU row, a bus and its feeders share a colour so
  groups read at a glance. An audio track's button fills with its output
  **VU**; a **MIDI-out track** instead fills with a *magenta velocity meter*
  (note-ons add a velocity-proportional loudness that decays like audio), and
  its top-right dot flashes on **CC** activity (the left cyan dot is the trig
  pulse). **Solo is routing-aware:** soloing a bus keeps
  its feeders audible (you hear what flows in), and soloing a feeder keeps
  its downstream bus chain audible (so it still reaches master).
- **Audio input** — a machine can *consume* audio instead of synthesising
  it, via an `input_source` tap: `None`, `Ext1`–`Ext4` (four stereo
  plugin/device input buses), or `Master` (the prior block's master sum,
  for whole-mix resampling). Only the first input bus (`In`) is enabled by
  default; enable `In 2`–`In 4` in the host to feed `Ext2`–`Ext4`. The
  **Route** machine is the pure router — it adds nothing,
  just passes its input (an external bus, or the bus sum routed to it) through
  the canonical FILTER/AMP/FX. A fresh Route defaults to `None`, so it is
  a silent sub-bus until you route audio in or pick a source. The source
  rotary omits any selection that would feed back (e.g. `Master` on a
  track whose own output reaches master, or a cyclic `Track` tap), exactly
  as the `Out` routing rotary omits cyclic destinations — and a stale
  selection loaded from disk is muted at run time as a second safety net.
  The `Out` rotary also offers only destinations that **accept inbound
  audio**. A track qualifies as a destination when its machine exposes an
  input source (and is not MIDI-out) — Route, Loop, Tape, Record and the
  other capture machines do; an ordinary synth track does not, and so never
  appears in another track's `Out`. That is why grouping a kit onto a
  sub-bus begins by giving the bus a **Route** machine: you are not choosing
  a mixer channel, you are pointing at something built to be fed. `Off` and
  `Master` are always offered; the **Aux** buses appear only when the host
  has enabled them, so in standalone there are none.

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
