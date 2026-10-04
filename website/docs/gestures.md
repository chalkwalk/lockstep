---
sidebar_position: 50
title: "Gesture tree"
---

# Gesture tree

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
**Func**. `Cue` is the audition + cue-balance scope (`Func+3`; audition,
`Cue+Mute` balance toggle, `Cue+hold(AMP)` console — 6.4), and
`Morph` is a fully live scope (5.2) with encoder, nav-qualifier, Clear,
and Mute gestures.

Legends in parentheses are the on-screen key labels (see
[§5.3](#53-verb-keys)).

### No scope — strike a key alone

```
(nothing held)
├─ step (tap)        → toggle a trig on the focused track — §5.6
├─ step (hold)       → multi-step edit context (hold several; relative P-Locks); +Func latches — §5.17
├─ step (long-press) → open that step's P-Lock inspector (single held step only) — §5.17
├─ Y (SNAP)          → push a checkpoint on the held scope (Song if none) — §5.15
├─ U (REC)           → toggle record-arm; double-tap = overdub — §5.4
├─ I (PLAY)          → play / stop transport; double-tap = stop-to-top — §5.4
├─ O (CLEAR)         → clear the active P-Lock slot — §5.4
├─ P (CONFIRM)       → confirm a pending prompt — §5.3
├─ 3 (TAP)           → tap tempo (short tap) — §5.4
│   └─ 3 (hold ≥350ms) → generator hub: step cells = EUCLID / DENSITY / VEL; press to enter; release closes picker — §5.4
└─ 4 / E / R / T     → navigate up / left / down / right (track + step page) — §5.4
```

When the focused track is in **CHROMATIC** or **LEVELS** input mode, the
step keys instead play notes / set velocity buckets — see
[§5.18](#518-modal-trig-grid-surfaces). The **mini-sequencer timeline** in the
nav row remains visible in all three modes; the mode badge in the top context
band identifies the active mode.

Links: [§5.6](#56-step-editing) · [§5.17](/docs/reference#517-keyboard-ui-revamp) ·
[§5.15](#515-checkpoints-live-undo) · [§5.4](#54-transport-and-navigation) ·
[§5.3](#53-verb-keys)

### Func — the qualifier, held alone

```
Func (1)
├─ Func + Y (RESTORE) → restore checkpoint: tap = pop one, hold = jump to floor — §5.15
├─ Func + U           → omni copy (scene + track + phrase; badge CPY:ALL) — §5.4
├─ Func + I           → unqualified paste (stamp the one captured layer) — §5.4
├─ Func + O           → UNDO — revert the last destructive op; a held scope narrows it — §5.15
│                       (delete moved to scope + hold(O) — 9.29; clear-locks to Trig + hold(O) — 9.37)
├─ Func + P           → cancel a pending prompt — §5.3
├─ Func + 3           → Cue (audition) scope: hold = pre-listen focused track; Cue+step = audition step — §2.5
├─ Func + 5…0         → secondary section page (machine deep params; COND/NOTE meta; Func+7 = transport globals) — §5.8
│   ├─ Func + FX (0)          → effect picker: step grid re-skins to effect catalogue; press step to load; re-pick active = toggle bypass — §5.8
│   └─ Func + Song + FX (0)  → master FX picker (same catalogue; loads into Song-scope master unit; re-press to cycle all 4 slots; re-pick active = toggle bypass) — §5.8
├─ Func + ← / →       → rotate the focused track's steps −1 / +1 — §5.17
├─ Func + ↑ / ↓       → double / halve the focused track length — §5.17
├─ Phrase + ↑ / ↓     → transpose the focused track's phrase ±octave (Func+ = ±semitone) — §5.17
├─ Func + step        → P-Lock clear mode (cells show set P-Locks; stage removals, release to commit) — §5.17
├─ Func + MOD (9)     → reserved/inert (Density entry moved to generator hub) — §39
├─ Func + AMP (8)     → reserved/inert (Vel entry moved to generator hub) — §39
└─ Func double-tap    → universal escape (clears latches, Euclid, density/vel sticky) — §39
```

Links: [§5.15](#515-checkpoints-live-undo) ·
[§5.4](#54-transport-and-navigation) · [§5.3](#53-verb-keys) ·
[§5.8](#58-sections-and-the-manipulation-zone) ·
[§5.17](/docs/reference#517-keyboard-ui-revamp)

### Trig — one or more held steps

```
step(s) held  (opens inspector: grid shows the step's P-Locks; tap a slot to clear it)
├─ + SRC             → note-edit mode for the held step (1-octave chromatic overlay) — §5.17
├─ + ← / →          → bubble-swap the held step with its neighbour (step follows; repeat to keep moving) — §5.1
├─ + Func + ← / →   → nudge micro-time ±5% of step length on the held step — §5.1
├─ + U               → copy held step(s) (trigs + conditions + P-Locks) — §5.9
├─ + I               → paste clipboard onto held step(s) — §5.9
├─ + O               → clear held step(s) (full: trig + condition + P-Locks) — §5.9
├─ + hold(O)         → clear all P-Locks on held step(s), keep the trig — §5.17
├─ + (MZ slot) + O   → clear only that one slot's P-Lock on held step(s) — §5.17
├─ + (section) + O   → clear that section's overrides on held step(s); SRC+O = clear notes/velocity/gate — §5.17
├─ + P (QUANT)       → Quantize: zero microOffset on held step(s) — §5.1
├─ + section key     → edit that section's field as a step override (P-Lock / trig override) — §5.7
└─ + encoder turn    → write a P-Lock on the held step(s) — §5.7
```

Links: [§5.9](/docs/reference#59-copy-paste-clear) · [§5.17](/docs/reference#517-keyboard-ui-revamp) ·
[§5.7](#57-parameter-editing-p-locks)

### Section — a held section key (5–0: TRIG/SRC/FILTER/AMP/MOD/FX)

```
section (5–0)
├─ (tap)             → select / page that section's params into the MZ (8 at a time) — §5.8
├─ + U               → copy that section's params (all steps) — §5.9
├─ + I               → paste that section onto the current track — §5.9
├─ + O               → reset that section to default — §5.9
├─ + step            → animate bypass (FX section only: bypasses insert slot; restores on release) — §5.8
├─ Track + section   → track-foundation row (post-machine FILTER/AMP, inserts) — §5.8
├─ Func + section    → the machine's secondary page / meta layer — §5.8
├─ hold FX (0)       → track effect picker (step grid re-skins to catalogue; press step to load) — §5.8
├─ Song + hold FX    → master effect picker — §5.8
└─ hold step + SRC   → note-edit mode for that step (1-octave chromatic overlay; retire Func+Src+step) — §5.17
```

(Section contents vary by machine — see the catalogue and value tables in
[§5.5](#55-track-selection-and-focus).)

Links: [§5.8](#58-sections-and-the-manipulation-zone) ·
[§5.9](/docs/reference#59-copy-paste-clear) · [§5.17](/docs/reference#517-keyboard-ui-revamp)

### Track (2)

```
Track (2)
├─ + step (D–; / C–/) → select & focus track 1–8 / 9–16 — §5.5
├─ + step (empty trk) → clone the active track's machine + params there, then focus it — §5.5
├─ + param edit (no track selected) → Control-All: broadcast the edit to every matching track — §5.10
├─ + ↑ / ↓ (no track selected) → cycle the focused track's input mode PLAY ↔ CHROMATIC ↔ LEVELS — §5.17
├─ + track-key + Nav → set the input mode on that specific track — §5.18
├─ + U / I / O       → copy / paste / clear the current phrase — §5.9 (confirm-gated). While held, the
│                      status lane spells the armed verbs out: `REC=COPY`, and `PLAY=PASTE` only when the
│                      clipboard actually holds something this scope accepts.
├─ + Song + O        → clear the whole track across every phrase — §5.9 (confirm-gated, wider blast radius)
├─ + hold(O)         → deletion picker: step grid shows tracks; tap to choose → named confirm (P=CONFIRM, Func+P=CANCEL) — §5.4a
├─ + P (QUANT)       → Quantize: zero microOffset on every step of the track — §5.1
├─ + Scene           → re-sync the focused track to the active scene — §5.14
├─ + TRIG → kit divider (DIV meta) — §5.8
├─ + (held) → shows song-track delta swing in band (SwTrk + (D)) — §5.8
├─ + hold(SRC)       → machine picker (press a step to load a machine) — §5.5
└─ Func + Track      → the MACHINE scope: sections page the sound; Rec/Play/Clear
                       = copy / paste / init the sound — §5.5
```

Links: [§5.5](#55-track-selection-and-focus) ·
[§5.10](#510-control-all) · [§5.17](/docs/reference#517-keyboard-ui-revamp) ·
[§5.18](#518-modal-trig-grid-surfaces) · [§5.9](/docs/reference#59-copy-paste-clear) ·
[§5.14](#514-scenes-phrases-and-songs-the-launch-model)

### Phrase (Q)

```
Phrase (Q)
├─ + step            → deviate focused track to that phrase (same as Track+Phrase+step) — §5.14
├─ Track + Phrase + step → deviate the focused track to that phrase — §5.14
├─ Scene + Phrase + step → deviate all tracks; diagonal row = clear all deviations — §5.14
├─ + U / I           → copy / paste the whole phrase (all tracks) — §5.9
├─ + O (CLEAR)       → clear this phrase (all tracks), confirm-gated — §5.9
├─ + Y (SNAP)        → mark this phrase onto its checkpoint stack — §5.15
├─ + hold O          → deletion picker: step grid shows phrase slots on the focused track; tap to choose → named confirm — §5.4a
├─ + P (QUANT)       → Quantize: zero microOffset across every step on every track — §5.1
└─ + Func + P        → cancel a queued Scene launch (Cancel = the pending-action verb) — §5.14

(Euclidean generator entry moved to the generator hub — hold `3`, pick EUCLID — §5.18.)
```

Links: [§5.14](#514-scenes-phrases-and-songs-the-launch-model) ·
[§5.9](/docs/reference#59-copy-paste-clear)

### Scene (W)

```
Scene (W)
├─ + step (occupied)        → launch (carries the live overlay); double-tap = floor launch — §5.14
├─ + step (active scene)    → revert to the saved floor — §5.14
├─ + step (empty slot)      → baked-copy create + launch (conflict-gated) — §5.14
├─ Func + Scene + step (occupied) → floor launch (arrive at saved floor) — §5.14
├─ Func + Scene + step (empty)    → default-create (blank) + launch — §5.14
├─ + U (REC)                → **commit-and-bake** deviations (confirm-gated) — §5.14
├─ + O (CLEAR)              → **SYNC**: discard live deviations, snap back to the stored scene — §5.14
├─ + Y (SNAP)               → mark the active scene onto its checkpoint stack — §5.15
├─ + Func + P               → cancel a queued Scene launch (Cancel = the pending-action verb) — §5.14
├─ Func + Scene + U         → copy the active scene to the clipboard (CPY:SCN) — §5.14
├─ Func + Scene + I         → paste the clipboard scene (baked; conflict-gated) — §5.14
├─ + hold O                 → deletion picker: step grid shows scene slots; tap to choose → named confirm — §5.4a
├─ Mute + Func + Scene + I  → paste floor only (strip the content overlay) — §5.14
├─ + (held)                 → shows scene-all delta swing in band (SwScn + (D)) — §5.8
└─ Scene + Mute + step      → per-scene mute (this track's active-mask) — §5.11
```

Links: [§5.14](#514-scenes-phrases-and-songs-the-launch-model) ·
[§5.11](#511-mutes)

### Song (S)

```
Song (S)
├─ + step (occupied) → switch Songs (quantized; a full reset, live deviations clear) — §5.14
├─ + step (empty)    → create-on-select: copy the active song into the slot + switch — §5.14
├─ Mute + Song + step (empty) → create a blank default song + switch — §5.14
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
├─ + TRIG (5) on slicer track → Slice-point picker: step cells = slice indices — §5.18
├─ + TRIG (5) on other tracks → reserved/inert (live stutter removed 9.10; use TRIG RTG band field)
├─ + SRC (6)         → Sound Pool overlay: step grid → saved-sound selector — §5.18
│   └─ press sound   → live-swap track to that sound; hold a step first to bake sound_id P-Lock
└─ Func + Fill       → (unbound — the old Accent generator was folded into the Velocity overlay, §5.8 / §39.10)
```

Links: [§5.12](#512-fills)

### Encoders — turn a control (any source)

```
encoder
├─ turn (no step held)  → edit the track base parameter — §5.7
├─ turn (step held)     → write / update a P-Lock on the held step — §5.7
├─ reset (double-click) → restore the slot default (or clear the P-Lock if a step is held) — §5.7
├─ tempo encoder        → ± BPM (TIME page Tempo field) — §5.16
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
