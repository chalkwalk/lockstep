# 6.4 Cue balance — design spec

**Status:** design approved (2026-07-14 session). Authoritative model lives in
**DESIGN §31** (rewritten in this session); this doc is the spec the build plan
is drawn from and records the decisions and their rationale.

**Milestone:** ROADMAP `6.4` — Cue + monitoring. Supersedes the earlier
"additive monitor send" framing of §31.

## 1. What cue is for

Let the performer pre-listen and *prepare* music the audience cannot hear yet,
then bring it in — the DJ "cue on the headphones, mix in on the crossfader"
workflow. Governing setup: one headphone (room audio still audible). Standalone
routes cue to device ch 3–4; plugin exposes a second stereo output bus.

## 2. The model — per-track cue balance (crossfade, not a send)

Each track carries a **cue balance** `b ∈ [0,1]` that crossfades the track's
output between its normal destinations and the cue bus:

- `b = 0` → main only (audience hears it), cue silent.
- `b = 1` → cue only (performer hears it, audience does not).
- between → the fade.

**Crossfade only, no additive mode.** Route/Send contributions scale by
`(1 − b)`; cue by `b`. This subsumes the old additive send: a **capture track**
(`Out = Off`) has zero normal-route contribution, so raising `b` is pure
additive monitoring; a **live track** fades out of the audience mix into the
headphones. One axis, both jobs.

## 3. Signal flow — cue is a fan-out gain at the distribution node

Cue is applied **after per-track Level/Amp**, at the point where a track's
post-insert output (`trackBuffers_[k]`) fans out to Master/Bus/Aux/Sends/Cue.
It does **not** modify `trackBuffers_[k]` itself. Consequences (all forced by
that placement — this is the load-bearing part):

| Reader | Reads relative to the split | Effect of a cued track's `b` |
|---|---|---|
| Direct track→track tap (`input_source = Track k`) | upstream (`trackBuffers_[k]`) | **bypasses cue** — sees full signal |
| Master / Bus tap (`input_source = Master`/Bus) | downstream (the bus sum) | **reflects cue** — cued track absent |
| Route (Master/Bus/Aux) contribution | downstream | scaled `× (1 − b)` |
| Send A/B contribution | downstream | scaled `× (1 − b)` (reverb/delay leaves the audience mix; existing tail decays) |
| Cue bus | downstream | scaled `× b`, from the **post-insert** signal only (shared master send-FX are **not** in the cue path — standard PFL) |

**Master-tap reflects cue** is a deliberate, confirmed behavior: a master
resample captures *what the audience actually heard*, so a cued track is
correctly absent from it.

**Declick.** Balance changes (encoder moves and especially the quantized flip)
ramp over **~5 ms** per track to avoid clicks — the effective per-block gain is
a smoothed `b`.

## 4. Composition invariant (unchanged from §27)

Cue only ever *removes* signal from the master-reaching path; it never adds a
routing edge, and the cue bus is never an `input_source`. Therefore
`outputReachesMaster()` stays **static** (a Master-routed track counts as
reaching master regardless of live `b`, since balance can move back), the
feedback guard and topo-sort are untouched, and cueing can never invalidate a
`Master` tap or form a loop. At any `b > 0`, strictly *less* reaches master.

## 5. Cue vs. mute — orthogonal axes, one rule

Level/Amp is upstream of the split, so cue and mute are independent:

- **Level / mute** = *how present*, to everyone **including the performer's own
  ears**. `Level = 0`/mute = gone from audience **and** cue.
- **Cue** = *where it goes* — audience vs. headphones. Never silences to
  everyone.

> **The rule:** **Mute** when a part should be *gone*. **Cue** when it should be
> *gone for the audience but still monitored by you*.

Subtle edge: **fluid mute** (§17.2) morphs `Level → 0` (fade to silence) where
cue fades to the headphones — same audience result, opposite performer result;
the rule ("do you still need to hear it?") resolves it. Document this edge
wherever the two appear.

## 6. Recall — a performance overlay, not scene state

Cue balance is a **live performance overlay**: per-track, **persistent across
scene launches** (recall like *global* mute, not scene mute), serialized with
the project, default `0`. Auditioning is performance, not arrangement — a scene
launch must never re-expose tracks the performer cued to edit.

**Morph participation is explicit, not automatic.** Morph (§17) morphs
scene/base params; cue balance is *not* a scene param, so the Morph endpoint
capture/interpolation must be **wired to include the cue-overlay values** as
performance values. Once wired, the DJ transition falls out: snapshot A =
current, snapshot B = next-section-cued; ride the fader A→B to bring B in and
push A out with the master fader untouched. **This wiring is a real task, not
free** — it may be split to a fast-follow if it risks the core.

## 7. Surfaces

One control, four reaches:

1. **Direct — `Cue + focused track`.** On the existing `Cue` scope (`Func+3`,
   §21): a gesture on the focused track toggles/nudges *its* balance. Coexists
   with the existing momentary audition (`Cue+step`, `Cue`-held) — those are
   unchanged.
2. **Cue overlay (two pages), opened by one gesture:**
   - **step-grid page** — 16 step keys = 16 tracks; tap arms a track's
     **quantized flip** in/out.
   - **param page** — encoders adjust each track's continuous balance.
   - Nav pages between them.
3. **AMP/CHANNEL page** — the per-track balance as an in-context param, edited
   like Level.
4. **Morph** — per §6.

**Quantized flip.** Arming tracks on the step-grid page and firing brings them
in (or out) together on the next quantum (reuse the launch-quantize authority,
§16 / 9.17), each with the ~5 ms declick. This is "N cued tracks in at once"
without setting up a morph.

## 8. Outputs & chrome

Standalone → cue on device ch 3–4. Plugin → the declared Cue output bus (host
bus index 1, already declared and fed by the metronome). Chrome shows "cue
unavailable" when the host has not wired the cue output. Cued-track cells carry
a cue indicator on the surface (a `CellState`/decoration channel, add-only).

## 9. Serialization

New per-track persistent `cueBalance` (float, default 0), round-tripped with
project state; a serializer version bump if it needs a new field (confirm during
planning whether it can ride existing per-track channel state like `out`).
Old files load with all balances 0 (no behavior change). Round-trip test
required (write → serialise → deserialise → value survives).

## 10. Build scope & deferrals

**In this milestone (6.4):**
- `cueBalance` per-track overlay state + serialization + round-trip test.
- Distribution-stage crossfade: `(1 − b)` on route (`sumRoutedToMaster`,
  `depositToBus`, `depositRoutedToAux`) and sends (`sendBusBufs_` deposits);
  `b` into the Cue bus (new `depositRoutedToCue`, mirroring
  `depositRoutedToAux` but additive into bus 1); ~5 ms smoothing.
- Tap semantics verified: direct track taps bypass (they read
  `trackBuffers_[k]`, already upstream — assert), master tap reflects (assert).
- `outputReachesMaster()` unchanged (assert static under any `b`).
- Direct `Cue + focused track` gesture.
- Cue overlay: two pages (step-grid quantized flip + encoder param page);
  quantized flip via launch-quantize with declick.
- AMP/CHANNEL page balance param.
- Morph participation (may split to fast-follow — see §6).
- Surface cue indicator.
- Unit tests per CLAUDE.md new-modality mandate: **resolution** (balance reaches
  the distribution gain), **scope routing** (gesture under Cue lands on the
  right track), **round-trip** (serialize), plus audio-path tests
  (`setRateAndBufferSizeDetails(44100, 512)` first) proving the crossfade,
  send-fade, tap-bypass, and master-tap-reflect behaviors.

**Deferred to follow-on ROADMAP items (not this build):**
- `Cue + Scene` double-resolved scene pre-listen.
- `Cue + MIDI-out` copy to a cue MIDI destination.

## 11. Code seams (for the plan)

- Per-track output buffers: `trackBuffers_[]`; route sums `sumRoutedToMaster` /
  `depositToBus` / `depositRoutedToAux` (PluginProcessor.cpp ~1403–1457); send
  deposits `sendBusBufs_[0/1].addFrom(trackBuffers_[i]…)` (~1174/1181). JUCE
  `addFrom(..., gain)` carries the `(1 − b)` / `b` factor with no extra buffers.
- Cue bus: `getBus(false, 1)`; metronome writer `processMetronome` (proof the
  bus is live).
- `outputReachesMaster()` (PluginProcessor.cpp ~1289) — leave static.
- Cue scope: `enterCueScope`/`exitCueScope`, `PrimaryScope::Cue`,
  `auditionStepDown` (PluginEditor.cpp ~4302). New gesture must not disturb the
  momentary audition path.
- Launch quantize: 9.17 `LaunchQuant` authority.
- Morph: §17 runtime (`morphFader`, fluid-mute `AMP Level` precedent).
- Overlay: `Overlay` enum in `UiState.h` + `kOverlays` descriptor in
  `ModeReducer.cpp` (add one enum value + one descriptor row + a test).
- Surface indicator: `CellState`/decoration (add-only) in `SurfaceModel`.
