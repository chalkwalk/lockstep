# Lockstep — Non-Goals

> This file is the long form of the *Non-Goals* summary in `PRINCIPLES.md`.
> It exists because a strong instrument is defined as much by what it refuses
> as by what it does. Each fence below names the **groovebox feature** that
> prompted it (Lockstep's lineage is Elektron, but the whole field was
> surveyed), the **principle that rejects it**, and — crucially — the
> **performable alternative** Lockstep offers in its place. A non-goal is not
> a gap; it is a decision.
>
> Read this alongside `PRINCIPLES.md`. When a feature request arrives, check
> here first: if it matches a fence, the answer is already written, and the
> reply is the alternative in the right-hand column, not "no."

## The two fences

Most rejections trace to one of two costs, captured by the principle
*"Reward mastery — no crutches, no dead weight"*:

- **Too easy — a crutch.** The feature does the musical work *for* the user,
  lowering the skill floor without raising the ceiling. Lockstep is an
  instrument you learn; a feature that removes the learning removes the point.
- **Too heavy — dead weight.** The feature's cognitive cost is never repaid in
  live performance. Every gesture is a tax on mastery; the surface budget is
  finite.

The older principles (determinism, the fixed surface, the bespoke ABI, state
refs) draw the rest of the fences. The test is always constructive: we reject
the *form*, then offer the performable version of the same desire.

## The catalogue

| # | Non-goal | Prompted by | Rejected by | What we offer instead |
|---|---|---|---|---|
| 1 | Song / arrangement / linear chaining | Akai MPC, Sonicware Step-Lego, Dirtywave M8 `HOP`, Cirklon branch | *Performance is the goal*; "No song arrangement" (DESIGN §16) | Scenes and Songs **launched live**; the set order is performed, not stored. (The Phase-7 **Song** *entity* is not this fence's target: it is a launchable container — Kits + Phrase pools + Scenes — not a stored linear arrangement or chain.) |
| 2 | Stochastic / generative note & pattern engines | Oxi One stochastic, Polyend Play smart genre-fills, Torso T-1 generative voicing, OP-Z `random`/`spark` | *Pragmatic determinism*; *Reward mastery* (crutch) | **Deterministic** Euclidean that prints ordinary trigs; trig conditions (probability, m:n) as the one sanctioned RNG |
| 3 | Un-clocked / "organic" / quantize-off timing | Soma Ornament-8, Korg Volca `Flux` | *Pragmatic determinism* | Per-step microtiming + swing (ROADMAP 5.1), all clocked and repeatable |
| 4 | Tracker command-column / hex-FX paradigm | Dirtywave M8, Polyend Tracker | *The grid is the menu* | P-locks + special trig types — the grid is a picker, not a typed command language |
| 5 | Unbounded / scrolling canvas | Synthstrom Deluge | *The grid is the menu*; *Hardware = fewer-key QWERTY* | A fixed, memorisable 10×4 surface with paging; muscle memory settles |
| 6 | Control axis the hardware can't honestly provide (MPE / pressure / tilt / per-track faders) | Ableton Push 3, TE EP-133, Roland Aira D-Motion, Roland MC-707 channel strips | *Hardware = fewer-key QWERTY* | Eight encoders + the single crossfader; no axis a typing keyboard can't stand in for |
| 7 | Foreign-plugin / standalone-host ecosystem | Ableton Push 3, Akai Force | "No sub-host" (DESIGN §2, §36) | A bespoke, in-process machine module ABI — trusted native modules, no IPC, no sandbox |
| 8 | Destructive tape workflow | Teenage Engineering OP-1 | *State refs, not contents* | Non-destructive P-locks / overrides; Recorder + Looper for live audio (refs, never baked-in PCM) |
| 9 | Companion app / external editor as the primary surface | Yamaha Seqtrak | One *Surface model* (DESIGN §35.8) | A single `buildSurfaceModel()` both screen and controllers render from; the host's own window — standalone or DAW — is the screen |
| 10 | Dual-project concurrent playback | Squarp Hapax | *Performance is the goal* | One set, performed; transitions are Scene/Song launches, not a second project |
| 11 | Heavyweight performance-FX **mode** | Polyend Play FX grid, OP-Z punch-in, Roland MC-707 Scatter, Sonicware stutter | *Reward mastery* (dead weight); "No design/perform split" | A **thin "Animate" toggle** (Novation-Peak style) that momentarily bypasses/enables the existing inserts — power without a mode. Shipped (6.5): hold `FX` + step. |
| 12 | Custom-LFO designer / free automation lanes | Octatrack LFO designer, Korg Electribe motion, Torso CC loops | *Reward mastery* (dead weight) | P-locks (stepped) + Morph (interpolated); rich modulation lives **inside a machine**, not in a canonical section |
| 13 | Note auto-correct ("no wrong notes") | Novation Circuit, Korg Electribe touch-scale, Torso tonal constraints | *Reward mastery* (crutch) | A scale-aware CHROMATIC **layout** — frets that let you move faster, not a net that catches wrong notes |
| 14 | Reserved-gesture overload: (a) adding a new meaning to modifier double-tap (latch), `Func` double-tap (universal escape), step double-tap (edit-context entry), verb double-press (amplified action), or any double-tap on a section key; or (b) assigning a duration (long-press) meaning to a modifier or `Func` — keys held for chords have no free "long" variant | Density sticky (`Func` double-tap toggle) and vel-sticky (`AMP` double-tap entry) design drafts pushed this to an explicit fence; the two-axis grammar audit (9.3) added the duration fence | *Reserved gestures are fences, not conventions* (PRINCIPLES §17); DESIGN §13.7 | New section-key gestures use `Func + section` (rung 2) or long-press; new modifier behaviours join an existing double-tap family explicitly or revise PRINCIPLES §17 first; hold-intensification targets verbs/operands, never modifiers |

## Notes on the close calls

A few of these are valuable precisely *because* they are the clean
philosophical opposite of Lockstep, and they make good "we are deliberately
not this" anchors:

- **Soma Ornament-8 (#3)** — its un-clocked, behavioural timing is the
  antithesis of *Pragmatic determinism*. We admire it; we are not it.
- **OP-1 tape (#8)** — a destructive, linear tape metaphor is the opposite of
  a non-destructive, reference-based state model.

One fence is narrower than it first reads, and the boundary is worth stating:

- **Faders / extra axes on a controller you already own (#6)** — fence #6
  rejects *designing the grammar around* an axis the eventual hardware can't
  honestly provide (MPE, per-pad pressure, tilt, a wall of per-track faders).
  It does **not** reject *supporting* a generic controller that happens to
  **offer** such axes. Mapping the encoders, transport, and the single
  crossfader onto an X-Touch-class box (an augmentation surface — DESIGN §35)
  is exactly the intended near-term input story; the fence bites only when a
  gesture comes to *require* an axis a typing keyboard can't stand in for. Use
  the fader a performer already has; never make the grammar depend on it.

One shipped feature sits **knowingly close to a fence** and is flagged as a
live tension, not blessed:

- **Retrig / ratchet live stutter (#11)** — the shipped `Fill+TRIG` overlay
  (5.7) live-stutters the focused track while a rate cell is pressed.
  Fence #11 names "Sonicware stutter" among its prompts. The shipped form has
  the mitigating properties — momentary (the hold is the mode), deterministic,
  and it bakes to ordinary per-step P-Locks — but whether a held live stutter
  is grammar or a performance-FX move in disguise is **under review**: the
  retrig model redesign is an explicitly deferred item in ROADMAP 5.9. Do not
  cite the current stutter as precedent for further punch-in-style features.

And three desires were *granted* in a reshaped form rather than fenced — they
are **not** non-goals, listed here only to forestall confusion:

- **Euclidean rhythm** (Deluge, Torso, Squarp) is **in** and shipped (5.9):
  hold `Phrase+Fill`, shape `PULSE / OFSET / ACCNT` on the encoders, release
  to print — it replaces the trigs in the phrase length with ordinary,
  hand-editable trig data (deterministic; DESIGN §13.5).
- **A "Chance" macro** (Elektron Model) was granted in reshaped form and has
  since been **superseded by Density** (DESIGN §39, ROADMAP 5.9). While `Func`
  is held the MZ becomes the per-track **Density** band — a live, subtractive
  overlay that thins *would-fire* trigs strictly downstream of the
  probability/condition system. It silences, never re-enables, and never
  touches the trig data itself, so it adds no new randomness (it is a
  deterministic generator under *Pragmatic determinism*). The earlier
  probability-scaling Chance band is retired.
- **Granular synthesis** (Roland Aira P-6, and our own Tonverk lineage) is
  **in**, as a machine module (DESIGN §29), not catalogue bloat.

When in doubt, the question is never "do other boxes have it?" — it is "does
it reward practice, and can a performer reach it inside scope + verb?"
