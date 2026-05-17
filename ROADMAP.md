# Lockstep — Roadmap

The long-running plan. Each milestone is a coherent, shippable slice;
sub-tasks are checkboxes so the state of the project is visible on
every return to the repo. Tick items as they land. When the active
milestone changes, update **Active focus** below.

For architecture see `DESIGN.md`.

**Active focus:** M8 — State serialization (ships v0; v1 with the
Project/Bank/Pattern/Part hierarchy lands in MC).
**Last completed:** M7 — Pattern recording. All M7.1–M7.4 complete.

After M8 the roadmap pivots from "core sequencer is usable" to
"performance instrument is usable" — see new milestones MB–MH below.
The framing comes from DESIGN.md §1 + §13: Lockstep is for both
bringing existing material on stage **and** improvising new material
from a blank pool. The performance-feature milestones are written
with both workflows as equal targets.

## Locked design decisions for the roadmap

(Captured here so future-you doesn't re-litigate them.)

- **`IMachine` is a C++ base class, not a sub-plugin format.** A new
  engine is added by subclassing in-tree. No CLAP/VST3 sub-hosting
  layer, no IPC. (Wrapping arbitrary plugins is one possible machine
  someone could write later — see DESIGN.md §9 — not a planned phase.)
- **Variable parameter schema, declared per machine.** No fixed slot
  count. Each machine declares its own `ParamSpec` list. The MZ still
  shows 4 at a time and the section bar still has 6 buttons; both
  paginate within whatever the machine declares.
- **Hybrid slot identity.** Integer index at runtime, stable string id
  on disk. P-Lock and CC-mapping serialization survives slot
  reordering across machine releases.
- **MIDI buffer + ParamFrame at the machine boundary.** The sequencer
  translates trig events into MIDI note-on/off (per §4.6 of DESIGN);
  machines receive `(MidiBuffer, ParamFrame, AudioBuffer)`. External
  MIDI is mixed into the same buffer.
- **Voice topology is per-machine.** A machine declares `maxVoices()`;
  `1` triggers sequencer-managed choke (1–2 ms micro-fade), `n>1` =
  self-managed polyphony, `0` = unbounded / MIDI-out.
- **No PCM in plugin state.** Sample references use path + `xxHash32`;
  raw audio bytes never enter the DAW save payload.
- **QWERTY-first UI.** The full editing flow is reachable from the
  keyboard. The mouse is a second-class citizen.
- **Override-ELSE-Base** is the single resolution rule. No reset
  sentinel values, no per-parameter precedence flags. Applies to both
  machine ParamFrames and sequencer-scope per-step trig fields
  (note / velocity / gate / condition).
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
- **Octatrack-style hierarchy: Project / Bank / Pattern / Part.**
  Per-track machine identity lives in the Part (not the Pattern).
  Multiple Patterns in a Bank can reference the same Part so that
  "swap pattern, keep the kit" works as a single gesture. (DESIGN
  §4.7.)
- **Control-All resolution: id-primary, role-fallback.** Each
  `ParamSpec` has an optional `role` enum tag. Control-All targets
  every track whose schema exposes the same `id`; for tracks that
  don't, it falls back to the same `role`. Most slots are
  `role = none` and don't participate. (DESIGN §13.1.)
- **Canonical sections reserved + machine extensions allowed.**
  Section bar keys 3–8 carry a fixed canonical taxonomy
  (TRIG / SRC / FLTR / AMP / LFO / FX). A machine fills what
  applies, leaves the rest empty, and may declare *extension*
  sections on additional section-bar pages reached by repeated
  press of the same section key. (DESIGN §6.)
- **Post-machine FLTR + AMP, machine-opt-out.** Sequencer-side
  multi-mode SVF + AHDSR live downstream of every internal-audio
  machine. Machines that own their own filter/envelope (analog
  emulations) opt out via `hasInternalFilter()` /
  `hasInternalAmp()`. MIDI-out tracks bypass both implicitly.
  (DESIGN §14.)
- **No song timeline.** The Chain (a RAM-only queued list of
  upcoming pattern changes) is the entire song-level surface.
  (DESIGN §16.)
- **Performance grammar: scope + verb.** Hold a scope key (`Trig`,
  `Track`, `Pattern`, a section key, `Mute`, `Fill`) and press a
  verb (`Record` = copy, `Play` = paste, `Stop` = clear, `Yes`/`No`
  = checkpoint push/pop). Verbs never change meaning by scope; only
  the scope changes. (DESIGN §13.)
- **Hardware = fewer-key QWERTY, no new features.** The eventual
  hardware controller is a denser physical mapping of the same key
  layout. Anything the hardware does must already be doable from
  software QWERTY. (DESIGN §1, pillar 1.)

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

### MA — Architecture pivot: variable-schema machines + MIDI boundary  [pending]

A non-negotiable refactor that lands before the rest of M6. The legacy
48-slot `IMachine`, the fixed `std::array<float, 48>` ParamFrame, and
the trigger-int call signature were leaking engine-specific assumptions
into the sequencer. Recommendation: complete this milestone before
finishing M6.7–M6.11, since the remaining UI work would otherwise be
built against an interface we're about to throw away. M6.1–M6.6 will
need light retouch to track the new schema (variable section/page
counts, `paramSpec(i)` instead of `getParamMetadata(i)`, etc.).

- [x] **MA.1** Replace `IMachine` slot constants with a per-machine
      `ParamSpec` list: stable `id` (string), `label`, range,
      default, stepped flag, unit, owning section index. Drop the
      48-slot `kNumParamSlots`, `kNumPages`, `kParamsPerPage`
      constants. Remove `pitchSlot()` / `sampleSelectSlot()` /
      `gateSlot()` hints.
- [x] **MA.2** Make `ParamFrame` machine-sized: a `std::vector<float>`
      owned by the sequencer's resolver, sized to the machine's
      `numParams()` at machine attachment. Pass to `process()` as
      `std::span<const float>` (or equivalent).
- [x] **MA.3** Hybrid slot identity. Add an id↔index map on each
      machine. P-Lock storage stays integer-keyed at runtime; the
      serializer translates id↔index on load/save. Unknown ids on
      load are dropped with a log entry.
- [x] **MA.4** Per-machine voice topology. `IMachine::maxVoices()`
      with default `1`. `IMachine::isVoiceActive()` query. Per-track
      `VoiceChoke` array scaffolded in `LockstepProcessor` (wired in
      MA.10). `SamplerMachine` retains its internal choke for the
      current code path.
- [x] **MA.5** Replace the `process(triggerAtSample, params, buffer)`
      signature with `process(MidiBuffer events, ParamFrame params,
      AudioBuffer<float> buffer)`. Sequencer injects a note-on per
      fired trig into the per-track MidiBuffer; external MIDI is
      mixed in via `onNoteOn`/`onNoteOff` callbacks. Note-off from
      external MIDI triggers Release on a sustaining voice. Sequencer
      note-off deferred to MA.6 (gate is not yet a sequencer field).
- [x] **MA.6** Add per-track sequencer-scope trig fields:
      `defaultNote`, `defaultVelocity`, `gateLength`. Add the
      corresponding per-step optional overrides
      (`noteOverride`, `velocityOverride`, `gateOverride`). Resolver
      applies Override-ELSE-Base to each.
- [x] **MA.7** Drop `Track::noteMode` and the corresponding
      `kSlotPitch`/`kSlotSampleId` routing logic. Pitch-recording
      gesture now writes to `step.noteOverride` (sequencer-scope)
      regardless of machine. The sample-select-via-key gesture moves
      to record-arm mode and targets a per-track-configured machine
      slot (M7.4).
- [x] **MA.8** Update `SectionBar` for variable section count
      (≤6) and variable page count per section (paginate by 4).
      Disable any trailing buttons the machine doesn't use.
- [x] **MA.9** Update `ManipulationZone` to read schema from
      `paramSpec(i)` and to handle variable page counts. Re-wire the
      meta sections per the new fixed layout: COND (Shift+3),
      TRIG (Shift+4, new — note/vel/gate), TRACK (Shift+5,
      length/divider — moved from Shift+4), reserved (Shift+6–7),
      GLOBAL (Shift+8).
- [x] **MA.10** Sampler machine cleanup: remove `kSlotGate` (gate is
      now sequencer-scope), keep `pitch_offset` as a fine-tune slot,
      respond to incoming MIDI note-on by starting a voice at the
      requested pitch, respond to note-off by entering release.
      Voice retains all current DSP (interpolation, AHDSR, sample
      pool lookup); only the trigger entry point changes.
- [x] **MA.11** Verify M1–M5 features still work end-to-end: load a
      sample, sequence a 16-step pattern, P-Lock a slot, hear it
      play back through the new MIDI boundary. Standalone smoke test
      with the choke fade audibly intact on retriggers.

### M6 — QWERTY overlay + Manipulation Zone UI  [pending]

The keyboard-first editor.

- [x] **M6.1** Real `QwertyOverlay::resolve` mapping.
- [x] **M6.2** ManipulationZone: 4 live parameter widgets driven by
      machine metadata, attached to the resolved frame.
- [x] **M6.3** SectionBar: 6 section buttons (keys 3–8) with per-
      machine labels and multi-press page cycling; Shift for track meta
      sections. Fixed meta layout: COND (Shift+3), TRACK (Shift+4),
      reserved (Shift+5–7), GLOBAL (Shift+8). Replaces the flat
      12-page model.
- [x] **M6.4** Sampler parameter layout: assign concrete section labels
      (Source / Env / etc.), expose the note slot and gate slot in the
      relevant sections so they are editable via the manipulation zone.
- [x] **M6.5** COND track meta section: wire the manipulation zone to
      show `[Prob] [m:n Num] [m:n Den] [Prev-dep]` when Shift+3 is
      active. No step held → reads/writes `Track::baseCond`; Prev-dep
      dimmed. Step held → reads/writes `step.condition` via EditContext;
      Prev-dep active. Requires M4.4 (`Track::baseCond` data model).
- [x] **M6.6** TRACK and GLOBAL track meta sections: wire Shift+5
      (length, divider) and Shift+8 (output gain, sync mode) into the
      manipulation zone. (Originally landed against Shift+4 / legacy
      schema; MA.9 re-pins to Shift+5 to free Shift+4 for TRIG.)
- [x] **M6.7** TRIG track meta section (Shift+4): wire `[Note]
      [Velocity] [Gate]` plus one spare slot. No step held →
      reads/writes `Track::defaultNote` / `defaultVelocity` /
      `gateLength`. Step held → reads/writes `step.noteOverride` /
      `velocityOverride` / `gateOverride` via EditContext, with
      Override-ELSE-Base fallback to track defaults. Replaces the
      legacy "sampler gate length slot" task — gate is now
      sequencer-scope per MA.6.
- [x] **M6.8** StepGrid: 2×8 with paginate keys; trig toggle, hold
      gesture; P-lock indicators.
- [x] **M6.9** Step-state preview: pre-compute fire/skip/probabilistic
      state for every visible step at the start of each pattern loop
      (using `TrigEvaluator::deterministicPercent` and the m:n check,
      both pure functions of the current absolute counter). Render
      as cell brightness levels: full = certain fire, dim = certain
      skip, intermediate = probabilistic (scaled to the probability
      value). Propagate uncertainty through prev-dep chains.
- [x] **M6.10** Transport (Play/Stop/Rec) bound to dedicated keys.
- [x] **M6.11** Step Grid overlay display modes: Staggered (realistic key silhouette
      with row offset, key legends visible — training mode), Ortholinear (uniform
      grid, legends visible — muscle-memory mode), Clean (uniform grid, no legends —
      hardware surface mode). Mode is a persistent global preference, not project
      state; cycle button in UI chrome or right-click on the grid. Key mapping
      (`QwertyOverlay::resolve`) is identical in all three modes.

### M7 — Pattern recording  [pending]

Live capture of MIDI input into trigs and P-Locks. Depends on the
QWERTY+MZ UI (M6) for the transport indicator and step affordances.

- [x] **M7.1** Record-arm transport state, visible in the transport
      bar.
- [x] **M7.2** Note-on while recording writes a trig at the nearest
      step on the destination track (quantised to track grid).
- [x] **M7.3** CC while recording on a held step writes a P-Lock;
      otherwise updates the track base. (Same EditContext rule;
      record arm doesn't bypass it, it just makes capture sticky.)
- [x] **M7.4** "Key-as-PLock" mode: with record on, each note key
      writes a distinct P-Lock value to the held step (drum-pattern
      play-in across one track).

### M8 — State serialization with P-Locks and sample refs  [pending]

Replace the M0 minimal serializer with the full payload.

- [x] **M8.1** Sequence + PLock data serialized into the plugin state
      blob (still XML or value-tree, no binary in this milestone).
- [x] **M8.2** Sample-pool entries persisted as `{path, xxHash32}`;
      missing-file UX on load (relink dialog).
- [x] **M8.3** Real `Hash::xx32` implementation.
- [ ] **M8.4** Forward-compatible `kCurrentVersion` upgrade path with
      a guard test.
- [ ] **M8.5** CC mappings (with scope), channel mode, focus state,
      and clock/sync settings persisted alongside the sequence.

### MB — UI / Input rethink: scope-and-verb grammar  [pending]

The performance feature cluster (MD onward) needs a clear, consistent
input model before the individual gestures land. This milestone is the
non-DSP equivalent of MA: refactor the input layer so every later
performance feature lands against a stable controller protocol.

Recommendation: complete MB before any of MD–MH. The UI work would
otherwise need to be rewritten as each feature was added.

- [ ] **MB.1** Controller protocol layer. Introduce a `ControllerEvent`
      stream (button-down, button-up, encoder-delta, value-change)
      between the QWERTY/MIDI/UI sources and the rest of the editor.
      All later modifiers (`Func`, `Track`, `Pattern`, `Trig`-hold,
      section keys, `Mute`, `Fill`) emit through this stream.
      Hardware-controller integration later wires its own producer
      onto the same stream — no parallel code path.
- [ ] **MB.2** Persistent scope-button state. Track the held-set of
      scope buttons in an `EditMode` state machine: which scope
      buttons are currently held, which (if any) is the primary, and
      what target set they imply (steps, tracks, sections, patterns).
- [ ] **MB.3** Verb keys. Bind `Record` / `Play` / `Stop` / `Yes` / `No`
      to dispatch through `EditMode`: at press time, the current scope
      set determines which handler the verb invokes (copy / paste /
      clear / checkpoint).
- [ ] **MB.4** Multi-step holds. The trig grid already supports a
      single held step; extend to N held steps with deterministic
      ordering by press order. EditContext exposes the held-set, not
      just a single held index.
- [ ] **MB.5** Step Grid mode overlay. The trig grid becomes a *modal*
      surface (default = step toggle, plus chord-entered modes for
      Keyboard / Retrig / Sound Pool — see MG). Define the mode-enter /
      mode-exit chord and the mode-indicator UI now, even if the
      individual modes' behaviour lands in MG.
- [ ] **MB.6** Visual chrome for scope state. Transport bar shows
      currently-held scope buttons, current clipboard type, checkpoint
      stack depth, mute mode (Global / Pattern), Fill state, and
      queued-pattern / chain state. All states discoverable at a
      glance without entering a menu.
- [ ] **MB.7** QWERTY layout for new scopes. Pick concrete keys for
      `Func`, `Track`, `Pattern`, `Mute`, `Fill`, and the
      mode-chord keys. Keep them within reach of the bottom-two-row
      trig grid for one-handed performance. (Constraint: must remain
      mappable onto the planned reduced-key hardware layout — pillar
      1, DESIGN §1.)
- [ ] **MB.8** Documentation pass: update DESIGN §13 verb/scope tables
      with the final chosen keys; update CLAUDE.md glossary.

### MC — Project / Bank / Pattern / Part hierarchy + v1 state  [pending]

Foundational for almost every later feature. Lifts the current
single-pattern model into the Octatrack-style hierarchy described in
DESIGN.md §4.7.

- [ ] **MC.1** Data model. Introduce `Project` (owns banks, sample
      pool, CC mappings, focus, channel mode, clock); `Bank` (owns
      patterns + parts); `Pattern` (owns trigs / overrides / P-Locks /
      track meta + Part ref); `Part` (owns per-track machine identity,
      base ParamFrame, sample refs, post-machine FLTR/AMP state — ME
      adds the FLTR/AMP state, can be stubbed empty here).
- [ ] **MC.2** Resolver wiring. `StateResolver` now resolves against
      the currently-active (Pattern, Part) pair rather than a flat
      sequence. Add an active-pattern selector at sequencer level.
- [ ] **MC.3** v1 serialization. Bump `kCurrentVersion`. v1 layout
      includes banks/patterns/parts. v0 (M8 format) load path
      auto-upgrades a v0 project into one Bank with one Pattern
      referencing one Part.
- [ ] **MC.4** Pattern-switch gesture: `Pattern + <stepkey>` queues a
      pattern to start at the next grid boundary (configurable; default
      = end of longest playing track). Cancel via `Pattern + Stop`.
- [ ] **MC.5** Part sharing UI: indicate when multiple patterns share a
      Part. Provide a "fork Part" gesture so editing in one pattern
      stops affecting siblings.
- [ ] **MC.6** Chain mode (DESIGN §16). RAM-only queue of upcoming
      pattern changes appended by `Pattern + Chain + <stepkey>`.
      Loop / single-shot toggle. Interruptible by a plain
      `Pattern + <stepkey>`.
- [ ] **MC.7** Unknown-machine fallback on load: an unknown machine id
      in a Part resolves to a silent stub that preserves base params
      and trigs, with a relink/replace dialog offered.

### MD — Performance modifier cluster  [pending]

Copy/Paste/Clear, Performance Mutes, Fills, Control-All, Checkpoint
stack. All five share the scope+verb grammar landed in MB and the
hierarchy landed in MC.

- [ ] **MD.1** Clipboard typed by scope (step / section / track /
      pattern). Multi-step clipboard preserves relative offsets.
      In-memory only.
- [ ] **MD.2** Copy/Paste/Clear for **step** scope: `Trig`-hold
      (1+ steps) + Record / Play / Stop. Preserves trig defaults,
      conditions, and P-Locks. Multi-target paste replicates a
      1-step clipboard; 1-target paste unrolls an N-step clipboard.
- [ ] **MD.3** Copy/Paste/Clear for **section** scope: section-key
      + verb. Targets the section's slots across all steps on the
      focused track (or all tracks under Control-All).
- [ ] **MD.4** Copy/Paste/Clear for **track** scope.
- [ ] **MD.5** Copy/Paste/Clear for **pattern** scope.
- [ ] **MD.6** Global mutes (per-Project, per-track). Entered by
      `Func + Track`. Non-destructive: suppression happens at the
      sequencer→machine MIDI boundary after condition evaluation.
- [ ] **MD.7** Pattern mutes (per-Pattern, per-track). Entered by
      `Func + double-tap Track`. Saved with the pattern. Resolver
      `muted[i] = global.muted[i] || pattern.muted[i]`.
- [ ] **MD.8** Multi-select-on-release: holding `Func` inside either
      mute mode defers the toggle; collected track keys all toggle
      atomically on `Func` release.
- [ ] **MD.9** `Fill` momentary modifier. `TrigCondition::fillRule`
      enum: `Always` / `OnlyFill` / `NeverFill`. Resolver conjoins
      fillRule with probability and m:n. Step-state preview (§4.5)
      shows fill-only cells distinctly while Fill is held.
- [ ] **MD.10** Control-All (DESIGN §13.1). Holding `Track` (with no
      specific track selected) broadcasts the next parameter edit
      to every track whose schema matches by id (primary) or role
      (fallback). Works for both base writes and P-Lock writes.
      Visual indicator on which tracks accepted vs were skipped.
- [ ] **MD.11** Checkpoint stack (DESIGN §13.6). RAM-only LIFO of
      (Pattern, Part) snapshots, capped at 8 per pattern, oldest
      evicted on overflow. `Func + Yes` push, `Func + No` pop. Stack
      depth chip in transport bar. Cleared on project save (does not
      persist).

### ME — Canonical sections + post-machine FLTR/AMP + role tags  [pending]

The DSP and schema work that makes the canonical section bar uniform
across machine types.

- [ ] **ME.1** Add `ParamSpec::role` (closed enum). Update all existing
      machines (currently just `SamplerMachine`) to tag their slots.
- [ ] **ME.2** Section bar canonical reservation: keys 3–8 fixed to
      TRIG / SRC / FLTR / AMP / LFO / FX. Section labels declared by
      the machine must match the canonical title where one applies.
      Update `SectionBar` rendering accordingly.
- [ ] **ME.3** Extension sections: repeated press of a section key
      cycles through both canonical-pages-within-section and the
      machine's declared extension pages on that section.
- [ ] **ME.4** Per-track post-machine FLTR block: multi-mode SVF
      (LP/BP/HP/Notch), Cutoff, Resonance, Drive, Env→Cutoff. Lives
      in `Part::track[i].fltrState`.
- [ ] **ME.5** Per-track post-machine AMP block: AHDSR responding to
      sequencer-emitted note-on/off; Pan; Level. Lives in
      `Part::track[i].ampState`.
- [ ] **ME.6** Machine opt-out: `IMachine::hasInternalFilter()` /
      `hasInternalAmp()` bypass the corresponding block. Section key
      for that section is repurposed to the machine's own slots.
- [ ] **ME.7** MIDI-out tracks (MF) implicitly bypass both; FLTR/AMP
      section keys are repurposed to MIDI CC banks on those tracks.

### MF — MIDI-out machine (first-class)  [pending]

DESIGN §15. A `MidiOutMachine` peer of `SamplerMachine`. Required for
the "external gear is a first-class workflow" pillar.

- [ ] **MF.1** `MidiOutMachine` skeleton inheriting `IMachine`,
      `maxVoices() = 0`. Schema: `dest`, `channel`, `program`,
      `cc[0..15]`.
- [ ] **MF.2** Destination resolution: enumerate JUCE MIDI output
      devices (standalone) and host MIDI buses (plugin). Persist
      destination by stable id (device name or bus index).
- [ ] **MF.3** Channel + program P-locking. Channel changes within a
      pattern emit clean note-offs on the previous channel.
- [ ] **MF.4** Per-track configurable CC numbers + labels for the
      16 generic `cc[i]` slots. Optional `cc_name_table` JSON file
      per destination (e.g. Digitone, Syntakt, A4, Rytm presets).
- [ ] **MF.5** All-Notes-Off + Reset-All-Controllers on transport
      stop / pattern stop, per channel. Prevents stuck notes
      downstream.
- [ ] **MF.6** Participation in performance features: Control-All
      across MIDI-out tracks, Sound Pool entries for MIDI-out
      sounds, Fills / Mutes / Copy-Paste / Checkpoints — verify
      end-to-end that each unmodified gesture works against
      MIDI-out tracks.
- [ ] **MF.7** Hardware-targeting factory tables for at least:
      Digitakt, Digitone, Syntakt, Analog Four, Analog Rytm,
      Octatrack, Tonverk. (Tonverk CC table TBD; ship what's
      published.)

### MG — Alternate trig modes  [pending]

DESIGN §13.5. The trig grid as a modal surface.

- [ ] **MG.1** Keyboard mode: 16 trig keys → 16 chromatic semitones
      from a configurable root. EditContext rules apply (held step +
      keyboard key writes `step.noteOverride`).
- [ ] **MG.2** Retrig mode: trig keys, while held, retrigger at a
      configurable rate (1/16, 1/32, 1/48, 1/96). Record-arm captures
      the retrig rate as a P-Lock.
- [ ] **MG.3** Slice sub-mode of Retrig (sampler tracks with slice
      data): 16 trig keys → first 16 slices, played live.
- [ ] **MG.4** Sound Pool data model: Project-scope library of
      (machineId, base ParamFrame, sample/destination refs) bundles.
      CRUD UI: save current track sound to pool, recall pool entry
      to track.
- [ ] **MG.5** Sound Pool mode: trig keys page through the pool and
      live-swap the focused track's sound while held. Record-arm
      captures pool index as a `sound_id` P-Lock on the next emitted
      step (or held step).
- [ ] **MG.6** Mode-chord UX consistent with MB.5; all three modes
      exit cleanly on chord release and never destructively alter
      the authored pattern unless record-arm is engaged.

### MH — Machine catalogue expansion  [pending, staggered]

DESIGN §1 (lineage). Inheritance from `IMachine` — each is a separate
contributor-sized project. Order is a suggestion, not a dependency
chain; any of these can land independently once MF (for MIDI-out
parity) is done.

- [ ] **MH.1** FMMachine — 4-op FM, Digitone-inspired voice topology.
- [ ] **MH.2** VAMachine — virtual-analog mono/poly, Analog Four-style
      voice with paraphonic option.
- [ ] **MH.3** DrumSynthMachine — Rytm-style per-track drum
      synthesis (kick, snare, hat, tom variants).
- [ ] **MH.4** SlicerMachine — Octatrack Static/Flex-inspired
      slice-playback with playback-rate and start-point modulation.
- [ ] **MH.5** Define and version-stamp a "machine pack" file format
      so individual machines can ship and be discovered without
      bloating the core.

### MI — Scenes and crossfader  [pending]

DESIGN §17. The one continuous-axis performance control. Per-Part
scene pair, continuous lerp resolution, identical MIDI-out parity.
Lands after the performance modifier cluster so the scope+verb
grammar (MB) and the Part hierarchy (MC) are stable; lands after MH
(or in parallel with it) so the full machine catalogue is available
for scene assignment.

- [ ] **MI.1** Scene data model. `Part::sceneA` / `Part::sceneB`,
      sparse `map<(trackIdx, slotIdx) -> float>`. Serializes with the
      Part (v2 state bump if MC v1 hasn't already accommodated it).
- [ ] **MI.2** Fader runtime state. `Sequence::faderValue : float`,
      RAM-only, range `[0, 1]`. Updated by the input layer at audio
      block rate, smoothed lightly for zipper-free continuous-CC
      morphs.
- [ ] **MI.3** Resolver extension. `StateResolver` consults the
      scene pair when neither a P-Lock nor an external override is
      set for (track, slot). Continuous slots lerp; stepped slots
      snap at `f = 0.5`.
- [ ] **MI.4** Scene assignment gesture. `Scene A` / `Scene B` join
      the scope-button set: hold + encoder turn captures the current
      value into the scene's map. `Scene + Stop` on an assigned slot
      removes it. MZ renders A/B indicators and endpoint values for
      assigned slots.
- [ ] **MI.5** MIDI-out parity. Verify that scene morph drives a
      MIDI-out track's generic `cc[i]` slots smoothly; that
      `channel` and `program` slots snap at the midpoint; and that
      a snap emits an All-Notes-Off on the previous channel.
- [ ] **MI.6** Hardware-axis input. Map the physical fader (hardware
      controller) to the fader axis 1:1. Software exposes the same
      axis as a chrome slider plus an automatic CC mapping
      (user-remappable). No QWERTY mapping for the continuous
      axis — assignment scope buttons only.
- [ ] **MI.7** P-Lock dominance test: a P-locked slot bypasses the
      scene mix on that step, on both audio and MIDI-out tracks.

### M9 — Plugin-wrapper machine (deferred indefinitely)

Originally scoped as a CLAP/VST3 sub-hosting phase against the
48-slot contract. With the variable-schema `IMachine` boundary in
place (MA), wrapping arbitrary plugins reduces to writing one
specific `IMachine` subclass — a `WrapperMachine` that loads a host
plugin via `juce::AudioPluginFormatManager`, exposes its parameter
tree as the schema, and forwards MIDI/audio across.

This is now an optional contributor project, not a planned phase.
No core sequencer changes are required to support it. See DESIGN.md
§9 for the design sketch.

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
