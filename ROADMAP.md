# Lockstep — Roadmap

The long-running plan. Each milestone is a coherent, shippable slice;
sub-tasks are checkboxes so the state of the project is visible on
every return to the repo. Tick items as they land. When the active
milestone changes, update **Active focus** below.

For architecture see `DESIGN.md`.

**Active focus:** M4 — Trig conditions.
**Last completed:** M3 — P-Lock editing model.

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

### M4 — Trig conditions  [pending]

Wire up the conditional firing rules carried in `TrigCondition` since
M0.

- [ ] **M4.1** Probability evaluator (deterministic seed per pattern
      so behaviour is reproducible across plays).
- [ ] **M4.2** Iteration rules (`m:n`) with a per-track iteration
      counter that survives loops.
- [ ] **M4.3** Previous-dependency state machine.

### M5 — MIDI ingestion layer  [pending]

The full input abstraction described in DESIGN.md §5.

- [ ] **M5.1** Absolute CC with soft-takeover, per-parameter mapping.
- [ ] **M5.2** Relative CC delta arithmetic, configurable scale.
- [ ] **M5.3** EditContext interception: writes during a held step
      land in the Step Override.
- [ ] **M5.4** MIDI Learn UX (right-click a parameter → "wiggle a
      controller").

### M6 — QWERTY overlay + Manipulation Zone UI  [pending]

The keyboard-first editor.

- [ ] **M6.1** Real `QwertyOverlay::resolve` mapping.
- [ ] **M6.2** ManipulationZone: 4 live parameter widgets driven by
      machine metadata, attached to the resolved frame.
- [ ] **M6.3** PageBar: 12-page selector with Shift+number-row.
- [ ] **M6.4** StepGrid: 2×8 with paginate keys; trig toggle, hold
      gesture; lock indicators.
- [ ] **M6.5** Transport (Play/Stop/Rec) bound to dedicated keys.

### M7 — State serialization with P-Locks and sample refs  [pending]

Replace the M0 minimal serializer with the full payload.

- [ ] **M7.1** Sequence + PLock data serialized into the plugin state
      blob (still XML or value-tree, no binary in this milestone).
- [ ] **M7.2** Sample-pool entries persisted as `{path, xxHash32}`;
      missing-file UX on load (relink dialog).
- [ ] **M7.3** Real `Hash::xx32` implementation.
- [ ] **M7.4** Forward-compatible `kCurrentVersion` upgrade path with
      a guard test.

### M8 — Phase 3 sub-hosting  [pending]

Lift `IMachine` into a CLAP/VST3 sub-host.

- [ ] **M8.1** Scan an application-specific directory; enumerate
      conformant plugins (0/2 or 2/2 buses, exactly 48 parameters).
- [ ] **M8.2** Bridge `ParamFrame` ↔ host parameter tree per block.
- [ ] **M8.3** UX for assigning a Machine to a track.

### M9 — Polish, CI, beta  [pending]

- [ ] **M9.1** GitHub Actions multi-platform CI (Linux/macOS/Windows).
- [ ] **M9.2** Performance pass: voice CPU profile, choke-fade SIMD
      review, voice cap configuration.
- [ ] **M9.3** Factory patch library.
- [ ] **M9.4** Final product name (replace "Lockstep"), bundle IDs,
      icons, About box.
- [ ] **M9.5** First public beta build.

## Play-test notes

**2026-05-08 — M1.5:** 16-step pattern, kick on every step. No clicks,
stable amplitude. Choke micro-fade working. M1 complete.

**M2.3 test procedure:** Load one sample. Set track 1 length to 16, track 2
length to 7 via the length sliders in the step grid. Switch between tracks
with the T1/T2 buttons and observe the amber playhead cycling at different
rates. Phasing is also audible if both tracks share pool index 0.
