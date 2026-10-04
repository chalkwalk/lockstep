---
sidebar_position: 11
title: "Tutorial: your first piece of music"
---

# Tutorial: your first piece of music

This walkthrough builds a simple beat from an empty project. It uses the
**standalone** application; the workflow is identical inside a DAW. The
key letters refer to the QWERTY layout described in
[§5.1](/docs/reference#51-the-keyboard-layout) — keep that diagram handy.

> Throughout: **step keys** are the bottom two QWERTY rows
> (`S D F G H J K L` = steps 1–8, `X C V B N M , .` = steps 9–16).

### Step 1 — Load a sample

Drag an audio file (a kick drum, say) onto the sample pool area of the
window, or use the file dialog. The sample is decoded and added to the
pool. Track 1 hosts the sampler by default and will use the first pool
entry.

On load each pool entry is analysed (message thread): tempo, musical key,
and tuning reference are detected and shown in the browser hint as
`128 bpm  Amin` (either part may be absent). A rhythm-less, keyless entry
reads `one-shot`. Detection is corroborated by hints parsed from the
filename (`drums_120bpm`, `pad_F#maj`) and embedded ACID WAV tags — detection
wins, a hint only resolves a tempo octave-fold or fills a gap. The result is
cached with the project (keyed by the sample hash), so reopening is instant
and only a changed file re-analyses. Analysis of very long files (>30 s) is
skipped — that is `StreamMachine`'s domain.

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

**Page dots** under a section key mean *pressing it again cycles* — one
dot per page, the current one filled. A key with several parameter pages
(e.g. Analog's two FILTER pages) shows a dot per page; while a subpage
overlay is open its owning section key shows its subpages instead (VEL on
AMP = 4 dots, DENSITY on MOD = 3, TIME/KEY on TRIG = 2). If a key shows one
dot or none, a re-press does nothing to cycle. This is the single "re-press
to cycle" affordance across the whole surface.

Now the magic: **hold a step key** and turn the same control. Instead of
changing the track's base value, you've written a **P-Lock** — that
parameter change applies *only* on that step. Hold step 9 and drop the
pitch, for example, and only the third kick is lower. Release the step;
the lock stays.

To remove a lock, hold the step and clear it (push the held encoder, or
use the section-clear gesture).

### Step 6 — Add a conditional trig

Open the **COND** layer. With no step held, `Func + TRIG` (i.e. `1 + 5`)
sets the **track's** base condition (probability, iteration m:n). Set
probability to, say, 50% and that track fires stochastically each loop.

Hold a single step and the same controls now write that **step's**
condition — so you can make just one trig 50%-likely, or fire it only on
every 4th pass (set m:n to 1:4). While a step is held you don't need `Func`:
a **bare `TRIG`** press promotes straight to that step's COND (the `TRIG`
key relabels COND while any step is down), so the per-step-conditions flow
is a single held-step gesture. Releasing every step, bare `TRIG` returns to
its normal `DIV` meta and `Func + TRIG` still reaches the track base
condition. The grid shows you what will fire before it happens: certain
hits are bright, skips are dim, probabilistic steps are in between.

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
  key (`Z + D` = mute track 1). Non-destructive. Audio tracks fade out over a
  short (~100 ms) declick ramp rather than cutting hard; MIDI-out tracks stop
  emitting and send note-offs so nothing hangs. Toggle again to bring it back
  (audio ramps back in). Scene mutes use the same fade.
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
