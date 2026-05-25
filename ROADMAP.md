# Lockstep — Roadmap

The long-running plan. Each milestone is a coherent, shippable slice;
sub-tasks are checkboxes so the state of the project is visible on
every return to the repo. Tick items as they land. When the active
milestone changes, update **Active focus** below.

For architecture see `DESIGN.md`. For the guiding principles every
feature must satisfy, see `PRINCIPLES.md`. **Before adding a
milestone here, confirm it is expressible within those principles
and within the existing scope+verb grammar (DESIGN §13).**

**Active focus:** MH.3 — DrumSynthMachine (Rytm-style per-track drum synthesis, kick/snare/hat/tom variants). Authoring against the MHY contract (scope-indexed sections, MOD canonical, Part scope, Yes/Rec/Play/Stop/No verbs). **Paused** while MHZ.1 → MHZ.3 (keyboard / UI revamp) land — MH.3 resumes against the improved surface.
**Last completed:** MHY — Surface revamp complete: modifier cluster remapped (Func/Track|Pattern/Part|Scene/Master|Mute/Fill), LFO→MOD, ParamSpec.variant, right-utility row Yes/Rec/Play/Stop/No, scope-section matrix scaffolding + reactive chrome, tap tempo implemented.
**Next up:** MHZ — Keyboard / UI revamp (MHZ.1 chrome + label grammar → MHZ.2 contextual modes + top bar + MZ streamline → MHZ.3 step-hold note capture + P-Lock clear + step-driven edit mode). Lands before MH.3 resumes.

After M8 the roadmap pivots from "core sequencer is usable" to
"performance instrument is usable" — see milestones MB–MI below
for the core performance feature surface, then MJ–MP for the
depth pass (pattern/part management, sampler depth, sequencer
refinement, 16-levels, sampling+resampling, audition, UI polish).
The framing comes from DESIGN.md §1 + §13 and `PRINCIPLES.md`:
Lockstep is for both bringing existing material on stage **and**
improvising new material from a blank pool. Every milestone is
written with both workflows as equal targets, and every milestone
must satisfy the ten principles.

## Locked design decisions for the roadmap

(Captured here so future-you doesn't re-litigate them.)

- **`IMachine` is a C++ base class, not a sub-plugin format.** A new
  engine is added by subclassing in-tree. No CLAP/VST3 sub-hosting
  layer, no IPC. (Wrapping arbitrary plugins is one possible machine
  someone could write later — see DESIGN.md §9 — not a planned phase.)
- **Variable parameter schema, declared per machine.** No fixed slot
  count. Each machine declares its own `ParamSpec` list. The MZ shows
  `kMZSlots` at a time (8 as of MHX, §33) and the section bar still has
  6 buttons; both paginate within whatever the machine declares.
- **Hybrid slot identity.** Integer index at runtime, stable string id
  on disk. P-Lock and CC-mapping serialization survives slot
  reordering across machine releases.
- **MIDI buffer + ParamFrame at the machine boundary.** The sequencer
  translates trig events into MIDI note-on/off (per §4.6 of DESIGN);
  machines receive `(MidiBuffer, ParamFrame, AudioBuffer)`. External
  MIDI is mixed into the same buffer.
- **Voice topology is per-machine and per-block.** A machine returns
  `currentVoices(baseParams)` as one of `Polyphony::V0..V4`, pulled by
  the sequencer at each trig so mode flips (VA Mono↔Para, FM Mono↔Poly)
  take effect immediately. `V1` triggers sequencer-managed choke,
  `V2..V4` = self-managed polyphony, `V0` = unbounded / MIDI-out.
  When a chord step holds more notes than the live voice count, the
  per-track `NoteSelection` (`TopBias` default, or `BottomBias`) picks
  endpoints first and spreads the remaining voices.
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
  Section-bar keys 5–0 (row 0) carry a fixed canonical taxonomy
  (TRIG / SRC / FLTR / AMP / MOD / FX — note `LFO`→`MOD` as of MHY).
  A machine fills what applies, leaves the rest empty, and may
  declare *extension* sections on additional section-bar pages
  reached by repeated press of the same section key. (DESIGN §6.)
- **Sections are a scope-indexed matrix (MHY).** The six section
  keys mean different things under different scope modifiers held:
  no-scope = machine's primary sections; `Func+section` = machine
  secondary; `Track/Pattern/Part/Scene/Master+section` = foundation-
  owned cells (post-machine FLTR/AMP, IEffect inserts, scene
  assigns, master FX, etc.). `Func` is the universal qualifier —
  composes with any other scope to give the secondary variant.
  (DESIGN §6.)
- **Machine schema snaps to canonical sections by meaning.** A
  machine may relabel a section under its no-scope pages, but a
  filter-like control belongs under FLTR (key 7) even if the label
  reads `MORPH`; a modulation matrix belongs under MOD (key 9). The
  discipline keeps cross-machine workflows (hold FLTR + COPY,
  Control-All by role) working uniformly.
- **Post-machine FLTR + AMP, machine-opt-out.** Sequencer-side
  multi-mode SVF + AHDSR live downstream of every internal-audio
  machine. Machines that own their own filter/envelope (analog
  emulations) opt out via `hasInternalFilter()` /
  `hasInternalAmp()`. MIDI-out tracks bypass both implicitly.
  (DESIGN §14.)
- **No song timeline.** The Chain (a RAM-only queued list of
  upcoming pattern changes) is the entire song-level surface.
  (DESIGN §16.)
- **Performance grammar: scope + verb.** Hold a scope key (`Func`,
  `Track`, `Pattern`, `Part`, `Scene`, `Master`, `Mute`, `Fill`, a
  section key, or a held trig) and press a verb (`Record` = copy,
  `Play` = paste, `Stop` = clear, `Yes`/`No` = checkpoint push/pop).
  Verbs never change meaning by scope; only the scope changes.
  (DESIGN §13.) Cluster set finalised in MHY: section-scopes
  `{Func, Track, Pattern, Part, Scene, Master}` plus performance
  specialists `{Mute, Fill}`; `Cue` is reserved as a scope but not
  bound to a cluster key until MU.
- **Hardware = fewer-key QWERTY, no new features.** The eventual
  hardware controller is a denser physical mapping of the same key
  layout. Anything the hardware does must already be doable from
  software QWERTY. (DESIGN §1, pillar 1.)
- **No "design mode" vs "performance mode".** The gestures that
  manipulate pre-authored material are the same gestures that
  improvise new material from a blank pool. Workflows that imply
  a mode switch are redesigned to fit the live grammar.
  (`PRINCIPLES.md` §3.)
- **No hidden randomness in the editing surface.** Trig conditions
  (probability, m:n) are the only sanctioned RNG; audio-side
  smoothing/voice-steal randomness is fine. Editing actions are
  deterministic. (`PRINCIPLES.md` §9.)
- **Beginner mode = more chrome, never less grammar.** Granular
  feedback toggles add annotation; they never remove or simplify
  the underlying gestures. Replaces the earlier "3 UI modes"
  sketch. (`PRINCIPLES.md` §1, §8; DESIGN §6.2.)
- **16-levels eligibility = role-tagged subset.** The trig-grid
  16-levels mode targets a closed eligible subset of role tags
  (`velocity`, `pitch`, `cutoff`, `resonance`, `attack`, `decay`,
  …). Machines opt in by tagging slots with the canonical roles.
  (DESIGN §20.)
- **Microtiming = per-step P-lockable offset, ±50% of step.**
  Stored in `Step::microOffset` as a signed fraction. Realtime
  record writes it; the `Quantize` verb zeros offsets in scope.
  (DESIGN §19.)
- **Sampling input = plugin audio input only.** No system mic /
  device input. Resampling reuses the same flow with a track or
  master tap as source. (DESIGN §22.)
- **Sample-name generator = 4 curated + 1 hash-derived.** On
  capture the UI offers five candidate names; user picks or
  refines. Curated wordlists are bundled with the build.
  (DESIGN §22.)
- **Coarse-adjust = `Func` + encoder.** Coarse step is derived from
  the slot's unit hint by default, overridable per `ParamSpec`.
  (DESIGN §25.)
- **MZ size is a single constant.** The "four slots per page" value
  appears exactly once in code. Growing it to 8 (planned hardware
  encoder count) is a one-line change. (DESIGN §26.)
- **State-colour taxonomy is canonical; specific colours are not.**
  The set of distinguishable UI states is reserved up front so
  hardware LEDs mirror software for free; specific RGB values are a
  later visual pass. (DESIGN §24.)
- **Audio routing = explicit source-select, topo-sorted, no cycles.**
  An input-consuming machine declares an `input_source`
  (`None | External | Track N | Master`); the engine topologically
  sorts track processing each block and **refuses cyclic routing at
  assignment time**. Feedback loops are a deliberate non-feature.
  `input_source = Master` is the one sanctioned prior-block tap.
  (DESIGN §27.)
- **Recorder buffers = volatile entries in the unified sample pool.**
  RAM-only, not serialised, badged `REC`; any audio-source slot can
  point at a volatile buffer or a persistent sample identically. The
  §22 naming flow doubles as freeze-to-disk promotion. (DESIGN §28.)
- **Overwrite in Recorder, overdub in Looper.** Recorder trigs
  overwrite a buffer (stateless resampling); overdub looping is a
  state machine encapsulated in a dedicated Looper machine (the
  Octatrack pickup equivalent), driven by existing verbs. No overdub
  state on the trig path. (DESIGN §29.)
- **Three special trig types.** Trigless/lock-only (`Func+step`,
  apply locks without retrigger), one-shot (fire once, RAM arm state,
  auto-rearm on (re)entry + Func arm-all/disarm-all), recorder trig
  (capture `rec_length`/RLEN into a buffer). One-shot composes with
  recorder trig. (DESIGN §30.)
- **Scenes morph parameters only, never trigs.** The fader lerps
  continuous slots / snaps stepped slots; it never rewrites the trig
  grid. "Fluid mute" = scene-assigning the AMP `Level` slot, with a
  `Scene+Mute` convenience gesture. (DESIGN §17.2, §17.3.)
- **Morph-aware editing (PolyBrute-style), 1:1 normalised.** A bare
  encoder turn on an *already-assigned* scene slot writes through the
  fader position, normalised so the heard value tracks the gesture
  1:1. Coexists with (does not replace) the explicit `Scene A/B`
  assignment gesture; no auto-assign at endpoints. (DESIGN §17.6.)
- **Cue = additive monitor send, never solo.** `Cue` scope: `Cue+track`
  adds an audio track to the cue bus (stays in main mix, post-FLTR/AMP
  tap); `Cue+Scene` previews a scene on the cue bus without moving the
  fader; `Cue+(MIDI track)` copies events to a cue MIDI destination.
  No cue output configured = no-op, never reinterpreted as solo.
  (DESIGN §31.)
- **AMP gate source `{Envelope | Held-open}`.** General per-track AMP
  property. `Envelope` = trig-gated (default). `Held-open` keeps the
  amplitude stage continuously open — the basis of continuous Thru and
  of drones. Subsumes the Octatrack Thru-vs-Neighbour split: Thru is
  the one input-consuming machine, source chosen by `input_source`,
  gated-vs-open chosen by AMP gate source. (DESIGN §14, §29.)
- **Foundation-owned effects: `IEffect`, 2 inserts/track + 2 master.**
  Effects reuse the `ParamSpec`/`role`/P-Lock infrastructure and fill
  the canonical FX section (key 8). Per-track inserts are fixed,
  post-AMP, **Part-scope**. Two master effects each switch between
  **Insert** and **Send** mode (per-track Send A/B levels in the AMP
  output mix). Master FX state is **Project-scope (provisional)**.
  Effect identity is a stable string id with stub fallback. (DESIGN
  §32.)

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
- [x] **MA.4** Per-machine voice topology. `IMachine::currentVoices()`
      returns a `Polyphony` enum (V0..V4) pulled by the sequencer per
      trig, so VA's Mono↔Para and FM's Mono↔Poly toggles are honored
      live. The chord-clamp picks notes using the per-track
      `NoteSelection` (top/bottom-bias spread). `isVoiceActive()` is
      kept as a per-machine helper. Per-track `VoiceChoke` array
      scaffolded in `LockstepProcessor` (wired in MA.10).
      `SamplerMachine` retains its internal choke.
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
- [x] **M8.4** Forward-compatible `kCurrentVersion` upgrade path with
      a guard test.
- [x] **M8.5** CC mappings (with scope), channel mode, focus state,
      and clock/sync settings persisted alongside the sequence.

### MB — UI / Input rethink: scope-and-verb grammar  [complete]

The performance feature cluster (MD onward) needs a clear, consistent
input model before the individual gestures land. This milestone is the
non-DSP equivalent of MA: refactor the input layer so every later
performance feature lands against a stable controller protocol.

Recommendation: complete MB before any of MD–MH. The UI work would
otherwise need to be rewritten as each feature was added.

- [x] **MB.1** Controller protocol layer. Introduce a `ControllerEvent`
      stream (button-down, button-up, encoder-delta, value-change)
      between the QWERTY/MIDI/UI sources and the rest of the editor.
      All later modifiers (`Func`, `Track`, `Pattern`, `Trig`-hold,
      section keys, `Mute`, `Fill`) emit through this stream.
      Hardware-controller integration later wires its own producer
      onto the same stream — no parallel code path.
- [x] **MB.2** Persistent scope-button state. Track the held-set of
      scope buttons in an `EditMode` state machine: which scope
      buttons are currently held, which (if any) is the primary, and
      what target set they imply (steps, tracks, sections, patterns).
- [x] **MB.3** Verb keys. Bind `Record` / `Play` / `Stop` / `Yes` / `No`
      to dispatch through `EditMode`: at press time, the current scope
      set determines which handler the verb invokes (copy / paste /
      clear / checkpoint).
- [x] **MB.4** Multi-step holds. The trig grid already supports a
      single held step; extend to N held steps with deterministic
      ordering by press order. EditContext exposes the held-set, not
      just a single held index.
- [x] **MB.5** Step Grid mode overlay. The trig grid becomes a *modal*
      surface (default = step toggle, plus chord-entered modes for
      Keyboard / Retrig / Sound Pool — see MG). Define the mode-enter /
      mode-exit chord and the mode-indicator UI now, even if the
      individual modes' behaviour lands in MG.
- [x] **MB.6** Visual chrome for scope state. Transport bar shows
      currently-held scope buttons, current clipboard type, checkpoint
      stack depth, mute mode (Global / Pattern), Fill state, and
      queued-pattern / chain state. All states discoverable at a
      glance without entering a menu.
- [x] **MB.7** QWERTY layout for new scopes. Pick concrete keys for
      `Func`, `Track`, `Pattern`, `Mute`, `Fill`, and the
      mode-chord keys. Keep them within reach of the bottom-two-row
      trig grid for one-handed performance. (Constraint: must remain
      mappable onto the planned reduced-key hardware layout — pillar
      1, DESIGN §1.)
- [x] **MB.8** Documentation pass: update DESIGN §13 verb/scope tables
      with the final chosen keys; update CLAUDE.md glossary.

### MC — Project / Bank / Pattern / Part hierarchy + v1 state  [complete]

Foundational for almost every later feature. Lifts the current
single-pattern model into the Octatrack-style hierarchy described in
DESIGN.md §4.7.

- [x] **MC.1** Data model. Introduce `Project` (owns banks, sample
      pool, CC mappings, focus, channel mode, clock); `Bank` (owns
      patterns + parts); `Pattern` (owns trigs / overrides / P-Locks /
      track meta + Part ref); `Part` (owns per-track machine identity,
      base ParamFrame, sample refs, post-machine FLTR/AMP state — ME
      adds the FLTR/AMP state, can be stubbed empty here).
- [x] **MC.2** Resolver wiring. `StateResolver` now resolves against
      the currently-active (Pattern, Part) pair rather than a flat
      sequence. Add an active-pattern selector at sequencer level.
- [x] **MC.3** v2 serialization. Bumped `kCurrentVersion` to 2. v2 layout
      includes banks/patterns/parts. v1 (M8 format) load path
      auto-upgrades a v1 project into one Bank with one Pattern
      referencing one Part.
- [x] **MC.4** Pattern-switch gesture: `Func+2` (PatternScope, held) +
      `<stepkey>` queues a pattern to start at the next grid boundary
      (end of longest running track). Cancel via `PatternScope + Stop`.
      Releasing PatternScope without pressing a step fires Snapshot
      (backward-compat). Pending switch shown as QUE:B.P chrome badge.
- [x] **MC.5** Part sharing UI: SHR:N chrome badge when N patterns share
      the active Part. PatternScope + VerbRecord forks the Part (copies
      it into the first free Part slot so edits no longer affect
      siblings). No-op if already unshared or all 4 Part slots used.
- [x] **MC.6** Chain mode (DESIGN §16). RAM-only queue. First
      PatternScope + step queues a direct switch (clears chain);
      subsequent step presses while still holding PatternScope append
      to the chain. PatternScope + NavRight (R) toggles loop/single-shot.
      Chain self-advances via callAsync each time a queued switch fires.
      Interruptible by a new PatternScope + step (clears chain).
      CHN:N (loop) / CHN1:N (single-shot) chrome badge.
- [x] **MC.7** Unknown-machine fallback on load: unknown machine IDs in
      a Part resolve to StubMachine (silent, numParams=0, data preserved).
      setStateInformation reinstalls machines from the active Part's
      machineIds, falling back to StubMachine for unrecognised IDs.
      Relink/replace dialog is a stub (offered at ME when machine
      catalogue expands).

### MD — Performance modifier cluster  [complete]

Copy/Paste/Clear, Performance Mutes, Fills, Control-All, Checkpoint
stack. All five share the scope+verb grammar landed in MB and the
hierarchy landed in MC.

- [x] **MD.1** Clipboard typed by scope (step / section / track /
      pattern). Multi-step clipboard preserves relative offsets.
      In-memory only. (`Clipboard.h`)
- [x] **MD.2** Copy/Paste/Clear for **step** scope: `Trig`-hold
      (1+ steps) + Record / Play / Stop. Preserves trig defaults,
      conditions, and P-Locks. Multi-step copy: all held steps with
      relative offsets; paste wraps within track length.
- [x] **MD.3** Copy/Paste/Clear for **section** scope: section-key
      held + verb. Targets the section's slots (by `sectionIndex`
      match) across all steps on the focused track. Section key held
      sets `sectionHeld_` scope so verbs dispatch to `PS::Section`.
- [x] **MD.4** Copy/Paste/Clear for **track** scope.
- [x] **MD.5** Copy/Paste/Clear for **pattern** scope. Pattern+Record
      now always copies (fixes MC.5 which used this gesture for fork).
      Fork Part moved to `Func+W` (`ControllerButton::ForkPart`).
- [x] **MD.6** Global mutes (per-Project, per-track). `A+step`
      toggles the APVTS `trackMute` param immediately.
- [x] **MD.7** Pattern mutes (per-Pattern, per-track). `Func+A+step`
      defers toggle; applied atomically on Func release (MD.8).
      Saved with the pattern as a bitfield. Resolver: `muted[i] =
      globalMute[i] || patternMute[i]`.
- [x] **MD.8** Multi-select-on-release: holding `Func` while pressing
      mute keys defers toggles into `deferredPatternMutes_`; all
      applied atomically on `Func` release.
- [x] **MD.9** `Fill` momentary modifier. `TrigCondition::fillRule`
      enum: `Always` / `OnlyFill` / `NeverFill`. Resolver conjoins
      fillRule with probability and m:n. Step-state preview shows
      fill-only cells in violet while Fill is not held.
- [x] **MD.10** Control-All (DESIGN §13.1). Holding `Track` (Q, with
      no track-step selected) activates `controlAllActive_` in the
      processor. `writeParam` broadcasts to all tracks whose machine
      schema has the same slot id. Works for both base writes and
      P-Lock writes (P-Lock if step held on target track).
      Note: per-track accepted/skipped indicator deferred to ME.
- [x] **MD.11** Checkpoint stack (DESIGN §13.6). RAM-only LIFO of
      `{Pattern, Part}` snapshots per pattern-slot, capped at 8,
      oldest evicted on overflow. `Func+2-release` (no step used) or
      `Snapshot` key pushes; `Restore` key pops. Depth shown in
      "CK:N" chrome badge. Not serialized.

### ME — Canonical sections + post-machine FLTR/AMP + role tags  [complete]

The DSP and schema work that makes the canonical section bar uniform
across machine types.

- [x] **ME.1** Add `ParamSpec::role` (closed enum). Update all existing
      machines (currently just `SamplerMachine`) to tag their slots.
- [x] **ME.2** Section bar canonical reservation: keys 3–8 fixed to
      TRIG / SRC / FLTR / AMP / LFO / FX. Section labels declared by
      the machine must match the canonical title where one applies.
      Update `SectionBar` rendering accordingly.
- [x] **ME.3** Extension sections: repeated press of a section key
      cycles through both canonical-pages-within-section and the
      machine's declared extension pages on that section.
- [x] **ME.4** Per-track post-machine FLTR block: multi-mode SVF
      (LP/BP/HP/Notch) with selectable 12 dB / 24 dB slope, Cutoff,
      Resonance, Drive, Env→Cutoff. Lives in
      `Part::track[i].fltrState`. Slope is a stepped slot
      (`{12dB, 24dB}`); P-lockable per step. Default 24 dB.
- [x] **ME.5** Per-track post-machine AMP block: AHDSR responding to
      sequencer-emitted note-on/off; Pan; Level; **gate source**
      (`{Envelope | Held-open}`, default Envelope — Held-open keeps
      the amp stage open for Thru/drones, DESIGN §14). Lives in
      `Part::track[i].ampState`. (Send A / Send B output-mix levels
      are added with the FX system, MV.)
- [x] **ME.6** Machine opt-out: `IMachine::hasInternalFilter()` /
      `hasInternalAmp()` bypass the corresponding block. Section key
      for that section is repurposed to the machine's own slots.

### MF — MIDI-out machine (first-class)  [pending]

DESIGN §15. A `MidiOutMachine` peer of `SamplerMachine`. Required for
the "external gear is a first-class workflow" pillar.

- [x] **MF.1** `MidiOutMachine` skeleton inheriting `IMachine`,
      `currentVoices() = V0`. Schema: `dest`, `channel`, `program`,
      `cc[0..15]`.
- [x] **MF.2** Destination resolution: enumerate JUCE MIDI output
      devices (standalone) and host MIDI buses (plugin). Persist
      destination by stable id (device name or bus index).
- [x] **MF.3** Channel + program P-locking. Channel changes within a
      pattern emit clean note-offs on the previous channel.
- [x] **MF.4** Per-track configurable CC numbers + labels for the
      16 generic `cc[i]` slots. `nameTable_` in `MidiOutMachine`
      for destination-specific CC name lookup (MF.8 populates it).
      CC config serialized as `<CCConfig>` in PluginState.
- [x] **MF.5** FLTR/AMP bypass: `MidiOutMachine` returns
      `hasInternalFilter() = true` and `hasInternalAmp() = true`,
      so both post-machine blocks are skipped. The FLTR (key 5) and
      AMP (key 6) section keys are repurposed to the machine's own
      CC bank pages instead. Landed in MF.1 (opt-out flags) + MF.4
      (sectionIndex=2/3 on cc slots); verified via `sectionsForKey`
      in `KeyboardArea` — processor's `section()` intercepts only
      fire when `!hasInternalFilter/Amp()`, so MidiOutMachine falls
      through to its own CC bank sections automatically.
- [x] **MF.6** All-Notes-Off + Reset-All-Controllers on transport
      stop / pattern stop, per channel. Prevents stuck notes
      downstream. `MidiOutMachine::allNotesOff()` emits CC 123 +
      CC 121 on `activeChannel_`; processor detects the
      `sequencerRunning` falling edge via `wasSequencerRunning_`
      and calls it on every MIDI-out track each stop event.
- [x] **MF.7** Participation in performance features: Control-All
      across MIDI-out tracks, Sound Pool entries for MIDI-out
      sounds, Fills / Mutes / Copy-Paste / Checkpoints — verified
      end-to-end against MIDI-out tracks.
      — Control-All: works; `idForSlot`/`slotForId` routes through
        `paramSpec().id` for all MIDI-out slots.
      — Fills: works; trig condition evaluation is sequencer-level.
      — Copy-Paste: works; track-scope copies the sequence layer
        (steps), not the Part, which is correct by design.
      — Checkpoints: works; `pushCheckpoint` captures the full Part
        including all MIDI-out PartTrack fields.
      — Mutes: fixed stuck-note risk — `wasSilent_[i]` rising-edge
        detection fires `allNotesOff()` on MIDI-out tracks when
        mute activates mid-note (`PluginProcessor.cpp`).
      — Sound Pool: deferred to MG; PartTrack now carries all
        MIDI-out config (`destinationId`, `midiCCNumbers/Labels`)
        so a sound bundle naturally extends to MIDI-out tracks.
- [x] **MF.8** Hardware-targeting factory tables for at least:
      Digitakt, Digitone, Syntakt, Analog Four, Analog Rytm,
      Octatrack, Tonverk. All seven shipped in
      `src/machine/MidiDevicePresets.{h,cpp}`. Tables source from
      published Elektron MIDI implementation charts; Tonverk from
      TE product spec. `PartTrack::midiPresetName` persists the
      active preset id; `setStateInformation` calls
      `MidiDevicePresets::getTable()` to restore `nameTable_` on
      reload. Preset selection UI deferred to MP (UI polish).

### MG — Alternate trig modes  [pending]

DESIGN §13.5. The trig grid as a modal surface.

- [x] **MG.1** Keyboard mode: 16 trig keys → 16 chromatic semitones
      from a configurable root. EditContext rules apply (held step +
      keyboard key writes `step.noteOverride`).
- [x] **MG.2** Retrig mode: trig keys, while held, retrigger at a
      configurable rate (1/16, 1/32, 1/48, 1/96). Record-arm captures
      the retrig rate as a P-Lock.
- [x] **MG.3** Slice sub-mode of Retrig (sampler tracks with slice
      data): 16 trig keys → first 16 slices, played live.
- [x] **MG.4** Sound Pool data model: Project-scope library of
      (machineId, base ParamFrame, sample/destination refs) bundles.
      CRUD UI: save current track sound to pool, recall pool entry
      to track.
- [x] **MG.5** Sound Pool mode: trig keys page through the pool and
      live-swap the focused track's sound while held. Record-arm
      captures pool index as a `sound_id` P-Lock on the next emitted
      step (or held step).
- [x] **MG.6** Mode-chord UX consistent with MB.5; all three modes
      exit cleanly on chord release and never destructively alter
      the authored pattern unless record-arm is engaged.

### MGX — 16-Track expansion + header pagination  [complete]

16 heterogeneous tracks (expanded from 8). Default layout: tracks 1–8 sampler,
tracks 9–16 MIDI-out (Digitakt-style split without the lock-in — any track can
be reassigned to any machine via the Part-edit flow at MH).

- [x] **MGX.1** `kNumTracks = 16` in `Sequence.h`; `machines_` and Part arrays
      scale automatically everywhere `kNumTracks` is used.
- [x] **MGX.2** Default machine assignment: tracks 0–7 → `SamplerMachine`,
      tracks 8–15 → `MidiOutMachine` (set in `LockstepProcessor` constructor
      and persisted via each Part's `machineId`). Old 8-track saves load
      cleanly: tracks 8–15 receive MIDI-out defaults.
- [x] **MGX.3** `QwertyOverlay` Track and Mute layers extended to 16:
      `Track+S–L = SelectTrack 0–7`, `Track+X–. = SelectTrack 8–15`;
      same extension for `Mute`.
- [x] **MGX.4** Track-header pagination in `PluginEditor`: page-toggle button
      (labelled "1–8" / "9–16") at the left of the track row; selecting a
      track via keyboard auto-flips the page; only the active page's 8 buttons
      are laid out and visible.
- [x] **MGX.5** Machine-type badge: a small "M" drawn in the top-right corner
      of MIDI-out track buttons in `paintMeters()`.
- [ ] **MGX.6** Machine selection UI for runtime track reassignment — deferred
      to MC/MH (Part-edit overlay; exact gesture TBD).

### MHX — The 10×4 surface revamp  [complete]

DESIGN §33 (+ §5.5, §13, §17.5, §26). The final control-surface and
UX-grammar pass, sequenced **ahead of the rest of MH** so the
catalogue machines are authored against a frozen surface. Widens the
9×4 layout to **10×4**: an eight-key one-hand modifier cluster (left
two columns) + the unchanged 8-wide functional block (16 steps, 6
canonical sections). Intended as the last large UI/UX revamp.

- [x] **MHX.1** `QwertyOverlay` rewrite to 10×4: eight modifiers
      (`Func/Track/Pattern/Mute` | `Fill/Cue/Scene/Master`), step keys
      move to `D–;` (0–7) / `C–/` (8–15), function strip relocated
      (sections `3–8`, nav `E R T Y`, verbs `U I O`, `9`=Arm,
      `0`=Play/Stop, `P`=Tap). Update `isEdgeKey` for the 10-wide grid.
- [x] **MHX.2** Modifier slate wired: `Pattern` promoted to its own key
      (`A`); single `Scene` modifier (`S`) with `Scene + ^/v` = endpoint
      A/B; `Master` modifier (`X`) bound to master focus state (§32.3);
      `Cue` modifier (`W`). `Mute` retains hold-and-tap-many (`Z`).
- [x] **MHX.3** Compound-chord engine (DESIGN §13): cross-column-only,
      modifier+modifier sets a compound scope and never fires alone,
      `Func` universal. Exceptions table starts empty. `hasCompoundScope()`
      / `hasSameColumnConflict()` on `EditMode`.
- [x] **MHX.4** `kMZSlots` 4 → 8; MZ re-layout to 4×2. `kParamsPerPage`
      bumped to 8. All meta-section arrays padded to 8 entries.
- [x] **MHX.5** Editor re-layout: staggered 4×2 encoder band above the
      grid; vertical crossfader on the band's right (Scene A top / B
      bottom, wires in MI). Window 990×596.
- [x] **MHX.6** Cell typography pass: four-register cells (corner
      key-legend / centre primary / bottom Func-label / amber
      compound-chord overlay); violet perf-modifier colour group; 10-cell
      section row (Func, Fill, TRIG–FX, ARM, PLY); 10-item function row
      (Q/TRK, W/CUE, E-Y nav, U-O verbs, P/TAP); two modifier columns
      per step row (A/PAT + S/SCN, Z/MUT + X/MST); kStaggerHalfUnits=23;
      key letters D–;/C–/; all abbreviations ≤5 chars.
- [x] **MHX.7** Provisional choices finalised: `9`=RecordArm (ARM/MET),
      `0`=PlayStop (PLY); nav on `E R T Y` (< ^ v >); verbs `U I O`
      (REC/PLY/STP) with Func-layer KEY/RTG/RST; `P`=TAP/SPL. Surface
      freeze confirmed — all MHX items shipped.

### MHY — Section matrix + modifier-cluster rethink  [complete]

DESIGN §6, §13, §33. A second-pass refinement of MHX, sequenced
**after** MHX (which froze key positions) and **before** the rest of
MH (so DrumSynth / Slicer / Static / Percussion author against the
finalised contract). MHX assigned cluster identities by intuition;
MHY measures chord-value across every key category and reshuffles. MHY
also promotes the section bar from a flat six-of-canonical to a
**scope-indexed matrix** (~36 first-tier section pages instead of 6),
renames `LFO`→`MOD` so machine modulation has a real home, introduces
`Part` as a first-class scope (kit half of the Part/Pattern split,
DESIGN §4.7) and drops `Cue` from the cluster (reserved for MU
reactivation). Moves machine-select to `Func+Part` (Part relabels to
MACH; replaces `Func+R`). Right-utility row (`Y U I O P`) is reassigned to
`Yes/Rec/Play/Stop/No`; key `3` keeps `TAP`.

**Frozen by MHX, untouched by MHY:** nav (`4 / E R T`), 16 step keys
(`D F G H J K L ;` / `C V B N M , . /`), section-bar position
(row-0 `5 6 7 8 9 0`), and the cluster region (cols 0–1 / rows 0–3 =
`1 2 / Q W / A S / Z X`).

- [x] **MHY.1** Cluster identity remap in `QwertyOverlay`:
      `Func/Track | Pattern/Part | Scene/Master | Mute/Fill`
      (frequency-of-use ordering, specialists on the bottom row).
      Add `Part` to the `Scope` enum; mark `Cue` as reserved (not
      bound). Update `EditMode` compound-chord rules: section-scope
      set is `{Func, Track, Pattern, Part, Scene, Master}`; cross-
      column rule still applies.
- [x] **MHY.2** Canonical section rename `LFO`→`MOD` in
      `kCanonicalSectionNames` (`src/machine/IMachine.h`). Audit
      existing machine `ParamSpec.sectionIndex == 4` slots; nothing
      moves (the index stays 4), only the canonical label changes.
- [x] **MHY.3** Add `ParamSpec.variant ∈ {Primary, Secondary}` field
      (default `Primary`). Existing machines stay primary; the
      `Func+section` page tier is now a declarable home for future
      machine deep-dives (FM matrix, VA voice-mode block, etc.).
- [x] **MHY.4** Right-utility row remap. `Y U I O P` →
      `Yes / Rec / Play / Stop / No`. `3` keeps `TAP`. Existing
      `Func+3=MetronomeToggle` migrated to `Func+I` (Func+Play =
      metronome). `Func+Y`=snapshot push, `Func+P`=pop. Without a
      scope modifier the verbs default to transport/confirmation;
      with a scope held, EditMode routes them to grammar handlers.
- [x] **MHY.5** Scope-section matrix scaffolding. `ScopedSectionMatrix.h`
      provides a static lookup for every `(scope, section)` cell:
      display label + `hasContent` flag. Cell content is all-false
      stubs in MHY — population accretes as ME / MC / MI / MV land.
- [x] **MHY.6** Reactive `SectionBar` chrome: relabel keys live
      under each held scope (Track/Pattern/Part/Scene/Master); dim
      cells with `hasContent=false`. Func secondary-variant tint
      deferred — lands when cell content populates.
- [x] **MHY.7** Documentation pass: rewrote `DESIGN.md` §6 + §7 +
      cross-refs (§13, §14, §17, §33); updated `CLAUDE.md` glossary
      (Section, Scope, Part) and 10×4 layout description; updated
      `README.md` glossary, tutorial, and §5 shortcut appendix.
- [x] **MHY.8** Verification: build clean (all three targets); standalone
      smoke-tested; cluster highlighting confirmed correct post UI-label
      fix; scope-section matrix labels confirmed live-updating in the
      section bar when scope modifiers are held.

### MHZ — Keyboard / UI revamp  [pending]

DESIGN §6, §13, §24. A third surface-revamp pass sequenced **after**
MHY (which froze the cluster identities + section matrix) and **before**
the remaining MH catalogue entries — in practice the three MHZ sub-
milestones land before MH.3 resumes so DrumSynth / Slicer / Static /
Percussion are authored against the improved surface.

MHX / MHY froze the geometry and the modifier cluster. MHZ closes the
chrome and grammar gaps that surfaced once the matrix was wired:

- key cells under-use the screen (small primary text, four-register
  cells designed when "contextual labels" was one special case);
- contextual labels (`COPY/PASTE/CLR`, scope-relabelled sections)
  reach for ad-hoc swap logic instead of a single rule;
- there is no project-wide colour grammar to tell the user which
  scope a held modifier is operating on;
- the top-bar "mode chips" duplicate what the cluster already says
  while real performance state (BPM, Bank/Pattern/Part identity,
  chain queue, checkpoint depth) has nowhere to live;
- the ManipulationZone repeats parameter name + value redundantly and
  has no textual display for stepped/enum params (filter mode, voice
  mode, …);
- there is no canonical step-edit path for chord notes (today MH.2
  chord capture only fires under transport-time record-arm), no
  P-Lock clear gesture, and no default-reset gesture on a rotary.

**Frozen by MHX/MHY, untouched by MHZ:** all key positions, the
modifier cluster identities, the section-bar canonical taxonomy, the
scope+verb grammar (DESIGN §13). MHZ is a chrome / grammar-helper /
gap-closing pass; it adds no new scopes and no new verbs.

#### MHZ.1 — Surface chrome (geometry + label grammar)

Goal: bigger, clearer, contextual keys; project-wide scope colour
grammar; no behavioural change to the sequencer itself.

- [x] **MHZ.1.1** Key cell typography pass. Grow primary-label font
      (~10pt → ~15pt), grow QWERTY hint and secondary band, drop the
      wasted inner margin. `KeyButton.{h,cpp}` paint primitive.
- [x] **MHZ.1.2** Label-length ceiling lifted to **6 characters
      (hard cap)**. Audit existing canonical and per-key abbreviations
      and lengthen the ones that benefit (`FILTER`, `ATTACK`,
      `RETRIG`, `COPY`, `PASTE`, `CLEAR`, `CONFIG`, …). Truncation
      logic in `paintKeyButton` updated; over-6 falls back to
      auto-shrink rather than truncation.
- [x] **MHZ.1.3** Unified label-resolution helper
      `resolveKeyLabel(KeyDef, UiState, EditContext) -> {primary,
      hint}`. Collapses today's ad-hoc `COP/PST/CLR` branch + the
      `scopedCell()` matrix lookup + the verb-key dimming into one
      rule. Step-hold becomes just another modifier flag in the
      resolver's input — the "section scope held OR step held"
      special case disappears.
- [x] **MHZ.1.4** Scope colour grammar in `UITheme.h`. Canonical
      palette entries: `step / track / pattern / part / machine /
      scene / master`. Light grey for step (default); distinct hue per
      remaining scope. Used by every UI surface from this point: key
      tints when a modifier is held, StepGrid re-skin cells (MHZ.2),
      held-context preview chrome (MHZ.2), badges. Taxonomy only —
      exact palette values defer to the later visual-design pass
      (DESIGN §24 policy).
- [x] **MHZ.1.5** Always-on hints kept where the secondary meaning is
      genuinely invariant under *any* scope (verb keys
      `COPY/PASTE/CLR` under any scope modifier; anything else swaps
      only when the relevant modifier is held). Encoded inside the
      label resolver so the policy lives in one place.
- [x] **MHZ.1.6** Documentation pass: DESIGN §6 sub-section "Contextual
      chrome and label resolution"; DESIGN §24 scope colour grammar
      taxonomy; CLAUDE.md glossary entries; README.md §5 + §6.
- [x] **MHZ.1.7** Verification: build clean (all three targets);
      standalone smoke-tested; visually confirm primary labels readable
      at arm's length, every modifier press lights its scope colour,
      hint-vs-primary policy correct.

#### MHZ.2 — Contextual modes (scope-driven re-skin + top bar + MZ streamline)

Goal: the surface tells you exactly what your held modifiers will
operate on; the MZ stops repeating itself; the top bar becomes useful.

- [x] **MHZ.2.1** Step-grid scope re-skin. When a scope modifier maps
      to a 1-of-16 selector (Track / Pattern / Part; Part+SRC =
      machine picker) the 16 step keys become a non-paginated index
      for that scope. **Pagination is suppressed** in the re-skinned
      mode — only "which key was pressed" matters. Unavailable indices
      dim (e.g. tracks 9–16 dim when only 8 tracks exist). Cells tint
      with the scope colour. Driven by an extended scoped-cell table
      (sibling of `ScopedSectionMatrix.h`) so new scopes are data, not
      paint code. Machine names render textually on Part+SRC; every
      other scope is numeric.
- [x] **MHZ.2.2** Top bar redesign. Drop the mode-chips strip;
      replace with two zones:
      - **Left dashboard:** BPM, Bank/Pattern/Part identity, transport
        position, chain queue glance, checkpoint depth `CK:N`.
      - **Right held-context preview:** derived from held modifiers —
        e.g. "TRACK 3 + …" or "PART + SRC → machine picker". Live
        cheat sheet for the cluster grammar.
      Both zones read a single view-model so behaviour and labels
      cannot drift.
- [x] **MHZ.2.3** ManipulationZone streamlining. Each slot collapses
      to **rotary + one value display**. Value display is textual
      when the slot's `ParamSpec` carries a `valueLabels` table
      (filter mode, voice mode, …) and numeric otherwise. Bigger
      rotaries fill the reclaimed space. Param name moves to a slim
      header (or piggybacks on the section key label since context
      already says what page you're on). The redundant separate
      label-and-value pair is gone.
- [x] **MHZ.2.4** Double-click rotary → reset to default. JUCE
      `Slider::onDoubleClick`. Routed through one helper so the
      eventual hardware push-encoder-twice gesture (DESIGN §17.5
      style) lands on the same code path.
- [x] **MHZ.2.5** `ParamSpec::valueLabels` (`std::span<const char* const>`),
      default empty. Machines populate it for stepped/enum slots; MZ
      render consults it. Existing `kSlotVoiceMode` / filter mode /
      LFO shape slots get textual values out of the box.
- [x] **MHZ.2.6** Documentation: DESIGN updates for `valueLabels`,
      scope re-skin, top-bar dashboard; CLAUDE.md glossary; README §5
      / §6 reference table.
- [x] **MHZ.2.7** Verification: hold Track and confirm step grid is a
      1-of-16 track picker with unavailable indices dimmed; hold
      Part+SRC and confirm machine names render; top bar dashboard
      shows current Bank/Pattern/Part/BPM; held-context preview
      updates live; MZ filter slot shows `LP24`/`LP12`/`HP`/`BP` text;
      double-click rotary resets to default.

#### MHZ.3 — Note capture, P-Lock clear, step-driven edit modes

Goal: close the real grammar gaps surfaced during MHY play-testing.

- [x] **MHZ.3.1** Step-hold MIDI capture as the canonical chord-edit
      path. Holding a step opens a capture window; held MIDI notes
      accumulate into a temporary chord buffer; **on step release**
      the buffer commits to that step's `TrigOverride.notes[] /
      noteCount` (reusing the MH.2 chord-step data model, up to
      `kMaxNotesPerStep`). Empty buffer = no change (preserves
      existing data). Transport-time record-arm capture stays as an
      orthogonal live-performance flow.
- [x] **MHZ.3.2** Replace-on-hold is the chord editor. No per-note
      add / remove / swap UI in MHZ — the simpler grammar wins, and a
      finer-grained editor only lands if play-testing proves
      replace-only too coarse.
- [x] **MHZ.3.3** P-Lock clear gestures. Trig + Func + Stop = clear
      all P-Locks on held step(s), trig intact. Trig + (active MZ
      slot) + Stop = clear only that slot's P-Lock. Both inside the
      existing scope+verb grammar. Documented in DESIGN §13.
- [x] **MHZ.3.4** Step-driven edit mode. `Func` + step → step cells
      re-skin orange: bright for P-locked slots, dim for empty;
      press a cell to clear that slot's P-Lock; release Func to exit.
      Slots 0-15 shown (each cell maps by index). Satisfies
      PRINCIPLES §4 (modifier hold = mode duration, no sticky state).
- [x] **MHZ.3.5** Machine picker migrated from `Part+SRC` to `Func+Part`.
      Func held → Part key relabels to `MACH`; step cells re-skin with
      machine names; press a step to set the machine on the active track.
      `Part+SRC` is now dimmed (`ScopedSectionMatrix` `hasContent=false`).
- [x] **MHZ.3.6** Held-context preview updated: shows
      `"FUNC + MACH | press step to select machine"` when Func+Part
      active; shows `"FUNC + STEP N | press cell to clear P-Lock slot"`
      when in P-Lock clear mode.
- [x] **MHZ.3.7** Documentation: DESIGN §21.4 marked implemented,
      velocity/gate semantics updated; DESIGN §13 grammar table pinned;
      CLAUDE.md glossary; ROADMAP checkboxes; README §5 / §6.
- [x] **MHZ.3.8** Verification: with transport stopped, hold step D,
      play C-major chord, release step — chord lands on step D.
      Repeat with another chord; confirm replace semantics. Place
      several P-Locks on one step; invoke the step-driven edit mode;
      confirm the selected slot clears only that lock; confirm the
      "clear all" gesture wipes them all. Hold Func+Part and confirm
      machine names render on step cells; press a step to select;
      confirm Part+SRC now shows part-base SRC params. Regression-check
      transport-time record-arm capture still works.

### MH — Machine catalogue expansion  [pending, staggered]

DESIGN §1 (lineage). Inheritance from `IMachine` — each is a separate
contributor-sized project. Order is a suggestion, not a dependency
chain; any of these can land independently once MF (for MIDI-out
parity) is done. **MHX, MHY and MHZ land first** (the surface freeze,
the contract, and the chrome/grammar revamp). In practice MH.3
resumes once MHZ.1 → MHZ.3 are complete.

- [x] **MH.1** FMMachine — 4-op FM, free modulation matrix (4×4), per-operator
      ADSR + ratio / fine-tune / mix, macro attack / release / sustain scalars.
      Voice mode (Mono / Poly): a 4-voice pool with oldest-voice stealing
      lights up when `kSlotVoiceMode` switches to Poly; chord steps fan out
      to independent voices, each with its own envelopes and FM matrix.
- [x] **MH.2** VAMachine — virtual-analog mono/para, Analog Four-style
      voice. 2× PolyBLEP oscillators (Saw/Pulse/Tri/Sin) + sub + noise,
      state-variable filter (LP4/LP2/HP/BP) with drive, filter ADSR,
      amp ADSR, LFO (6 shapes, 4 targets), portamento, Mono/Para-4 voice
      modes. Para: 4 independent pitches → shared filter + amp envelope.
      Polyphonic trig infrastructure also added: steps carry up to 4 notes;
      chord capture (hold step + play keys) + gate-length auto-write on
      last-note-off. Backward-compatible serialization.
- [ ] **MH.3** DrumSynthMachine — Rytm-style per-track drum
      synthesis (kick, snare, hat, tom variants).
- [ ] **MH.4** SlicerMachine — Octatrack Static/Flex-inspired
      slice-playback with playback-rate and start-point modulation.
- [ ] **MH.5** Define and version-stamp a "machine pack" file format
      so individual machines can ship and be discovered without
      bloating the core.
- [ ] **MH.6** StaticMachine — disk-streaming sampler for long-form
      audio (DESIGN §29). Shares Flex's overlapping slot vocabulary
      (start/end, level) minus the RAM-only manipulations streaming
      can't cheaply support. Audio never decoded wholesale into RAM.
- [ ] **MH.7** PercussionMachine — Volca-Drum-style two-layer
      percussion synth. Each voice = 2 parallel layers, each layer
      = excitation osc (sine / saw / noise / folded variant) with
      FM/ring-mod partner + pitch envelope (depth, decay) → waveguide
      / modal resonator (Tube / String / Membrane / Modal-bank;
      pitch, decay, damping, nonlinearity, send level). Layer A↔B
      crossfade + bit/sample-rate reduce + drive in the mix stage.
      Canonical FLTR (SVF) + AMP downstream as usual; resonator
      block is the new DSP. Algorithm presets (kick / snare / hat /
      tom / bell / cymbal) ship as Sound Pool entries, not schema
      variants. Inharmonic / tuned-metal / struck-physical-object
      territory neither Sampler nor VA can fake. Distinct machine
      from any future Cydrum-style wavetable+animation drum
      (different excitation philosophy; do not merge).

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
- [ ] **MI.8** Morph-aware editing (DESIGN §17.6). A bare encoder turn
      on an already-assigned scene slot writes through the live fader
      position, normalised so the heard value tracks 1:1
      (`da = Δ(1−f)/D, db = Δf/D, D=(1−f)²+f²`). Stepped slots write
      to the resolved side. Unassigned slots edit base as before; no
      auto-assign. P-Locks still dominate.
- [ ] **MI.9** Fluid mute. `Scene + Mute` on a track captures
      `Level → silence` into the held scene (sugar over assigning the
      AMP Level slot), so the fader fades the track in/out rather than
      snapping. Binary mutes (MD) stay separate and instantaneous.

### MJ — Pattern/Part Management UI  [pending]

DESIGN §23. MC introduced the Project/Bank/Pattern/Part data model
and the queue-and-switch gesture; MJ is the performance-time
*management* layer that sits on top.

- [ ] **MJ.1** Pattern + Part naming. Each carries a user-editable
      short name (≤16 chars). On-disk in the project; default is
      bank-letter + slot-index. Live rename via a small inline
      editor (no modal dialog).
- [ ] **MJ.2** Pattern + Part colouring + tags. Each carries one
      colour (small fixed palette tied to §24 taxonomy) and an
      optional tag string. Both surface in the management browser
      and in the chrome (queued-pattern chip shows colour + name).
- [ ] **MJ.3** Browser overlay. A `Func + ?` (chord TBD,
      consistent with §13 grammar) opens a non-modal pattern /
      part browser: list of banks → patterns → parts with
      names / colours / tags, navigable while playback continues.
      Selecting a pattern triggers the existing queue gesture; no
      new verb introduced.
- [ ] **MJ.4** Copy / move / duplicate across banks. The existing
      `Pattern + Record` (copy) and `Pattern + Play` (paste)
      verbs gain a destination-bank prefix gesture: hold
      `Pattern + bank_letter` after copy to choose where the
      paste lands. Move = paste-then-clear-source variant. Same
      rules apply to Parts via `Func + W` (existing fork
      gesture).
- [ ] **MJ.5** Pattern / Part queue cue. While in the browser,
      `Yes` cues the highlighted pattern (queue without playing
      immediately); `No` cancels the cue. Live continuity
      preserved.

### MK — Sampler depth  [pending]

DESIGN §3.1. Adds first-class sample trim and loop to the baseline
sampler. Machine-internal — no sequencer changes.

- [ ] **MK.1** Sampler schema additions: `start_sample`,
      `end_sample`, `loop_start`, `loop_end` (all stepped at
      sample boundaries; P-lockable). Defaults: full sample, no
      loop region.
- [ ] **MK.2** Playback model: play `[start_sample, end_sample)`
      once; if a loop region is set, on reaching `loop_end`,
      wrap to `loop_start` and continue until the AHDSR envelope
      reaches zero. Note-off triggers release; the loop continues
      through release until envelope-zero.
- [ ] **MK.3** Loop seam crossfade: fixed small crossfade
      (≤4 ms) at `loop_end → loop_start` to suppress clicks. No
      user control in v1.
- [ ] **MK.4** Sample-pool waveform display (manipulation zone or
      dedicated panel): visualises sample with draggable
      start / end / loop markers. Editing markers writes the
      corresponding slots through the normal EditContext.

### ML — Sequencer refinement: microtiming + swing + quantize  [pending]

DESIGN §19. Captures live timing nuance and provides a uniform
quantize verb.

- [ ] **ML.1** `Step::microOffset : float ∈ [-0.5, +0.5]`
      (fraction of step length). Resolver shifts trig sample
      position by `microOffset × step_samples`. Stored
      per-step; serialised as part of the trig override map.
- [ ] **ML.2** Realtime record writes `microOffset` automatically:
      a note-on arriving between step boundaries records to the
      nearest step with the residual delta stored as the
      offset. The existing M7 record path is the integration
      point.
- [ ] **ML.3** Per-track `swing` parameter, range `[0, 1]`,
      default `0.5` (no swing). At `0.5 < swing ≤ 1`, every
      odd-indexed step within the track grid is delayed by
      `(swing - 0.5) × step_samples`. Lives on `Track`, not
      `Part` (it's a sequencer-scope feel, not a kit-scope
      feel). UI in TRACK meta section.
- [ ] **ML.4** `Quantize` verb. Mapped onto the existing verb
      set: `<scope> + Stop` already clears overrides; introduce
      `<scope> + No` as the quantize verb (zero microOffsets
      in scope without touching trigs / P-Locks). Step, Track,
      and Pattern scopes supported. Section / Mute / Fill
      scopes: no-op.
- [ ] **ML.5** Step-grid preview: a step with a nonzero
      `microOffset` renders with a small left / right tick
      indicator showing direction of nudge. Visible at all
      times (not gated by held step).

### MM — 16-levels trig-grid mode  [pending]

DESIGN §20. Extends MG's modal trig-grid surface. Lands after MG.

- [ ] **MM.1** Eligibility set: a closed subset of `ParamSpec::role`
      values that 16-levels can target (`velocity`, `pitch.coarse`,
      `cutoff`, `resonance`, `attack`, `decay`, `release`,
      `lfo.depth`, `level`, `pan`, `drive`). Stored as a constant
      next to the role enum.
- [ ] **MM.2** Mode-enter chord consistent with MG.6. While
      active, the trig grid does not toggle trigs; pressing key
      `i ∈ [0..15]` writes value `i / 15` of the parameter's
      range to:
        - the focused track's base (no step held), or
        - the held step's P-Lock (step held), or
        - the next emitted step (record-arm + no step held).
- [ ] **MM.3** Parameter selector: while in 16-levels mode, the
      currently-bound role is shown in chrome; hold the mode
      chord + encoder to cycle through the eligible roles
      present on the focused track's machine. Defaults to
      `velocity`.
- [ ] **MM.4** MIDI-out parity: works identically against
      MIDI-out tracks where the bound role tags a `cc[i]` slot.

### MN — Sampling and resampling  [pending]

DESIGN §22.

- [ ] **MN.1** Audio-input capture: a Sampling overlay (chord
      TBD, consistent with §13 grammar) opens a capture
      surface. Input source picker = `{Plugin audio input,
      Track 1..N, Master}`. System / device input is
      explicitly **not** an option.
- [ ] **MN.2** Free-form capture: start / stop verbs are the
      existing `Record` / `Stop` within the Sampling scope.
      Capture writes to a temporary buffer; on stop, the user
      enters the naming flow (MN.5).
- [ ] **MN.3** Capture-N-bars: an alternate within the Sampling
      scope. Hold `Sampling + length-key` (1 / 2 / 4 / 8) and
      arm; capture starts at the next bar boundary and ends
      precisely after N bars. Useful for grabbing loops live.
- [ ] **MN.4** Resample taps. `Track + Sampling` selects the
      named track as source; `Pattern + Sampling` selects the
      master. Both reuse the MN.2 / MN.3 capture paths verbatim.
- [ ] **MN.5** Naming flow. On capture completion the UI offers
      five candidate names: four `adjective-noun` pairs from a
      bundled wordlist (`assets/wordlist.json` checked into the
      repo) and one consonant-vowel pseudo-word derived from
      the sample's content hash. Two encoders scroll through
      the adjective and noun lists alphabetically; a third
      encoder picks the candidate; `Yes` accepts, `No` cancels.
      Typed entry via QWERTY also accepted.
- [ ] **MN.6** Pool integration: accepted captures land in the
      project sample pool with the chosen name, the path
      pointing to a project-relative `samples/recorded/`
      folder, and the standard `xxHash32` ref. Behaves
      identically to drag-and-dropped samples thereafter.

### MO — Audition and cross-track record  [pending]

DESIGN §21. Refines the live-capture story.

- [ ] **MO.1** Preview gestures (no record):
        - `Trig + Yes` (held step + Yes verb) → fires that
          step's resolved trig once, off the sequencer's
          schedule, audible immediately.
        - `Track + Yes` (Track scope held, no track-key) →
          fires the focused track's base trig once.
      Both bypass the sequencer event stream (one-shot
      injected into the track's MidiBuffer directly).
- [ ] **MO.2** Per-track record arms in Per-Track-MIDI channel
      mode. Each track carries a `recordArmed` boolean; only
      armed tracks capture live MIDI. UI on TRACK meta
      section. Default armed = focused track.
- [ ] **MO.3** Arm-all gesture (Per-Track-MIDI mode):
      `Func + RecordArm` (`Func + T`) toggles every track's
      arm in lockstep. The chord cleanly composes; the
      existing `T` keeps its track-grouping meaning.
- [ ] **MO.4** Omni mode: arming is global (the existing M7
      record-arm). Note-on always captures to the focused
      track only. Match the existing behaviour; document the
      asymmetry as a consequence of Omni's design.
- [ ] **MO.5** Step-as-keyboard live record: while MG.1's
      Keyboard mode is active and record-arm is on, each
      keypress writes the corresponding note onto the next
      emitted step on the focused (or armed-set) track's
      pattern. Composes with MG.2 / MG.3 / MG.5 identically.

### MP — UI polish: layout, palette, toggles, coarse-adjust  [pending]

DESIGN §24, §25, §26.

- [ ] **MP.1** Vertical layout. The main editor window grows
      vertically so that StepGrid cells, SectionBar cells, and
      the manipulation-zone quadrants are square — matching the
      eventual hardware key caps. ManipulationZone moves above
      the StepGrid; on-screen MIDI keyboard moves to a
      collapsible drawer. Layout constants centralised in one
      header.
- [ ] **MP.2** State-colour palette. Implement the §24 state
      taxonomy: a `StateColor` enum and a single resolver that
      maps each state to a colour. StepGrid, SectionBar, chrome
      badges, and (future) hardware LED packets all consume
      the same enum. Specific colours are a later visual pass;
      MP.2 ships with placeholder colours that are
      *distinguishable* but not yet "designed".
- [ ] **MP.3** Granular feedback toggles. A small Settings
      panel (`Func + ,`?) exposes individual toggles:
      `show-scope-help`, `show-pending-paste-preview`,
      `show-key-legend`, `show-mode-banner`,
      `show-microtiming-ticks`, `show-fillrule-preview`, etc.
      Each toggle defaults to "on" so first-run UX is the
      beginner experience. Persisted in global settings, not
      project state. Replaces the M6.11 three-overlay-mode
      sketch (Staggered / Ortholinear / Clean are recovered
      as preset bundles of toggles).
- [ ] **MP.4** Coarse-adjust modifier. `Func` held while
      turning an encoder snaps writes for that block to the
      slot's coarse step. Coarse step is unit-derived by
      default (time → musical division, hz / cutoff → octave,
      generic float → `0.1`, int → `1`); `ParamSpec` may
      override per slot. Wire through both encoder turns and
      relative-CC deltas.
- [ ] **MP.5** Manipulation Zone size constant. Replace every
      hard-coded `4` denoting MZ slot count with a single
      `lockstep::kMZSlots` constant. Verify a clean build with
      `kMZSlots = 8`; revert to `4` for shipping. Documents the
      hardware-grow path without committing UI to it yet.

### MQ — Special trig types  [pending]

DESIGN §30. Core-sequencer trig-grammar additions. Small and
high-value; trigless in particular could be pulled earlier (e.g.
alongside ME) if convenient — it has no machine dependencies.

- [ ] **MQ.1** Trigless / lock-only trig. Per-step tri-state
      `off → note → lock-only`, cycled by `Func + step`. A lock-only
      step applies its P-Locks/overrides to the sounding voice with no
      note-on emitted. New `StateColor` for lock-only cells.
- [ ] **MQ.2** One-shot trig. `TrigCondition` variant; RAM-only
      armed/spent state. Auto-rearm on pattern (re)entry and on
      transport stop→start.
- [ ] **MQ.3** One-shot arm-all / disarm-all per track, on the Func
      command layer (chord TBD, sibling to `Func+W` fork). Armed vs.
      spent announced in chrome.
- [ ] **MQ.4** Step-state preview integration: lock-only steps render
      distinctly; spent one-shots dim; armed one-shots read as their
      condition otherwise.

### MR — Audio-input boundary + routing + Thru machine  [pending]

DESIGN §27, §29. The engine work that lets a machine consume audio.
Gates MS/MT. The largest engine change in this cluster.

- [ ] **MR.1** Add the optional audio-input path to the machine
      boundary: the sequencer fills `buffer` from the machine's
      declared `input_source` before `process()`.
- [ ] **MR.2** `input_source` slot (`None | External | Track N |
      Master`) with per-machine declaration. External = plugin audio
      input bus.
- [ ] **MR.3** Per-block topological sort of track processing so
      sources compute before consumers. Reject cyclic routing at
      assignment time with a chrome message.
- [ ] **MR.4** Master prior-block tap: `input_source = Master` reads
      the previous block's master sum (the one sanctioned 1-block tap).
- [ ] **MR.5** ThruMachine: passes `input_source` through at unity so
      the canonical post-machine FLTR/AMP/FX (§14) process external or
      inter-track audio. Verify a Thru track filters an external input
      and a sibling track's output.
- [ ] **MR.6** MIDI-out parity check: MIDI-out tracks declare no input
      source and are excluded from the routing graph cleanly.

### MS — Recorder buffers + recorder trigs  [pending]

DESIGN §28, §29, §30. Depends on MR (audio-input boundary).

- [ ] **MS.1** Volatile pool entries: RAM-only, `REC`-badged,
      not serialised. Unified address space with persistent samples so
      any audio-source slot can reference either kind.
- [ ] **MS.2** Fixed set of volatile buffer slots in the project
      (~8, exact count TBD).
- [ ] **MS.3** RecorderMachine: `input_source`, `target_buffer`,
      `rec_length` (RLEN, P-lockable, default = track loop length).
      Overwrite-only capture.
- [ ] **MS.4** Recorder trig (MQ-style trig variant): captures
      `rec_length` into `target_buffer` on fire. Plain = re-capture
      each loop; one-shot recorder trig = capture once then spent.
- [ ] **MS.5** Freeze-to-disk: run a volatile entry through the §22.3
      naming flow to promote it to a persistent file-backed sample.
- [ ] **MS.6** Looper-record path round-trip test: Recorder writes
      buffer B; a Flex/Static track plays buffer B (no routing cycle,
      since buffer read/write is not an audio edge).

### MT — Looper machine (overdub)  [pending]

DESIGN §29. The pickup-machine equivalent: encapsulated overdub
state machine. Depends on MS (volatile buffers).

- [ ] **MT.1** LooperMachine skeleton: owns a loop buffer + state
      (empty → record → play → overdub → stop → clear).
- [ ] **MT.2** Verb-driven control while the track is focused:
      `Record` cycles record → overdub, `Play` plays, `Stop` stops; a
      clear gesture empties the loop. No new grammar.
- [ ] **MT.3** Overdub (sound-on-sound) mixing with click-free loop
      seams; optional decay/feedback on overdub layers.
- [ ] **MT.4** Transport-synced loop length option (snap loop to bar /
      pattern length) alongside free-length looping.

### MU — Cue bus and monitoring  [pending]

DESIGN §31. Adds the monitor bus and the `Cue` scope. Relates to
MO (audition) and MI (scene preview); lands after MF for MIDI cue.

- [ ] **MU.1** Cue/monitor output bus: standalone audio device ch 3–4;
      plugin second stereo output bus. Chrome shows "cue unavailable"
      when unwired.
- [ ] **MU.2** `Cue` scope button (QWERTY key TBD, hardware mapping
      preserved). `Cue + track` = additive monitor send, post-FLTR/
      AMP/Level tap; track stays in main mix.
- [ ] **MU.3** `Cue + Scene` previews a scene on the cue bus without
      moving the live fader (cued tracks resolve twice for that block).
- [ ] **MU.4** Cue MIDI destination + `Cue + (MIDI-out track)` copies
      events to it, main destination untouched. No cue MIDI dest =
      no-op with chrome note. (Depends on MF.)

### MV — Insert and master effects (FX system)  [pending]

DESIGN §32. Fills the canonical FX section (key 8). Depends only on
ME (canonical sections + post-machine FLTR/AMP) and the §14 signal
path, so it can land any time after ME — it is independent of the
MQ–MU recorder/cue cluster.

- [ ] **MV.1** `IEffect` interface reusing `ParamSpec` / `role` /
      P-Lock infrastructure; stable string id with bypassed-stub
      fallback on unknown id. A small starter catalogue (e.g. delay,
      reverb, EQ) to exercise it.
- [ ] **MV.2** Per-track insert chain: two fixed slots, post-AMP
      (`… → AMP → FX1 → FX2 → track sum`). State in
      `Part::track[i].insertFX[2]` (identity + base params); P-Locks
      live with the Pattern as for any slot.
- [ ] **MV.3** FX canonical section rendering: generic renderer of the
      loaded effect's schema; repeated key-8 press paginates slot-1
      pages then slot-2 pages (extension-section mechanism from ME.3).
      Effect-load gesture to assign a catalogue effect to a slot.
- [ ] **MV.4** Two master effect slots (post track-sum, pre master
      gain), each with mode `{Insert | Send}`. Master FX edited under
      the `Master` focus state. State **Project-scope (provisional)**.
- [ ] **MV.5** Send routing: per-track Send A / Send B levels in the
      AMP output mix (P-lockable); Send-mode master slots act as
      return buses summed back into master. Insert-mode processes the
      master in-line.
- [ ] **MV.6** Performance-grammar parity: effect params P-lockable,
      scene-assignable + morph-aware (§17.6), Control-All by id/role,
      and FX-section copy/paste/clear copies effect *identity* + params
      (extends §13.2 section-copy). Verify on a Thru track end-to-end.
- [ ] **MV.7** MIDI-out tracks carry no inserts/sends; their FX section
      remains the ME.7 MIDI CC bank. No special-casing elsewhere.

### M9 — Polish, CI, beta  [pending]

- [ ] **M9.1** GitHub Actions multi-platform CI (Linux/macOS/Windows).
- [ ] **M9.2** Performance pass: voice CPU profile, choke-fade SIMD
      review, voice cap configuration.
- [ ] **M9.3** Factory patch library.
- [ ] **M9.4** Final product name (replace "Lockstep"), bundle IDs,
      icons, About box.
- [ ] **M9.5** First public beta build.

### M10 — Plugin-wrapper machine (deferred indefinitely)

Originally scoped as a CLAP/VST3 sub-hosting phase against the
48-slot contract. With the variable-schema `IMachine` boundary in
place (MA), wrapping arbitrary plugins reduces to writing one
specific `IMachine` subclass — a `WrapperMachine` that loads a host
plugin via `juce::AudioPluginFormatManager`, exposes its parameter
tree as the schema, and forwards MIDI/audio across.

This is now an optional contributor project, not a planned phase.
No core sequencer changes are required to support it. See DESIGN.md
§9 for the design sketch.

## Play-test notes

**2026-05-08 — M1.5:** 16-step pattern, kick on every step. No clicks,
stable amplitude. Choke micro-fade working. M1 complete.

**M2.3 test procedure:** Load one sample. Set track 1 length to 16, track 2
length to 7 via the length sliders in the step grid. Switch between tracks
with the T1/T2 buttons and observe the amber playhead cycling at different
rates. Phasing is also audible if both tracks share pool index 0.
