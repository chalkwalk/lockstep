---
sidebar_position: 30
title: "Feature reference"
---

# Feature reference

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
secondary legend: `Func+Y`=RESTORE, `Func+P`=CANCEL (the `U` key has no Func
legend — `Func+U` is omni copy; `O`'s secondary rail is now the **hold**, not
Func — see §5.4a). The `I` key still paints
a stale `PANIC` legend, but `Func+I` is **unqualified paste** — Panic
moved to `Song+Clear`.)

The verb keys are **context-sensitive** — they read three layers:

- **No scope held** — transport / confirm: `Y`(SNAP) pushes a checkpoint,
  `U`(REC) toggles record-arm (double-tap = overdub), `I`(PLAY)
  starts/stops the transport (double-tap = stop-to-top), `O`(CLEAR) clears
  the active operand, `P`(CONFIRM) confirms a pending prompt.
- **A scope held** — the scope verbs: `U`=Copy, `I`=Paste, `O`=Clear.
  `Y` is the scope's snapshot (reserved/dim on most scopes); `P`=**QUANT**
  under Trig/Track/Phrase (zero microOffset), dim on Scene/Morph/Song/Mute/Fill.
- **`Func` qualifier** — `Func+Y`=Restore (pop/floor, on the held scope's stack),
  `Func+U`=omni copy, `Func+I`=unqualified paste, `Func+P`=cancel a prompt.
  `Func+O` is **not** a delete (9.29): it is **UNDO**, in every state (9.37).
  The per-step *clear P-Locks, keep the trig* it briefly shared the chord with
  now lives on `Trig` + hold(`O`). Delete is `scope + hold(O)` — §5.4a.

There is no separate transport key — the verb row does double duty, which
is why the surface needs no extra buttons. The full action set is indexed
by press-order in [§7](/docs/gestures#7-gesture-tree).

In **Ortholinear** and **Staggered** display modes the keys immediately
outside the 10-column block — `` ` ``, `Tab`, `CapsLock`, `Shift` on
the left and `-`/`=`, `[`/`]`, `'` on the right — are shown as dimmed
decorative anchors. They have no sequencer function, but pressing one
briefly lights it so you can reorient if you overshoot a key.

The grid-display toggle (`STG` / `ORL` / `CLN`) cycles the key layout and
**defaults to Staggered**; the choice persists across sessions and is only
written when you press the toggle. The whole window is **resizable** with a
locked aspect ratio — drag any edge to scale the entire UI (fonts included)
crisply; the scale defaults to 1.2× and persists.

### 5.2 Modifier (scope) keys

The eight modifiers form the left two columns, ordered by
frequency-of-use (most-touched at row 0, performance specialists on
row 3). Six of the modifiers are **section scopes** (each owns a row
in the scope-section matrix); two are **performance specialists**
(no section row).

| Key | Scope | Selects |
|---|---|---|
| `1` | **Func** | Universal qualifier — composes with any other scope to flip to its "secondary variant." Also the modifier layer for snapshots, verbs, and machine secondaries. |
| `2` | **Track** | One or more tracks; or, with none selected, Control-All. `Track+section` opens the track-foundation row (post-machine FILTER/AMP, IEffect inserts). `Track + hold(SRC)` = the **machine picker**. **`Func+Track`** = the **Machine** scope (the sound: its params + copy/paste/init). |
| `Q` | **Phrase** | A per-track musical phrase (pure note content). `Phrase+step` (or `Track+Phrase+step`) deviates the focused track to that phrase. Only **existing** phrase rows launch — a row exists when its scene (diagonal owner) is created or the track has an authored phrase there; dim rows are inert (`Phrase+step` is launch, not create — new phrases come from scene-create and Browser copy/move/fork). `Scene+Phrase+step` deviates all tracks; landing on the diagonal row clears all deviations. To clear all deviations: `Scene+Phrase+step` on diagonal, re-launch active Scene, or `Func+Scene+step`. |
| `W` | **Scene** | A launchable cross-track row (diagonal phrase row + active-mask + core time). Scene N always plays phrase row N. `Scene+step` occupied = carry overlay; on active = revert to floor; on **empty** = baked-copy create + launch. `Func+Scene+empty` = baseline-copy create; `Mute+Scene+empty` = blank create. `Func+Scene+occupied` = floor launch. `Scene+Record` = commit-and-bake. `Func+Scene+Record/Play` = copy/paste. |
| `A` | **Morph** | The A/B crossfader scope. Hold/latch + encoder sculpts overlay at current fader split; `Morph+^`/`v` forces pure A/B writes; `Morph+Mute` = fluid mute a track. |
| `S` | **Song** | Song select (`Song+step`). Songs create-on-select like scenes: an **empty** slot creates + switches (`Song+step` = copy the active song, `Mute+Song+step` = blank default). Only created songs light in the selector. `Func+Song` = Global / master-bus focus. |
| `Z` | **Mute** | Global mute mask (hold and tap several tracks). `Scene+Mute+step` = per-scene mute. |
| `X` | **Fill** | "While held, fills fire." `Fill+step` marks step as fill-only. |
| step key (held) | **Trig** | The held step(s). Multi-step holds allowed. |
| `5`–`0` | **Section** | The held section's parameters. Cell meaning depends on which scope (if any) is held alongside. |

(`Cue` is the **audition** scope, entered as the compound `Func+3` (it needs
no dedicated cluster key — hardware parity). Holding it auditions the focused
track's base trig; `Cue+step` auditions a step's resolved trig. Both write
nothing. The cue *bus* / pre-listen crossfade shipped in 6.4 — see the **cue
balance** glossary entry and the `Cue + Mute` / cue-console shortcuts.)

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
| `Y` | **SNAP** | Push a mark (Song scope) | **Snapshot** that scope (§5.15) | **Restore** — tap = pop one mark, hold = jump to floor |
| `U` | **REC** | Toggle record-arm (double-tap = overdub) | **Copy** scope → clipboard | **Omni copy** (scene + track + phrase) |
| `I` | **PLAY** | Play / Stop transport (double-tap = stop-to-top) | **Paste** clipboard → scope | **Unqualified paste** (stamp the one captured layer) |
| `O` | **CLEAR** | Clear active P-Lock slot (`Song` held = Panic) | **Clear** scope contents · **hold** = delete the entity · `Scene+O` = **SYNC** (discard live deviations) | **Undo** — revert the last destructive op (§5.15) |
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
| `O` (Clear) | Clear scope contents (tap) · **delete** the entity (`scope + hold(O)`, confirm-gated); `Func + O` = **UNDO** (9.29 moved delete to the hold rail; 9.37 gave `Func+O` to Undo in every state). |
| `U` (Rec) | Toggle record-arm (overwrite). Double-tap = overdub (append); `Func + U` = **omni copy** (captures scene + active track + pattern in one grab; badge `CPY:ALL`). |
| `Func + Song + U` | **CAPTURE** — the tape deck. tap = arm (rolls on Play) · double-tap = roll now · while recording tap = stop / double-tap = hard cut · long-press in the just-saved window = discard (see §5.20). |
| `3` | Tap tempo (short tap). **Hold ≥350 ms** = generator hub: step cells show EUCLID / DENSITY / VEL / MELODY / CHORD; press one to enter that generator with its own lifetime; release `3` closes the picker. |
| `Func + 3` | **Cue** (audition + balance) scope — key `3` shows a **CUE** hint under `Func`, in cue cyan. Hold = pre-listen the focused track's base trig; `Cue+step` = audition that step's resolved trig (writes nothing). **`Cue + Mute`** = toggle the focused track's **cue balance** (send it to the headphones / bring it back). **`Cue + hold(AMP)`** = open the **cue console** (see below). |
| `Cue + hold(AMP)` → console | The **cue console** (sticky) — the twin of `Track + hold(AMP)` = MIXER (**AMP is the mixer key**). Opens straight to the **param page** — the eight encoders set the bank's eight cue balances continuously; **re-press `AMP` to page the track bank** (1-8 ↔ 9-16), with pips under AMP. **Nav** → **flip page** — a step key arms that track's cue flip; the armed tracks cross to/from the headphones together on the next launch quantum (cyan strip = cued, hollow dot = armed). Foreign scope or double-tap `Func` exits. |
| `4` | Navigate up (inverted-T above `E R T`). |
| `E` / `R` / `T` | Navigate left / down / right. |

### 5.4a Deletion picker and named confirms

**Holding** the Clear key under a scope (`Track` / `Phrase` / `Scene` + hold `O`)
enters the **deletion picker** modality — the step grid repaints as a slot-selector for
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
- **The prompt cannot go missing (9.30).** It renders as a **double-height
  pop-over** dropping out of the STATUS lane, over the top of the Manipulation
  Zone, and it is **derived from the pending state** — so it stays for exactly as
  long as the arming does. It used to be a 1.5-second fading toast: 1.5 seconds
  after arming a delete there were no pixels left saying that the next `P` would
  destroy a track, while the arming was still live. It occludes the MZ on purpose;
  that is safe because while a confirm is pending **every key either confirms or
  cancels it**, so there is nothing underneath you could have pressed anyway.
- **Tap vs hold.** `scope + tap(O)` **clears** that scope's contents (recoverable
  from the checkpoint stack); `scope + hold(O)` **deletes** the entity. The
  destructive verb costs the deliberate gesture — the same tap/hold split the
  section keys use (tap = navigate, hold = pick).
- **Scope coverage.** Track / Phrase / Scene deletions use the picker.
  `Song + hold(O)` is inert (no entity to delete). `Morph+Func+O` = morph
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

Tracks 1–8 default to `SampleMachine` and tracks 9–16 to `MidiOutMachine` (Digitakt-style default split). Any track can be reassigned to any machine via **`Track + hold(SRC)`** (hold `2`, hold the SRC section key — the step grid re-skins to machine names; press a step to load one). A small **"M"** badge in the top-right corner of a track button identifies MIDI-out tracks at a glance.

#### The Machines — what each one is for

Every track hosts one **machine** — its sound-making (or sound-shaping) engine.
The catalogue is a small, opinionated "greatest hits" set: one machine per iconic
idea, each authored against the same boundary and snapping to the same canonical
sections, so a skill you learn on one carries to the rest. This is the *why and
when* of each; the [table below](#machine-catalogue) is the *what* (every
parameter). Names are chosen to say what the machine does at a glance — the four
sample-players in particular (**Sample / Slice / Stretch / Stream**) differ by
one clear idea each.

**Sample** — *Your workhorse for turning any sound into a playable, lockable
instrument.*
The Sample machine plays one sample from RAM with pitch, a trim window, looping,
and an amp envelope, all P-lockable per step. It is the first machine you reach for:
drop in a one-shot and play it chromatically, or trim a longer sample to a hit
and lock a different start point, pitch, or loop region on individual steps. Pitch
is rate-based (higher note = faster = shorter), which is exactly what you want for
drums, chops, and classic sampler character.
*In performance:* you tap a vocal one-shot onto a track, hold a step and nudge its
start point so each repeat bites a different consonant, then P-lock a pitch ramp
across four steps for a riser — all without stopping the transport. It is the
machine you improvise *with*, because everything it does is one lock away.

**Slice** — *Chop a loop or phrase into pieces you can replay, reorder, and
scrub live.*
Point a Slice track at a break or vocal phrase and it divides it into slices — evenly,
or on detected transients — that you trigger from the grid or a keyboard; SCRUB
mode instead drives playback speed (and reverse) from the note. It turns a single
loop into a whole kit of playable fragments without any pre-editing.
*In performance:* you load a drum break, hit TRANS to snap slices to the hits, and
play the grid to rebuild the beat in a new order; a negative-rate P-lock throws a
slice into reverse for a fill, and holding a note in SCRUB mode tape-warps the
phrase down as a transition.

**FM** — *Metallic, glassy, and percussive tones that subtractive synths
can't reach.*
A 4-operator FM engine with a free modulation matrix and per-operator envelopes:
the home of bells, electric pianos, clangs, hollow basses, and sharp digital
percussion. Where the Analog machine is warm and familiar, FM is bright, precise,
and inharmonic — the sound of DX-era and Digitone-era gear.
*In performance:* you P-lock the modulation index up over a held note so a mellow
bell blooms into a metallic stab, then drop an operator's ratio on the accent steps
for a talking, evolving lead that never sits still.

**Analog** — *The warm, familiar subtractive voice for basses, pads, leads,
and stabs.*
A virtual-analog dual-oscillator synth with sub, a state-variable filter, two
envelopes, an LFO, and mono/paraphonic modes — the bread-and-butter synth voice.
An always-on gentle glue saturation and an **Age** drift macro keep it from sounding
sterile.
*In performance:* you hold the FILTER page and sweep the cutoff by hand for a
classic filter-open build, latch a paraphonic chord across four steps, and dial in
Age so the stack detunes and breathes as the section runs.

**Drum** — *A full synthesized drum kit on a single track, every voice
tweakable and lockable.*
One machine with a stepped **Type** (KICK, SNARE, HAT, TOM, CLAP, COWBELL, CYMBAL,
RIMSHOT), each a dedicated Rytm-style synthesis model — no samples, so every drum
is fully synthetic and P-lockable. Put one type per track for a kit, or lock the
Type per step for a drum-line that morphs.
*In performance:* you tune the kick's decay longer on the downbeats, P-lock the
snare's pitch up for a fill, and ride the hat's decay with a held FILTER move so
the groove opens and closes under your fingers.

**Route** — *Turn a track into an effects block, a sub-mix bus, an aux send, or an
external-audio input.*
Route makes no sound of its own — it feeds audio into the track's normal
FILTER/AMP/FX chain, so the sequencer doubles as a small performance mixer. Its
`input_source` can be the external input, the master mix, or a zero-latency copy of
another track (a *tap-fork*); other tracks can also be routed *into* it to build a
bus.
*In performance:* you route your drum tracks into one Route "bus" and ride a single
filter+delay over the whole kit; on another Route you tap the guitar coming in the
external input and treat it exactly like an internal track — same locks, same FX,
same scenes.
Because a Route sequences nothing, its step buttons are **lock-only anchors**: a
step press carries P-Locks (source, filter, level…) that ride onto the flowing
audio as the playhead crosses it, but never a note. **Long-press `SRC`** to open the
Route's **routing console** — the step grid becomes an all-tracks matrix showing
where every track's audio goes (`OFF` / `MST` / `T<n>`). Tap a cell to cycle that
track's destination; edits stage in amber until you **`Confirm`** (commit) or
**`Func+Confirm`** (cancel) — so a mis-route is always undoable. Long-press `SRC`
again to close.

**Stream** — *Play long-form audio — full songs, mixes, long recordings — straight
from disk.*
Stream is the long-player: it streams from disk on a background thread and never
loads the whole file into RAM or the project, so a 10-minute mixdown or a field
recording costs almost nothing. Only the file path is saved. **Pick the source**
either by dropping a file on the focused Stream track, or via the sample-pool
manager — its **Load** button reads **Stream…** on a Stream track and assigns the
disk source there. Stream now runs through the same Bungee stretch engine as
Stretch, so it **resamples a file whose rate differs from the session** (a 48 kHz
file on a 44.1 kHz project plays at the right pitch, not slightly flat) and gains
**Pitch** and **Tune** controls plus a **Timestretch** toggle.
*In performance:* you run a full backing track or a DJ-style mixdown on a Stream
track, trigger it to start on the downbeat, and play your live parts over the top —
the heavy audio never touches memory.

**Stretch** — *Play captured loops and pitched samples with pitch and tempo that
move independently.*
Where the Sample machine ties pitch to speed, Stretch (the Flex engine, now driven
by the Bungee time/pitch stretcher) transposes without changing duration and, in
**Tempo** mode, time-stretches a buffer to the project tempo using its stamped
bar-length — so a loop stays in time as you change the BPM. Alongside **Pitch** it
adds a **Tune** (±50 cents) fine control, a **Loop** toggle (seamless — the loop
wraps by source position with no click, so a held trig plays the whole cycle instead
of retriggering per step) and a **Rev** (reverse) toggle. The **Loop** toggle takes
effect **live on a sounding voice** — flip it on mid-note and the current pass
re-latches into a loop, flip it off and the pass plays out then stops — but only
while the note is still held; if the trig's gate has already ended, or the sample
was auto-fitted (which sets Loop on for you), toggling looks inert. Point it at any
pool slot, including a live Record/Loop capture.
*In performance:* you resample a phrase into a REC slot, play it back on a Stretch
track locked to the grid, then pull the master tempo down for a breakdown — the
captured loop follows in time and in tune instead of chipmunking.

**Record** — *Live resampling: capture the mix (or one track, or the input) into
a buffer you can immediately play.*
A Record track captures its `input_source` into a volatile REC buffer on a trig,
**overwriting** each pass — continuous live resampling. It pairs with the Sample,
Slice, or Stretch, which play the buffer it just filled; `monitor` lets you hear
the source as it records.
*In performance:* you drop a Record track tapping the master, fire a one-shot to grab
the last bar of the jam, then Slice that capture on the next track and rebuild it
into something new — sampling your own performance as it happens.

**Loop** — *Hands-on overdub looping with a dedicated transport-and-FX console.*
The Loop machine is the overdub counterpart to Record, and because it doesn't
sequence, a focused Loop turns its 16 step buttons into an **always-on console**:
record/overdub/undo/halve/double on the top row, momentary beat-repeat and tape FX
(tape-stop, dip, half-speed, reverse) on the bottom. It layers sound-on-sound and
locks to the track's grid.
*In performance:* you lay a bass loop, overdub a chord pass, then hold beat-repeat
on the last cell to stutter the turnaround and slap a tape-stop on the drop — the
whole take built and mangled from one always-live surface, no menus.

**MIDI Out** — *Sequence and automate external gear as a first-class track.*
A MIDI-out track drives outboard synths, drum machines, or another plugin: note
output plus 16 assignable CC lanes with your own labels, all P-lockable and
scene-able exactly like an audio track's parameters. External gear is not a
second-class citizen — it locks, morphs, and recalls like everything else.
*In performance:* you sequence a hardware synth's notes on one track and lock its
filter CC per step, then let a scene morph re-voice both your internal machines and
the outboard box together in one gesture.

<a id="machine-catalogue"></a>
#### Machine catalogue

| Machine | Badge | Description |
|---|---|---|
| `SampleMachine` | SMPL | Monophonic sample playback. SRC section: sample, pitch, trim window (`samp_start` / `samp_length`), loop mode (OFF / SUS / S+R / ALL), loop region (`samp_loop_start` / `samp_loop_len`), loop-seam crossfade (`samp_loop_xfade`, ms). The crossfade declicks the loop wrap: it **borrows real tail material past the loop end** when the sample has it (loop period unchanged — right for bar-synced loops), and falls back to **eating into the loop** when the loop reaches the sample end. `0 ms` = the old hard wrap. All position slots snap to zero-crossings on write. AMP section: level + AHDSR. |
| `SliceMachine` | SLCE | Slice/scrub sample playback. SLICE mode: incoming MIDI note selects slice 0–15; `slicer_start` / `slicer_length` are relative to the active slice. SCRUB mode: note drives playback rate vs. root 60 (identical to Sample semantics). `slicer_rate` P-lockable for per-step rate; negative rate = reverse playback. `slicer_slice_src` (EQUAL / TRANS / SYNC) and `slicer_slice_count` auto-recompute slices on change; transient detection uses 5 ms RMS blocks with fast/slow envelope ratio and centre-weighted search. **SYNC** slices on a beat grid at the sample's detected tempo — the Count slot becomes a clock division (`4bar…1/16`, shown as a `Div 1/4` label), the grid anchors on the first transient (so a loop that doesn't start on the 1 still lines up) and snaps to zero crossings; a sample with no detected tempo falls back to EQUAL. VOICE section: MONO / POLY toggle (V4). |
| `FMMachine` | FM | 4-operator FM synthesis. Free 4×4 modulation matrix (diagonal = smoothed self-feedback). Exponential per-operator ADSR, ratio, fine-tune, mix. Macro attack/release/sustain scalars. MONO / POLY voice modes (V4 pool). Operator core is 2× oversampled for clean high-index FM. Carrier mixer normalizes above unity (stacking operators won't blow up the level) and polyphony is loudness-compensated (a chord ≈ 1/√N), level-matched to the drum/Analog reference. |
| `AnalogMachine` | ANLG | Virtual-analog dual-osc synth. PolyBLEP oscillators + sub + shared noise. State-variable filter (LP24/LP12/HP/BP + drive). Filter ADSR + amp ADSR. LFO (6 shapes). Mono / Paraphonic-4 voice modes. Para topology: chord notes 1 & 3 → osc1+sub; notes 2 & 4 → osc2+sub. Always-on gentle glue saturation + paraphonic loudness compensation; **Age** (MOD) macro dials in analog drift (detune/cutoff/PW wander). Filter **Key Trk** (FILTER) tracks the cutoff to pitch (default full; audible once the cutoff is below maximum). Output level-matched to the drum/FM reference. |
| `DrumMachine` | DRUM | Rytm-style per-track drum synthesis. One stepped `Type` param selects the variant; each has dedicated DSP. Eight types ship: KICK, SNARE, HAT, TOM, CLAP, COWBELL, CYMBAL, RIMSHOT. |
| `RouteMachine` | ROUT | Pure audio router. `input_source` `{None / Ext / Master / Track N}` feeds audio into the track's signal path at unity; the universal FILTER/AMP/FX do the work. Use it as an FX block, sub-bus, or **aux send**: selecting `Track N` taps a read-only copy of that track's post-FX output (same-block, zero latency) while it keeps flowing to its own destination — a parallel processing chain (DESIGN §27 tap-fork). A Route sequences no notes, so its steps are **lock-only** P-Lock anchors. **Long-press `SRC`** opens the **routing console** — an all-tracks output matrix (each cell = one track's `Out`), tap a cell to cycle its destination, `Confirm` commits / `Func+Confirm` reverts. |
| `RecordMachine` | REC | Live resampler. Captures `input_source` `{None / Ext / Master / Track N}` audio into a volatile REC buffer (`target_buffer`, 1 of **16**) for `rec_length`, **overwriting** each time a trig fires. `Track N` taps one specific track post-FX (resample a single track, not just the whole mix). `monitor` `{Off / On}`: Off is a silent tap, On passes the input through so you hear the source while recording. On a Record track a trig **is** the recorder trig — a plain trig re-captures every loop, a one-shot captures once (lock-only is disabled). Record is the deck engine's **1-track linear face**: it captures onto a tape reel through a transport-chasing head, so a **tempo change mid-take varispeeds the take like tape** (deliberately *not* pitch-preserved — that is the Loop's `Free Len` fit). At a steady tempo the capture is bit-identical to a straight copy. `rec_length` is a **wall-clock** cap; the take that lands stretches or compresses with the tempo trajectory. The take commits to the buffer **when it closes** (not while it is still recording), and one level of **undo** brings back the buffer's previous take. The captured buffer is immediately playable by a Sample/Slice/**Stretch** pointed at it; its musical bar-length is stamped for tempo-tracking playback. REC buffers are RAM-only and lost on quit (freeze-to-disk is a later milestone). Each slot holds up to **`RecLen`** seconds (`Func+7`, default 60 s); the buffers are allocated lazily, so an unrecorded slot costs no memory. A fresh capture machine takes the next free slot; when all sixteen are spoken for, the slot it lands on is marked `!` rather than silently overwriting another track's take. |
| `LoopMachine` | LOOP | Overdub looper (Octatrack pickup machine), and one face of the deck engine. **Up to four sub-tracks** (`subtrack_count`, default 1): a four-sub-track loop records four independent taps into one wide slot and mixes them back with per-sub-track `level`/`pan`/`mute`/`solo`. **ARM gates every write** — the length-defining first pass and every overdub alike. Sub-track 0 is armed by default (a single-track looper never thinks about it); the other subs arm when you give them a source (the **SRC** cell and **ARM** cell are two views of one intent) and the TRACKS page arms them explicitly. A disarmed sub records silence. Whole-deck `UNDO` restores every sub-track at once. With more than one sub-track, **Nav left/right pages the console** between the DECK layout and a **TRACKS** page (`ARM · MUTE · SOLO · SRC` per sub-track) — a looper doesn't sequence, so those keys are free. Promote a multi-track take and it saves as a **take-group** (one WAV per non-empty sub-track + a materialised stereo downmix). A single-sub-track loop is byte-identical to before. A verb-driven state machine with an **always-on console**: with a looper track focused the 16-button step grid becomes the looper's transport + performance surface (a looper doesn't sequence). **Top row** — `REC` (the one record verb: the length-defining pass while the deck is empty, an overdub once a loop exists — the cell **relabels to `DUB`** then; double-tap = record now / punch-out now; **hold = momentary punch-replace** on the armed subs), `PLAY`, `STOP`, `ERASE`, `UNDO`, `HALF`, `DBL`, `DUB` (explicit overdub toggle while a loop plays). **Bottom row** — momentary performance: beat-repeat `1/16 · 1/8 · 1/4 · 1/2`, then tape FX `TSTOP · DIP · x1/2 · REV`. Fed two ways: select an `input_source` on its **SRC** panel (a read-only tap/fork — the source still reaches its own destination), or route another track's `Out` **to** the looper track (it sums in). `monitor` `{Auto / On / Off}` governs live-thru: **Auto** monitors an `None`/`External` insert in every state **except while the take is Playing back** (the capture replaced the live source — dropped on the record→play transition, restored when stopped), and stays loop-only for a `Track`/`Master` tap (already audible); **On**/**Off** are absolute. Records/overdubs `input_source` and self-plays the loop, which lives in a shared volatile REC slot (`target_buffer`) — so a Sample/Stretch can also play it (the OT recording-buffer model). `loop_sync` `{Free / Free Len / Sync}` (**defaults to Sync** — the sane grid-locked mode; Free, which ignores tempo, is the hardest to reason about): **Free** ignores tempo (native, instant record), **Free Len** rounds the recorded length **up to the next launch-quant multiple** and time-stretches to fill it **pitch-preserved** (streamed live the instant the take closes, then baked to a plain grid-aligned static loop in the background — so it survives a transport stop and costs nothing to keep playing; supersedes the old varispeed conform), **Sync** grid-locks length to the track's own length × divider (varispeed phase-locked; the grid reflects the loop by construction — no looper-only length params). In Free Len / Sync the record-start and stop edges **quantize to the bar grid** (loops phase-lock; a quantized stop is a **punch-out** that lands on the bar and hands straight to Play); **double-tap** `REC` to fire instantly, overriding quantize. That instant override also **remembers your first tap** (§40.13): a double-tapped record *start* backfills the loop from the pre-roll so it begins where you first pressed, and a double-tapped *close* lands the loop length at the first tap — the overshoot between the two taps is discarded and playback wraps phase-continuously. While a quantized edge waits, `REC` shows **ARM** and the mini-seq shows a landing pip. **HALF/DBL** (also `Func+↓`/`Func+↑` on a looper) resize the loop *window* with no resample / no pitch change. **Beat-repeat** (hold): captures the grid cell under the playhead and loops it — silent at the press instant, first audible repeat ≤ one interval later; release resyncs. **Tape FX** (hold): `TSTOP` decelerates to a graceful stop (release while moving = accelerate-back tease), `DIP` is tape **wow** — the head speed wobbles around unity for as long as you hold it (a plateau would just be `x1/2`), `x1/2` half-speed (octave-down), `REV` reverse; releases resync to the grid. `loop_decay` (0 = hold forever … 1 = full) with `loop_decay_mode` `{Overdub / Always}` — Overdub fades the old layer only where you overdub (feedback knob); Always fades the whole loop each iteration (tape echo). The **mini-seq strip** shows the loop position: a continuous playhead over the whole loop plus a landing pip for a pending quantized edge. RAM-only (lost on quit). |
| `TapeMachine` | TAPE | A **linear tape** addressed by the song's own position (the third face of the deck engine). Where Record overwrites a buffer and Loop is a circular loop, Tape is a reel you punch into against the transport's timeline: playback reads whatever is on the reel at the current position, recording writes the input there, and **a locate winds the tape** — the reel follows the playhead because it *is* the playhead (hosted, dragging the DAW's playhead winds it). Its **always-on console** — punch on the top row (`REC` punch in/out, `CLEAR`, `UNDO`), markers on the bottom (`DROP` a mark, `|< CUE >|` to wind to the previous / nearest / next mark). **The tape follows the main transport — there is no separate tape Play/Stop.** When the song plays the reel chases; when the song is parked the head detaches for scrub/wind. A punch is **non-destructive**: `UNDO` restores the original of the last punched span. **Retro double-tap** (§40.13): because the tape punches instantly (no quantize to beat), a quick *second* tap on `REC` **while recording** backfills the run-up before your punch-in from a pre-roll ring (for when you punched a hair late) — undoable like any punch, rather than punching out. **Markers are dumb** navigation points — dropped manually and automatically at every Scene/Song switch *while recording* (so a take you performed by launching scenes comes back with the launches marked), cued by a **locate** (never a launch — a marker fires nothing). The reel is a host-allocated fixed-length RAM medium (`medium_length`, default 5 min, lazily committed) with an **honest end**: run the transport off it and nothing records or plays; `medium_depth` picks **32-bit float or 16-bit** stock (16i halves the reel's RAM — one depth for the whole deck, converting every sub-track in place). Like the Loop, the Tape is a **four-sub-track deck** (`subtrack_count`, default 1): the reel is allocated at full width and lazily committed, so a single-sub-track tape is byte-identical to before. Each sub-track pulls its own `input_source_k` and **arms when that source is not `None`**; a punch records onto every armed sub-track's own channel-pair while the unarmed subs keep playing back, and playback sums the enabled subs through per-sub `level`/`pan`/`mute`/`solo`. Whole-deck `UNDO` restores every armed sub over the punched span. With more than one sub-track, **Nav pages the console** to a shared **`TRACKS`** page (`ARM · MUTE · SOLO · SRC` per sub-track — `ARM` is a status light, since the tape auto-arms from `SRC`), and a multi-sub take **promotes as a take-group** (one WAV per sub-track + a stereo mix). The tape is **chase-locked to musical time** like a studio deck slaved to timecode: the head is `ppq × K`, where the calibration `K` latches from the tempo at the first record onto an empty reel (reset by `CLEAR`). A tempo change is varispeed — pitch follows, the chase ratio shows on the strip (e.g. `x0.50`) — but a bar stays a bar, so a punch or cue lands on the musical position it names under any tempo history. **Winding** (standalone only): the `<< / >>` console cells FF/RW the reel (audible, slewed) and — while the song is parked — SRC slot 0 renders as a **spinning tape reel** you rock to jog, both dragging the transport with the head so play/punch resume where your ear found the point; the reel widget shows only when a wind is live (parked + standalone) and gives way to the Source picker the moment the song plays. Suppressed when the host owns the playhead. A **permanent, display-only timeline strip** under the context inspector (always visible, tape or not — so you can leave the tape face and still watch the recording time advance) shows a **bar ruler** and a **wall-clock ruler** over a domain of `max(32 bars, longest tape, cursor)`, the position in `bars.beats` and `m:ss`, the recorded extent, the markers, a per-tape recorded-end lug (the chosen tape highlighted), a marker-approach brighten, a recording pulse, and a near-full warning. Tape audio is RAM-only until **promoted** to a WAV (promote-or-lose); markers live and die with the session. |
| `StreamMachine` | STRM | Disk-streaming sampler for long-form audio (full songs, long recordings). Streams from disk on a background thread and **never decodes into RAM** or project state. Its source is a **Stream-origin sample-pool entry** (path + light hash, no PCM) selected on the SRC page via the standard sample picker (`sample_id`) — so it participates in the pool like any sampler, without the audio ever entering RAM. Assign it by dropping a file on a focused Stream track (registers the reference and points the picker at it), or via the sample-pool manager's **Stream…** button (the Load button relabels on a Stream track). Sample pickers are **type-filtered** (9.18): a Stream track's picker offers only Stream-origin entries, and the PCM players (Sample/Slice/Stretch) never list Stream entries — so a disk-stream can't be mis-picked into a sampler as silence, and vice versa. Sample slot is `sample_id` on the SRC page (slot 0). A trig plays from `start`; note-off stops (the track gate governs duration); a short (~5 ms) anti-click gate fades note-on, note-off and end-of-file so a hard-started disk stream doesn't pop (pair it with a single loop-length trig — retriggering every step restarts the stream). Rate-based (no time-stretch); tempo-tracking long-form is a later milestone. |
| `ToneMachine` | TONE | **General MIDI** — 128 named instruments plus drum kits, from a bundled SoundFont bank, so the box reaches genres the Elektron-lineage catalogue cannot (orchestral, jazz combo, pop-band sketching). The brief is a **cheap home keyboard with a questionable GM bank, and the character is the point** — preset quality is not a bug report. **Picking an instrument is two presses:** bare **hold(SRC)** opens the picker on the 16 GM families, press one to drill in, press one of its 8 programs to choose (**BACK** sits top-left) — 128 instruments with no paging, because GM's 16x8 shape and the 16-cell grid agree exactly. (Re-hold `SRC` to close; `Track + hold(SRC)` still picks the *machine*.) `Program` is also SRC slot 0, a stepped param carrying the GM names, so the encoder browses instruments too and it **P-Locks like any other** — a step can change the instrument mid-pattern, and **holding a step before you open the picker P-Locks the instrument onto that step** — the same hold-first rule every modal picker follows, with no gesture of its own. (A physically held step blocks its *own* cell — you cannot press a key that is already down — so to reach that one, **latch** the step first: hold it, tap `Func`, let go. The step stays the operand hands-free and every cell is reachable; `Func` double-tap releases it.) `Kit` (also SRC) swaps the melodic set for a drum kit, on **any** track — there is no fixed drum channel. FILTER is brightness/resonance, AMP is attack/release, MOD is mod-wheel and portamento; level and pan stay on Lockstep's own channel strip rather than being duplicated here, and there is no FX page because the engine's internal reverb/chorus are off (the track's own FX section is where reverb belongs). Unique in the catalogue: **one engine is shared by every Tone track**, with MIDI channel N rendering into audio group N and N being the track index — so sixteen Tone tracks cost one bank in RAM (~30 MB, fixed) and share one voice pool, which means a busy pad track can steal voices from a drum track exactly as a hardware GM module would. The bank loads in the background on first use, so the first Tone track is silent for about half a second. |
| `StretchMachine` | STCH | The **Flex** analog: plays a pool buffer with **independent pitch and tempo** (vs the rate-based Sample where pitch = speed). `pitch` (±24 st) transposes without changing duration; `timestretch` `{Off / Tempo}` — **Tempo** stretches the buffer to the project tempo using its stamped bar-length, so a captured loop stays in time as the BPM changes (live-tracked, WSOLA). Point its `sample_id` at any pool slot — including a Record/Loop REC slot — to play captured audio pitch-locked and tempo-true. Monophonic v1 (poly + AHDSR are later). Assigning a sample to a Stretch (or Stream) track that has **no trigs yet** auto-fits it: the track length snaps to the sample's musical loop length (when its bar-count is known) and a single trig is seeded on step 1, so the trig-gated loop plays cleanly out of the box instead of restarting every step. A track you've already sequenced is left untouched. |
| `MidiOutMachine` | M | MIDI CC / note output to external gear. Configurable destination, channel, program, 16 CC slots with user-assignable numbers and labels. |

**Per-track DSP chain (universal, 8.28).** Every audio track runs the same
post-machine signal chain regardless of machine type:

```
machine → FLTR (LP/HP/BP/Notch/OFF) → [ENVELOPE] → CHANNEL → inserts → sends
```

- **FLTR** — always present; default mode is **OFF** (bit-exact passthrough, no CPU cost).
  Shared with the machine's FILTER section key on machines that don't own one natively;
  Analog/FM/DrumSynth (which have internal filters) get a second FLTR page appended.
- **ENVELOPE** (AHDSR + gate source) — present only for machines that don't provide
  their own amplitude envelope (`SampleMachine`, `SliceMachine`, `MidiOutMachine`).
  Analog/FM/DrumSynth handle amplitude internally and bypass this block.
- **CHANNEL** (level, pan, sendA, sendB) — always present for all machines including
  Analog/FM/DrumSynth. P-locking `lockstep.amp.level` on any track audibly scales output.
  Pan is a track-level operation applied once here; machines output dual-mono and do not
  apply their own pan. (Analog's internal pan slot is inert; the CHANNEL pan is canonical.)

**Stepped (enum) parameter values.** These are the closed value sets the
Manipulation Zone shows as text instead of numbers (from each machine's
`ParamSpec.valueLabels`; exhaustive as of Phase 4):

| Machine | Parameter | Values |
|---|---|---|
| *(all audio tracks)* | FLTR mode | `LP` · `HP` · `BP` · `NO` · `OFF` |
| | FLTR slope | `12dB` · `24dB` |
| | ENV gate src | `Envelope` · `Held-open` |
| `SampleMachine` | Loop mode | `OFF` · `SUS` · `S+R` · `ALL` |
| | Retrig | `LEGATO` · `RETRIG` |
| | Vel>Amp | continuous 0–100 % (default 0 = velocity-independent; increase to scale level by note velocity) |
| `SliceMachine` | Mode | `SLICE` · `SCRUB` |
| | Slice source | `EQUAL` · `TRANS` · `SYNC` |
| | Count (`EQUAL`/`TRANS`) | slice count `1–16` |
| | Count (`SYNC`) → division | `4bar` · `2bar` · `1bar` · `1/2` · `1/4` · `1/8` · `1/16` (Count 1–7; 8–16 clamp to 1/16) |
| | Loop mode | `OFF` · `SUS` · `S+R` · `ALL` |
| | Voice | `MONO` · `POLY` |
| `FMMachine` | Voice mode | `MONO` · `POLY` |
| | Retrig | `LEGATO` · `RETRIG` |
| `AnalogMachine` | Voice mode | `MONO` · `PARA` |
| | Filter type | `LP24` · `LP12` · `HP` · `BP` |
| | Osc 1 wave | `SAW` · `TRI` · `SQR` · `SIN` |
| | Osc 2 wave | `SAW` · `TRI` · `SQR` · `SIN` · `OFF` |
| | LFO shape | `SIN` · `TRI` · `SAW` · `SQR` · `S&H` · `RND` |
| | LFO target | `CUT` · `PITCH` · `PW` · `AMP` |
| | LFO sync | `FREE` · `SYNC` |
| | Retrig | `LEGATO` · `RETRIG` |
| `DrumMachine` | Type | `KICK` · `SNARE` · `HAT` · `TOM` · `CLAP` · `COWBELL` · `CYMBAL` · `RIMSHOT` |

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

Reaching COND: `Func + TRIG` always opens it (track base with no step held).
While a step is held, a **bare `TRIG`** press also promotes to COND — the
`TRIG` key relabels COND while any step is down — so per-step conditions are a
one-hand held-step gesture, no `Func` required. `Track + TRIG` still resolves
`DIV` (the canonical chord is preserved).

The rule is identical whether the edit comes from an on-screen encoder,
a mapped MIDI CC, or a QWERTY action.

**Live P-Lock recording (motion recording).** With the transport **running**, the
sequencer **record-armed**, and **no step held**, turning a knob records that
motion into the pattern: the live value is written as a P-Lock onto every step the
playhead crosses while you keep moving. One pass round the loop records one loop;
keep turning and the next pass overwrites it — that is the escape hatch, not a
mode. Steps you never cross keep the locks they had. A step with no trig is
promoted to a **trigless trig** so the motion is actually heard (§5.13, "lock-only
trig"). The slot's name turns **record red** while it is recording, and the
recording stops on its own about a beat after you let go. Holding a step still
means what it always meant — edit *that* step — so the classic gesture is never
taken away from you.

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
shows eight at a time, in two rows of four — `kMZSlots`). The unqualified
view shows the **machine's** params; where a machine owns no params at a
section (e.g. a bare sampler under FILTER), the key falls back to the
track-level page. Holding **Track** flips the same key to the
**track-level** params only (post-machine FILTER, AMP/CHANNEL+ENV, track
FX). The two views never stack — a machine that owns FILTER no longer
appends the track FILTER as an extra page (that used to bury the track
AMP behind every machine section on deep machines like FM); reach it via
`Track+AMP`. Track-level pages read in **cyan** (the Track scope colour)
in both the MZ header and the active section-key highlight, so a track
page is never mistaken for a machine page. Holding
`Func` is the universal qualifier. Held on its own it exposes the parallel
**meta** row (COND/NOTE + the transport-globals shortcut); combined with a scope
it *promotes* that scope (`Func+Song` = the **Global** master-bus scope). Either
way, `Func` does not recolour the row a flat orange: every non-dim section key
keeps the **colour of its content's origin**, and the **func-colour border** is
layered on top purely as the "modifier held" signal. Cells with nothing to show
under `Func` dim out.

The section row is a **true underlay**. Holding a scope and pressing a section
is the *taught contract* — it opens that scope's own content. But there is **no
per-scope parameter layer**: a step's params resolve step-override-else-track-base
(OEB), and holding a scope never changes where a knob writes. So `FILTER`/`FX` are
not scope-assignable — there is one filter and one FX chain. A key with no content
at the held scope **falls through to the nearest real layer** (up or down) as a
convenience, and **each key is coloured by the scope its content truly comes
from** — so the colour teaches you where each thing lives.

| Held scope | What `+5` (TRIG) means | …`+7` (FILTER) | …`+0` (FX) |
|---|---|---|---|
| `Track` | Kit **subdivision** (labelled `DIV`; note value + flavour Straight/Dotted/Triplet) | the filter (the machine's own, or a track-DSP block if the machine leaves filter to the track) | Track inserts (via `Track`+hold; the picker) |
| `Phrase` | Phrase **length** (labelled `LEN`, per active phrase) | falls to the filter (coloured by its owner) | falls to the nearest FX chain |
| `Scene` | **TIME** sticky mode (Scene+TRIG toggle): TIME page, entry scope = Scene | falls to the filter (coloured by its owner) | falls to the master FX chain (nearest) |
| `Morph` | (dim — Morph never affects trigs) | Morph-assign FLTR | Morph-assign FX |
| `Song` | **TIME** sticky mode (Song+TRIG toggle): TIME page, entry scope = Song | falls to the filter (coloured by its owner) | **Master FX 1+2 + Send A/B** (4 units, cycled by re-press) |
| `Func+Song` (**Global**) | falls to Song **TIME** (nearest) | **TRSP** — transport globals (Global's own content, azure) | (dim — master FX is `Song+FX`) |

`Morph` is the exception: it is a bespoke crossfader scope with genuine per-pole
snapshots (`Morph`-assign), so it keeps its own assignments rather than falling
through.

Each scope's `TRIG` cell opens the parameter owned by that hierarchy level.
Two metas sit on bare `Func`: **COND** (probability, m:n, prev-dep) on
`Func+TRIG` and **NOTE** (explicit note / velocity / gate step entry) on
`Func+SRC`. The **transport globals** (output gain, sync mode, channel mode,
plus the focused track's **Scale** stage — Off/Snap/Filter pitch conform to the
key, §4.10) are the content of the **Global** scope (`Func+Song`, azure hue) and
sit on its `FILTER` key (`TRSP`); the bare **`Func+7`** chord is a shortcut to the
same page without holding `Song`. Global is *floor-only* — a shallower scope
pressing `FILTER` reaches the filter, not `TRSP`. Trig defaults remain on bare
`TRIG`. (Pre-6.5 the transport globals sat under `Song+FX`; that cell now carries
the master insert parameters.)

**FX inserts and the effect picker.** Each track has two insert slots (slot
0 / slot 1). **Tap `FX`** to navigate the insert's params in the MZ. **Hold `FX`**
(long-press) to open the effect picker — the step grid re-skins to the available
effects catalogue; press a step to load that effect into the focused slot (the
other slot's loaded effect shows a dim cross-slot hint). Re-press the active
effect to toggle bypass. Re-press `FX` (hold again) while the picker is open to
cycle the targeted insert slot (0 → 1 → 0). Hold `FX + step` momentarily to
**animate bypass** (bypass on press, restore on release). MIDI-out tracks show no
inserts. `Func+FX` navigates FX as a meta-section (same as other meta-sections).
Under **Song+FX focus**, hold `FX` targets the master units — steps 0-3 bypass
FX1, 4-7 bypass FX2, 8-11 bypass Send A, 12-15 bypass Send B — and is suppressed
when the master picker is open.

**Master bus: 2 inserts + 2 send returns.** The master bus has four FX units at
Song scope (DESIGN §32.3):

| Unit | Key | Role | Signal flow |
|---|---|---|---|
| Insert 1 | `Song+FX` (cycle 1) | Post-sum insert | track sum → Ins 1 → Ins 2 → out |
| Insert 2 | `Song+FX` (cycle 2) | Post-sum insert | (chained after Ins 1) |
| Send A | `Song+FX` (cycle 3) | Send return | accumulated send bus A → return FX → sum |
| Send B | `Song+FX` (cycle 4) | Send return | accumulated send bus B → return FX → sum |

Sends are post-fader, post-insert taps from each track. Set **Send A** / **Send B**
on **AMP page 2** (hold `AMP`, repeat to page-turn). **`Song` + hold `FX`** opens the
master picker for the currently focused unit (re-press to cycle all four slots
regardless of whether they are loaded; re-pick the active effect to toggle bypass;
the other units' loaded effects show a dim cross-slot hint). `Song+FX` tap cycles
through **loaded** units only, skipping empty ones (falls back to Insert 1 if
none are loaded); exit back to track params by pressing any bare section key.
Send return effects are typically loaded with Mix=1.0 (wet-only); insert effects
apply across the whole mix. MIDI-out tracks have no sends.

**External send (`EXT`).** In either send slot you can load **External** instead of
a return effect. Rather than processing the accumulated send bus and summing it
back into Master, it routes that send tap straight out of the plugin's dedicated
**"Send A" / "Send B" host output bus** — so the DAW can patch it into an outboard
chain or a separate track. It is offered **only** in the two send slots (never a
track or master insert) and has no parameters. When the host has not enabled the
matching Send bus (e.g. standalone, or the DAW left it disabled) the send is
**silent** and the slot reads amber "loaded-but-bypassed" — the tap is dropped,
never folded back to Master (unlike a disabled `Aux N` out route, which folds).

*DAW setup.* The "Send A/B" outputs are **separate plugin output buses**, declared
disabled-by-default. Many hosts still auto-sum every plugin output into the track's
main channel — so the send can appear **doubled** (once via its own bus, once folded
into Master). There is no portable, host-respected way for the plugin to force these
outputs muted (VST3 tags them as inactive aux buses; the CLAP wrapper reports them as
always-present non-main ports and ignores the disabled-by-default flag), so the
routing is a manual DAW step: **route each Send output to its destination (typically
PRE-fader on a send/aux track) and mute it on Lockstep's main output.** Loading an
External send shows a one-time explainer with a "don't warn again" toggle.

**Available effects:**

Some effects are **quality-tiered**: one catalogue entry presents a lean LQ face
on track inserts and a richer, oversampled HQ face when placed on the master bus
or sends. The tier is chosen by *placement* — you do not pick it. So `Reverb`,
`Delay` and `Saturation` show a single picker entry; drop them on a track for the
4-param version, on master for the 8-param oversampled version.

**Delay time: divisions by default, continuous with `Func`.** The master (HQ)
delay's `Time` is one continuous **tempo-relative** axis. A bare encoder turn
snaps it to the musical divisions — `1/16 · 1/8T · 1/8 · 1/8. · 1/4 · 1/4. · 1/2`
— and reads out by name. Hold **`Func`** while you turn and it sweeps freely
between them, reading out in milliseconds at the current tempo; the next bare turn
re-snaps to the nearest division. Either way the tape-bend glide absorbs the
change, so a sweep bends and a jump crossfades. The track (LQ) `Delay` keeps its
plain millisecond `Time` — that is the absolute, character delay, and it does not
follow the tempo.

*Track inserts (any slot):*
| Badge | Name | Key params |
|---|---|---|
| `DLY` | Delay | Time, Feedbk, Mix, LPF — Time is **tempo-relative** (1/64 … 1/2; bare turn snaps to the divisions, `Func`+turn sweeps between them). *(HQ on master: + Color, Width)* |
| `REV` | Reverb | Size, Decay, Damp, Mix *(HQ on master: + PreDly, LoCut, Mod)* |
| `DRV` | Distortion | Drive, Tone, Mix |
| `SAT` | Saturation | Drive, Tone, Mix, Output *(HQ on master: + Bias, Comp, Crisp, Low — 2× oversampled tape glue)* |
| `CHR` | Chorus | Rate, Depth, Mix, Feedbk — Rate is a **period in beats** (1/16 … 16 bars), so the chorus breathes with the tempo *(3-voice Hermite; Feedbk=0 is the classic no-feedback sound)* |
| `TLT` | Tilt EQ | Tilt (−1..+1), Gain (dB) |
| `CMP` | Compressor | Thresh, Ratio, Atk, Rel, Mkup |
| `BIT` | Bitcrush | Bits, Rate, Mix |
| `FLG` | Flanger | Rate (**period in beats**), Depth, Feedbk, Mix |
| `PHA` | Phaser | Rate (**period in beats**), Depth, Centre, Feedbk, Mix |
| `LDR` | Ladder | Cutoff, Reso, Drive, Mode (LP/BP/HP · 12/24 dB) — Moog-style self-oscillating ladder |
| `FSH` | FreqShift | Shift (±Hz), Mix, Feedbk — single-sideband (inharmonic) frequency shifter |
| `CNV` | Convolve | IR (Pool \| Bundled 1–4), PreDly, Damp, Mix — zero-latency convolution reverb |

**Time is musical time.** Every parameter that *is* a duration in the groove —
delay time, and every modulation rate (chorus / flanger / phaser, and the Analog
LFO) — is stored in **beats**, so it follows the tempo: change the BPM and the
echo still lands on the eighth, the sweep still takes a bar. Modulation rates are
a **period in beats** ("one cycle per bar"), not a frequency in Hz. A **bare turn
snaps to the musical divisions; `Func` + turn sweeps freely between them** — and a
Func-swept value is still in beats, so it still follows the tempo. What is *not*
tempo-relative, on purpose: reverb pre-delay and decay, compressor attack/release,
the phaser's centre frequency, bitcrush rate — those describe a space or a
transient, not a rhythm.

**Convolution IR workflow.** The `Convolve` effect's IR comes from either a
**bundled** starter (Room / Plate / Hall / Long-Dark, rendered from Lockstep's
own HQ reverb) or **any pool sample** (load a real-space impulse response, or use
a drum/vocal for creative smearing). To pick a pool IR: load `Convolve` on a slot,
open its FX picker, and press **Confirm** — the pool browser opens in *pick-IR*
mode; double-click a sample. The IR reference is saved with the project
(content-hashed, never the PCM). A fresh `Convolve` with no IR is inert until you
pick one (IR defaults to *Pool*); advance the IR param to reach the bundled slots.

`Saturation` is the gentle, gluey tape-style cousin of the rougher `Distortion`;
put the HQ face on a Route track or the master bus for clean bus glue.

*Master inserts + send returns only (`masterOnly`):*
| Badge | Name | Key params |
|---|---|---|
| `BUS` | Bus Compressor | Thresh, Ratio, Atk, Rel (Auto), SC HPF, Mkup, Mix |
| `UTL` | Master Utility | Tilt, Width (M/S), Trim (dB) |
| `LIM` | Limiter | Gain (drive dB), Ceiling (dB), Release (ms) |
| `EXT` | External | *(send slots only; no params)* — routes the send bus to the host "Send A/B" output |

`Limiter` is a **zero-lookahead safety limiter**: it drops onto the master with
the rest of the zero-latency chain (no PDC) and catches overs, but a hard
transient can momentarily overshoot the ceiling by a fraction of a dB. A true
lookahead brickwall maximiser is a future PDC-milestone item, not this.

*Gain staging is master-only:* tracks and buses stay linear (float headroom); the
only structural clip is a transparent soft-knee clipper at the master output that
is unity below ~−3 dBFS and only catches peaks. Machine-internal character
saturation (Analog drive, drum kick) is separate and unaffected. Every machine is
level-matched so a single note at internal level ~0.5 / track 1.0 sits near the
same reference. The **master output level** is set from the `Func+7` band
(`Master`), from the **MIXER** page (below), or by **dragging the master VU
meter** vertically (double-click resets it to 0 dB); its current value shows as a
**VOL** chip beneath the meter, and as a tick across the meter itself.

**The MIXER page.** `Track` + **hold** `AMP` opens a mixer under the eight
encoders: the levels of the eight tracks in the current bank, with the master on
the second sub-page. (A *tap* of `AMP` under `Track` still pages to that track's
AMP section — the hold is the mixer, exactly as `Track` + hold `SRC` is the
machine picker.) Mixer moves are always **track base values**, never P-Locks, even
with a step held: a mixer is where you balance the kit, not where you automate it.

Each track's VU cell carries a **level tick** showing where that track's
`lockstep.amp.level` sits. The tick is a readout, not a control — the old
click-and-drag on the VU cells is gone: it was invisible, it was mouse-only, and
the surface it implied (a mixer) now exists as a page you can reach from the
keyboard, an encoder, or a controller.

**MIDI-out tracks are mixed the same way.** A track whose machine is `MIDI Out`
sends its level as **CC7** (channel volume) on its own channel — on project load,
whenever the level changes, and again if the track is re-pointed at another port,
channel, or machine. So "turn that track down" means the same thing whether the
track ends in a machine or in a synth across the room. **CC7 has one owner:** a
MIDI-out track's `Level` param. A CC-bank slot pointed at CC7 is skipped rather
than allowed to fight it — and since `cc7` mapped to CC7 by default, this also
means a project no longer opens by telling the synth on channel 1 that its volume
is zero.

State round-trips in serializer v21 (hierarchical time-sig + tempo).

**Density overlay.** While `Func` is held, the Manipulation Zone shows a transient
**Density** band: 8 rotaries for the 8 tracks in the current bank (bank follows
focused track). **`Func+MOD`** pins sticky DENSITY mode (`Func+Song+MOD` enters it
with the master page already engaged); any nav key (↑↓←→)
pages between banks 1-8 and 9-16. While in the band (transient or sticky),
`Song`-held (encoder or on-screen drag) adjusts the master offset additively to all tracks;
the arc on each rotary shifts to show the offset and a tick marks the effective (audible) value.
`Func` double-tap (universal escape) or a foreign scope key to exit sticky mode.

Pressing **Track, Phrase, Scene, Morph, Mute, or Fill** while sticky exits the mode
before the scope's normal handler runs — so holding Track to pick a track then
releasing returns to Base, not back into Density. `Song` (master offset), nav keys
(bank flip), and the MOD section key (Amount/Mode sub-page) remain density's own
controls and do not exit sticky mode. (`FX` stays the effect picker — `hold FX`
loads a track insert, `Song + hold FX` the master FX.)

Inside sticky DENSITY mode, press the **MOD** section key to toggle between:
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
| **Trig** (1+ steps) | Copy steps (trigs + conditions + P-Locks) | Paste onto held steps | Full clear: trig off + condition reset + all P-Locks. `hold(Clear)` = P-Locks only (keep trig). `SRC+Clear` = note/velocity/gate only (keep trig + P-Locks); any other `section+Clear` = that section's P-Locks. |
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
| `Mute (Z) + step key` | Toggle **global** mute on that track (survives scene/song changes); hold Mute and tap many. **Quantized to the launch grid** (see Launch quantize, below) while playing — double-tap the step to fire now. Un-mute rejoins **in phase**. |
| `Scene (W) + Mute (Z) + step key` | Toggle **scene** mute (this track's active-mask in the current scene). Also quantized. |
| `Func (1) + Mute (Z) + step key` | **Solo** that track (additive toggle). Solo is the secondary/advanced layer of mute; `Func` is the cheapest qualifier (PRINCIPLES §15). Also quantized. |
| `Mute (Z) + Play + step key` | **Relaunch** (unmute + restart the track's pattern from step 1) if muted, or **retrigger** (phase-reset only) if already playing. Quantized to the launch grid; double-tap the step = instant. |

Mutes are non-destructive: trigs are suppressed at the output, no
note-offs are forced.

Solo is **routing-aware** (§2.5): soloing a bus keeps the tracks routed into
it audible (so you preview what feeds it), and soloing a feeder keeps its
downstream bus chain audible (so it still reaches master). Other tracks are
silenced as usual.

*Planned (not yet implemented):* a deferred **atomic** multi-mute — flag
several tracks while a qualifier is held and commit them all on release,
so a group drops in on the same beat. Its gesture is TBD: it cannot reuse
`Func + Mute` (now solo). Today, plain `Mute + step` hold-tap-many is the
immediate, one-track-at-a-time equivalent.

**Launch quantize (9.17).** Mute and unmute **arm to the shared
launch-quantize grid** — the one `LaunchQuant` value (set on `Func + 7`,
slot 4) that also governs Scene / Song / Phrase launches and the looper's
record/play edges. So "drop the drums on the bar" and "bring them back on the
1" are one gesture; double-tap the step to fire *now*. **`Mute + Play + step`**
*relaunches* a track (unmute **and** restart its pattern from step 1) or, on an
already-playing track, *retriggers* it (phase-reset only); bare unmute resumes
**in phase**. Each track can override the Set grid on `Func + 7` slot 5
(`T-LnchQ`), including a `Phrase`-end option that waits for that track's own
cycle. There is deliberately no separate per-track "stopped" state — "stopped"
is just muted. See DESIGN §4.8 / §13.4 / §16.1, PRINCIPLES §25.

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
- **Iteration (m:n)** — fire on *m* of every *n* loops, spread as evenly as
  the numbers allow (the Euclid rule). `1:4` fires once every four passes;
  `2:3` fires on two passes of every three; `4:4` always fires.
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
| `Scene + Clear` | **SYNC** — discard every track's live phrase deviation and snap back to the Scene as stored. The mirror of `Scene + Record`, which *bakes* deviations in. (To revert the Scene's saved mask/sig too, re-launch it, or walk `Func + Y` down to the Scene floor — §5.15.) |
| `Scene + Func + Confirm` (`Func+P`) | Cancel a queued Scene launch before it fires. Cancel is the verb for pending actions; it used to sit on `Scene + Clear`, which shadowed SYNC. |
| `Scene + Record` | **Commit-and-bake** (confirm-gated): for each deviated track, copy its effective phrase content into the Scene's diagonal row (`sceneIdx`), then clear the deviation. If no deviations, no-op. |
| `Func + Scene + Record` | Copy the active Scene (floor + all effective phrases) to the typed clipboard. Badge: `CPY:SCN`. |
| `Func + Scene + Play` | Paste clipboard Scene onto the active Scene (baked layout). Conflict-gated. |
| `Mute + Func + Scene + Play` | Paste **floor only** (strip deviations). The `Mute` qualifier reads as "strip the content overlay; apply floor metadata only". |
| `Song (S) + Clear (O)` | **Panic** — kill all voices immediately. |
| `Phrase (Q) + step key` | **Deviate focused track** to that phrase. Same as `Track+Phrase+step`. |
| `Track + Phrase (Q) + step key` | Deviate the focused track to that phrase. |
| `Scene + Phrase (Q) + step key` | **Deviate all tracks** to that phrase. Landing on the Scene's diagonal row (row N for Scene N) clears all deviations. |
| `Song (S) + step key` | Switch Songs (quantized) — a full reset; live deviations clear. |

### 5.15 Marks and undo (live undo)

> **Implemented** (ROADMAP 9.4, with the gestures settled by 9.37; rationale in
> DESIGN §13.6). 9.4 built the model and 9.37 made every gesture in it reachable —
> the CUJ suite found that a per-scope mark could be pushed but not popped, and that
> `Func+O` was claimed by three features at once.

There are two mechanisms, and the distinction is the whole point:

- **Marks** are *yours*. You push them with `Y`, you walk them back with `Func+Y`.
- **Undo** is the *system's*. It arms itself before anything destructive, and you
  reach it with `Func+O`.

They never interfere. A restore always takes you to **your mark** — it will never
land you on some auto-capture you didn't ask for — and an undo will never quietly
swallow work you did after it was armed.

| Gesture | Action |
|---|---|
| `Y` (SNAP) | Push a **mark** for the currently-held scope. |
| `Func + Y` (RESTORE — tap) | Pop one **mark** from the held scope's stack. |
| `Func + Y` (RESTORE — hold+release) | Jump straight to the **floor** (the saved state at last load). |
| `Func + O` (UNDO) | Revert the **last destructive operation**, wherever it happened. Hold a scope to narrow it to that scope's newest. |
| `Trig` + hold(`O`) | Clear **every P-Lock** on the held step(s), leaving the trig. |
| `Scene + O` (SYNC) | Discard live deviations — snap the tracks back to the scene as stored. |

**Scope-respecting:** the mark captures whichever modifier is held — none = Song,
`Track` = that track's Kit + phrases, `Scene` = that Scene (who plays, time-sig,
key-sig), `Phrase` = that Phrase's steps + P-Locks. Each scope has its own LIFO, bounded
only by a generous per-scope memory backstop you won't hit in normal use, and each
scope key shows a **pip with its mark count** so you can see what's there without
pressing anything.

**An empty stack does nothing.** `Func+Y` with no marks says `NOTHING TO RESTORE`
and leaves your state alone. The floor is still reachable — but only by the
deliberate hold, never by one tap too many.

**Undo knows what it touched.** You never have to tell it a scope; the operation
that armed it already knew — clear track 3 and `Func+O` reverts *that*. If you *want*
to be specific, holding a scope narrows it: `Track+Func+O` reverts the newest thing
that happened to that track, skipping newer edits elsewhere.

**The checkpoint keys read scopes, not steps.** `Y`, `Func+Y` and `Func+O` take their
scope from the held modifier (`Track` / `Phrase` / `Scene` / none = Song). A held step
or section key is ignored by them — it is an operand for the *edit* verbs, and letting
it retarget a mark would silently point the gesture somewhere you never named.

**Each scope is independent.** Restoring one scope's mark never touches another's.
Restore the whole Song, then restore a Track mark you took at a different moment, and
that track becomes exactly what you saved — a real, playable state, even if you never
snapshotted that particular combination as a unit. The tool does what you press; it
does not second-guess you with a consistency rule. (A more elaborate,
globally-consistent model was designed and deliberately set aside as too clever for
live use — DESIGN §13.6 and `docs/snapshot-undo-rejected-elaborate-model.md`.)

**A restore is undoable.** Because a snapshot overwrites live state, a restore counts
as a destructive op — so `Func+O` (Undo) reverts it, exactly as it reverts a clear or
a paste. A mis-fired restore is one press to take back; there is no separate
"unrestore" to learn. (Redo — re-applying what you undid, a restore included — is
designed but ships after the first cut.)

**RAM-only** — marks do *not* survive save/reload; the floor is re-seeded
from disk so "reload saved" always works.

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
  → scene picker; `Track + hold(SRC)` → machine picker showing machine
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
- **P-Lock clear gestures.** `Trig + hold(Clear)` clears all
  P-Locks on the held step(s), trig left intact. `Trig + (active MZ
  slot) + Clear` clears only that one slot's P-Lock. Both use the
  existing scope+verb grammar.
- **Step-driven P-Lock clear mode.** Hold Func (1), then press a
  step → step cells re-skin orange: cells map to the *packed* list
  of set P-locks only (not by raw slot index). Press a cell to stage
  it for removal; press again to cancel. Release Func to commit all
  staged removals.
- **Machine picker.** Hold Track (2) and **hold** the SRC section key —
  step cells show available machine names. Press a step to load that
  machine on the focused track (selecting closes the picker). Tapping
  SRC still pages its params; the *bare* SRC hold is the machine
  console, so the scope gate is what keeps the two apart.
- **Machine scope (9.29).** Hold Func (1) + Track (2) — Track relabels to
  MACHINE. This does not open a picker: it names the **sound** as the
  operand. Section keys page the machine's own params, and the verbs act
  on it: `Rec` = copy the sound, `Play` = paste it, `Clear` = init.

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
- **Multi-step holds.** Holding a step builds a multi-step edit context — hold
  as many steps as you like on the same track (Elektron flow) and every encoder
  turn, note key, condition, or clear acts on all of them at once. Continuous
  parameters nudge *relative* (each held step keeps its own offset, shifted by
  the amount you turn); stepped/enum parameters write the same absolute value to
  all. A bare hold no longer opens the P-Lock inspector; **long-press a single
  held step** to open it (two-plus held steps never do).
- **Latched step operands.** While one or more steps are held, **tap** `Func` to
  virtual-hold them into the edit context — the finger is freed so encoder edits,
  and taps on a step's *own* cell, land hands-free. It latches on `Func`'s key-up
  and only if `Func` qualified nothing in between, so the whole `Func + step`
  family (`Func+←/→` nudge, `Func+SRC` note-edit, the meta pages) still works on
  a single press. A single tap on any step still
  toggles its trig as normal. The latch never touches the trig (unlike the retired
  double-tap latch, which transiently flipped it — a live hazard). Apply staged
  edits and exit with a `Func` double-tap.
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
- **Transpose a phrase.** `Phrase + ↑ / ↓` shifts every authored note in the
  focused track's phrase up / down an **octave**; `Func + Phrase + ↑ / ↓` shifts by
  a **semitone**. Moves the track's base note and both trig layers together,
  clamped to 0–127; snapshots first, so it's undoable.
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
- **Analog paraphonic topology.** Analog Para-4 mode now routes chord notes to
  oscillators by slot: notes 1 & 3 → osc1+sub, notes 2 & 4 → osc2+sub.
  A single shared noise generator replaces per-voice noise generators.

### 5.18 Modal trig-grid surfaces

The step grid can be re-skinned into non-step roles. Three paths exist:

- **Per-track input modes (shipped, 3.9).** `Track + NavUp/Down` cycles
  the focused track between `PLAY ↔ CHROMATIC ↔ LEVELS`. In **CHROMATIC**
  the 16 step cells become a one-octave keyboard (NavUp/Down shift the
  octave) — this is the shipped form of the old "keyboard mode." In
  **LEVELS** the cells become quantised velocity buckets.
- **Retrig / ratchet (9.10).** Authored as field 5 "RTG" in the TRIG
  meta-band: hold a step and turn the RTG encoder. Values: 0 = off, 1 = /4,
  2 = /4T, 3 = /8, 4 = /8T, 5 = /16, 6 = /16T, 7 = /32, 8 = /32T. The rate
  is a normal P-Lock on `hasRetrig`/`retrigRate` — fires automatically during
  playback. **Live stutter removed** (NON-GOALS fence #11). `Fill+TRIG` is
  now a slicer-only binding:
  on slicer tracks `Fill+TRIG` shows the slice-point picker — each cell
  addresses a slice; pressing one auditions that slice and bakes the
  `note = sliceIdx` override onto any held steps. The TRIG key glows in Fill
  colour while Fill is held to announce the slicer overlay.
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
- **Euclidean generator (shipped, 5.9; entry rerouted 9.10).** **`3` held** → generator hub → **EUCLID** cell enters Euclidean generator mode on the **focused track** (`Phrase + Fill` entry retired). The Manipulation
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

- **Live velocity "feel" — see the Velocity overlay (§5.8, §39.10).** Per-track
  metric-weighted velocity is a *live, non-destructive* overlay reached via the
  **generator hub** (`3` held → VEL cell), computed at emit time. The earlier
  `Func + AMP` entry and `Func + Fill` bake/print **Accent velocity generator**
  have both been **retired** — the live overlay (v20) supersedes them.

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

### 5.20 Performance capture — the tape deck *(shipped)*

`Func + Song + U` (Func + Song + Rec) is the **CAPTURE** cell: a separate
recording device for the master output, not a DAW timeline export. All of
its gestures live on that one cell's timeline — **tap = gentle, double-tap =
decisive, long-press = deliberate** — so there is no mode to learn.

| State | tap | double-tap | long-press |
|---|---|---|---|
| **Idle** | Arm (record on Play; or now if already playing) | Roll immediately (tape) | Reveal Captures folder |
| **Armed** | Disarm | Roll immediately | — |
| **Recording** | Stop (let tails ring out) | Hard cut (close now) | — |
| **Just-saved** (~6 s) | Arm next take | Roll immediately again | **Discard the take** |

- **Emergent stop — no mode.** The only automatic stop is the **silence
  tail**: once you are *winding down* (you stopped/paused the transport, or
  tapped stop), the file closes after the master stays quiet for ~3 s, so
  reverb/delay tails are never clipped. Silence **never** finalises during
  active playback, so a long musical rest cannot chop a take. A drone or
  self-oscillating patch that never goes quiet is ended with a **double-tap
  hard cut**.
- **Always visible.** A compact banner floats at the right of the master
  section: `● REC m:ss` during the take, `◐ STOPPING — waiting for silence`
  while it rings out, then `✓ SAVED → ~/…/Captures/…wav`. The filename fades
  in only at arm and record-start (so it never covers the meter mid-take) and
  the **folder path persists while just-saved**, so you always learn where it
  went. Since 9.30 the capture indicator sits in the **transport band**, beside
  the Play button that starts the take — it is the highest-stakes status in the
  whole flow, and it used to live in peripheral vision on the master meter. The
  master meter is a **dB VU** (−48…0 dBFS) with a peak high-water mark and a clip
  pip, now a **vertical column beside the crossfader** (the MZ's right flank is
  the output column: sculpt → blend → level).
- **File location:** every take is its own **directory**,
  `Captures/capture-YYYYMMDD-HHMMSS/`, holding `master.wav` plus the stems —
  next to the current project (standalone) or under `~/Music/Lockstep/Captures/`
  (plugin / no project). Written **directly** to the destination, so a crash
  still leaves real partial files. 32-bit float WAV, stereo, device sample rate.
- **Tap point:** master is post master gain, DC blocker and soft-clip — exactly
  what reaches the physical outputs.
- **Exit while recording** asks first: *Stop recording & exit* (finalise) /
  *Discard recording & exit* / *Cancel*, ahead of the usual save prompt.
- **Stem export is always on.** Alongside `master.wav`, each non-empty track
  whose **Out** routes to Master is written as `track-NN.wav` (post-fader,
  post-FX). **Routing is the stem grouping:** route tracks into a Route bus and
  that bus is one stem with its feeders folded in; tracks routed to a bus or to
  Off get no separate stem. A take is a tape of a performance, so the stems are
  saved every time — you never lose them. See §2.5 (output routing & buses).

---

### 5.21 Key signatures & the brightness model *(shipped — Phase 10, except where marked)*

Lockstep has a **key signature** set at the same granularity as the time
signature (Set → Song → Scene), built on an opinionated idea: the **circle of
fifths as a bright→dark line**. Instead of picking "Dorian," you set a **root**
and a **brightness** (Lydian = brightest … Locrian = darkest); "exotic" colour
comes from a small set of **functional modifiers** (Harmonic, Melodic,
Double-harmonic, Harmonic-major, Blues, Neapolitan) that carry their character
across relative modes. Classical names show as labels; chromatic is always
available. The KEY editor is a sub-page off the TIME band.

On top of the key:

- **Scale-aware editing** *(partial — in-scale highlighting shipped; diatonic
  nav gestures planned, 10.5)* — in-scale notes highlighted in the chromatic
  layout and note editor (root emphasized); the planned remainder: nav keys
  move **diatonically** with `Func+`nav **chromatic** while a note is held.
- **Per-track scale-quantize** *(planned — 10.6)* — an opt-in, default-off
  "quantize as a MIDI effect" that snaps played/sequenced notes into the key
  without rewriting your authored steps.
- **Melodic generator** — a deterministic, seeded line generator (Generator Hub
  cell 4, **MELODY**) that *prints* editable steps in the key. Metric strength is
  the spine: onsets land on the strongest beats first, strong beats get strong
  (triad-core) notes held longer, weak beats get colour notes held short, and the
  rest a short note leaves bridges into the next stronger beat. Encoders: density,
  core (triad/penta/full), contour (rise/fall/arch/walk), octaves, leap, seed,
  and **SRC** (*Gen* generates the rhythm too; *Keep* locks onto the track's
  existing trigs and only writes pitch onto them — "fit a line to my groove").
  **P** prints, **Func+P** / escape cancels.
- **Harmonic voice-mover** — a sticky overlay (Generator Hub cell 5, **CHORD**)
  where you **move chord voices by ear** within the key and audition them, then
  print the progression to steps. It carries no chord theory: a chord is just up
  to four voices, each a rung on the diatonic ladder, so every voice stays in the
  key. The four voices (`V1` bass … `V4` top) are shown as **one chord view
  spanning four columns** (one per voice); the three rows are the **previous /
  current / next chord**, so each column reads that voice's motion and `CUR`
  scrolls the chords through the bright middle row. A bare turn steps that voice
  in-scale, **`Func`+turn** reaches a chromatic borrowed tone; turn a voice below
  its floor to drop it, turn the first empty column up to add one. **No two voices
  ever share a pitch** — an edit that would land on a note already in the chord
  skips past it to the next free one. Then `LEN` (how many chords — starts at
  **1**; growing **clones the previous chord** so motion starts from rest,
  shrinking is lossless), `CUR` (which chord you're shaping), `MOVE` (slide the
  whole chord one scale degree; **`Func`+`MOVE`** slides it chromatically by a
  semitone) and `OCT` (octave-shift it). A transpose (`MOVE`/`OCT`/`Func`+`MOVE`)
  that would push **any** voice past the ladder's range (octaves 2–7) is
  **refused outright** — the chord stays put rather than collapsing against the
  ceiling. Editing a voice / `MOVE` / `OCT` / cursor
  **re-strikes** the chord, so you can preview by slow-turning. The progression
  prints **one chord per bar** of the in-scope time signature (even-spacing
  fallback when there are more chords than bars). **P** prints, **Func+P** /
  escape cancels.

Design: PRINCIPLES §23, DESIGN §4.10 + §39.11–39.12.

---

### 5.22 The deck engine — Record, Loop, Tape *(shipped — Phase 11)*

Note the difference from §5.20 above: the **tape deck** is a capture *device*
that writes your master output to a WAV file. The **deck engine** is a
*machine* — audio you record onto a medium, inside the instrument, that you
then play, overdub, punch into, and sample from.

Record and Loop predate it (§2.5). Phase 11 made them, and the new **Tape**,
three faces of one four-sub-track deck:

| Face | What its medium is | You reach for it to… |
|---|---|---|
| **Record** | linear, overwritten each trig | grab a volatile sample to *process* |
| **Loop** | circular, layered | build a looping part live |
| **Tape** | linear, layered, on the song's timeline | record a take you can punch into |

**Every deck starts as a single stereo track.** Record and Loop behave exactly
as they do today until you go looking for the other three sub-tracks, on the
deck's console. Nothing you already know changes.

- **There is one timeline, and in a DAW it is the host's.** Winding the tape is
  dragging the host playhead; the tape and the sequencer are never two clocks.
  Standalone, Lockstep's own clock is that timeline. (PRINCIPLES §25.1.)
- **Markers are places, not cues.** While a deck records, every Scene or Song
  you launch drops a marker; you can drop them by hand too. A marker lets you
  wind back to the chorus. It never *plays* the chorus for you — Lockstep does
  not have, and will not have, an arrangement that performs itself.
- **Nothing you record is destroyed.** Overdubs are layers, a punch is a layer
  over a span, and `UNDO` pops. A take is saved by promoting it to the pool
  (per-sub-track files plus a downmix, all one "take") — never as audio buried
  inside the project file.
- **Scrub and jog.** One encoder is the jog wheel: the deck plays under your
  hand while the band keeps playing, and the transport catches up at the next
  bar when you let go. Wind (`<<`/`>>`) and jog are **standalone-only** —
  hosted, the DAW's playhead is the locate, and the cells are absent
  (PRINCIPLES §3: transport acts belong to the transport's owner).
- **A timeline strip** appears under the context inspector: bars, markers, the
  cursor, the punch region. You read it; you never click it. Everything you
  *do* lives on the 16 console cells and the encoders — the way hardware will
  have to do it too.

Design: DESIGN §40, PRINCIPLES §25.1. The standalone tape instrument that would
grow from the same engine is a concept brief only: `docs/partner-app-concept.md`.

---

### 5.23 Workflows — the capture family, end to end

Six devices touch audio capture: **Record** (grab), **Loop** (build),
**Tape** (the 4-track), **Stream** (bring long material back), the
**capture deck** (§5.20 — the archive), and the **Aux outs** (the DAW
spigot). Each is simple alone; this chapter is how they compose
(DESIGN §41 is the design-side contract).

**The defining flow: improvise a live set, leave with stems.**

1. Before the first note: `Func + Song + U`, one tap. Armed — recording
   starts on Play and runs across every transport stop until you end it.
2. Perform. Launch Scenes and Songs, play in, P-lock, morph, mute, fill.
   The take is one uninterrupted stream; stopping the transport doesn't
   end it.
3. End the set: stop the transport (or tap the cell) and let the silence
   tail close the file — or double-tap for a hard cut.
4. What you take home: `Captures/capture-<date>/` holding `master.wav`
   (exactly what the outputs played, including master FX and sends) plus
   `track-NN.wav` for every non-empty track routed to Master —
   post-fader, post-FX. **Routing is the stem grouping**: send three drum
   tracks into a Route bus and the bus is one drum stem with the feeders
   folded in. You grouped the stems by mixing the set.

**Improvise from nothing and the stems still arrive.** The stem list is
not fixed when you arm — a track you bring in halfway through the set
gets its own file, silence-padded back to the take's start, so every stem
stays sample-aligned with `master.wav` and drops straight onto a DAW
timeline. Mute, unmute, and re-route freely: files record what tracks
output. Arming tells you what you'll get (`ARMED ▸ master + 4 stems`), and
the count grows live as tracks join.

Two things to know about stems: they are **dry per track** — master
inserts and send returns live only in `master.wav`, so stems + nothing ≠
master when sends are hot (the stems are for post-processing; the master
is the performance document). And **hardware needs a return channel**: a
MIDI-out track makes no audio, so bring the synth's output into an Ext
input, put a Thru/Static/Route track on it, and *that* track is the stem.
There are **four** stereo Ext inputs, live in both hosts — four hardware
returns, four return tracks, four stems.

**Rehearsal & self-review (the Tape as practice mirror).** Put a Tape on
a track, record a run-through along the song timeline, wind back
(standalone), listen, punch a better pass over the weak bars — punch is
a layer, `UNDO` pops it. Iterate until the run feels right, then either
promote the take (it becomes pool files) or just play the set again for
the capture deck. Nothing here needs a DAW.

**The set that samples itself.** Mid-performance: the Loop grabs four
taps of what's playing; promote turns the take into pool files
(per-sub-track + downmix, linked as a take-group); load a member into
Slice or Stretch and redeploy it — re-pitched, sliced, stretched — later
in the same set. Record is the quick single grab when you don't need
layers. This is a performance skill: practice the promote-and-reload move
until it lands on a bar.

**The hybrid set (stems → Stream).** Last set's stems are this set's
material: point a Stream track at a stem WAV from a previous capture and
it plays as a long-form bed under the live tracks — disk-streamed, so
length is free. Capture the new set and the loop closes: perform →
stems → Stream → perform. The instrument eats its own output.

**In a DAW.** The host owns transport, tempo, and position; MIDI-out
tracks drive plugin instruments through host MIDI routing. For stems you
have both paths and they answer different needs:

- the **capture deck** works exactly as standalone (files, up to one stem
  per Master-routed track) — but the files start at arm time, not at the
  host's bar 1, so align them by ear or marker when you drag them in;
- the **Aux outs** (Master + Cue + 6 stereo Aux buses, all live — no host
  opt-in to hunt for)
  deliver stems as live, timeline-locked DAW tracks — route tracks or
  buses to Aux, record in the host, and skip the alignment problem, at
  the cost of the 6-bus ceiling.

**The morning after.** Every take is one directory with predictable names,
plus a **`take-sheet.txt`**: the project, the tempo and time signature,
the length, every stem it kept (named by the machine that made it), and
the **launch log** — where each Scene and Song change happened in the
take. Drag the stems in, line them up against the log, and the session
starts. Like the Tape's markers the sheet is **dumb**: places, never cues.
Nothing reads it back, and no Scene is ever fired by it.

```
Lockstep take
=============

Project     : night-set
Recorded    : 12 Jul 2026 9:10pm
Length      : 41:02.027
Sample rate : 48000 Hz
Tempo       : 128.00 BPM
Time sig    : 4/4

Files
-----
master.wav
track-01.wav  Analog
track-04.wav  Route

Launches
--------
0:16.000  Scene 2
4:48.000  Song  3
```
