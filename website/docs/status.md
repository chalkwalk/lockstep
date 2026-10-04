---
sidebar_position: 40
title: "Implemented vs. planned"
---

# Implemented vs. planned {#implemented-vs-planned}

Lockstep is under active development. This manual describes both the
shipped behaviour and the design intent. `ROADMAP.md` is the single source of
truth for per-milestone status; this section is a reader's orientation, kept
deliberately coarse so it drifts less.

**Working today** — all of **Phases 1–3** (core sequencer, performance
grammar, the 10×4 control surface), the **Phase 4 catalogue** except
Percussion/Digital, **Phase 7** (the Set/Song/Scene/Phrase hierarchy),
**Phase 8** (hardening + FX/master-bus), most of **Phase 9**, the shipped
majority of **Phase 10** (key signatures + generators), and all of
**Phase 11** (the deck engine). Concretely:

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
  track / phrase; Clear/Delete confirm tiers with named confirms; global and
  phrase mutes; the Fill modifier; Control-All; the checkpoint stack;
  Scene/Song launch + queueing under one launch-quantize authority;
  per-track input modes (CHROMATIC / LEVELS); the held-step inspector
  (P-Lock badges, tap-to-clear, note edit, move-step).
- **Canonical sections + post-machine FILTER/AMP** with role tags; the
  first-class **MIDI-out machine** (per-track CC banks, device presets);
  machine reassignment via `Track + hold(SRC)` (the machine picker).
- **MIDI** CC ingestion (soft-takeover + scoped mappings), MIDI clock +
  sync modes; full **project serialization** (samples as `{path, hash}`
  refs).
- **Machines:** Sample (trim + loop modes + ZC-snap), Slice (SLICE / SCRUB,
  transient detection, MONO/POLY, reverse), FM (4-op, free matrix), Analog
  (dual PolyBLEP + SVF + LFO, Mono/Para-4), Drum (Rytm-style, eight voices),
  Stretch (Bungee time-stretch player), Stream (disk-stream long-form
  sampler), Route (audio router / sub-bus), Record / Loop / Tape (the three
  faces of the four-sub-track **deck engine** — §5.22: layers + undo,
  punch-as-a-layer, take-group promote, chase-locked Tape on the project
  timeline, standalone scrub/wind), MIDI Out, plus the Stub fallback. Shared
  post-machine CHANNEL/FILTER/AMP.
- The **surface-model foundation** for external controllers (6.6.5a):
  one pure `buildSurfaceModel()` the screen renders from.
- **Microtiming, Swing & Quantize** (`5.1`): per-step `microOffset` (±50% of step
  length, P-lockable via TRIG meta section); hierarchical additive swing (song-all +
  song-track + scene-all, ±50%, `effectiveSwing = clamp(sum, ±50%)`); morph-style
  qualifier editing (hold Song or Scene while TRACK meta is open to retarget the
  Swing encoder); sample-accurate look-ahead scheduler; live-record residual capture;
  `Quantize` verb (`scope + Yes` zeros microOffset — see §5.3); amber/cyan step-grid nudge ticks;
  TRACK band effective-swing readout.

- **Retrig / ratchet** (authored RTG field in TRIG band, per-step P-lock; live stutter removed 9.10) and
  **Sound Pool overlay** (`Fill+SRC` live-swap + `sound_id` P-Lock bake), plus
  the **Slice-point picker** on slicer tracks (`Fill+TRIG`).
- **Sound Bank** (5.7c): the Sound Bank overlay provides full management of the
  project sound pool — save, recall (with machine-mismatch guard), delete (with
  `sound_id` P-Lock remap), inline rename, and auto-naming. Pool is serialized
  at v16 and survives save/reload.
- **Standalone project files** (9.1): `.lockstep` XML files, shared serializer +
  upgrade chain, New/Open/Save/Save As with dirty guard, last-project auto-open.
- **Per-track FX inserts** (2 slots, `hold FX` picker, `FX+step` animate-bypass).
- **Master FX bus** (2 post-sum slots, `Song + hold FX` picker, MZ params under `Song+FX`, serializer v14).
- **Density overlay** (entered via generator hub, `3` held → DENSITY cell; sticky DENSITY mode: nav = bank 1-8/9-16; `Song`-held = master offset (encoder or drag); MOD key = Amount/Mode/Selection sub-page; any non-MOD section key or foreign scope = exit sticky mode; `Song`-alone = swing).
- **Generator hub** (`3` held ≥350 ms → momentary picker EUCLID / DENSITY / VEL; short tap = tap tempo retained).
- **In-cell gesture affordances** (9.11/9.12): each key cell shows its full gesture set in a fixed-slot layout (double-tap · tap · PRIMARY · hold · func hint) with painted vector glyphs (dot = tap, two dots = double-tap, ring = hold, amber chip = func). PRIMARY is the strongest action. `3` shows PRIMARY = GEN HUB (ring), tap slot above = TAP TEMPO. Slot content is **derived from the grammar table** — the same `resolveBinding` query that dispatches — so display and behaviour cannot silently diverge (the earlier `KeyAffordances` side-table was deleted in 9.12).
- **Context inspector** (9.11): always-on 4-region strip below the top info row. Regions: KEY (focused key + gesture list), HELD (active modifier scope + grammar note), OVERLAY (active picker or mode name + cancel hint), EDIT (held-step overrides). Each region has an idle fallback; built by `buildInspectorModel()` — unit-tested and dual-target. The **timeline strip** (Phase 11) sits below it: bars + wall-clock rulers, tape end lugs, markers, cursor — read-only.
- **The STATUS lane captions the MZ.** The lane sits directly above the Manipulation Zone, and when it has no confirm / alert / toast to show it says **where the eight knobs are about to write** — the one thing the knobs cannot say about themselves:

  | Held | Where a knob writes |
  |---|---|
  | *(nothing)* | the focused track's **base** params |
  | a step | that step's **P-Lock** override |
  | `Fill` + a step | that step's **fill** override (a second set of locks) |
  | `Fill`, no step | **nowhere** — the write is dropped, and the lane tells you |
  | `Morph` | a deviation into the **morph** layer (A/B) |
  | Control-All | **every track** with the same parameter |
  | a meta band | that band's own controls (COND, MIXER, DENSITY …) |

  It is quiet on purpose: it is always true, so it must never compete with the things that are only sometimes true — any confirm, alert or toast outranks it.
- **Hierarchical time** — per-Song/per-Scene tempo ratios and time signatures
  on the unified TIME page; **key signatures** (root + brightness + functional
  modifiers) on its KEY sub-page (§5.21).
- **Deterministic generators** off the `3`-key hub: Euclid, Density, the
  velocity overlay, the **melodic generator** (seeded, prints editable trigs,
  Keep-rhythm mode) and the **harmonic voice-mover** (in-key chord sculptor,
  one chord per bar) — §5.21.
- **Morph + crossfader** (5.2), Cue-scope **audition** + **cue balance**
  crossfade (`Func+3` / `Cue+Mute` / cue console, 6.4),
  **lock-only / one-shot / recorder trigs** (5.6), **per-take stem export**,
  the **capture tape deck** (§5.20), and the **sample pool** with content-hash
  identity, typed pickers, save-and-promote and missing-file relink (9.18).

**Planned / open** — the remaining catalogue synths (`4.6` Percussion, `4.7`
Digital — unblocked, first-party); Song/Scene **management UI** (`5.3` — names,
colours, browser, Kit recall) and the snapshot restore-semantics spec (`9.4`);
the **cue** follow-ons (`6.4` — `Cue + Scene`, `Cue + MIDI-out`; the balance
crossfade and the `6.4a` P-Lock/morph overlay tiers shipped); UI polish + preset-selection leftovers
(`5.8`); the sampling-overlay remainder (`5.4`, largely superseded by the
capture machines — being re-scoped); diatonic nav + per-track scale-quantize
(`10.5`/`10.6`) and chord placement on existing rhythm (`10.11`); external
controller surface polish (`6.6.8`); the capture-stems completion (`11.11` —
dynamic stem set, arm preview, take sheet; see §5.23); the structural passes in
flight (`9.12` dispatch migration, `9.15` unified surface invalidation, `9.14`
stage 5); the
Machine Module ABI (`6.7`, deferred until a second consumer exists); and beta
polish — CI, factory content, final name (`6.8`).

See `ROADMAP.md` for the authoritative milestone breakdown and current
status — it is the single source of truth for what ships when.

---
