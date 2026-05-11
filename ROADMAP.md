# Lockstep — Roadmap

The long-running plan. Each milestone is a coherent, shippable slice;
sub-tasks are checkboxes so the state of the project is visible on
every return to the repo. Tick items as they land. When the active
milestone changes, update **Active focus** below.

For architecture see `DESIGN.md`.

**Active focus:** M5 — MIDI ingestion layer.
**Last completed:** M4 — Trig conditions.

## Locked design decisions for the roadmap

(Captured here so future-you doesn't re-litigate them.)

- **48-slot IMachine contract is sacred.** Exactly 12 pages × 4
  parameters. The sequencer never grows engine-specific knowledge.
- **Track monophony.** Every track has one ringing voice; retrigger is
  a 1–2 ms micro-fade, never an instantaneous cut.
- **No PCM in plugin state.** Sample references use path + `xxHash32`;
  raw audio bytes never enter the DAW save payload.
- **QWERTY-first UI.** The full editing flow is reachable from the
  keyboard. The mouse is a second-class citizen.
- **Override-ELSE-Base** is the single resolution rule. No reset
  sentinel values, no per-parameter precedence flags.
- **Single input gate.** All input sources (MIDI CC, MIDI note, QWERTY,
  UI encoders, future hardware) route through `EditContext` identically.
  No source-specific paths. A held step receives P-Locks from any
  source — encoder twist, CC, or note-on — with no distinction.
- **Focus is first-class state.** Selection is one of `{Global,
  Track1..8}` and is what `SelectedTrack`-scoped CCs and the
  contextual encoders follow.
- **Auto-sync degradation.** In Auto mode, MIDI clock dropout =
  freewheel; explicit transport stop (DAW stop / MIDI Stop / MMC) =
  freeze.

## Milestones

### M0 — Skeleton + buildable empty plugin  [complete]

The shortest path from empty repo to "loads in a DAW and ticks every
block."

- [x] Root + `src/CMakeLists.txt` produce `Lockstep` in Standalone /
      VST3 / CLAP (+ AU on Apple) sharing a `lockstep_core` static lib.
- [x] All headers and stub `.cpp` files for `core/`, `machine/`, `io/`,
      `state/`, `ui/` exist and compile under strict warnings.
- [x] `IMachine` interface, `SamplerMachine` stub, `Clock`, `Sequence`,
      `Track`, `Step`, `PLock`, `StateResolver` all in place.
- [x] `processBlock` exercises the full pipeline every block: clock
      advance, per-track step resolve, machine call, output gain.
      Result is silence (no samples loaded). No NaNs, no crashes.
- [x] `DESIGN.md` and `ROADMAP.md` (this file).
- [x] Verified: standalone launches, plugin loads in Reaper/Bitwig,
      APVTS round-trips through host save/load.

### M1 — Audible sampler  [complete]

Make the sampler actually produce sound. One-shot sample playback per
trigger, AHDSR envelope, 1–2 ms choke micro-fade.

- [x] **M1.1** Sample loader: drag-and-drop / file dialog onto the
      sample pool. Decoded PCM stored in `SamplePool`, hashed with
      `xx32`.
- [x] **M1.2** Voice playback: linear interpolation, monophonic per
      track, AHDSR amplitude envelope.
- [x] **M1.3** Choke: new trigger on a busy track schedules a 1–2 ms
      micro-fade on the current voice before retrigger (no clicks).
- [x] **M1.4** Output stage: DC blocker, soft-clip safety limiter,
      parameter smoothing on `output_gain`.
- [x] **M1.5** Self-test: standalone, play a 16-step pattern with a
      kick on every step. Confirm no clicks, stable amplitude.

### M2 — Polymetric clocking + multi-track  [pending]

Promote tracks to first-class polymetric citizens.

- [x] **M2.1** Per-track step length [1..64] and divider exposed on the
      track and persisted in state.
- [x] **M2.2** Step-grid pagination for patterns > 16 steps.
- [x] **M2.3** Modulo-against-shared-position step resolution verified
      with mismatched lengths (7 vs 16 phasing test).
- [x] **M2.4** Eight tracks routable to one stereo bus (sub-bus split
      lands later).

### M3 — P-Lock editing model  [pending]

Bring the Override-ELSE-Base model to life as an editing surface.

- [x] **M3.1** Hold-step gesture: holding a step (QWERTY or MIDI)
      sets `EditContext::active = true` against that step.
- [x] **M3.2** Parameter writes routed to the correct layer based on
      the EditContext flag.
- [x] **M3.3** Visual indicator: held step + locked parameters render
      distinctly in the Step Grid.
- [x] **M3.4** "Clear lock" gesture (push-encoder while held step is
      active) removes the override for that slot.

### M4 — Trig conditions  [complete]

Wire up the conditional firing rules carried in `TrigCondition` since
M0.

- [x] **M4.1** Probability evaluator (deterministic seed per pattern
      so behaviour is reproducible across plays).
- [x] **M4.2** Iteration rules (`m:n`) with a per-track iteration
      counter that survives loops.
- [x] **M4.3** Previous-dependency state machine.
- [x] **M4.4** Track-level base condition: add `baseCond : TrigCondition`
      to `Track`. The evaluator falls through to it when a step's
      condition is trivial, mirroring the Override-ELSE-Base rule.
      Probability and m:n are the primary UI-exposed fields; prev-dep
      is present in the struct but not surfaced in the track-level UI.

### M5 — MIDI ingestion layer  [pending]

The full input abstraction described in DESIGN.md §4.3 + §5.

- [x] **M5.1** Absolute CC with soft-takeover, per-parameter mapping.
- [x] **M5.2** Relative CC delta arithmetic, configurable scale.
- [x] **M5.3** EditContext interception: writes during a held step
      land in the Step Override. Rule applies to CC, encoder, and
      QWERTY input identically.
- [x] **M5.4** MIDI Learn UX (right-click a parameter → "wiggle a
      controller"). Each mapping carries a scope:
      `{Master | Track[N] | SelectedTrack}`. Mappings project-saved.
- [x] **M5.5** Channel modes: Omni→Selected and Per-Track. Global
      setting; channels 9–16 ignored in Per-Track.
- [x] **M5.6** Four contextual encoders: configurable CC inputs that
      always drive the current focus quadrant. Focus is a first-class
      state `{Master, Track1..8}`.
- [x] **M5.7** Note-on triggers the destination track's machine
      (focus-routed in Omni, channel-routed in Per-Track).
- [x] **M5.8** Pitch recording gesture: note-on while a step is held
      (EditContext active) writes the note's MIDI pitch to the machine's
      note slot as a P-Lock on that step. No record arm required —
      same single-input-gate rule as encoder P-Locking. For
      monophonic machines, last note-on within the hold wins.
      Track::noteMode {Pitch, SampleSelect} selects whether note-on
      writes kSlotPitch (semitone offset from MIDI 60) or kSlotSampleId
      (pool index, note 60 = 0).
- [x] **M5.9** Standalone MIDI clock input drives the internal
      timeline. Sync modes (Locked / Auto) with freewheel-on-clock-
      dropout and freeze-on-transport-stop semantics.

### M6 — QWERTY overlay + Manipulation Zone UI  [pending]

The keyboard-first editor.

- [ ] **M6.1** Real `QwertyOverlay::resolve` mapping.
- [ ] **M6.2** ManipulationZone: 4 live parameter widgets driven by
      machine metadata, attached to the resolved frame.
- [ ] **M6.3** SectionBar: 6 section buttons (keys 3–8) with per-
      machine labels and multi-press page cycling; Shift for track meta
      sections. Fixed meta layout: COND (Shift+3), TRACK (Shift+4),
      reserved (Shift+5–7), GLOBAL (Shift+8). Replaces the flat
      12-page model.
- [ ] **M6.4** Sampler parameter layout: assign concrete section labels
      (Source / Env / etc.), expose the note slot and gate slot in the
      relevant sections so they are editable via the manipulation zone.
- [ ] **M6.5** COND track meta section: wire the manipulation zone to
      show `[Prob] [m:n Num] [m:n Den] [Prev-dep]` when Shift+3 is
      active. No step held → reads/writes `Track::baseCond`; Prev-dep
      dimmed. Step held → reads/writes `step.condition` via EditContext;
      Prev-dep active. Requires M4.4 (`Track::baseCond` data model).
- [ ] **M6.6** TRACK and GLOBAL track meta sections: wire Shift+4
      (length, divider, gate default) and Shift+8 (output gain, sync
      mode) into the manipulation zone.
- [ ] **M6.7** Sampler gate length slot: machine reads `kSlotGate` (ms)
      and triggers envelope release at `triggerTime + gate_samples` when
      gate > 0; gate = 0 retains current behaviour (release on retrigger
      only). P-lockable per step like any other slot.
- [ ] **M6.8** StepGrid: 2×8 with paginate keys; trig toggle, hold
      gesture; P-lock indicators.
- [ ] **M6.9** Step-state preview: pre-compute fire/skip/probabilistic
      state for every visible step at the start of each pattern loop
      (using `TrigEvaluator::deterministicPercent` and the m:n check,
      both pure functions of the current absolute counter). Render
      as cell brightness levels: full = certain fire, dim = certain
      skip, intermediate = probabilistic (scaled to the probability
      value). Propagate uncertainty through prev-dep chains.
- [ ] **M6.10** Transport (Play/Stop/Rec) bound to dedicated keys.
- [ ] **M6.11** Step Grid overlay display modes: Staggered (realistic key silhouette
      with row offset, key legends visible — training mode), Ortholinear (uniform
      grid, legends visible — muscle-memory mode), Clean (uniform grid, no legends —
      hardware surface mode). Mode is a persistent global preference, not project
      state; cycle button in UI chrome or right-click on the grid. Key mapping
      (`QwertyOverlay::resolve`) is identical in all three modes.

### M7 — Pattern recording  [pending]

Live capture of MIDI input into trigs and P-Locks. Depends on the
QWERTY+MZ UI (M6) for the transport indicator and step affordances.

- [ ] **M7.1** Record-arm transport state, visible in the transport
      bar.
- [ ] **M7.2** Note-on while recording writes a trig at the nearest
      step on the destination track (quantised to track grid).
- [ ] **M7.3** CC while recording on a held step writes a P-Lock;
      otherwise updates the track base. (Same EditContext rule;
      record arm doesn't bypass it, it just makes capture sticky.)
- [ ] **M7.4** "Key-as-PLock" mode: with record on, each note key
      writes a distinct P-Lock value to the held step (drum-pattern
      play-in across one track).

### M8 — State serialization with P-Locks and sample refs  [pending]

Replace the M0 minimal serializer with the full payload.

- [ ] **M8.1** Sequence + PLock data serialized into the plugin state
      blob (still XML or value-tree, no binary in this milestone).
- [ ] **M8.2** Sample-pool entries persisted as `{path, xxHash32}`;
      missing-file UX on load (relink dialog).
- [ ] **M8.3** Real `Hash::xx32` implementation.
- [ ] **M8.4** Forward-compatible `kCurrentVersion` upgrade path with
      a guard test.
- [ ] **M8.5** CC mappings (with scope), channel mode, focus state,
      and clock/sync settings persisted alongside the sequence.

### M9 — Phase 3 sub-hosting  [pending]

Lift `IMachine` into a CLAP/VST3 sub-host.

- [ ] **M9.1** Scan an application-specific directory; enumerate
      conformant plugins (0/2 or 2/2 buses, exactly 48 parameters).
- [ ] **M9.2** Bridge `ParamFrame` ↔ host parameter tree per block.
- [ ] **M9.3** UX for assigning a Machine to a track.

### M10 — Polish, CI, beta  [pending]

- [ ] **M10.1** GitHub Actions multi-platform CI (Linux/macOS/Windows).
- [ ] **M10.2** Performance pass: voice CPU profile, choke-fade SIMD
      review, voice cap configuration.
- [ ] **M10.3** Factory patch library.
- [ ] **M10.4** Final product name (replace "Lockstep"), bundle IDs,
      icons, About box.
- [ ] **M10.5** First public beta build.

## Play-test notes

**2026-05-08 — M1.5:** 16-step pattern, kick on every step. No clicks,
stable amplitude. Choke micro-fade working. M1 complete.

**M2.3 test procedure:** Load one sample. Set track 1 length to 16, track 2
length to 7 via the length sliders in the step grid. Switch between tracks
with the T1/T2 buttons and observe the amber playhead cycling at different
rates. Phasing is also audible if both tracks share pool index 0.
