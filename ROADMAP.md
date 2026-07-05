# Lockstep — Roadmap

The long-running plan. Work is grouped into eight **phases**; each milestone has a
stable `phase.item` id (e.g. `3.4`) and a status flag. Sub-tasks are checkboxes
so the state of the project is visible on every return to the repo. Tick items as
they land; flip an item's status when its checklist completes.

For architecture see `DESIGN.md`. For the guiding principles every feature must
satisfy, see `PRINCIPLES.md`. **Before adding a milestone here, confirm it is
expressible within those principles and within the existing scope+verb grammar
(DESIGN §13).**

**Active focus:** Phase 8/9 ongoing. **8.28 track channel/envelope split shipped**
(CHANNEL always-on, ENVELOPE optional, FLTR universal with OFF mode, serializer
**v18**, behavioral tests). **8.26 master-bus re-arch + FX catalogue shipped**
(2-insert + 2-send master bus, 13 effects, WAV capture, Animate for master units).
**8.11 A-series closed** (A4 dispatch fully wired; Task B done).
Open verification sweeps: `3.10` standalone, `7.8` VST3/CLAP v18 round-trip,
`7.9e` vocabulary rename, `8.24` standalone visual smoke
(picker → named confirm → YES/NO live colour).
**Last completed:** `9.8` (F1-F3) hierarchical tempo + tempo bar retirement.
**Serializer v21 shipped:** time-sig hierarchy (§4.8) + tempo hierarchy (§4.9);
velocity Phrase mode (B2); contextual param-name aliasing (D1/D2). Mix baseline
centred on velCenter for un-authored steps (B1). Song+TRIG enters TEMPO sticky mode;
Scene+TRIG enters TIME SIG sticky mode. StandaloneTempoBar retired; replaced by
scope-coloured BPM + time-sig header readout. Density lookahead barIndex corrected.
**Active:** `9.9` README + end-to-end verification; `9.5 C` skip-disabled sub-pages +
inert affordance (velocity + density); contextLabel unit test (9.6); tempo resolution
tests (9.8). **Audio-quality foundation shipped:** master-only gain staging
(per-track soft clip removed; transparent soft-knee clipper at master,
`dsp/SoftClip.h`); tape-style `Saturation` effect with placement-aware quality
tiers (`EffectTier`, one catalogue entry → LQ on track / oversampled HQ on
master) and unified Delay/Reverb (HQ ids folded in + migrated); `dsp/Oversampler2x.h`
(polyphase halfband); Analog character (paraphonic loudness compensation, always-on
glue, `Age` drift macro); FM clean (exponential op envelopes, smoothed diagonal
self-feedback, 2× operator oversampling).
**Capture-machine catalogue shipped (`6.2` + `6.3` + `4.5`):** volatile (RAM-only)
REC buffers in the unified pool; RecordMachine (overwrite live-resampler, contextual
recorder trig); LoopMachine (verb-driven overdub state machine, `Track+verb`);
StreamMachine (disk-streaming long-form sampler, per-Kit path). Freeze-to-disk (§22)
deferred.
**Flex-parity audio shipped (tap-fork + Stretch + time-stretch + looper unify):**
tap-forking (revived `input_source = Track N` as a same-block read-only post-chain
tap; topo-sort + cycle refusal extended over mix+tap edges) enables aux sends and
resample-a-single-track; pool `sourceBars` metadata (capture stamps, playback
stretches); `ITempoAware`/`TransportInfo` seam (no machine-boundary change);
**StretchMachine** (`lockstep.stretch.v1`, the Flex analog — WSOLA `TimeStretch` voice,
independent pitch + tempo-tracking); Record opt-in `monitor`; **Loop unified into
the volatile pool** + `loop_sync` varispeed (Free / Free Len / N-Bar phase-lock) +
loop-wrap crossfade; multi-capture default-distinct slots + collision query. Loop
self-play = varispeed (tape); WSOLA stretch lives only in the Stretch. **Deferred
(Milestone C):** Stream/streaming time-stretch; looper stop-fade; Stretch poly + AHDSR;
the on-surface shared-slot indicator (query `captureSlotShared` is wired).
**Next:** `6.7` Machine Module ABI; `4.6`/`4.7` Percussion/Digital synths.
**Playback-correctness + gain-staging pass shipped (post-audio-quality):** metronome
downbeat-skip fix + fresh-start trig anchor frame (trigs were on-grid; the
"half-step-late" feel traced to a *stale Delay left on a new project*); project load
now tears down effect instances on empty slots and reinstalls master sends (phantom
effects fix); on-screen playhead advances with no focused track; FX picker reachable
with Track held (no more wrong-track edits); all-machine loudness calibration (Analog was
~5× hot → matched to drum/FM); FM carrier-mixer normalization + polyphony comp; Analog
filter cutoff key-tracking (`va_keytrack`); master output level surfaced (meter VOL
chip + `Master` band label). Mute-over-soloed-bus verified correct (regression test).
**Octatrack-parity arc shipped (6.1 + 5.5 + 5.6):** audio-input boundary,
output-directed track buses (CHANNEL "Out", topo sort, cycle refusal), RouteMachine,
per-take stem export; Cue-scope audition (`Func+3`); lock-only + one-shot trigs.
Remaining from the arc: A2 topo-sort/B/C all done; `6.2` RecordMachine + `6.3`
LoopMachine shipped (the recorder-trig follow-on).

Phases 1–3 took Lockstep from an empty plugin to a frozen, playable performance
surface; Phase 4 fills the machine catalogue; Phases 5–6 are the depth and
platform passes; Phase 7 built the Set/Song/Scene/Phrase musical hierarchy;
Phase 8 is the hardening/maintainability pass; Phase 9 brings standalone up to a
co-equal host (project files, file bar, quit guard). The framing comes from
DESIGN §1 and `PRINCIPLES.md`: Lockstep is for both bringing existing material on
stage **and** improvising new material from a blank pool, and it runs equally
standalone or as a plugin (`PRINCIPLES.md` §3). Every milestone targets both
workflows and must satisfy the principles. The dedicated hardware controller is a
*later* distillation, designed only after playtesting on the keyboard +
generic-controller surface (`PRINCIPLES.md` §4, DESIGN §10/§35) — it is not a
near-term phase.

> **Numbering note.** This roadmap was renumbered from an earlier mixed
> `M0–M10 / MA–MW / MH.x / MHZ.x` scheme into the phase-based decimal scheme
> below. Historical commit messages and notes use the old codes; the
> **Legacy code → new id** appendix at the bottom maps every one.

## Locked design decisions

Each decision below has a one-line statement and its authoritative home in
`DESIGN.md` / `PRINCIPLES.md`. Read the cited section for the full reasoning;
this list is an index, not a second source of truth. Items marked *(roadmap)*
are sequencing decisions with no other home.

- **`IMachine` = one authoring model, two link paths.** First-party machines
  statically linked; third-party machines as loadable modules behind a JUCE-free
  C ABI, fronted by `WrapperMachine`. Bespoke contract, not a CLAP/VST3 sub-host;
  no IPC, no sandbox. → DESIGN §2, §36 (full spec), §9.
- **Machines generate or capture; effects process.** Sources/routers/capture
  engines are machines; pure timbre processing is an `IEffect`. → PRINCIPLES
  "Machines generate; effects process"; DESIGN §29, §32.
- **Stock catalogue = the iconic set keyed to the lineage.** Sample/Slice (DT),
  FM (DN), Analog (A4), DrumSynth (RYTM), Digital (Monomachine), Percussion (modal),
  Static/Thru/Record/Loop (OT). Neighbour folds into Route; Syntakt = voices +
  a master-drive `IEffect`. Anything more specialised is a third-party module
  (e.g. granular — Aira P-6 / Tonverk — is a module, not stock). → DESIGN §29.
- **Variable parameter schema, declared per machine.** No fixed slot count; MZ
  shows `kMZSlots` (8) at a time, section bar has 6 keys; both paginate within
  what the machine declares. → DESIGN §2, §6.1.
- **Hybrid slot identity.** Integer index at runtime, stable string id on disk;
  survives slot reordering across releases. → DESIGN §2.
- **MIDI buffer + ParamFrame at the machine boundary.** Trig events become MIDI
  note-on/off; machines receive `(MidiBuffer, ParamFrame, AudioBuffer)`. → DESIGN
  §2, §4.6.
- **Voice topology is per-machine, per-block.** `currentVoices()` returns
  `V0..V4`, pulled per trig so mode flips take effect immediately; chord steps
  over the live voice count resolve via the per-track `NoteSelection`
  (Top/Bottom bias spread). → DESIGN §2.
- **No PCM in plugin state.** Sample refs = `{path, xxHash32}`. → PRINCIPLES
  "State refs, not contents"; DESIGN §7.
- **Single input gate.** All sources (MIDI CC/note, QWERTY, encoders, future
  hardware) route through `EditContext`; a held step receives P-Locks from any
  source identically. → DESIGN §5, §4.1.
- **Override-ELSE-Base is the only resolution rule.** Applies to machine
  ParamFrames and sequencer-scope trig fields alike; no reset sentinels, no
  precedence flags. → PRINCIPLES "Override-ELSE-Base"; DESIGN §4.1.
- **Focus is first-class state** (`{Global, Track1..16}`); `SelectedTrack`-scoped
  CCs and the contextual encoders follow it. → DESIGN §5.3.
- **Musical hierarchy: Set / Song / Scene / Phrase** (Phase 7; supersedes the
  earlier Octatrack-style `Project / Bank / Pattern / Part` from 2.2). Kit per
  (track, Song); Scenes launched live; Phrases shared by reference; core time
  per-Scene drives launch-quantize grid. → DESIGN §4.7, §4.8.
- **Auto-sync degradation:** clock dropout = freewheel; explicit stop = freeze.
  → DESIGN §4.3.
- **Performance grammar = scope + verb.** Cluster `Func/Track | Phrase/Scene |
  Morph/Song | Mute/Fill` + held-step + section keys; verbs `Record/Play/Stop/
  Yes/No`. Cross-column compounds only; `Func` is the universal qualifier; `Cue`
  reserved until 6.4. → PRINCIPLES "One grammar"; DESIGN §13.
- **Canonical sections reserved + machine extensions.** Keys 5–0 = TRIG / SRC /
  FILTER / AMP / MOD / FX; machines fill by meaning and may add extension pages.
  Sections are a scope-indexed matrix. → PRINCIPLES "Canonical sections"; DESIGN
  §6.
- **Control-All resolution: id-primary, role-fallback.** → DESIGN §13.1.
- **Hardware = fewer-key QWERTY, no new features.** → PRINCIPLES "Hardware =
  fewer-key QWERTY"; DESIGN §1.
- **No "design mode" vs "performance mode".** → PRINCIPLES "Performance is the
  goal".
- **The grid is the selection surface; the hold is the mode.** Pick-one-of-N is
  a step press, not a popup; modifier hold = mode duration, nothing left armed.
  → PRINCIPLES "The grid is the menu".
- **No hidden randomness in the editing surface.** Trig conditions are the only
  sanctioned RNG. → PRINCIPLES "Pragmatic determinism".
- **Beginner mode = more chrome, never less grammar.** Granular feedback toggles
  annotate; they never remove gestures. → PRINCIPLES "Chrome announces state";
  DESIGN §6.4.
- **Post-machine FLTR + AMP, machine-opt-out** via `hasInternalFilter()` /
  `hasInternalAmp()`; MIDI-out bypasses both. → DESIGN §14.
- **No song arrangement.** Scenes are launched live (`Scene + step`); Songs
  queued via `Song + step`. No arrangement track, no chain queue — the set
  order is performed, not stored. → DESIGN §16, §4.8.
- **More specific scope wins.** A live phrase deviation sticks; Scene launch
  re-asserts only non-deviated tracks; global unison swap skips already-deviated
  tracks. Re-sync is explicit (`Track + Scene` / `Scene + Yes`). → PRINCIPLES §13.
- **16-levels eligibility = role-tagged subset.** → DESIGN §20.
- **Microtiming = per-step P-lockable offset, ±50% of step**; `Quantize` zeros
  offsets in scope. → DESIGN §19.
- **Sampling input = plugin audio input only** (no system/device input);
  resampling reuses the flow with a track/master tap. → DESIGN §22.
- **Sample-name generator = 4 curated + 1 hash-derived.** → DESIGN §22.
- **Coarse-adjust = `Func` + encoder**, step unit-derived, per-`ParamSpec`
  override. → DESIGN §25.
- **MZ size is a single constant** (`kMZSlots`). → DESIGN §26.
- **State-colour taxonomy is canonical; specific colours are not.** → DESIGN §24.
- **Audio routing = explicit source-select, topo-sorted, cycles refused;**
  `input_source = Master` is the one sanctioned prior-block tap. → DESIGN §27.
- **Record buffers = volatile entries in the unified sample pool**, RAM-only,
  `REC`-badged; the §22 naming flow doubles as freeze-to-disk. → DESIGN §28.
- **Overwrite in Record, overdub in Loop.** No overdub state on the trig
  path. → DESIGN §29.
- **Three special trig types:** trigless/lock-only, one-shot, recorder trig.
  → DESIGN §30.
- **The Morph morphs parameters only, never trigs;** fader lerps continuous slots /
  snaps stepped slots. "Fluid mute" = morph-assigning AMP `Level`. → DESIGN §17.
- **Modifier-gated sculpting**: hold/latch `Morph` + encoder writes morph overlay at 1:1 normalised fader split; bare encoder writes kit base. `Morph + ^/v` forces pure A/B. → DESIGN §17.3/§17.6.
- **Cue = additive monitor send, never solo.** No cue output = no-op. → DESIGN §31.
- **AMP gate source `{Envelope | Held-open}`** — the basis of continuous Route and
  drones; subsumes the Thru/Neighbour split. → DESIGN §14, §29.
- **Foundation-owned effects: `IEffect`, 2 inserts/track + 2 master**, reuse the
  `ParamSpec`/`role`/P-Lock infrastructure, fill the canonical FX section. → DESIGN
  §32.
- **Surface model is the single source of truth for screen + hardware.** One pure
  `buildSurfaceModel()` both render from; `CellState` is add-only; base layer +
  closed decoration channels; controllers authored via `IControllerSurface` +
  `ControllerRegistry`. → DESIGN §24, §35.8.
- *(roadmap)* **Surface frozen at the 10×4 shape (3.1) / cluster identities (3.2).**
  Phases 4–6 author against that frozen surface; surface-affecting changes must
  re-open Phase 3, not bolt on.
- *(roadmap)* **The machine catalogue waits on the surface freeze and the SDK.**
  Catalogue machines 4.5+ are authored against the frozen surface and the Machine
  Module ABI (6.7), so they ship as modules from day one.
- **Reward mastery — no crutches, no dead weight.** A feature earns its place only
  if it rewards practice; it is rejected as a *crutch* (does the musical work for
  the user) or *dead weight* (cost never repaid in performance). → PRINCIPLES §14.
- **Deterministic generators print; stochastic authoring is refused.** A Euclidean
  fill or Density overlay is admissible *because* it is deterministic and
  leaves the trig data unchanged (subtractive only); engines that roll dice at
  edit time are out. → PRINCIPLES "Pragmatic determinism" / "Reward mastery";
  DESIGN §13.5, §39.
- **MOD is a shallow canonical promise.** Minimal performable modulation in the
  canonical section; deep modulation is machine-internal; no custom-LFO designer,
  no free automation lanes. → DESIGN §6.1.
- **Performance punch-in = thin Animate toggle, not a mode.** Momentary enable /
  bypass of existing inserts; no dedicated performance-FX mode. → DESIGN §32.5.
- **Non-goals are a maintained record.** What Lockstep refuses to become, with the
  competitor feature and rejecting principle for each. → `NON-GOALS.md`; PRINCIPLES
  "Non-Goals".

---

## Phase 1 — Core Sequencer  *[shipped]*

The empty-plugin-to-playable-sequencer foundation: clocking, the P-Lock model,
MIDI ingestion, the variable-schema machine boundary, the QWERTY editor, and
state serialization.

### 1.1 — Skeleton + buildable empty plugin  *[shipped]*  *(was M0)*
- [x] Root + `src/CMakeLists.txt` build `Lockstep` (Standalone / VST3 / CLAP,
      +AU on Apple) over a shared `lockstep_core` static lib.
- [x] All `core/ machine/ io/ state/ ui/` headers + stub `.cpp` compile under
      strict warnings.
- [x] `IMachine`, `SampleMachine` stub, `Clock`, `Sequence`, `Track`, `Step`,
      `PLock`, `StateResolver` in place.
- [x] `processBlock` exercises the full pipeline every block; output is silence,
      no NaNs, no crashes. APVTS round-trips through host save/load.

### 1.2 — Audible sampler  *[shipped]*  *(was M1)*
- [x] Sample loader (drag-drop / dialog) → `SamplePool`, `xx32`-hashed.
- [x] Monophonic voice playback, linear interpolation, AHDSR envelope.
- [x] Choke: 1–2 ms micro-fade before retrigger (no clicks).
- [x] Output stage: DC blocker, soft-clip limiter, `output_gain` smoothing.
- [x] Self-test: kick-on-every-step pattern, no clicks, stable amplitude.

### 1.3 — Polymetric clocking + multi-track  *[shipped]*  *(was M2)*
- [x] Per-track length `[1..64]` + divider, persisted.
- [x] Step-grid pagination for patterns > 16 steps.
- [x] Modulo-against-shared-position resolution (7-vs-16 phasing verified).
- [x] Eight tracks → one stereo bus (sub-bus split lands later).

### 1.4 — P-Locks + trig conditions  *[shipped]*  *(was M3 + M4)*
- [x] Hold-step gesture sets `EditContext::active` against that step; writes
      route to the correct layer by the flag; held + locked steps render
      distinctly; clear-lock gesture removes a slot override.
- [x] Probability evaluator (deterministic per-pattern seed).
- [x] Iteration (`m:n`) with a per-track counter surviving loops.
- [x] Previous-dependency state machine.
- [x] Track-level base condition (`Track::baseCond`) with Override-ELSE-Base
      fallthrough; probability + m:n UI-exposed, prev-dep in-struct.

### 1.5 — MIDI ingestion layer  *[shipped]*  *(was M5)*
- [x] Absolute CC with soft-takeover; relative CC delta arithmetic.
- [x] `EditContext` interception identical for CC / encoder / QWERTY.
- [x] MIDI Learn UX; per-mapping scope `{Master | Track[N] | SelectedTrack}`,
      project-saved.
- [x] Channel modes (Omni→Selected, Per-Track); contextual encoders driving the
      focus quadrant; note-on triggers the destination machine.
- [x] Pitch-recording: note-on while a step is held writes the pitch (single
      input gate, no record-arm needed).
- [x] Standalone MIDI clock input; Locked / Auto sync (freewheel-on-dropout,
      freeze-on-stop).

### 1.6 — Variable-schema machine pivot + MIDI boundary  *[shipped]*  *(was MA)*
The refactor that retired the 48-slot fixed `IMachine` for per-machine schema.
- [x] Per-machine `ParamSpec` list (id/label/range/default/stepped/unit/section);
      dropped the fixed slot/page constants.
- [x] Machine-sized `ParamFrame` (resolver-owned vector); `process()` takes
      `std::span<const float>`.
- [x] Hybrid slot identity (id↔index map; serializer translates; unknown ids
      dropped with a log).
- [x] Per-machine voice topology (`currentVoices() → V0..V4`, pulled per trig);
      chord-clamp via `NoteSelection`.
- [x] New `process(MidiBuffer, ParamFrame, AudioBuffer)` signature; sequencer
      injects a note-on per trig; external MIDI mixed in.
- [x] Per-track sequencer-scope trig fields (`defaultNote/Velocity/gateLength`)
      + per-step overrides; resolver applies OEB.
- [x] Dropped `noteMode`; pitch-record writes `step.noteOverride`.
- [x] SectionBar + ManipulationZone re-wired to read schema; meta sections
      re-laid-out. Sample cleanup (gate now sequencer-scope).
- [x] 1.1–1.5 features verified end-to-end through the new boundary.

### 1.7 — QWERTY overlay + Manipulation Zone  *[shipped]*  *(was M6)*
- [x] `QwertyOverlay::resolve` mapping; MZ live parameter widgets from machine
      metadata; SectionBar with per-machine labels + page cycling.
- [x] COND / TRACK / TRIG / GLOBAL meta sections wired into the MZ.
- [x] StepGrid (2×8 + paginate, trig toggle, hold, P-Lock indicators).
- [x] Step-state preview (fire / skip / probabilistic brightness, prev-dep
      propagation).
- [x] Transport keys; step-grid display modes (Staggered / Ortholinear / Clean).

### 1.8 — Pattern recording + serialization  *[shipped]*  *(was M7 + M8)*
- [x] Record-arm transport state; note-on-while-recording writes a quantised
      trig; CC-while-recording obeys EditContext; key-as-PLock mode.
- [x] Sequence + P-Locks serialized; sample pool as `{path, xxHash32}` with
      relink UX; real `Hash::xx32`; forward-compatible version guard.
- [x] CC mappings, channel mode, focus, clock/sync persisted.

---

## Phase 2 — Performance Grammar  *[shipped]*

The non-DSP backbone: the scope+verb input model, the Project/Bank/Pattern/Part
hierarchy, the performance modifier cluster, canonical sections + post-machine
FLTR/AMP, the first-class MIDI-out machine, and the 16-track expansion.

### 2.1 — Scope+verb grammar  *[shipped]*  *(was MB)*
- [x] `ControllerEvent` stream between all input sources and the editor.
- [x] `EditMode` state machine tracking held scopes + implied target set.
- [x] Verb keys dispatch through `EditMode` (copy/paste/clear/checkpoint).
- [x] N-held-step support (ordered by press); `EditContext` exposes the held set.
- [x] Step-grid modal-surface scaffold + mode-indicator chrome.
- [x] Chosen QWERTY keys for the scopes; scope-state chrome.

### 2.2 — Project / Bank / Pattern / Part hierarchy  *[shipped → superseded by Phase 7]*  *(was MC)*
The Octatrack-style hierarchy shipped here is fully replaced by the
musical hierarchy in Phase 7 (`Set / Song / Scene / Phrase`). The
Phase 7 stages carry out the re-architecture; the code from 2.2 is the
starting point for the refactor.
- [x] `Project` / `Bank` / `Pattern` / `Part` data model (machine identity in
      the Part); resolver against the active (Pattern, Part).
- [x] v2 serialization with v1 auto-upgrade.
- [x] Pattern-switch queue gesture (`QUE:B.P` badge); part-sharing (`SHR:N`) +
      fork; chain mode (`CHN:N`); unknown-machine → `StubMachine` fallback.

### 2.3 — Performance modifier cluster  *[shipped]*  *(was MD)*
- [x] Scope-typed clipboard (step/section/track/pattern), multi-step offsets,
      in-memory.
- [x] Copy/Paste/Clear for step / section / track / pattern scopes.
- [x] Global + pattern mutes; deferred multi-select-on-release.
- [x] `Fill` momentary modifier (`Always/OnlyFill/NeverFill`).
- [x] Control-All (id-match broadcast, base + P-Lock).
- [x] Checkpoint stack (RAM-only LIFO, depth 8, `CK:N` badge).

### 2.4 — Canonical sections + post-machine FLTR/AMP + role tags  *[shipped]*  *(was ME)*
- [x] `ParamSpec::role` closed enum; existing machines tagged.
- [x] Section-bar canonical reservation; machine labels match canonical titles
      where applicable; extension-page cycling.
- [x] Per-track post-machine FLTR (multi-mode SVF, slope, cutoff/res/drive/
      env→cutoff) in `Part::track[i].fltrState`.
- [x] Per-track post-machine AMP (AHDSR, pan, level, gate source
      `{Envelope|Held-open}`) in `Part::track[i].ampState`.
- [x] Machine opt-out (`hasInternalFilter()` / `hasInternalAmp()`).
  *(Note: `hasInternalFilter()` deleted in 8.28; FLTR is now always-present
  with OFF mode. `hasInternalAmp()` retained to gate the ENVELOPE block only.)*

### 2.5 — MIDI-out machine (first-class)  *[shipped]*  *(was MF)*
- [x] `MidiOutMachine` (`currentVoices() = V0`); schema `dest/channel/program/
      cc[0..15]`; destination enumeration + stable-id persistence.
- [x] Channel + program P-locking with clean note-offs on channel change.
- [x] Configurable CC numbers + labels; `<CCConfig>` serialization.
- [x] FLTR/AMP bypass → section keys repurposed to CC-bank pages.
- [x] All-Notes-Off + Reset-All-Controllers on stop.
- [x] Verified participation in Control-All / Fills / Mutes / Copy-Paste /
      Checkpoints.
- [x] Hardware factory tables: Digitakt, Digitone, Syntakt, A4, Rytm, Octatrack,
      Tonverk (`MidiDevicePresets`). Preset-selection UI deferred to 5.8.

### 2.6 — 16-track expansion + header pagination  *[shipped]*  *(was MGX)*
- [x] `kNumTracks = 16`; arrays scale off the constant.
- [x] Default split: tracks 1–8 Sample, 9–16 MIDI-out; old 8-track saves load
      cleanly.
- [x] Track + Mute layers extended to 16; track-header pagination (`1–8` / `9–16`
      page toggle, keyboard auto-flip); MIDI-out `M` badge.
- [x] Runtime machine reassignment shipped via the Func+Part picker (see 3.5),
      superseding this milestone's deferred machine-select stub.

---

## Phase 3 — Control Surface  *[shipped; 3.11 active]*

The 10×4 control-surface and grammar revamp, frozen so the catalogue (Phase 4)
authors against a stable contract. 3.1 froze geometry; 3.2 froze the cluster +
section matrix; 3.3–3.10 closed chrome and grammar gaps; 3.11 (pattern length)
is the one open item.

### 3.1 — The 10×4 surface revamp  *[shipped]*  *(was MHX)*
DESIGN §33. Widened 9×4 → 10×4: an eight-key one-hand modifier cluster + the
8-wide functional block.
- [x] `QwertyOverlay` rewrite to 10×4; step keys → `D–;` / `C–/`; function strip
      relocated; compound-chord engine (cross-column only, `Func` universal).
- [x] `kMZSlots` 4 → 8 (4×2 MZ); editor re-layout with the vertical crossfader
      placeholder; four-register cell typography; surface freeze confirmed.

### 3.2 — Section matrix + modifier-cluster rethink  *[shipped]*  *(was MHY)*
DESIGN §6, §13, §33.
- [x] Cluster identity `Func/Track | Pattern/Part | Scene/Master | Mute/Fill`;
      `Part` added to the `Scope` enum; `Cue` reserved (unbound).
- [x] Canonical rename `LFO → MOD`; `ParamSpec.variant {Primary, Secondary}`.
- [x] Right-utility row → `Yes / Rec / Play / Stop / No`; `3` keeps TAP.
- [x] Scope-section matrix scaffold (`ScopedSectionMatrix.h`); reactive
      SectionBar chrome.
- [x] Machine-select moved to `Func+Part` (was `Func+R`).
- [x] Doc + glossary pass.

### 3.3 — Surface chrome (typography, label rule, scope colour)  *[shipped]*  *(was MHZ.1)*
- [x] Key-cell typography pass; 6-character label ceiling; longer abbreviations.
- [x] Unified `resolveKeyLabel(KeyDef, UiState, EditContext)` collapsing the
      ad-hoc label branches into one rule.
- [x] Scope colour grammar in `UITheme.h` (step + track/pattern/part/machine/
      scene/master); always-on hints where the secondary meaning is invariant.

### 3.4 — Contextual modes (scope re-skin, top bar, MZ streamline)  *[shipped]*  *(was MHZ.2)*
- [x] Step-grid scope re-skin (1-of-16 selector; pagination suppressed;
      unavailable indices dim; scope tint).
- [x] Top-bar dashboard (BPM, Bank/Pattern/Part, position, chain, `CK:N`) +
      live held-context preview, both off one view-model.
- [x] MZ streamline (rotary + one value display; textual values via
      `ParamSpec::valueLabels`); double-click rotary → default.

### 3.5 — Note capture, P-Lock clear, step-driven edit  *[shipped]*  *(was MHZ.3)*
- [x] Step-hold MIDI capture as the canonical chord-edit path (commit on
      release; empty = no-op). Replace-on-hold is the chord editor.
- [x] P-Lock clear gestures (`Trig+Func+Stop` = all; `Trig+slot+Stop` = one).
- [x] Step-driven P-Lock clear mode (`Func+step`, orange re-skin).
- [x] Machine picker migrated to `Func+Part` (Part relabels `MACH`); this also
      delivers runtime machine reassignment (2.6's deferred item).

### 3.6 — Polyphonic step authoring + Analog para topology  *[shipped]*  *(was MHZ.4)*
- [x] Realtime chord record (aggregate notes on the same step, cap 4).
- [x] Step-hold snapshot-currently-held capture; multi-step parallel.
- [x] Note-count badge (1–4 ticks) on step cells.
- [x] P-Lock clear mode packed + toggle-until-commit.
- [x] Analog paraphonic osc-by-slot routing + shared noise.
- [x] Keyboardless note-edit mode (1-octave chromatic overlay, octave shift).
- [x] NoteSelection (TOP/BOT bias) in the TRIG meta-section.

### 3.7 — Engine hygiene (first-trig, envelopes, RETRIG, skew)  *[shipped]*  *(was MHZ.5)*
- [x] First-trig loudness fix across FM / Analog / DrumSynth / Sample.
- [x] `ParamSpec::skew` (non-linear encoder mapping; raw on disk); envelopes
      re-authored.
- [x] Per-track RETRIG mode (`LEGATO / RETRIG / FREE`).
- [x] Trig/notes decoupling gesture (`Trig+Func+No` strips notes, keeps trig +
      P-Locks); octave-badge legibility.

### 3.8 — Record-time capture parity (velocity + musical gate)  *[shipped]*  *(was MHZ.6)*
- [x] Quantised realtime record captures velocity + gate.
- [x] Gate length as musical time (`gateValue`, plain/dotted/triplet) resolved at
      emit time; serializer upgrade from `gateMs`.
- [x] Per-note velocity in `TrigOverride`; emitted note-ons carry it.

### 3.9 — Per-track input modes (CHROMATIC, LEVELS) + overwrite/overdub  *[shipped]*  *(was MHZ.7 + MHZ.7.x)*
Subsumes the old MM (16-levels) as the LEVELS mode and the old MG Keyboard mode
as CHROMATIC.
- [x] `TrackInputMode {PLAY, EDIT, CHROMATIC, LEVELS}` on the focused track.
- [x] CHROMATIC: step cells = 1-octave keyboard (NavUp/Down octave shift);
      record-arm captures via the 3.8 path.
- [x] LEVELS: step cells = quantised velocity buckets (held-step / base /
      record-arm semantics). Generic role-tagged target deferred to 5.7.
- [x] Realtime record defaults to overwrite; double-tap Record → overdub (amber
      "OD"); single-tap returns to overwrite.

### 3.10 — Latch (hands-free virtual-hold) + Track+Nav mode cycle  *[shipped]*  *(was MHZ.9)*
DESIGN §13.7. Adds no new per-key meaning — latch is persistence of an existing
hold.
- [x] Latch state (7 latchable modifiers + latched steps); `xxxHeld` stays the
      effective (physical OR latched) value.
- [x] Generalised `DoubleTapDetector`; double-tap a modifier latches it (column
      exclusivity); double-tap again releases.
- [x] `Func` never latches; double-tap `Func` = universal escape (only when
      latches exist, so deferred `Func` key-up flows are untouched).
- [x] Step latch / operand (net-zero trig on latch-in); latch-pip chrome.
- [x] `Track + NavUp/Down` cycles `PLAY ↔ CHROMATIC ↔ LEVELS` (supersedes the
      earlier interim verb radio); a mode switch escapes all latches.
- [ ] Standalone verification sweep (a–f) — feature shipped; final scripted
      run pending.

### 3.11 — Pattern-length authoring  *[absorbed → Phase 7.5]*  *(was MHZ.8)*
The length-authoring UX is fully absorbed into **Phase 7, Stage E** (7.5),
where it ships as part of the complete phrase model: per-track `Phrase.length`,
the momentary re-skin, the double-tap-NavRight scroll-past-end, the TRACK encoder
home, and the CellState tokens (`LengthInRun / LengthBoundary / LengthOutRun`).
The gestures are updated in §34.4. See 7.5 for the full checklist.

---

## Phase 7 — Musical Hierarchy Re-architecture  *[active]*

Full replacement of the `Project > Bank > Pattern > Part` (Octatrack-style)
container model with a musically-derived model (see DESIGN §4.7, §4.8, §16).
Supersedes **2.2**; absorbs **3.11**; rescopes **5.2** and **5.3**. State
format: **clean break + version bump** (pre-release; no faithful legacy
migration). One commit per stage minimum.

> **Vocabulary refinement (post-ship).** Stages 7.0–7.7 shipped the model
> with the working names `Set / Piece / Section / Phrase` (structs `Piece`,
> `Section`, `TrackKit`; gestures `Part/Master + step`). A naming pass then
> aligned the user-facing vocabulary with DAW convention — **`Set / Song /
> Scene / Phrase`**, the A/B morph renamed **Morph**, the per-`(Track,Song)`
> container `SongTrack`, full-word caps `FUNC TRACK PHRASE SCENE MORPH SONG
> MUTE FILL`. **Docs are updated; the code struct/symbol rename is a tracked
> follow-up (Stage 7.9).** Shipped `[x]` items below keep their original
> code-symbol names because the code still uses them.

### 7.0 — Stage 0: Documentation  *[shipped]*
Docs first — PRINCIPLES → DESIGN → ROADMAP — before any code changes.
- [x] `PRINCIPLES.md`: add *"More specific scope wins"* (§13).
- [x] `DESIGN.md`: rewrite §4.7 (musical hierarchy), add §4.8 (core time),
      rewrite §16 (launch model), update §17.1 (scenes on Section), update §13
      scope+verb grammar table, rewrite §34.4 (phrase-length authoring).
- [x] `ROADMAP.md`: Phase 7 added; 2.2 superseded; 3.11 absorbed; 5.2/5.3
      re-scoped; locked-decisions and legacy-appendix updated; header updated.

### 7.1 — Stage A: Core data model  *[shipped]*
New `src/core/` structs added alongside legacy (legacy removed when editor
migrates in Stage D+). Old Bank/Pattern/Part/Sequence kept as compat stubs.
- [x] `TrackKit`, `Phrase`, `Section`, `Piece`, `TimeSig` structs in place.
- [x] Constants: `kNumPieces = kSectionsPerPiece = kPhrasesPerTrack = 16`.
- [x] `Project.h` adds `pieces[]` + `launchQuantizeBars` alongside `banks[]`.
- [x] `PluginProcessor.h` adds `piece()/section()/lane(t)/kit(t)` +
      `activePieceIdx_/activeSectionIdx_` alongside legacy accessors.
- [x] Build clean under strict warnings.

### 7.2 — Stage B: Processor state + resolvers  *[shipped]*
- [x] `deviated_[]`, `deviationPhraseIdx_[]`, `phraseEndMode_[]` arrays.
- [x] Mute logic: `patternMutes[]` → `!section().activeMask[]`.
- [x] FLTR/AMP: `activePart().tracks[i].{fltr,amp}State` → `kit(i).*`.
- [x] `setTrackMachine`: mirrors `kit(t).*` alongside old Part write.
- [x] New methods: `activePhrase(t)`, `setActiveSection()`, `setActivePiece()`,
      `syncSequenceFromCurrentSection()`, `reinstallMachinesFromActiveKit()`.
- [x] Startup seed: Piece[0]/Lane kits seeded alongside legacy Bank[0].
- [x] Build clean.

### 7.3 — Stage C: Core time + launch engine  *[shipped]*
- [x] `Metronome` parameterized by numerator/denominator (4/4 default).
- [x] `queuedSectionIdx_` atomic + `queueSection/cancelQueuedSection/hasQueuedSection`.
- [x] Section launch at `ceil(blockStart / barPpq) * barPpq` boundary.
- [x] Metronome passes `section().coreTime` to `Metronome::process()`.
- [x] Legacy pattern queue engine preserved alongside (removed in Stage D).

### 7.4 — Stage D: Gestures / dispatch  *[shipped]*
- [x] `Part + step` → `queueSection()` (playing) / `setActiveSection()` (stopped).
- [x] `Track + Pattern + step` → `swapPhraseForTrack()` (sticky deviation).
- [x] `Pattern + step` → `swapPhraseForAll()` (non-deviated tracks).
- [x] `Track + Part` → `resyncTrackToSection()` (fires on Part key-down with Track held).
- [x] `Part + Yes` → `resyncAllToSection()`.
- [x] `Master + step` → `setActivePiece()`.
- [x] `Part + Record` → `commitSectionState()`.
- [x] `Part + Stop` → `cancelQueuedSection()`.
- [x] Fork/chain/queue-pattern gestures removed from editor dispatch.

### 7.5 — Stage E: Surface model + UI  *[shipped]*
- [x] `LengthInRun(90)`, `LengthBoundary(91)`, `LengthOutRun(92)`, `SelectorDeviated(95)`
      CellState tokens (add-only); `compatColour()` entries.
- [x] Pattern scope re-skin → 16 phrases per track; deviation badge (`SelectorDeviated`).
- [x] Part scope re-skin → 16 sections; queued-section `SelectorNext` badge.
- [x] Phrase-length re-skin: `Phrase+Func` (focused) / `Morph+Func` (broadcast) momentary
      branch; `LengthInRun/LengthBoundary/LengthOutRun` per absolute step index.
- [x] Length-**write** gestures wired (`Phrase+Func+step` focused / `Morph+Func+step`
      broadcast → `setTrackLength`); the re-skin had shipped visual-only. (DESIGN §34.4.)
- [x] `isTrackDeviated(t)` / `deviationPhraseIdxForTrack(t)` public accessors.
- [x] Double-tap-NavRight scroll-past-end unlock (`clampStepPage`/`PageNav.h`; nav-row
      reveals the empty page).
- [x] `SurfaceModelTest.cpp` length-edit + page-clamp assertions (`lengthEditCellState`,
      `clampStepPage` pure helpers).

### 7.6 — Stage F: Serialization (clean break)  *[shipped]*
- [x] `kCurrentVersion = 5`; `upgrade_v4_to_v5` drops old Project node.
- [x] `writeNewHierarchyNode` / `readNewHierarchyNode`: Piece → Lane(Kit+Phrases),
      Section(phraseIdx, activeMask, coreTime). Legacy node preserved as fallback.
- [x] `writePhraseNode/readPhraseFromNode`, `writeKitNode/readKitFromNode` helpers.

### 7.7 — Stage G: Scene hooks  *[shipped]*
- [x] `Section.sceneA/sceneB` maps serialized under `<SceneA>/<SceneB>` children.
- [x] Runtime crossfader resolver deferred to ROADMAP 5.2.

### 7.8 — Stage H: Verification  *[partial — code shipped; manual play-test pending]*
- [x] Build clean (tests `-Werror` under Clang); all tests pass (incl. new 7.5).
- [x] Standalone binary built; no crashes / NaNs on cold start (startup self-tests
      pass, incl. the v8 serializer round-trip).
- [x] Update "Active focus" to next milestone on completion (now: 6.7 ABI).
- [ ] **Manual** live play-test: Scene launch (quantized), phrase swap (sticky
      deviation), re-sync, two-layer mutes, Song switch, polymeter.
- [ ] **Manual** phrase-length authoring: `Phrase+Func+step` (focused) /
      `Morph+Func+step` (broadcast) write + re-skin; double-tap-NavRight
      scroll-past-end reveals one empty page and the nav row shows `Length: N`.
- [ ] **Manual** VST3/CLAP save → reload round-trips the current serializer
      format (v8 when written; **v14** as of 6.5) in Reaper/Bitwig.

### 7.9 — Stage I: Vocabulary rename  *[a–d + doc redesigns shipped; e pending]*
Align names with DESIGN's refined vocabulary (`Set/Song/Scene/Phrase` + Morph;
see the Phase 7 header note). Docs landed first; the pure rename followed in
build-verified sub-stages 7.9a–d.
- [x] Docs: DESIGN (§4.7/§4.8/§13.6/§16/§17 + reference tables), PRINCIPLES §13,
      this ROADMAP (summary + planned items + legacy note).
- [x] **7.9a** Visible label strings (caps full words `FUNC TRACK PHRASE SCENE
      MORPH SONG MUTE FILL`; verb `RECORD`; `S-MUTE`; nav/chrome).
- [x] **7.9b** Scope enum + UiState/LatchState/ModPhysHeld + ControllerButton +
      colour constants (`Pattern→Phrase`, `Part→Scene`, `Scene→Morph`,
      `Master→Song`; `kScopePhrase/Scene/Morph/Song`).
- [x] **7.9c** Core structs `Piece→Song`, `Section→Scene`, `Lane→SongTrack`
      (files moved); coupled methods/members; serializer tags + state version
      `5→6` (`Song/SongTrack/Scene/MorphA/MorphB`). `sceneA/B→morphA/B`.
- [x] **7.9d** Machine/Kit picker `Func+Part → Func+Track`; `KIT` + `GLOBAL`
      func-labels; `MACH` removed from the Scene key.
- [x] Resolve flagged ⚑ DESIGN redesigns: §6.1.2 Phrase/Scene matrix rows
      (content pass from §4.7 ownership), whole-Scene copy verb (`Func+Scene`
      lifts copy/paste/clear; bare `Scene+Play`=launch-now, `Scene+Stop`=revert,
      §16), §23 management UI (re-derived Song/Scene/Phrase/Kit; SHR:N moves to
      Phrases; Fork-Part → Phrase-fork), §33.2 slate (settled to historical record).
- [x] **7.9e** Checkpoint scope-respecting stacks (DESIGN §13.6: default Song;
      Track/Scene/Phrase; floor = saved state; reload-on-release). `Func+Yes`
      snapshots the held scope; `Func+No` tap=pop/hold=floor. CK:N badge shows
      scoped depth. Legacy Bank/Pattern/Part deleted; serializer v7.

#### 7.9e-pre — Legacy `Pattern/Part/Bank` consolidation *(the unfinished Stage B/D removal)*
The processor still runs sound-state on the legacy `project_.banks[].patterns/
parts` (`activePattern()`/`activePart()`). The new Song/Phrase/Kit model was a
one-way shadow: `syncSequenceFromCurrentScene()` projected Phrase/Kit → the
legacy working `sequence()` but never wrote back, so a scene/song/phrase switch
silently dropped live edits. 7.9e cannot snapshot a Song/Kit faithfully until
this is retired. Plan: test net → SoT fix → go direct → 7.9e.
- [x] **Test net** (stage 1): `StateResolverTest` (OEB merge) + `HierarchyNavTest`
      pin the Phrase/Kit⇄Track projection contract. New pure seam
      `src/core/HierarchyNav.h` (`projectPhraseToTrack` + reversible write-back).
- [x] **Extract switching to core** (stage 2a): `src/core/Arrangement.h` owns the
      Songs + playhead + working Sequence; every switch writes back before
      re-projecting (fixes the lost-edits bug). `ArrangementTest` proves it.
      Pure/JUCE-free; processor not yet wired.
- [x] **Wire processor to Arrangement** (stage 2b): Songs + playhead + deviation +
      working buffer now live in `arrangement_`; `song()/section()/kit()/sequence()`
      + the switch gestures delegate to it; dropped `Project::pieces` (~50MB) and the
      `activeSongIdx_/...` members. Write-back wired at save / load / ctor-seed.
      Legacy `Part` kept for FLTR/AMP + machineId sound state (still serialized).
- [x] **Move sound-state into Kit** (stage 3a): machineId + MIDI-out + base params
      all single-sourced to `Kit`; `copyKitTrack` replaces `copyPartTrack`; dead
      legacy nav (`setActivePatternPart`, `forkActivePart`, `materialise*`) removed.
- [x] **Drop legacy + go direct** (stage 3b): `Bank/Pattern/Part` structs + source
      files deleted; serializer v7 (new-hierarchy only); `project_.banks` removed;
      `removeSample`/`swapSamples` remapped to Arrangement songs + working buffer.
- [x] **7.9e** Checkpoint scope-respecting stacks built on the consolidated model.

> Note: a full save→load round-trip test needs `LockstepProcessor` under test
> (its `createEditor` pulls the UI in), so it is out of the headless net for now;
> the serializer's pure `applyUpgrades` chain stays covered by the existing
> `PluginState` UnitTest, and `Arrangement` covers the switching contract.

### 7.10 — Scene model redesign (floor + overlay)  *[shipped]*
From the manual-test design conversation: a Scene = a **saved floor** (global
pattern + committed deviations, + activeMask/coreTime/Morph) plus a **live
overlay**; single/double-tap launch (single keeps the overlay, double reverts to
floor); `Scene+Record` commits. New phrase grammar; `Track+Scene`/`Scene+Yes`
re-sync dropped. See DESIGN §4.7/§16/§13.6, PRINCIPLES §13.
- [x] Design written into DESIGN §4.7/§16/§13.6 + gesture tables + PRINCIPLES §13.
- [x] **Build 1**: `Scene::globalPhrase` (serialized) + grammar — `setGlobalPhrase`
      (Phrase+step: set global, focused rejoins, others kept), `forceAllToPhrase`
      (Scene+Phrase+step); editor/processor rewired; `ArrangementTest` pins it.
- [x] **Build 2**: dual-marker phrase selector (home/global border = new add-only
      `CellState::SelectorHome`; fill = current playing phrase) + persistent
      deviation badge (`SurfaceModel::trackDeviated[]` + amber strip marker).
- [x] **Build 3**: per-scene live overlay (remembered) + single/double-tap launch.
      `Arrangement` keeps a runtime per-(song,scene) overlay store; single-tap
      launch stashes the departing scene's deviations and restores the arriving
      scene's; double-tap (`setActiveSceneToFloor`, `queueScene(toFloor)`) arrives
      at the saved floor and discards the overlay; `commitSceneState` folds the
      overlay into the floor and clears the stash. `ArrangementTest` pins remember /
      double-tap-discard / commit-fold.
- [ ] (Later) scene-copy-on-create + conflict hints (§23); pattern chaining (§16).

### 7.11 — Manual-test bug fixes
**Round 1 *[shipped]***: serializer scene persistence + version chain; Track-compound
dispatch (machine picker, Track+Phrase deviation) + P-lock clear no-toggle;
FLTR/AMP→Kit (audio); AMP held-open (one-shots); transport pause/resume;
scene-mute→`Scene+Mute` + nav labels + PANIC teal; empty-track overlay; README refresh.
**Round 2 *[shipped]***:
- [x] Sample-pool `addItem(0)` assertion → section header.
- [x] MZ "X" P-lock clear toggling the trig → `markParamWritten`.
- [x] P-lock latch exit on any step.
- [x] Scene-mute **visuals** (`S-MUTE` under Scene-held + scene-mute grid view).
- [x] **Phrase-content persistence** — load clobbered non-active scenes' phrases via
      a stale write-back; added `Arrangement::loadPosition` (no write-back) for the
      load path; `ArrangementTest` regression.

### 7.12 — Colour vocabulary + verb-row rethink  *[shipped]*
UITheme rewrite: one hue per modality (resting/active/accent stepped from B~35%/80%/94%);
role-neighbourhood hue map (Func=amber, structural scopes=cool arc, Morph=magenta,
Performance=warm, Verbs=neutral-slate with conventional-on-active); removed generic
violet/indigo modifier borrow. Verb row redesigned Y U I O P →
Snapshot/Record/Play/Clear/Yes with Func-layer Restore/Panic/Delete/No.
See DESIGN §6.6, §13.

- [x] Stage 1: append `VerbClear` / `VerbDelete` / `VerbPanic` to `ControllerButton` enum (ABI add-only); exhaustive-switch audit.
- [x] Stage 2: QwertyOverlay rebind + dispatch semantics + pending-confirm state + re-homed solo/delete gestures.
- [x] Stage 3: SurfaceModel `kFRowDefs` labels + relabels + scope-glow / reserved-dim update.
- [x] Stage 4: UITheme taxonomy rewrite + `groupForCell()` + named decoration constants.
- [x] Stage 5: fold stray hardcoded colour literals into UITheme constants.
- [x] Stage 6: DESIGN §6.6 + §13 verb table updated; ROADMAP marked.

### 7.13 — Scene commit-and-bake + placeable payloads + Scene clipboard  *[shipped]*
Resolves the model-2-vs-3 tension from the §4.7 design conversation: a
hand-curated heterogeneous arrangement is persisted by **baking** the live
per-track deviations into phrase *content*. `Scene + Record` becomes
**commit-and-bake**, guarded by Yes/No confirmation (destructive: overwrites
phrase slots, severs phrase sharing). Enables the scratch-pad workflow —
audition ideas via deviation, bake the keepers. See DESIGN §4.7 / §16 / §13.2.
*(Note: `globalPhrase` routing field added here was later removed in 7.14.)*

Core shipped (Stages 1–3, prior plan):
- [x] `Scene::phraseIdx[]` removed; floor routing via `globalPhrase` index
      (serializer bumped to v8 — clean break; v11 in 7.14 removes globalPhrase).
- [x] `Scene + Record` dispatch → `PendingConfirm::BakeScene`; `P=Yes` bakes,
      `Func+P` cancels; zero-deviation case bails early with no confirm.
- [x] Bake op in `Arrangement::bakeSceneState()`: for each deviated track, copy
      effective phrase content into the home slot, clear deviation.
      `ArrangementTest` pins bake + no-op + same-slot cases.
- [x] Confirm chrome: affected-track count + `SHR:N` when another initialised
      scene shares the home phrase.

Follow-up plan (placeable payloads + Scene clipboard + omni copy) shipped:
- [x] **Scene occupancy helpers**: `Scene::initialised` set on all live-mutation
      paths; `sceneSlotOccupied`, `firstFreePhraseSlot`, `phraseSlotSharers`.
- [x] **Placeable payloads** (§23.3): `Scene+empty-step` = baked copy,
      `Func+Scene+empty-step` = default/empty. `Func+Scene+occupied-step` = floor launch.
      Conflict gate (`phraseSlotSharers` / phrase content) raises Yes/No confirm.
- [x] **Scene clipboard** (`ClipboardType::Scene`, `SceneClip`): `Func+Scene+Record`
      = capture active scene (floor + all effective phrases); `Func+Scene+Play` = baked
      paste; `Mute+Func+Scene+Play` = floor-only paste. Conflict-gated (same gate).
- [x] **Omni copy** `Func+U` (`ClipboardType::All`): captures all layers (scene +
      track + pattern) in one grab; badge shows `CPY:ALL`.
- [x] **Unqualified paste** `Func+I`: stamps the single captured layer via type tag;
      rejects with "Paste: pick a scope" when type is `All` (omni grab).
- [x] **Panic → Song + Clear (O)**. `Func+I` freed for unqualified paste.

### 7.17 — Swing anchored rotary + reusable reference-mark element  *[shipped]*

Replaces the two-slot (editable + read-only `Effct`) swing band with a single
`"Swing"` rotary showing the **cumulative groove at the held scope** and
scope-coloured reference ticks marking the inherited floor:

- [x] **`ReferenceMark`** struct added to `SurfaceModel.h`; `marks[2]` carried
      on both `SurfaceSlot` and `MetaFieldView` — data-model is ready for
      controllers (no controller render yet).
- [x] **Semantic fix:** `swingSongTrackShown` and `setSwingSongTrack` now include
      `section().swing` — track-scope cumulative was previously off by the scene
      delta. Storage of the raw `SongTrack::swing` delta is unchanged; no
      serializer bump.
- [x] **Single rotary:** `buildSwingBand` builds one active slot whose value is
      the cumulative swing at the held scope. Drops the `Effct` slot and the
      `SwScn (D)` / `SwTrk (D)` labelling; label is `"Swing"` throughout.
      Tick model: 0 ticks at song scope; 1 gold tick (song floor) at scene
      scope; faint gold + green ticks (song + scene floors) at track scope.
- [x] **`MetaRotaryLookAndFeel`** (new `src/ui/MetaRotary.{h,cpp}`) overrides
      `drawRotarySlider` to honour `RingMode` on screen (fixes bipolar params
      rendering as unipolar fill) and draw reference ticks.
- [x] **`ManipulationZone`** uses `MetaRotary` instead of `juce::Slider`; machine-param
      branch now sets `ringMode` from the param spec.
- [x] **Docs:** DESIGN §19.2, README §5.8.

The `ReferenceMark` element is reusable for any layered/delta parameter —
morph is the obvious next consumer (DESIGN §19.2).

### 7.16 — MetaBand unification: controller exposure + transient dismissal  *[shipped]*

Fixes four defects in the 7.15 meta-band implementation:

- [x] **#1 Controller exposure:** `buildSurfaceModel` and `applyParamDelta` now
      branch on `resolveMetaBand(ui)` — meta bands (COND/TRIG/DIV/PHRASELEN/GLOBAL/
      Swing) render ring positions, labels, and values on Push1/X-Touch and encoders
      write through `writeMetaField`.  Screen and controller share the same builder
      (`MetaBand.{h,cpp}`) and cannot diverge.
- [x] **#2 Picker leak fixed:** `samplePickerBtn_` is now hidden at a single central
      site in `ManipulationZone::refreshSliders` whenever band != None, so the sample
      selector no longer floats over the swing view.
- [x] **#3 Transient swing default:** `swingDismissed` (new field in UiState) is set
      true by any non-swing-scope interaction while the swing band is shown; reset
      false on scope down/up so each re-hold opens the swing view cleanly.  DIV /
      PHRASELEN / GLOBAL bands remain sticky (masterSection-selected).
- [x] **#4 Dead bands fixed:** `metaContentExists` now accepts `{0,1,3,4,5}` instead
      of the stale `{0,1,2,5}` — Track+TRIG (DIV) and Phrase+TRIG (PHRASELEN) now
      reach ManipulationZone correctly.

Architecture: new `src/ui/MetaBand.{h,cpp}` — pure `resolveMetaBand`, `swingScopeFor`,
`buildMetaBand`, `writeMetaField`.  Four consumers: screen render, screen write,
controller render, controller write.

### 7.15 — Song selector display; meta-band split; scope-surfaced swing  *[shipped]*

- [x] **A1 Song selector:** holding Song shows a 16-slot grid (slot 0 occupied,
      rest empty) instead of tinting the trig grid. `songSlotOccupied()` added to
      `Arrangement` and `LockstepProcessor`.
- [x] **A2 Auto-dismiss meta on track change:** any open meta section is cleared
      when the active track changes, preventing stale-track edits.
- [x] **A3 Band split:** retired the combined TRACK meta (length/divider/swing/effct).
      - `Track+TRIG` → metaSection 3 (DIV): kit divider only, label `DIV`.
      - `Phrase+TRIG` → metaSection 4 (PHRASELEN): active phrase length only.
      - Each owns the hierarchy level it controls (Kit / Phrase).
- [x] **A4 Swing by held scope:** holding Song/Scene/Track now surfaces that scope's
      swing directly in the band — no meta prerequisite. Slot 0 = editable level,
      slot 1 = `Effct` (clamped sum, read-only). Non-sticky; clears on release.
- [x] **A5 Docs:** DESIGN §19.2 / §6.1 / §5 table updated; README §5.8 and shortcut
      appendix rewritten.

### 7.14 — Pin Scene→Phrase diagonal; remap gestures; morph drag fix  *[shipped]*

Removes `Scene::globalPhrase` entirely (serializer v11). Scene N always plays
phrase row N (the diagonal is a structural invariant, not a stored field).
Migration: v10 states with a re-homed scene materialise the old row content
onto the diagonal at load time, preserving playback. See DESIGN §4.7/§16/§23.3.

- [x] `Scene::globalPhrase` removed; `resolveActivePhraseIdx` uses `sceneIdx`
      directly; `ArrangementTest` tests updated.
- [x] Serializer bump to v11; v10 `"gp"` field materialised at load time.
- [x] **Gesture remap:** `Phrase+step` = deviate focused track (was set-global);
      `Scene+Phrase+step` = deviate all tracks; diagonal row = clear all.
      `deviateAllToPhrase()` added to `Arrangement`.
- [x] **Morph drag fix:** bare `Morph` + mouse-drag on MZ param now writes the
      morph overlay at the fader split, matching the encoder path.
- [x] **Scene-held grid occupancy:** empty slots shown distinctly; active scene
      always non-empty.
- [x] **Three create variants** on empty slot: bare=baked, `Func`=baseline copy
      (new `createBaselineCopyScene`), `Mute`=blank.
- [x] **Overwrite guard:** no-op skip for identical content; free-slot hint.
- [x] Default: `scenes[0].initialised=true` in fresh Song.

---

## Phase 4 — Machine Catalogue  *[partial]*

DESIGN §1 (lineage), §29. Each machine is a contributor-sized engine inheriting
the SDK base. 4.1–4.4 shipped; 4.5+ are authored against the frozen surface and
the Machine Module ABI (6.7), so they ship as loadable modules.

### 4.1 — FMMachine  *[shipped]*  *(was MH.1)*
- [x] 4-op FM, free 4×4 matrix, per-op ADSR/ratio/fine/mix, macro scalars,
      Mono/Poly (4-voice pool, oldest-steal).

### 4.2 — AnalogMachine  *[shipped]*  *(was MH.2)*
- [x] Dual PolyBLEP oscs + sub + noise, SVF (LP4/LP2/HP/BP + drive), filter +
      amp ADSR, LFO (6 shapes, 4 targets), portamento, Mono/Para-4.
- [x] Polyphonic-trig infrastructure (≤4 notes/step, chord capture, gate
      auto-write), backward-compatible serialization.

### 4.3 — DrumMachine  *[shipped]*  *(was MH.3)*
- [x] Rytm-style per-track drum synthesis; `type` stepped slot selects KICK /
      SNARE / HAT / TOM, each with dedicated DSP.

### 4.4 — Sample depth + SliceMachine  *[shipped]*  *(was MH.4; absorbs the old MK)*
- [x] Sample trim window (`samp_start/length`), four loop modes, loop region,
      edit-time zero-crossing snap; shared `SamplePlayingMachineBase`.
- [x] `SliceMachine` (SLICE / SCRUB dual mode, 16-slice cap, transient
      detection, MONO/POLY, anti-click fade, reverse at rate < 0).

### 4.5 — StreamMachine (disk-stream)  *[shipped]*  *(was MH.6)*
- [x] Disk-streaming sampler for long-form audio (DESIGN §29). Streams via a
      background `BufferingAudioReader`; audio never decoded wholesale into RAM.
      Source file path held per-Kit (`TrackKit::staticPath`, serializer key
      `staticPath`, additive — no version bump), not a SamplePool entry; assigned
      by dropping a file on a focused Stream track. `start` slot; gated stream
      (`hasInternalAmp`, level/pan via CHANNEL). Resampling on rate mismatch is a
      later refinement. `StreamMachineTest` covers open/stream/stop/bad-path.

### 4.6 — PercussionMachine (physical model)  *[planned]*  *(was MH.7)*
- [ ] Volca-Drum-style two-layer percussion: excitation osc (+FM/ring + pitch
      env) → waveguide / modal resonator (Tube/String/Membrane/Modal); layer A↔B
      crossfade + bit/SR reduce + drive. Canonical FLTR/AMP downstream. Algorithm
      presets ship as Sound Pool entries, not schema variants.

### 4.7 — DigitalMachine (Monomachine archetype)  *[planned]*  *(was MH.8)*
- [ ] Model-based digital monosynth (`model` stepped slot): SWAVE (supersaw),
      SID (PWM+ring+sync), WAVE (single-cycle wavetable/PWM), VO (formant).
      `V1` + live Mono/Poly; canonical FLTR/AMP (no opt-out). Monomachine
      GND/FM/drum engines subsumed (Route / FMMachine / DrumSynth). Authored
      against the 6.7 SDK + post-3.11 contract.

### 4.8 — DrumSynth voice expansion  *[shipped]*  *(was MH.9)*
- [x] Extended the DrumSynth `type` enum to eight voices: KICK, SNARE, HAT, TOM,
      CLAP, COWBELL, CYMBAL, RIMSHOT — each with dedicated DSP.
      Boundary rule: 808/909 analog/FM-metal lives in DrumSynth; modal/waveguide
      struck-metal stays in PercussionMachine. Shipped as `type` values.

### 4.9 — Sample analysis metadata  *[shipped]*
Completes the sample-analysis story the pool already anticipated (tempo
detection shipped earlier; `Sample::detectedBpm` + `StretchMachine` tempo sync).
- [x] **Key + tuning detection** (`dsp/KeyEstimate.h`): a full-sample FFT →
      band-limited (60–2000 Hz) chroma → tuning reference (deviation from A440,
      via parabolic peak interpolation) → key scored over 12 roots × 7
      brightnesses with the circle-of-fifths `noteStrengthRank` (the same tonal
      core the melodic generator uses; DESIGN §4.10). Depth is **root +
      brightness only**. Unknown = root −1, gated by chroma concentration +
      score-margin confidence. Runs at load, message thread, under the shared
      ≤30 s `kMaxAnalysisSeconds` gate.
- [x] **Filename + ACID hints** (`dsp/SampleHints.h`): parse tempo/key from the
      filename (bpm-adjacent tokens; note+quality patterns) and ACID WAV tags
      (`acidTempo`/`acidRootSet`/`acidRootNote`/`acidOneShot`); fuse
      detection-first — a hint only resolves the tempo estimator's octave fold
      (~2×/0.5×) or fills a gap; a one-shot flag suppresses any tempo hint.
- [x] **Cached analysis, serialized v26** — per pool entry, keyed by the sample
      hash. A hash match on load adopts the cache and skips re-analysis; a
      mismatch (or a legacy v25 entry) re-analyses. Missing files keep their
      cache across an offline session.
- [x] **SliceMachine SYNC source** — third `slicer_slice_src` value; the Count
      slot reinterprets as a clock division (`4bar…1/16`, shown as a "Div 1/4"
      context label). Beat-grid boundaries at the detected tempo, anchored on
      the first transient (a loop may not start on the 1) and ZC-snapped;
      bpm == 0 falls back to EQUAL.
- [x] **Pool visibility** — the browser hint reads `128 bpm  Amin`; an analysed
      entry with neither tempo nor key is flagged `one-shot`.
- **Excluded by design:** StreamMachine files (never enter the pool — no PCM in
  RAM to analyse) and volatile REC/Loop captures (never serialized).
  **Documented follow-up:** key-synced StretchMachine playback (pitch offset to
  the project key) — the fused key metadata is the input it will consume.

---

## Phase 5 — Performance Depth  *[planned]*

The depth pass on top of the frozen surface: timing feel, scenes, pattern/part
management, sampling, audition, special trigs, the remaining trig-grid modes, and
the UI-polish/palette pass.

### 5.1 — Microtiming + swing + quantize  *[shipped]*  *(was ML)*
Completes the record-time capture story (gate / velocity / microtiming).
- [x] `Step::microOffset ∈ [-0.5, +0.5]`; serializer v9 (`mo` property).
- [x] Sample-accurate look-ahead scheduler: per-step emit, `pendingTrigs_`
      deferral, combined ±0.5 cap (DESIGN §19.2). Also fixes the pre-existing
      single-emit-per-block limitation.
- [x] Realtime record writes `microOffset` (residual to nearest swung position).
- [x] Signed additive swing (DESIGN §19.2): initial implementation: global
      `swing` + per-track `track_t_swing` ∈ [-0.5, +0.5], both APVTS.
- [x] Hierarchical swing v2 (serializer v10): swing moved from APVTS into
      Song/SongTrack/Scene musical state (Song-all + Song-track + Scene-all,
      three additive levels, morph-style qualifier editing). v9→v10 upgrade
      migrates legacy APVTS values. Swing is no longer host-automatable.
- [x] `Quantize` verb (`<scope> + No`) zeroing microOffsets in scope.
- [x] Authoring UI: MicroTime widget (TRIG band, P-lockable); Swing widget
      in TRACK band — qualifier-driven (hold Song = song-track Δ, hold Scene =
      scene-all Δ, no scope = song-all root); per-track effective-swing readout.
- [x] Step-grid nudge-direction tick indicator (amber=late, cyan=early).

### 5.2 — Morph + crossfader  *[shipped]*  *(was MI)*
DESIGN §17. *(Morph A/B snapshot fields are carried on the Scene after Phase 7
Stage G — shipped as `Section.sceneA/B`, renamed `Scene.morphA/B` in 7.9; a
placeholder crossfader slider exists from 3.1. The full crossfader
implementation ships here.)*
- [x] `faderValue` `std::atomic<float>` + smoothed follower (RAM-only, not serialized; default f=0/A).
- [x] Resolver: three-tier P-Lock ▷ morph-lerp ▷ kit-base; **mirror resolution** (absent pole = other pole ?? kit base); fader inert until A ≠ B. Both process paths (stopped + running).
- [x] Modifier-gated sculpting: hold/latch `Morph` + encoder → normalised split `da=Δ(1-f)/D, db=Δf/D`; bare encoder → kit base (DESIGN §17.3/§17.6).
- [x] `Morph + ^/v` pole-forcing + `Morph+Stop` removal; MZ A/B indicators.
- [x] Stepped snap (f<0.5 → A, else B) + MIDI-out parity (channel/program snap + All-Notes-Off on channel flip).
- [x] Fluid mute: `Morph+Mute` captures AMP `Level→silence` into near pole, unity into far pole.
- [x] Fader MIDI-learn: `CCScope::Crossfader`; right-click on crossfader_ slider → learn.

### 5.3 — Song/Scene management UI  *[planned]*  *(was MJ; re-scoped for Phase 7)*
DESIGN §23 (re-derived for the Phase 7 model). The old Pattern/Part management UI
is re-scoped to manage Songs and Scenes.
- [ ] Song + Scene names (≤16 chars, inline editor).
- [ ] Song + Scene colours + tags (palette tied to §24).
- [ ] Non-modal browser overlay (Songs → Scenes), navigable while playing;
      selection reuses the launch gesture.
- [ ] Copy / move / duplicate Phrases across tracks or Songs.
- [ ] In-browser Scene queue cue (`Yes` cues, `No` cancels).
- [ ] Kit as a recall unit (DESIGN §4.7.2): Kit name (inline); save/load
      against a Set-level Kit library; machine-vs-library paging in the
      `Func+Track` picker. (Kit reload = `Track`-scope Checkpoint floor,
      §13.6 — no separate gesture.)

### 5.4 — Sampling + resampling  *[planned]*  *(was MN)*
DESIGN §22.
- [ ] Audio-input capture overlay; source picker `{Plugin input, Track 1..N,
      Master}` (no system/device input).
- [ ] Free-form capture (`Record`/`Stop` in the Sampling scope) → temp buffer →
      naming flow.
- [ ] Capture-N-bars; resample taps (`Track+Sampling`, `Song+Sampling`).
- [ ] Naming flow (4 curated + 1 hash-derived) from a bundled wordlist.
- [ ] Pool integration (`samples/recorded/`, standard `xxHash32` ref).
- [ ] Resample-time stretch/pitch decision (preserve pitch / length / independent
      ratios; baked, no realtime DSP here).

### 5.5 — Audition (Cue scope) + cross-track record  *[audition shipped]*  *(was MO)*
DESIGN §21.
- [x] Audition gestures via the **Cue** scope on the freed `Func+3` compound (no
      new physical key — hardware parity). `Cue+step` fires that step's resolved
      trig once; `Cue` alone fires the focused track's base trig. Both bypass the
      event stream (reuse `liveNoteOn/Off`) and write nothing.
- [ ] Per-track record arms in Per-Track-MIDI mode; arm-all (`Func+RecordArm`).
- [ ] Omni-mode arming behaviour documented.
- [ ] Step-as-keyboard live record composing with the trig-grid modes.

### 5.6 — Special trig types  *[lock-only + one-shot shipped]*  *(was MQ)*
DESIGN §30.
- [x] Trigless / lock-only trig: `Step::lockOnly`; the running path now advances
      `firedStepIdx_` on a lock-only crossing so its FLTR/CHANNEL/ENV/insert
      overrides ride onto the sustaining voice with **no** note. Toggle =
      `Trig+step` (`off → note → lock-only`). `CellState::StepLockOnly` chrome.
      Serializer "lo" (v24).
- [x] One-shot trig (`TrigCondition::oneShot`): fires once then spent (RAM-only),
      auto-rearm on transport (re)start + scene apply; `rearmOneShots()` per-track
      arm-all. COND meta-band "1Shot" field. (Manual arm-all key binding deferred.)
- [x] Step-state preview integration for lock-only (one-shot armed/spent chrome
      pending a follow-up).
- [x] Record trig (6.2) — contextual: a trig on a RecordMachine track is a
      recorder trig (capture); one-shot composes; lock-only disallowed there.

### 5.7 — Alternate trig modes: Retrig/ratchet + Sound Pool  *[shipped]*  *(was MG remainder + MM generic-role)*
The trig-grid modal surface beyond CHROMATIC/LEVELS (which shipped in 3.9).
- [x] Retrig / ratchet trig-grid mode (`Fill+TRIG` momentary hold): grid shows 8
      ratchet rates (/4…/32T); ISliceable tracks show slice indices instead.
      Step press live-stutters using the step's own note (fixes hardcoded note-60).
      Record-arm or held-step authoring writes `hasRetrig`/`retrigRate` P-Lock.
- [x] Sound Pool mode (`Fill+SRC` momentary hold): grid pages pool entries; step
      press calls `liveSwapTrackSound` for live audition; record-arm bakes a
      `sound_id` P-Lock (`hasSoundId`/`soundId`). Pre-existing serializer bug fixed.
- [x] Mode-chord UX consistent with the surface model; clean exit on Fill release.
- [x] Serializer bumped to v12 (retrig + soundId fields; `upgrade_v11_to_v12`).
- [x] CellState tokens: `SoundPoolOccupied/Empty/Current`, `RetrigRate/Selected`,
      `SlicePoint/Selected/Empty`; mapped on Push 1 and X-Touch Mini.
- [ ] Generic role-tagged LEVELS sub-mode (extend 3.9's velocity-first LEVELS to
      a closed eligible role set: cutoff, attack, pan, … — the surviving MM.1).

### 5.7c — Sound Bank overlay completion  *[shipped]*
Extends the `Fill+SRC` pool established by 5.7 with full management UI.
- [x] Sound Bank overlay reworked to real Row components (single-click recall,
      per-row Recall/Del buttons, double-click inline rename).
- [x] Machine-mismatch guard on recall: emits status rather than silently failing.
- [x] Delete: `remapSoundIdsAfterRemoval` traverses all songs × tracks × phrases × steps
      + working sequence; fixes `trigOverride.soundId` and `fillTrigOverride.soundId`
      under `withQuiescedEngine`.
- [x] Rename: direct name write on message thread (no quiesce).
- [x] Auto-naming: `saveTrackToSoundPool("")` generates `"<Engine> T<n>"`, uniquified.
- [x] **Critical bug fixed:** `Project::soundPool` was never serialized; serializer
      bumped to v16 (missing SoundPool node on load = empty pool, trivial upgrade).
- [x] Status feedback for all operations (saved, recalled, deleted, renamed, mismatch).
- [x] `tools/check.sh` format gate (clang-format `--dry-run -Werror` over src/tests).

### 5.8 — UI polish: layout, palette, toggles, coarse-adjust  *[planned]*  *(was MP)*
DESIGN §24, §25, §26.
- [ ] Vertical / square-cell layout matching hardware key caps; constants in one
      header.
- [ ] State-colour palette (`StateColor` enum + single resolver) feeding grid /
      sections / chrome / future LEDs.
- [ ] Granular feedback toggles (Settings panel; defaults on); recovers the
      Staggered/Ortholinear/Clean overlays as toggle presets.
- [ ] Coarse-adjust modifier (`Func` + encoder, unit-derived step).
- [ ] MZ size single constant verified at `kMZSlots = 8`.
- [ ] MIDI-device preset-selection UI (deferred from 2.5).

### 5.9 — Deterministic generators + performance macros (groovebox sweep)  *[planned]*
From the competitive sweep (see `NON-GOALS.md`): the admitted, principle-clean
additions. Each is authored against the frozen surface and must stay within
scope+verb. Stochastic / generative authoring is explicitly *not* here (NON-GOALS).
- [x] **Euclidean print-on-release** (DESIGN §13.5): `Phrase+Fill` chord enters
      generator mode on the focused track; MZ shows `PULSE / OFSET / ACCNT` via
      `MetaBand::Euclidean`; release replaces trigs in `[0, phrase.length)`;
      checkpoint pushed first if phrase has existing trigs. Output is ordinary
      hand-editable trig data. Accent layer Euclidean-distributes N accented
      onsets over the K pulses (higher velocity). See `core/Euclidean.h`.
- [x] **Density overlay** (replaces Chance macro): a live, subtractive trig-thinning
      overlay strictly downstream of fill/iteration/prev-dep/probability — only
      silences would-fire trigs, never re-enables them. Per-track density amounts +
      master offset are ephemeral (reset on song change; ride scene sticky/floor
      launch). Musicality (Uniform/Mixed/Metric) and Selection (Scrub/Reroll) are
      durable per-song-per-track in TrackKit (serializer v18→v19). Gestures:
      `Func`-held → 16-track Density band (paginated); `Func+Song+encoder` → master
      offset; `Song`-held → durable mode editor. Visual: rotary = per-track value,
      arc = master offset, tick = effective (sticks at rail with dimmed overshoot).
      See DESIGN §39. *(Chance macro superseded.)*
- [x] **Meter-aware metric weighting** (`MetricGrid.h`): replaced the 4/4-only
      trailing-zero depth with Lerdahl–Jackendoff dot-counts; `densitySurvives`
      and `metricDrop` now take `numerator`/`denominator` and produce musically
      correct thinning in 3/4, 6/8, 7/8, 9/8, etc. Regression-safe: 4/4 behaviour
      unchanged. See DESIGN §39.2. *(b29d1bf)*
- [x] **Deterministic Scrub density selection** (`MetricSelect.h`, §39.3a): Scrub mode
      is fully deterministic and loop-stable — no per-step hash. A fixed per-track
      rotation offset (`densityScrubHash(track,0,0)`) de-correlates same-density tracks.
      Metric importance tiers (from `MetricGrid::metricWeight`) are filled strongest-first;
      the partially-included boundary tier uses `bjorklund(M, k)` — evenly spread,
      per-count recomputed (not drop-point). Three independent modes: **Uniform** =
      `euclidHit(loopPos, L, Tl, off)` over the whole loop (repeats exactly every
      loop); **Metric** = `metric[T]` bitmask, global-bar scope, downbeats anchored;
      **Mixed** = `metric[P]` core protected + `bjorklund(N-P, T-P, off)` fill on
      unprotected positions, global-bar scope. Table gains `metric[]` + `mixed[]`;
      `euclidHit()` (O(1), no allocation) added to `Euclidean.h`. Reroll path unchanged.
      See DESIGN §39.3a. *(bbc2392, 2233b5f, 381f7a3, c811675)*
- [x] **Accent velocity generator** (`Func+Fill` chord, §39.10): shipped then
      **replaced** by the live velocity overlay (v20). See §39.10.
      *(7fb3f01 → replaced by 427a5d7)*
- [x] **Musical subdivision picker** (DIV band two-field — base note value +
      flavour Straight/Dotted/Triplet; range 4/1 … 1/64; serializer v19→v20 remap).
      See DESIGN §4.2. *(Commits 1-2, v20)*
- [x] **Density Exempt detent** — third DensitySelection state; engine early-out;
      Amount + Musicality cells greyed. See DESIGN §39.2. *(Commit 3, v20)*
- [x] **Live velocity overlay** — **Func+AMP** entry (was double-tap AMP, which
      collided with AMP page cycling); 4 sub-pages (Depth/Center/Mode/Blend);
      Replace/Mix blend; Bar-metric weight at emit time; durable TrackKit fields;
      serializer v20. See DESIGN §39.10. *(Commits 4-5, v20; entry gesture fixed v20+1)*
- [ ] **Scale-aware CHROMATIC layout** (DESIGN §34.2): the per-phrase scale lock
      remaps the CHROMATIC keyboard to scale degrees — a *playable layout*, never
      note auto-correct (out-of-scale entry stays verbatim and reachable).
- [ ] **Arpeggiator** — *design-stub only* (DESIGN §13.5 DRAFT). PRINCIPLES-cleared
      as a performable engine; **gated**: must close the grammar-fit open questions
      in DESIGN before it earns a checklist here.
- [ ] **Retrig model redesign** (future): the current rate-picker overlay selects a
      global ratchet rate per trigger event. A richer model — hold-steps-to-isolate
      (step-isolate/solo gesture), step-held ratchet-on-hold, polymeter-safe per-step
      ratchet — was intentionally deferred. Design must fit the scope+verb grammar
      before this earns a checklist. (Deferred 2026-06-08.) **Flagged tension:**
      the shipped live stutter sits knowingly close to NON-GOALS fence #11
      ("Sonicware stutter") — see the close-calls note in `NON-GOALS.md`; the
      redesign must resolve that tension, not extend it.
- [ ] **Step-isolate/solo** live gesture (future): hold one or more steps to
      temporarily isolate their tracks/voices during playback — a punch-in
      performance verb. Grammar and exact scope deferred. (Deferred 2026-06-08.)

> The **Animate** momentary insert-toggle (FX-held + step) shipped in 6.5.

### 5.10 — Func-layer legibility + meta-section relocation  *[shipped]*
DESIGN §6.1 rule 3 + §6.2. The `Func`-held section row only swapped *text*, never
colour, so reachable secondaries were invisible (the §10 "chrome must announce
state" failure). And two metas (`TRACK`, `GLOBAL`) sat on `Func` although DESIGN
§6.2 assigns them to the scope that owns their domain. This item makes `Func`
obey the same glow/dim grammar the scopes already use, and finishes the §6.2
relocation. (`Phrase+LEN` length/divider access is left untouched — paused 3.11
context.)
- [x] Docs: DESIGN §6.1/§6.2 pin `Func` metas to `COND`/`NOTE`, relocate
      `TRACK`→`Track+TRIG`, `GLOBAL`→`Song+FX`, and require the colour grammar.
- [x] `Func`-held section keys glow in the secondary hue (`kScopeFunc`) when a
      secondary is wired, dim to `Disabled` when not (mirrors scope-glow).
- [x] Relocate `TRACK` (length/divider) to `Track+TRIG`; drop from `Func+FILTER`.
      Split the meta predicate: `kMetaLabels` (Func-row label) vs
      `metaContentExists()` (MZ content reached by scope gestures).
- [x] Relocate `GLOBAL` (gain/sync/clock) to `Song+FX`; drop from `Func+FX`.

---

## Phase 6 — Routing, FX & Platform  *[6.1 + 6.5 shipped; 6.6 in progress]*

The audio-input boundary and the machines it unlocks, the effects system, the cue
bus, external controller surfaces, the machine-module ABI, and the beta polish.

### 6.1 — Audio-input boundary + routing + Route machine  *[shipped]*  *(was MR)*
DESIGN §27, §29. Gates 6.2 / 6.3. Shipped phased: A1 outside-world sources +
Route, then A2 output-directed track buses.
- [x] Optional audio-input path at the machine boundary (sequencer fills `buffer`
      from `input_source` before `process()`).
- [x] `input_source` slot — outside-world tap `{None | External | Master}`
      (inter-track routing moved to the CHANNEL "Out" slot below).
- [x] **Output-directed routing** (model revised from input-select): per-track
      CHANNEL "Out" `{Master | Track N | Off}`. A bus reads the sum of tracks
      routed into it. Removes a track from master (mute can't). `core/OutputDest.h`.
- [x] Per-block topological sort (`core/RoutingGraph.h`, pure/tested); cyclic
      routing refused at the "Out" write (best-effort + authoritative engine guard).
- [x] Master prior-block tap (`input_source = Master`).
- [x] RouteMachine (unity pass-through; canonical FLTR/AMP/FX process it; defaults
      to `None` so a fresh Route is a silent sub-bus).
- [x] MIDI-out parity (no input source; no audible route).
- [x] **Stem export (Workstream D):** capture is now a take directory —
      `master.wav` + one `track-NN.wav` per non-empty Master-routed track (buses
      fold in their feeders; routing IS the stem-grouping UI). Always-on. Reuses
      the 9.16 tape-deck infra; tap is post-fader/post-FX in `processTrackChain`.

### 6.2 — Record buffers + recorder trigs  *[shipped, freeze-to-disk deferred]*  *(was MS)*
DESIGN §28, §29, §30. Depends on 6.1.
- [x] Volatile pool entries (RAM-only, `REC`-badged, unified address space):
      `SamplePool::addVolatile/prepareVolatile/nthVolatileIndex`; skipped on save.
- [x] Fixed set of volatile buffer slots (8) reserved at the top of the pool,
      re-seeded on load (`seedVolatileSlots`).
- [x] RecordMachine (`input_source`, `target_buffer`, `rec_length`,
      overwrite-only); V1 note-on capture edge; captured buffer immediately
      playable from a Sample (live-resample round-trip test).
- [x] Record trig is **contextual** (a trig on a Record track), not a stored
      step field; lock-only disallowed there (`isRecorderTrack` guard).
- [ ] Freeze-to-disk via the §22 naming flow — **deferred** to a later milestone
      (captures are RAM-only / lost on quit, Octatrack parity).

### 6.3 — Loop machine (overdub)  *[shipped]*  *(was MT)*
DESIGN §29. Depends on 6.2.
- [x] LoopMachine state machine (Idle→Record→Play→Overdub + Clear + one-level
      Undo); internal RAM loop (kept machine-internal by design — the deliberate
      path into the volatile pool is the post-FX resample flow, not auto-capture).
- [x] Verb-driven while focused with no new grammar: `Track+Record` cycles
      record→overdub, `Track+Play` toggles play/stop, `Track+Clear` empties
      (shadowing the track clipboard on Loop tracks); lock-free command mailbox;
      `LoopMachineTest` drives the full state machine.
- [ ] Click-free overdub seams + transport-synced loop-length option — later
      refinement (loop length is free-running for now).

### 6.4 — Cue + Aux output buses + monitoring  *[in progress]*  *(was MU)*
DESIGN §31 / §31.1. Static output complement + the `Cue` scope (key TBD).
- [x] Static output complement: **Master + Cue + 6 Aux** stereo buses, non-main
      declared disabled-by-default (`BusesPropertiesAccessor::make`). No dynamic
      port rescan (host lottery — rejected). **CLAP/VST3 port exposure in a real
      host (Bitwig/Reaper) is unverified in this environment — verify before UI.**
- [x] **Aux mix routing**: the CHANNEL "Out" slot destination set grows to
      `Off | Master | Bus(track) | Aux 1–6` (`OutputDest.h`); an Aux route whose
      host bus is disabled **folds to Master** (never silent data loss). Master
      processing confined to a main-bus view so aux buses never get master FX.
      No serializer bump needed — the Aux encoding rides the existing v25
      `channelState.out` float (old files decode as Master/Track unchanged).
- [ ] Cue/monitor output bus DSP (standalone ch 3–4 / plugin Cue bus), additive
      post-FLTR/AMP/Level send; excluded from `outputReachesMaster()`. (Cue bus is
      *declared* but not yet fed — awaits the send tap + Cue-scope key.)
- [ ] `Cue + track` / `Cue + Scene` / `Cue + MIDI-out` **gestures** — await the
      `Cue`-scope key allocation (Aux DSP + Out-slot routing shipped first).
- [x] Live stem capture via Aux outs documented as the blessed stem-export path
      (DESIGN §31.1) — the offline per-take stem-export item is demoted.

### 6.5 — Insert + master effects (FX system)  *[shipped]*  *(was MV)*
DESIGN §32. Depends on 2.4 + the §14 path (independent of 6.1–6.4).
- [x] `IEffect` interface (reuses `ParamSpec`/`role`/P-Lock; stub fallback) +
      starter catalogue (Delay, Reverb, Distortion, Chorus).
- [x] Per-track 2-insert chain (post-AMP, Part-scope).
- [x] FX canonical-section rendering + `Func+FX` effect-load picker gesture
      (picker routes via CB::MetaSection; section key illuminates under Func).
- [x] **Animate** momentary insert toggle (DESIGN §32.5): FX-held + step bypasses
      insert for the hold duration; release restores. No dedicated performance-FX mode.
- [x] Serializer v13: insert chains round-trip (effectId/baseParams/bypass per slot).
- [x] Performance-grammar parity for inserts (P-Lock via namespaced IDs, Control-All, section-copy).
- [x] **Two master FX slots** (post-sum, Song scope): `Func+Song+FX` picker; MZ shows params under `Song+FX` (`MetaBand::Global`); serializer v14.
- [x] **FX-section clean separation:** `Song+FX` shows master insert params only;
      transport globals (Gain / Sync / Chan) relocated to `Func+7`
      (`MetaBand::Transport`).
- [x] Send routing (per-track Send A/B in AMP slots 8–9; 2 send-return FX slots on the
      master bus; HQ send-first candidates: verb, delay) — see **8.26**.
- [x] MIDI-out tracks carry no inserts/sends (parity enforced at AMP CC-bank).

### 6.6 — External controller surfaces  *[in progress]*  *(was MW)*
DESIGN §35. Generic third-party MIDI controllers as augmentation surfaces (worked
examples: Behringer X-Touch Mini — see `XTOUCHMINI_MCU.md` — and Ableton Push 1).
The load-bearing piece is the surface model (§35.8): one pure `buildSurfaceModel()`
both screen and controllers render from. **End-state:** contributors add
controllers via `IControllerSurface` + `ControllerRegistry`; the JSON profile is
the default data-driven impl.

> **Architecture status (drift note).** The surface model and two concrete
> controllers shipped **ahead of** the registry/JSON layer. Today
> `Push1Surface` and `XTouchMiniSurface` are **hardcoded `IControllerSurface`
> subclasses instantiated directly in `PluginEditor`** and driven by
> `ControllerPortManager`; feedback is rendered inline from
> `ControllerPortManager::drain` (no separate throttled emitter yet). This is
> **interim** — the `ControllerRegistry`, the data-driven JSON profile loader
> + schema validation, and the `ControllerFeedbackEmitter` remain the intended
> end-state (they are what make third-party controllers a contributor surface
> rather than a core code change). Items below mark what shipped vs. what the
> end-state still needs.
- [~] **6.6.1** `IControllerSurface` seam shipped (concrete subclasses).
      **Still planned:** `ControllerRegistry` + profile loader + JSON schema +
      validation; graceful unknown-device / malformed / unknown-token handling.
- [x] **6.6.2** `ControllerPortManager` (dedicated MIDI port, disjoint from the
      host bus; drains buffered input to the surface then renders).
- [x] **6.6.3** Input routing (encoders→CC, buttons→`ControllerEvent`, fader→
      interim slider) — handled inside the concrete surfaces.
- [x] **6.6.4** X-Touch Mini built-in surface shipped (hardcoded C++).
- [~] **6.6.5** Surface model + feedback. **(a) done** — pure
      `buildSurfaceModel()` → `SurfaceModel` with the screen re-pointed at it
      (slices 0–6: `SurfaceModel.{h,cpp}`, `CellState`, decoration channels,
      `tests/SurfaceModelTest.cpp`) + the **6.6.5a UX-consistency pass** (unified
      hint-band rule, note-edit → `Func+Src`, CPC under-scope relabel,
      `TrigGridMode` removed, `Func+arrow` rotate/×2/÷2) + meta-band controller
      exposure via `MetaBand`/`MetaRotary` (7.15–7.17). **(b) [pragmatic closeout]**
      idle surface model rebuild gating added (`dirty || playing` guard in
      `timerCallback`); per-surface shadow diffing in each concrete surface already
      satisfies the diff intent — the separate `ControllerFeedbackEmitter` is deferred
      to the JSON/registry end-state.
- [~] **6.6.6** Feedback colour / state mirroring — **shipped for Push 1**
      (static semantic→palette-index table, `Push1Surface.cpp`) and X-Touch.
      **Still planned:** the generic token-aware + dumb-device fallback that the
      JSON/registry path needs.
- [x] **6.6.7** Crossfader binding — two-way: controller fader → `setCrossfader` →
      `morphFader` (both Push 1 touch strip and X-Touch fader); Push touch strip
      LED echoes on-screen fader position (shadow-diffed, echo suppressed on input
      so the hardware is not fought by immediate feedback).
- [ ] **6.6.8** Adaptive `layoutMode` (opt-in, deferred-most).
- [x] **(unplanned, shipped)** **Push 1 surface** — full render/display/buttons,
      static semantic→palette matcher, meta-band exposure. Not in the original
      6.6 plan; added as a second worked example. (See memory
      `project_push1_refinement`.)

### 6.7 — Machine Module ABI  *[planned]*  *(was M10; supersedes the old MH.5)*
DESIGN §36. One authoring model, two link paths: first-party statically linked,
third-party loadable modules behind a JUCE-free C ABI fronted by `WrapperMachine`.
Not a CLAP/VST3 sub-host; in-process, no IPC/sandbox.
- [ ] **6.7.1** Registry + SDK base, static path only (`lockstep_machine_abi.h`,
      `sdk::MachineBase`, `MachineRegistry`); port DrumSynth first as proof; POD
      conversions unit-tested.
- [ ] **6.7.2** Dynamic load + host-services + discovery (`WrapperMachine`,
      `LsmHostVTable` bridging the shared SamplePool/transport/RNG/logging,
      directory scan + manifest, `abiVersion` gating, the CI template module, the
      missing-module `StubMachine` opaque round-trip).
- [ ] **6.7.3** Template repo + install flow + ABI freeze (drag-drop install +
      rescan, MIDI-out emit-only validation, port SampleMachine to host-services
      sample access, freeze ABI v1 with a golden-header CI test). Author 4.5 / 4.6
      against the SDK thereafter.

### 6.8 — Polish, CI, beta  *[planned]*  *(was M9)*
- [ ] Multi-platform GitHub Actions CI (Linux/macOS/Windows).
- [ ] Performance pass (voice CPU profile, choke-fade SIMD, voice cap).
- [ ] Factory patch library.
- [ ] Final product name (replace "Lockstep"), bundle ids, icons, About box.
- [ ] First public beta build.

---

## Phase 8 — Hardening & Maintainability  *[shipped]*

DESIGN §37 (Command Core), §35.8.7 (Cell appearance table), §37.4 (text SSOT),
§37.5 (ParamRow). Root causes addressed: dispatch duplication across three input
paths; off-model UI text; appearance data trapped in three switch statements;
serializer silent-loss risk; test gaps in EditMode, dispatch, and serializer
round-trips. See the plan file at `~/.claude/plans/so-over-time-i-distributed-conway.md`
for full stage detail.

One commit per work item; build + tests green after every commit.

### 8.1 — Docs-first milestone definition  *[shipped]*
DESIGN §37/§35.8.7/§37.4/§37.5 added; ROADMAP Phase 8 entry; CLAUDE.md status updated.
- [x] **8.1** Docs: DESIGN §35.8.7 + §37 (Command Core, §37.4 Status SSOT,
      §37.5 ParamRow); ROADMAP Phase 8 entry; CLAUDE.md status.

### 8.2 — Characterization tests before moving code
- [x] **8.2a** `tests/EditModeTest.cpp` — pin priority order, compound scope,
      verb dispatch under chords.
- [x] **8.2b** `tests/LayerResolveTest.cpp` — golden table for all
      scancode × layer combos through `QwertyOverlay::resolve()` (oracle for 8.3).
- [x] **8.2c** `tests/SerializerRoundTripTest.cpp` — sentinel round-trip for every
      serialized field; separate from upgrade-chain `SerializerTest.cpp`.

### 8.3 — Single `resolveLayer` (replaces three diverged implementations)
- [x] **8.3a** `src/command/ButtonLayers.{h,cpp}` + `kLayerRemaps[]` table;
      refactor `QwertyOverlay::resolve()` — golden test must pass unchanged.
- [x] **8.3b** Wire into mouse (`KeyboardArea`) + controller sink (`PluginEditor`);
      delete in-dispatch remaps; add `layerContext()` helper.

### 8.4 — Command core extraction (~6–8 commits, incremental)
- [x] **8.4a** Seam scaffolding: `CommandContext.h`, `CommandEffects.h`,
      `CommandCore.{h,cpp}`, `EditorEffects`; `activeTrack` into `UiState`.
- [x] **8.4b** `VerbCommands`: `PS::Trig` scope (step copy/paste/clear).
- [x] **8.4c** `VerbCommands`: `PS::Track`, `PS::Phrase`.
- [x] **8.4d** `VerbCommands`: `PS::Scene`, `PS::Song`, snapshot/restore.
- [x] **8.4e** `VerbCommands`: `PS::Mute`, `PS::Morph`, `PS::Fill`, `PS::Func`;
      delete residual `dispatchVerb` body.
- [-] **8.4f** `handleDown/Up`: scope modifiers + latch.
- [-] **8.4g** `handleDown/Up`: step semantics (trig toggle, held step, P-Lock
      clear, note-edit).
- [x] **8.4h** `handleDown/Up`: Section/MetaSection, transport; editor loses
      ~1500–2000 lines.

### 8.5 — Gesture-level test harness
- [x] **8.5** `tests/GestureHarness.h` + `tests/GestureTest.cpp` (12 named
      scenarios; real core model, no processor; grows with 8.4b–h).

### 8.6 — CellState appearance table
- [x] **8.6a** `src/ui/CellStates.def` + `src/ui/CellAppearance.h` (X-macro table,
      literal values; `pidx` consts moved to `Push1Palette.h`).
- [x] **8.6b** Consume in `XTouchMiniSurface`, `Push1Surface`, `KeyButton`;
      manual colour smoke (standalone + Push).

### 8.7 — Status & contextual text SSOT + transport fix
- [x] **8.7a** `src/command/StatusText.h` + sweep of 40 `setStatus()` call sites;
      `tests/StatusTextTest.cpp`.
- [x] **8.7b** `gridBanner` + `pageDots` into `SurfaceModel`; `ScopedSectionMatrix`
      canonical-name dedup; extend `tests/SurfaceModelTest.cpp`.
- [x] **8.7c** `TransportModel` struct + `InPluginTransport::refresh()`; eliminates
      label desync.

### 8.8 — ParamSpec constexpr tables (LsmParamSpec-shaped)
- [x] **8.8a** `tests/ParamSpecTest.cpp` — golden ids + invariants per machine.
- [x] **8.8b** `src/machine/MachineParamTable.h` (`ParamRow` + `toParamSpec`).
- [x] **8.8c** Convert `AnalogMachine`.
- [x] **8.8d** Convert `DrumMachine` + `SampleMachine`.
- [x] **8.8e** Convert `SliceMachine` + `MidiOutMachine`.
- [x] **8.8f** Convert `FMMachine` + deduplicate parallel operator arrays.

### 8.9 — Serializer hardening
- [x] **8.9a** `src/state/StateKeys.h` — all property names as `constexpr`
      constants; mechanical sweep of read/write sites.
- [x] **8.9b** Round-trip mutation self-test; fix silent-loss bugs; v12 only if
      format must change.

### 8.10 — Final docs pass
- [x] **8.10** DESIGN reconcile + residuals noted; README shortcut sweep; ROADMAP
      ticks; CLAUDE.md layout map + gotchas; ControllerEvent.h nav-comment fix.

### 8.11 — A-series: key-cell label/action SSOT  *[shipped]*
Post-8.10 follow-up in the same hardening spirit: one table answers both "what
does this key cell say/show" and "what does pressing it do", replacing scattered
per-renderer label logic and per-key modifier checks. See DESIGN §37.6.
- [x] **A0** Delete stale `SurfaceModel.cpp~` editor backup.
- [x] **A1** `src/command/ScopePriority.{h,cpp}` — `kScopePriority`, the single
      encoding of scope precedence; `EditMode::recomputePrimary`, label/colour
      resolvers, and binding-row tiebreaks all derive from it.
- [x] **A2** `src/command/SurfaceLayer.{h,cpp}` — `resolveActiveLayer()`, the
      modal-layer SSOT for the step-grid renderer (one priority-ordered enum of
      every grid overlay); `tests/SurfaceLayerTest.cpp`.
- [x] **A3** `src/command/KeyBindings.{h,cpp}` — unified binding table
      (`ActionId` + label + `CellState` per row; most-specific-wins resolution,
      ties broken by `kScopePriority`); drives key-cell rendering;
      `tests/KeyBindingTest.cpp`.
- [x] **A4** Wire `ActionId` rows to the actual dispatch handlers (table becomes
      the dispatch SSOT, not just the render SSOT).
  - [x] **A4.0** Delete dead `VerbStop` + `ForkPart` no-op cases.
  - [x] **A4.1** `CommandCore::handleAction(ActionId, ev, ctx, fx)` pilot —
        mute/solo cluster (`GlobalMuteToggle` / `SoloToggle` / `SceneMuteToggle` /
        `FluidMuteToggle`) migrated from legacy `dispatchDown` to `KeyBindings`
        table + `handleAction`; four new `CommandEffects` virtuals; layer-aware
        binding rows (`MuteView` / `MorphMuteView`); `GestureTest` coverage.
  - [x] **A4.2** Scope-modifier up dedup: `CommandCore::handleUp` handles the
        common "if not latched, clear held + editMode + repaint" for all 7
        latchable scopes + unlatchable `CueScope`; `dispatchUp` cases simplified
        to physical-hold clear + per-scope unique effects only; latch sentinel
        pattern replaces per-scope `!latch.xxx` guards; `GestureTest` coverage.
  - [x] **A4.3** Step-grid overlay layers routed via `resolveActiveLayer()`:
        `RetrigPicker`, `SoundPool`, `MasterFxPicker`, `TrackFxPicker`,
        `MachinePicker` branches now dispatched from a single layer-resolved
        block at the top of the step handler, removing the scattered
        `if (uiState_.xxx)` precondition checks; behavior fix: `MachinePicker`
        (Func+Track) now takes correct priority over `ChromaticInput` /
        `LevelsInput` per the `resolveActiveLayer()` ordering.
  - [x] **A4.4** Verb-row dispatch dedup: `editMode_.onVerb` →
        `onVerbDispatched` → `dispatchVerb` → `handleVerb` callback chain
        replaced with direct `commandCore_.handleVerb(primaryScope, verb,
        ctx, *editorEffects_)` calls at each verb case; `dispatchVerb`
        function and `onVerbDispatched` callback removed; all verb scopes
        already in `VerbCommands` — no fallback switch needed.
  - [x] **A4.6** `VerbPanic` + `TapTempo` migrated to `CommandCore::handleDown`
        as `TransportAction::Panic` / `TransportAction::TapTempo`; legacy
        cases removed from `dispatchDown`. Remaining `dispatchDown` cases
        (nav/scope-down/section/step) remain for 8.19 mop-up — dispatchDown
        is a staged shim, not yet a full thin shim.
  - [x] **A4.5** Section-key scope detection: the hand-rolled five-clause
        `if/else if` chain (Track/Phrase/Scene/Morph/Song UiState flags)
        replaced with `firstHeldSectionSuiteScope(uiState_)` — the
        `kScopePriority`-ordered SSOT from `ScopePriority.h`.
- [x] **Task B** Confirm-prompt + master-FX-picker overlay wiring:
      `LayerFacts.pendingConfirm` threaded from `pendingConfirm_` into
      the step handler; `resolveActiveLayer` now returns `MasterFxPicker`
      when `ui.masterFxPickerOpen`; `TrackFxPicker` / `MachinePicker`
      ordering corrected to match enum priority (MasterFxPicker > TrackFxPicker
      > MachinePicker); `SurfaceLayerTest` golden table extended with
      `MasterFxPicker` single + priority tests. **(8.11 complete.)**

### Accepted residuals (non-goals)
- SamplePoolOverlay / SoundBankOverlay internals; InPluginTransport beyond the
  label fix.
- No runtime JSON for internal tables (6.6 JSON profiles and 6.7 ABI untouched;
  `ParamRow` only pre-shapes 6.7).
- No ControllerRegistry/MachineRegistry; no per-node serializer descriptor tables;
  no scope×verb function-pointer table.
- Per-node serializer field-descriptor tables deferred to possible 6.7-era follow-up.
- *(Stale at write time, since closed: the "TrigGridMode remains unwired" residual —
  5.7 wired it as the `Fill+TRIG` / `Fill+SRC` overlays.)*

---

## Phase 8 — Quality: threading, dispatch, dedup (8.12–8.27)  *[shipped]*

Code-quality audit findings across four workstreams (safety net, threading
architecture, dispatch SSOT, dedup/cleanup). See DESIGN §38 for the threading
contract. Staged to deliver shippable increments; each stage is one focused commit
that builds clean and passes tests.

### 8.12 — Sanitizer support
`LOCKSTEP_SANITIZE` CMake cache option (`OFF|asan|tsan`) applies
`-fsanitize=address,undefined` / `-fsanitize=thread` to `lockstep_core` +
`lockstep_tests` while keeping `-Werror`. Build/run line documented in CLAUDE.md.
- [x] **8.12** Add `LOCKSTEP_SANITIZE` option; document ASan/TSan build lines in
      CLAUDE.md; verify `lockstep_tests` passes clean under ASan/UBSan.

### 8.13 -- Machine DSP smoke + envelope characterization
`tests/MachineDspTest.cpp`: per-machine construct/prepare/note-on/render/note-off
cycle; asserts non-silence after trigger, no NaN/Inf throughout, envelope decays
to silence after short release. Block-size invariance (64 vs 512 samples).
IEffect catalogue smoke. Envelope goldens for Analog and FM with explicit ADSR values.
- [x] **8.13** `MachineDspTest.cpp`: smoke + envelope goldens for Analog/FM/DrumSynth/
      Sample/Slice + 4 IEffects; ASan/UBSan clean.

### 8.14 — Engine testability: headless processBlock harness
`lockstep_engine` static lib; `EngineHarness.h` + `EngineTest.cpp` drive
`processBlock` without a plugin host. Also fixed latent `preparedSampleRate_`
bug: `getSampleRate()` returns 0 until a host calls `setRateAndBufferSizeDetails`;
all install/swap helpers (reinstallMachinesFromActiveKit, setTrackInsert,
setMasterInsert, setTrackMachine, copyKit, state-load) now use
`preparedSampleRate_`/`preparedBlockSize_` (set in `prepareToPlay`) instead.
- [x] **8.14** C1/C2: `lockstep_engine` lib; `TestEditorStub.cpp` stub createEditor.
- [x] **8.14** C3: `EngineHarness` + `EngineTest`: NaN-free default, clock advance,
      state round-trip, trig emission, mute suppress, block-size invariance.
      Fixed `preparedSampleRate_` bug (was: machines prepared at sampleRate=0).
- [x] **8.15**: Threading contract: DESIGN §38; `PluginProcessor.h` member annotations;
      `currentSampleIndex_` → `mutable std::atomic<int>`.
- [x] **8.16** C1: `PLock` `unordered_map` → sorted flat vector + `reserve()`.
- [x] **8.16** C2: `EngineCommand.h` (JUCE-free POD, 12 bytes); SPSC `AbstractFifo`(1024)
      in processor; `pushEngineCmd` / `drainEngineCmds`; migrate all message-thread writes
      (`writeParam`, `clearParam`, `writeFillParam`, `clearFillParam`,
      `setMasterInsertParam`) to enqueue. Audio thread drains at block top. THREADING-DEBT
      tag left for `slicePositions_` race (8.18) and stopped-audio fallback (8.16).
- [x] **8.16** C3: `EngineTest` queue coverage: enqueue→block→applied; queue-full drop
      (no crash/hang, result remains a finite in-range float).
- [x] **8.17** C1: Scene switch at block boundary — pre-staged double-buffer (DESIGN §38.4).
      `queueScene` calls `Arrangement::prepareSceneLaunch` (writeBack + overlay stash +
      project new working Sequence) into `stagedSwap_` on the message thread. At the bar
      boundary the audio thread sets `pendingSceneApply_`; top-of-next-block swaps via
      `applySceneLaunch` (O(N) bounded, no allocation). Message thread still reinstalls
      machines via `callAsync` (kit data is ready from the writeback). THREADING-DEBT(8.18)
      tagged on `sceneIdx`/`deviated` writes.
- [x] **8.17** C2: `EngineTest::testSceneSwitchAtBoundary`: verify `activeSectionIdx`
      transitions to 1 within one bar boundary + working-sequence step data updates.
- [x] **8.18**: `withQuiescedEngine(fn)` template helper added (suspend → drain EngineCmd
      queue → fn() → resume). All structural suspend sites migrated: `setTrackInsert`,
      `clearTrackInsert`, `setMasterInsert`, `clearMasterInsert`, `setTrackMachine`,
      `copyKitTrack`, and the machine-install loop in `reinstallMachinesFromActiveKit`
      (now a single quiesce window covering machine swap + baseParams copy + PLock reserve).
      `getStateInformation` wrapped for consistent snapshot. DESIGN §38 finalized;
      CLAUDE.md mutation-contract gotcha added. Residual THREADING-DEBT: `slicePositions_`
      and `applySceneLaunch` index writes — tagged for future sweep.
- [x] **8.19** Post-A4 precedence/duplication mop-up:
      (a) `SurfaceModel.cpp` Func modifier label: replaced hardcoded
          `c.primary = "FUNC"` with a `resolveBinding` lookup (consistent
          with Track/Song/Mute/TapTempo cells that already use the table).
      (b) `SurfaceModel.cpp` function-row `sectionScopeHeld`: replaced
          5-clause `||` chain with `firstHeldSectionSuiteScope(ui) != None`
          (canonical SSOT ordering via kScopePriority). Line 401 already
          used this function; line 556 now matches.
      (c) `PluginEditor.cpp` Section-case scope detection: done in A4.5.
      `isReservedMeta` dedup (KeyboardArea ↔ SurfaceModel) deferred —
      circular dep (KeyboardArea.h → SurfaceModel.h) prevents direct
      sharing; both remain as 4-line identical functions.

- [x] **8.22** UiState gesture-group reset methods:
      Added four bundled reset helpers to `UiState`: `resetNoteEdit()`,
      `resetPLockClear()`, `resetFxPickers()`, `resetEuclid()`. Each bundles
      all fields for its gesture so no field can be silently omitted. Replaced
      12 scattered field assignments in the Func-release block of
      `PluginEditor.cpp` with the four reset calls + one conditional. Scattered
      partial resets (e.g. single-flag picker-close on selection, Song-scope
      FX close) remain intentionally targeted. Note: full struct grouping
      (per-gesture nested structs) deferred — would require ~160 access-site
      renames; method-based approach delivers same safety with zero churn.

- [x] **8.21** Controller-surface dedup:
      `src/controller/SurfaceShared.h` added: `decodeSignedMagnitudeDelta`
      (X-Touch signed-magnitude) and `decodeTwosComplementDelta` (Push 1
      two's-complement) as `constexpr` free functions. Both surface
      `decodeDelta()` implementations delegate to the shared functions.
      Fixed wrong Push1Surface.h comment ("same as X-Touch" — they are NOT
      the same encoding). XTouch double-click migrated from raw `lastPushMs_`
      array to `std::array<DoubleTapDetector, 8>` (reuses existing
      `io/DoubleTapDetector.h`; threshold 350 ms vs old 400 ms — inaudible
      difference). `ControllerTest.cpp` added: golden table for both decode
      functions + difference assertion. Hardware verification pending (user).

- [x] **8.20** Envelope unification — DrumSynth amp AHD → `dsp::Envelope`:
      Removed `AmpPhase` enum + 7 manual `DrumVoice` fields + `advanceAmp()`;
      replaced with `dsp::Envelope ampEnv` using `setADSR(attackMs, decayMs,
      0.f, releaseMs, holdMs, forceZeroSustain=true)`. Hat uses 1 ms Release
      so note-off triggers `gateOff()` for the open-hat choke; other types
      use 0 ms Release (instantly idle after AHD). Behavior change: 0-attack
      drums get a 1-sample ramp instead of an immediate jump (≤0.02 ms,
      inaudible). FM per-operator envelopes deferred (linear ramps; would
      require extending Envelope.h).

- [x] **8.23** Serializer v15 — P-Lock string ids:
      `kCurrentVersion` bumped to 15. P-Lock entries now written as
      `("id", paramId, "v", value)` instead of `("s", slotIdx, "v", value)`,
      matching the documented contract in `PLock.h` and the CC-mapping pattern.
      `writePhraseNode` takes `LockstepProcessor& proc, int trackIdx` and calls
      `proc.idForSlot(trackIdx, slot)` to resolve ids (slots with no id are
      skipped). `readPhraseFromNode` takes a `slotResolver` lambda — call site
      passes `[&](id){ return proc.slotForId(t, id); }`. Dual-path loader:
      checks `"id"` first, falls back to legacy `"s"` int (v14 saves still
      load). Fill P-Locks (`FPL`) updated identically. `upgrade_v14_to_v15`
      is a version-stamp no-op (dual-path reading handles compat without a
      tree transform). `StateKeys.h` gains `kPLockSlot = "s"` and
      `kPLockVal = "v"`. `SerializerRoundTripTest` extended with
      `testV15PLockFormat`: v15 id-keyed round-trip, v14 legacy s-keyed
      backward compat, and unknown-id drop.
      Open: `7.8` host round-trip in Reaper/Bitwig (user to verify).

- [x] **8.24** UI/UX consistency pass — label/action mismatches + confirm lifecycle:
  - [x] **Stage 1** `tests: pin KeyBindings label-length conventions` — `testLabelLengths()`;
        asserts primary ≤ 8 and hint ≤ 8 UTF-8 code points across the whole table.
  - [x] **Stage 2** `KeyBindings: scope×Func combined rows; fix Scene/Morph verb mislabels` —
        explicit `kModScope|kModFunc` rows for Track/Phrase/Scene delete; Morph BAKE (hint
        ERASE) + ERASE rows; Scene+Record = BAKE (hint COPY); Scene+Play bare row removed
        (dims honestly); raw bake literal moved to `status::confirmBake()` in `StatusText.h`.
  - [x] **Stage 3** `KeyBindings: hint = Func-variant primary everywhere; test pins the rule` —
        `testHintRule()` verifies universal secondary rule across the table.
  - [x] **Stage 4** `state: pending-confirm into UiState; SurfaceLayer reads it directly` —
        `ConfirmKind` enum + `ConfirmState` struct in `UiState.h`; `SurfaceLayer` no longer
        needs `LayerFacts::pendingConfirm`; kind/target captured at arm time (scope-sticky).
  - [x] **Stage 5** `SurfaceModel: render PendingConfirm — live YES/NO on P, all else dimmed` —
        `ConfirmYes`/`ConfirmNo` CellState tokens; binding rows for `PendingConfirm` layer;
        SurfaceModel dims everything except Func and paints P as YES/NO per Func state.
  - [x] **Stage 6** `command: sticky confirm — survives chord release, foreign press cancels` —
        `CommandCore::handleDown` intercept: Func never cancels; any other press = No (swallowed);
        `CommandEffects::executeConfirm` seam; confirm bodies migrated out of PluginEditor.
  - [x] **Stage 7** `command/ui: deletion picker — scope+Func+Clear opens selector; tap → confirm` —
        `DeleteScope` + `DeletePickerState` in `UiState`; `SurfaceLayer::DeletePicker`;
        `deletePhraseSlot()` + `deleteSceneSlot()` in `PluginProcessor`; picker grid in
        `SurfaceModel`; CommandCore wires the picker; `EditorEffects::executeConfirm` routes all
        three kinds (DeleteTrack / DeletePhrase / DeleteScene).
  - [x] **Stage 8** `SurfaceModel: close nav/Mute scope-glow tint gaps` — NavDown under Track
        gets `kScopeTrack` tint; Mute key gets `kScopeScene` tint when Scene held and Mute
        itself not held; `testScopeTintBindings()` pins the binding-table preconditions.
  - [x] **Stage 9** Docs: PRINCIPLES §16 (target selected, not playing); DESIGN §13 / §6.5 /
        §37.6; README shortcut table + §5.4a deletion picker section + Morph BAKE/ERASE +
        Scene BAKE/COPY/PASTE corrections; ROADMAP 8.24; CLAUDE.md gotcha bullet.
  User to verify: standalone visual smoke (Phrase+Func+Clear → picker → tap → named
  confirm; release chord → prompt stays; P=YES green / Func+P=NO red; any other key
  cancels). Hardware: Push 1 / X-Touch render ConfirmYes/ConfirmNo + picker states sanely.

- [x] **8.25** Format/lint gate — `tools/check.sh`; `.clang-format` adapted to house style
      (Allman braces, 4-space, `ColumnLimit:0`); mechanical whole-repo reformat; `.clang-tidy`
      `HeaderFilterRegex` fixed; `CMAKE_EXPORT_COMPILE_COMMANDS` pinned.

### 8.26 — Master-bus re-arch: 2 inserts + 2 sends; FX catalogue expansion  *[shipped]*
DESIGN §32.3 rewritten. Phase A: quality pass. Phase B: topology + catalogue.
Phase C: WAV capture.

- [x] **A0** Fix master-insert chain silent while sequencer running (`processMasterChain`
      helper called from both transport paths; test added).
- [x] **A1/A2** FM legato timbral-param updates; DrumSynth time-param skew 1.0→0.3;
      Analog/FM envelope `[SUSPECTED-BUGGY]` markers removed (goldens pass); per-sample
      param smoothing on Delay/Distortion/Chorus effects; FM ratio `valueLabels` added.
- [x] **B1** Data model: `masterSends`, AMP slots 8–9 (sendA/B), serializer v17;
      send-bus accumulation in both transport paths; `setTimeInfo` broadcast;
      4-unit `Song+FX` pagination (FX1→FX2→Snd A→Snd B); `masterOnly` filter.
- [x] **B2** Track effects: TiltEQ, Compressor, Bitcrusher, Flanger, Phaser
      (header-only, per-sample smoothed, skew/units/roles correct).
- [x] **B3** HQ master effects: HQ Reverb (8-line FDN), HQ Delay (tempo-synced
      ping-pong), Bus Compressor (soft knee + auto-release), Master Utility
      (tilt + M/S width + trim). All `masterOnly=true`.
- [x] **C1** `CaptureRecorder`: N-stream ThreadedWriter + `capturing_` atomic tap
      at the end of `processBlock` in both transport paths.
- [x] **C2** `Func+Song+Record` gesture; arm/disarm status chrome.
- [x] **C3** Docs: DESIGN §32.6 capture subsection; README §5.20 + shortcut table.
- [x] **A3/gap closure** (post-audit): A0 regression test; legato goldens for Analog/FM;
      retrig click-metric tests; effect smoke + specific assertions for all 13 effects;
      serializer v17 round-trip + v16 upgrade tests; Animate for the 4 master units
      (DESIGN §32.5 + Song+FX quadrant mapping; test pins `setMasterSendBypass`);
      bugfix: internal-amp machines (DrumSynth/Analog/FM/Sample) never routed sendA/sendB
      to master send buses (both transport paths fixed).

### 8.27 — Smoothing policy  *[shipped as part of 8.26-A]*
Per-sample one-pole smoothing (~5 ms) on all gain-path effect params.
See DESIGN §32.3 "Smoothing policy" addendum.

### 8.28 — Track channel/envelope split + universal filter  *[shipped]*
DESIGN §14 rewritten. Separates the conflated AMP block into a **CHANNEL** block
(always-on: level/pan/sendA/sendB) and an optional **ENVELOPE** block (AHDSR +
gate source; only for machines without `hasInternalAmp()`). Adds **OFF mode** to
the track filter (always-present, defaults to OFF on new tracks). Deletes
`hasInternalFilter()`; Analog and other machines with internal filters get the track
filter as an additional page rather than bypassing it. Serializer bumped to v18
(trivial stamp; string-keyed ids resolve unchanged).

- [x] **B0** Docs: DESIGN §14 / §32.3 rewrite; ROADMAP entry. *(this item)*
- [x] **B1** Core state + DSP: `TrackChannelState` {level,pan,sendA,sendB};
      `TrackEnvState` {gateSrc,AHDSR}; `TrackEnvDsp` (envelope-only);
      `TrackChannelDsp`; `TrackFltrState` OFF mode (value 4, early-exit, new-track default).
- [x] **B2** Slot layout + `hasInternalFilter()` delete; constexpr FLTR/CHANNEL/ENV
      param-spec row tables (C2 folded in); all ~25 conditional-offset sites collapsed.
- [x] **B3** processBlock wiring: filter always → env iff `!hasInternalAmp()` →
      channel always → inserts → send taps; delete internal-amp send special case.
- [x] **B4** Section/page UI: virtual sections for track FLTR/CHANNEL on
      internal-amp/filter machines via `parentCanonical`.
- [x] **B5** Serializer v18; delete send special cases in write + load paths;
      v17-fixture upgrade tests.
- [x] **B6** Behavioural tests: channel P-Lock on Analog, filter-OFF passthrough,
      filter-on-Analog attenuates.
- [x] **B7** README + ROADMAP closeout.

---

## Phase 9 — Standalone & Files  *[active]*

### 9.1 — Project file flow  *[shipped]*
`.lockstep` plain-XML project files sharing the v16 serializer + upgrade chain.
- [x] `buildStateTree` / `applyStateTree` core helpers split from `writeTo`/`readFrom`.
- [x] `writeToFile` / `readFromFile` on `PluginState` (fail-safe: returns false before
      touching processor state on parse failure).
- [x] `newProject`, `saveProjectFile`, `loadProjectFile`, `stateHash`, `savedStateHash_`,
      `currentProjectFile_` on `LockstepProcessor`. `finishStateLoad()` extracted from
      `setStateInformation` so both paths share the reinstall pass.
- [x] `StandaloneFileBar`: New / Open / Save / Save As… + project-name label.
      Three-way dirty guard (Save / Discard / Cancel) before New/Open.
      Last-project persistence via `appProps_`; auto-opens on launch.
- [x] Serializer v16: `Project::soundPool` (SoundPool/SE nodes); missing node on load =
      empty pool (trivial upgrade from v15).

### 9.2 — Standalone quit guard  *[shipped]*
Intercepts the standalone window's close button before the JUCE wrapper saves
its session; shows the existing Save/Discard/Cancel dirty guard if the project
has unsaved changes.

- [x] Minimal patch to `juce_StandaloneFilterWindow.h`: add `onCloseRequested`
      callback field; `closeButtonPressed()` calls it (with the saveState+quit
      lambda) when set, else falls through to the original behaviour.
- [x] `PluginEditor::parentHierarchyChanged()`: when a `StandaloneFilterWindow`
      parent is detected (standalone only via `JucePlugin_Build_Standalone`),
      wire `window->onCloseRequested = [fileBar](doQuit){ fileBar->withDirtyGuard(doQuit); }`.
- [x] `StandaloneFileBar::withDirtyGuard` promoted to public; used for both
      New/Open buttons and the new quit path.
- [ ] Optional: single-instance enforcement, native menu bar (macOS).
- [ ] Full custom `JUCEApplication` subclass (future; not needed for the guard).

### 9.3 — UX-clarity pass  *[active]*
Surface honesty and mode-legibility improvements surfaced during gain-staging
and dogfooding (8.28 era). Docs shipped in the same pass; code follows.

- [x] **PRINCIPLES §17** — Reserved gestures are fences, not conventions:
      double-tap families documented; section-key exclusion stated.
- [x] **NON-GOALS §14** — Reserved-gesture overload fence.
- [x] **DESIGN §13 grammar conventions** — "hold = all the way"; "bare verb
      = universal-scope synonym" (bare `Y` ≡ `Song+Y`).
- [x] **DESIGN §26.4** — Modal-identity spec: MZ header strip + page
      indicator; P-Lock amber / meta-modal violet colour families;
      section-key fill highlight.
- [x] **DESIGN §6.9** — Naming-clarity policy: param label and value-label
      rules; avoid/prefer audit table; idiomatic-short-form exceptions.
- [x] **DESIGN §39.10** — Fix vel-sticky entry from `AMP` double-tap to
      `Func+AMP` (PRINCIPLES §17 compliance).
- [x] **A1/A2** — Two-axis time-gesture model: PRINCIPLES §17 extended with
      nav reveal/unlock family, operand double-tap = reset-to-default family,
      step-double-tap scope-locality note, and explicit duration fence (hold on
      verb/operand only; modifiers/`Func` forbidden). NON-GOALS §14 widened to
      include (b) duration-meaning on modifier or `Func`.
- [x] **A3** — DESIGN updated: §13 "Time-based gestures" two-axis table +
      band-pin note; §13.7 Latch "Func never pins via double-tap" note; §39.5
      density entry gesture `Func double-tap` → `Func+FX` chord; §39.8 entry
      guard updated; §16 double-tap floor bullet removed; README + all stale
      "double-tap Func/Scene" refs fixed.
- [x] **B1 (density rebind)** — `PluginEditor.cpp`: deleted double-tap density
      entry; added `Func+FX` (index 5) sticky entry after vel block; Func double-tap
      still clears density sticky as part of universal escape; fixed status string.
- [x] **B1-fix (FX→MOD relocation, 2026-06-17)** — the B1 `Func+FX` entry was
      unreachable: `ButtonLayers` remaps `Section`→`MetaSection` under `Func`, so
      `Func+FX` always hit the effect-picker (`MetaSection` case) and the density
      entry (in the dead `Section` case) never fired. Same bug killed `Func+AMP`
      vel-sticky. Relocated **density → `Func+MOD`** (index 4) and moved both sticky
      entries into the `MetaSection` case; in-sticky toggle + clear-guard moved to
      MOD; vel-sticky entry made reachable on `AMP` (index 3). Added `kDensitySecIdx`
      / `kVelSecIdx`; SurfaceModel announces `DENS`/`VEL` Func secondaries; KeyLabel
      cycle labels follow; tests updated (MetaBandTest, SurfaceModelTest).
- [x] **B2 (scene double-tap removal)** — `PluginEditor.cpp`: removed
      `doubleTap_.recordAndCheck(3000+ev.index)` from scene `!funcHeld` branch;
      single-tap always overlay; floor only via `Func+Scene+step`.
- [x] **B1** — Stereo master meter: `masterPeakR_` added to
      `PluginProcessor`; two stacked 3px bars (L/R) in `paintOverChildren`.
- [x] **B2** — MZ header strip (§26.4.1) + modal colour families (§26.4.2,
      amber/violet) + section-key fill highlight (§26.4.3);
      `ManipulationZone.cpp` / `KeyboardArea.cpp` / `MetaBand.cpp`.
- [x] **B3** — Value-label clarity per §6.9: "Trk N" density labels;
      "EXEMPT"/"SCRUB"/"RE-ROLL"/"REPLACE"/"UNIFM"/"METRIC"/"SUS+REL".

### 9.4 — Snapshot design session  *[planned — unscheduled]*
Dedicated session to resolve the snapshot dual-purpose tension (J1 safety-net
vs J2 performance scratch) and produce shippable CUJ docs and restore-semantics
spec. Seeded by the study brief in the plan that shipped 9.3.

Prerequisites: 9.3 B2 shipped (MZ header provides the feedback surface for
restore-label display). **Intent decided (9.x usability pass): _both_ — Snapshot
is a safety-net *and* a performance scratchpad (DESIGN §13.6).** The session no
longer chooses among A/B/C *purposes*; it specs restore-semantics + CUJ docs for
the "both" model. Output lands in `README.md` (workflow) and `DESIGN.md` §13.6
(rationale + UX).

### 9.5 — Velocity overlay polish  *[active]*
Polish pass on the live velocity overlay (§39.10): Mix baseline fix, Phrase
mode, and enable UX improvements. Serializer v21.

- [x] **B1 — Mix baseline.** Mix blend swings around `velCenter` for steps with
      no authored velocity (baseline = velCenter when `!trig.hasVelocity &&
      !trig.hasNoteVelocities`). Mix ≡ Replace on flat material; diverges only
      where steps carry authored velocities. See DESIGN §39.10.
- [x] **B2 — Phrase velocity mode.** Extend `VelMode` to `{Off, Bar, Phrase}`;
      Phrase anchors the coreTime bar grid to the phrase start
      (`fmod(stepIdx * divPpq, barPpq)`) so accents don't drift against
      bar-co-prime phrase lengths. MetaBand Mode labels: `OFF / BAR / PHRASE`.
      Serializer v21 — confirm read path accepts value 2.
- [x] **C — Skip-disabled sub-pages (general rule).** Modal-band sub-page cycle
      and landing skip inapplicable pages. For velocity: Depth/Center/Blend
      skipped when all tracks in scope have velMode == Off; Mode is always
      reachable. Landing = Mode when none enabled, else Depth. `velAnyEnabled()` /
      `nextVelSubPage()` in PluginEditor; `ScopeCtx.velAnyEnabled` wired to
      `handleOverlayEvent`. *(716ed55)*
      Per-cell `writable=false` greying removed from vel band builders
      (Depth/Center/Blend); pages absent from cycle makes per-cell greying redundant.
      *(Stage 7b)*
- [x] **C — "Available-but-inert" affordance.** `Func+AMP` key uses `ModalEntryInert`
      CellState (dim amber, §35.8.6) when all tracks have velMode==Off. *(716ed55)*
      `Func+MOD` (density): assessed n/a — density amounts are ephemeral (reset on
      overlay exit), so "no durable enabled content" has no meaningful state to read.

### 9.6 — Contextual parameter-name aliasing  *[active]*
`ParamSpec` hook for mode-dependent labels; applied to DrumSynth + Sample/Slice.
See DESIGN §6.10.

- [x] **D1 — Mechanism.** Add `juce::String (*contextLabel)(const ParamFrame&) = nullptr`
      to `ParamSpec` (`IMachine.h`). Render hook in `ManipulationZone.cpp` at the
      label-draw site: call `spec.contextLabel(frame)` when non-null.
- [x] **D2 — DrumSynth.** Per-TYPE `contextLabel` for Tone/Body/Snap/Punch/Sweep/
      SwpDec/NoiseDec slots (7 type-dependent slots × 8 types).
- [x] **D2 — Sample/Slice.** `contextLabel` for LpStart/LpLen annotating active/auto/free
      loop mode.
- [x] **Tests.** Unit test: `contextLabel` returns expected string for representative
      TYPE/loop-mode values. Also fixed latent bug: DrumSynth `punchLabel`/`bodyLabel`/
      `snapLabel` used `return "—"` which asserts on JUCE's `const char*` path;
      replaced with `juce::CharPointer_UTF8("\xe2\x80\x94")`. *(Stage 7c)*

### 9.7 — Hierarchical time signature  *[shipped]*
Make time signature a first-class, grammar-editable, hierarchical value.
See DESIGN §4.8. Serializer v21.

- [x] **E1 — Data model.** `Project.defaultTimeSig`; `Song.hasTimeSig`/`Song.timeSig`;
      `Scene.hasTimeSig` alongside existing `coreTime`. `effectiveTimeSig()` accessor
      replaces direct `section().coreTime` reads in all consumers
      (launch-quantize, metronome, velocity, density, phrase seeding). `sceneHasContent()`
      updated. Serializer v21: Set default + Song/Scene presence flags. v20→v21 upgrade stamp.
- [x] **E2 — Grammar editing UI.** `buildTimeSigBand` / `writeMetaField` in MetaBand.
      Curated stepped list + INHERIT at Song/Scene levels. Scope selection via held-scope
      flags (Func+Song = Set; Song = Song; Scene = Scene).
- [x] **Tests.** Round-trip each level; resolution precedence; 4/4 default when absent.

### 9.8 — Hierarchical tempo + top-display rework  *[shipped]*
Make tempo a hierarchical peer of time signature; retire the mouse-driven
standalone tempo bar. See DESIGN §4.8 (unified TIME page). Serializer v21.

- [x] **F1 — Data model (highest risk).** `Song.hasTempo`/`Song.tempoRatio`;
      `Scene.hasTempo`/`Scene.tempoRatio`. `effectiveTempoRatio()` multiplies song ×
      scene ratios; feeds density/velocity metric math. DAW host BPM as root.
      Serialize ratios under v21 (kHasTempo/kTempoRatio for Song and Scene nodes).
      Density lookahead barIndex fixed to use musicalGridPpq.
- [x] **F2 — Grammar editing.** Unified TIME page (`MetaBand::Time`) with two controls
      (Tempo + Sig). `Song+TRIG` or `Scene+TRIG` opens the band; entry modifier sets
      entry scope. Per-control INHERIT floor reverts; no hold-scope+Clear needed on
      this band. Time-sigs ordered by ascending bar length.
- [x] **F3 — Remove tempo bar + top readout.** `StandaloneTempoBar.*` deleted;
      PluginEditor replaced with `juce::Label tempoReadout_` showing scope-coloured
      effective BPM + time-sig.
- [x] **Tests.** CUJ sequence tests (entry, retarget, latch, swing suppression,
      sticky exclusivity); build/write round-trips; bar-length order assert;
      v21 serializer round-trip; v20 projects load cleanly.

### 9.8a — TIME page cleanup pass  *[active]*
Structural cleanup: merge Tempo + Time-Sig into one TIME page; per-control INHERIT
floor; bar-length-ordered time-sigs; pure transition layer; CUJ tests.
See DESIGN §4.8 and §13.

- [x] **Docs.** DESIGN §4.8/§4.9 merged; §13 grammar updated; README TIME row
      collapsed; CLAUDE.md single-sticky invariant noted.
- [x] **Collapse model.** `UiState`: `timeStickyMode` + `timeEntryScope` replace four
      fields. `MetaBand::Time` replaces `Tempo` + `TimeSig`. `timeScopeFor` unifies
      the two scope fns. `ScopedSectionMatrix` kSong[0] "TEMPO"→"TIME". `KeyLabel`
      single relabel.
- [x] **Pure transition layer.** `applyTimeEntry` / `escapeTimeSticky` /
      `isTimeEntryChord` as pure fns. PluginEditor wired; VerbClear Tempo+TimeSig
      blocks deleted; Swing block kept.
- [x] **Single TIME band builder + writer.** Two fields (Tempo + Sig); `kTimeSigs`
      ascending bar length; `writeMetaField` `case MetaBand::Time`; INHERIT floor.
- [x] **CUJ + unit tests.** 7 sequence tests; INHERIT floor round-trip; bar-length
      order assert; SurfaceModelTest label update. All tests pass.

### 9.9 — README + verification  *[active]*
- [x] **G — README.** TIME page grammar (single row), gesture tree updated.
- [x] **9.8a cleanup.** Unified TIME page shipped; all tests pass (5 staged commits).
- [ ] **End-to-end verification.** Build; run tests; standalone smoke (TIME page shows
      two controls; Song+TRIG and Scene+TRIG both open it; scope retarget works;
      INHERIT on each control clears its override; time-sigs in bar-length order;
      entering density/vel exits TIME; swing not triggered while TIME open). DAW
      (v21 round-trip; v20 project compat).
- [x] **Remaining open items resolved.** 9.5 C fully closed; 9.6 D2 done. *(Stage 7)*

### 9.10 — Grammar-allocation pass  *[shipped]*
Follow-on to the 9.x usability deep-dive (`USABILITY-REVIEW.md`). The
goal-sharpening shipped in PRINCIPLES (North-Star preamble + §2 orthogonality
clause + §4/§10 single-purpose reconcile + §15 frequency × stakes + new §19
visual grammar) and NON-GOALS (fence #11 retrig ruling; generator-family note).
The deep-dive *reclaimed* a large block of surface; this pass re-allocates it
under the sharpened rules. Each binding move carries the CLAUDE.md modality-test
triad (resolution + scope-routing + round-trip); any moved overlay gets a
`kOverlays` descriptor test; `layerBanner` stays exhaustive under `-Werror`.

- [x] **Generator hub on `3`.** `3` held ≥350 ms → momentary picker for the
      deterministic-generator family (Euclid / Density / Vel); pick a cell →
      that generator activates with its own existing lifetime; `3` tapped →
      tap-tempo (retained). Retires scattered entries: `Phrase+Fill` (Euclid),
      `Func+MOD` (Density), `Func+AMP` (Vel), bare-`Func`-hold / `Func+Song`
      Density band peeks. New `SurfaceLayer::GeneratorHub` + banner +
      `CellState::GeneratorEuclid/Density/Vel` tokens (distinct hues per §19).
      *(Stage 9.10c — 7c45af6)*
- [x] **Metronome → TIME overlay.** `Func+3 → MetronomeToggle` remap removed;
      `Func+3` freed/reserved. TIME band gains field 2 "CLICK" (stepped 0/1)
      reading/writing `Clock::isMetronomeEnabled()`.
      *(Stage 9.10d — f766987)*
- [x] **Retrig split (NON-GOALS fence #11 ruling).** Per-step authored ratchet
      promoted to TRIG-band field 5 "RTG" (stepped 0..8; 0=off, 1-8=/4.."/32T";
      P-lockable via hold-step + encoder). Free-running live stutter
      (`setRetrigActive` on non-slicer tracks) removed. `Fill+TRIG` now
      slicer-only (slice-point picker preserved intact).
      *(Stage 9.10e — 815df00)*
- [x] **Reclaimed slots left reserved/inert.** `Func+3`, `Func+MOD`, `Func+AMP`,
      `Phrase+Fill`, `Fill+TRIG` (non-slicer) are freed and currently
      reserved/inert — correct §2 default until next design input. `CueScope`
      binding deferred. Full visual-token audit deferred (separate item below).
- [x] **Home-key orientation cues (PRINCIPLES §19).** `SurfaceCell::homeKey` bool
      set for step indices 1 (F) and 4 (J) in every layer — pure orientation cue,
      screen renderer and controller render can mark these cells.
      Hardware spec: literal nibs at the same positions.
      *(Stage 9.10f — 435f012)*
- [ ] **Visual-grammar token audit (PRINCIPLES §19).** Per-`CellState` pass (with
      `CellStates.def`, 8.6): confirm every performable state has a
      colour/brightness/blink proxy; flag text-only states as hardware bugs.
      *(deferred — needs separate review)*

### 9.11 — Gesture-affordance visual language + context inspector  *[shipped]*

Closes the gap between the gesture grammar and the visual language; folds in two
concrete bugs. Ships PRINCIPLES §19 affordance/inspector clauses, DESIGN §6.11,
README affordances + inspector.

- [x] **Bug A: stale Func+3 "MET" binding.** `KeyBindings.cpp` `Func+3 →
      MetronomeToggle` row removed; bare TapTempo hint cleared. `KeyBindingTest`
      updated. *(Stage 1 — ddb96eb)*
- [x] **Bug B: generator hub cells had no paint block.** `paintStepRows` had no
      early-return for `generatorHubHeld` — fell through to step-number renderer.
      Added dedicated block consuming `model.step[].primary`. Model-driven text
      also adopted for machine picker, TrackFxPicker, MasterFxPicker.
      `SurfaceModelTest` covers `testGeneratorHubPrimary` + `testMachinePickerPrimary`.
      *(Stage 2 — 7b7204a)*
- [x] **Affordance data model.** `SurfaceCell` extended with `tapLabel`,
      `holdLabel`, `doubleTapLabel`, `primaryIsHold`. New `KeyAffordances.{h,cpp}`
      (14-entry seed table). `SurfaceModel` injects at rest only. `KeyAffordanceTest`.
      *(Stage 3 — 8c56659)*
- [x] **4-slot KeyButton rendering.** `paintCell` gains a 5-zone affordance path
      (dbl-tap / tap / primary / hold / func); painted vector glyphs; faint access
      glyph; fast-path for single-action keys. *(Stage 4 — 9a965f3)*
- [x] **Full affordance table.** `KeyAffordances` extended to 22 entries: all 8
      modifiers (hold=scope, dbl=LATCH; Func dbl=ESCAPE), verb keys (VerbPlay
      dbl=STOP), nav keys (TRACK UP/DOWN, PAGE LEFT/RIGHT), RecordArm, PlayStop.
      *(Stage 5 — b88ee51)*
- [x] **Top-chrome consolidation.** `tempoReadout_` (28px) + `fileBar_` (24px)
      merged onto one 28px row; 26px freed; `inspectorRow_` reserved.
      *(Stage 6 — 82f4edd)*
- [x] **Context inspector.** `InspectorModel.{h,cpp}` pure builder (KEY / HELD /
      OVERLAY / EDIT, idle fallbacks, KEY reuses `KeyAffordances`). `InspectorBar.{h,cpp}`
      slim JUCE component 4 columns. Wired into `PluginEditor` resized + 30Hz tick.
      `InspectorModelTest` (5 tests). *(Stage 7 — 54c45c2)*
- [x] **Docs.** PRINCIPLES §19 + in-cell affordance + inspector clauses. DESIGN §6.11
      (§6.11.1 slots/glyphs, §6.11.2 chrome consolidation, §6.11.3 inspector regions).
      README affordances + inspector in implemented list; shortcut map already correct.
      *(Stage 8 — this commit)*

### 9.12 — Unified Gesture Grammar (table-driven dispatch + derived affordance display)  *[active]*

Closes the display–dispatch drift: every key's visual frame (five fixed
slots, top→bottom: dbl-tap · tap · **PRIMARY** · hold · func) is derived from the
same `resolveBinding(..., Gesture)` query that will dispatch behaviour, so
they cannot silently disagree. `KeyAffordances` deleted; `Gesture` axis added to
the grammar; grid picker cells share a common `paintGridCell*` renderer.

- [x] **Stage 1 — Gesture axis in grammar.** `Gesture` enum (Tap / Hold /
      DoubleTap) extracted to `src/command/Gesture.h` (breaks circular
      dependency). `KeyBinding` grows `gesture` + `promoted` fields (appended,
      positional rows unaffected). `resolveBinding` filtered by gesture;
      `promotedGesture()` added. New Hold / DoubleTap / promoted rows for
      modifiers, verbs, nav, TapTempo hub. `KeyBindingTest` extended.
      *(Stage 1 — c394b1b)*
- [x] **Stage 2 — Display derives from grammar; `KeyAffordances` deleted.**
      `buildSurfaceModel` replaces `findAffordance` with a `deriveSlots` lambda
      that queries `resolveBinding` per gesture for every cell. `SurfaceCell`
      `primaryIsHold` → `primaryGesture`. `KeyAffordances.{h,cpp}` +
      `KeyAffordanceTest.cpp` deleted; `InspectorModel` rebuilt from grammar.
      Anti-drift test in `SurfaceModelTest`. *(Stage 2 — 8a01d8e)*
- [x] **Stage 3 — Fixed-reserved 5-slot rendering.** `paintCell` always
      reserves all four rail rows (dbl + tap + hold + func) so the primary locks
      to the same centre band on every key. Blank space held when a slot is empty;
      rail glyph + text drawn only when non-empty. `paintAffordanceSlot` gains
      glyphType 3 (amber func chip via `theme::kFuncAccent`). *(Stage 3 — 3b1c0d4)*
- [x] **Stage 4 — Shared grid-cell renderer; picker blocks migrated.**
      `paintGridCellFill` / `paintGridCellText` (KeyButton.h/cpp) normalise
      fill + press + primary text to 9 pt across grid cells. Five picker/hub
      blocks in `paintStepRows` migrated (machine, FX insert [preserves masterOnly
      dimming], master FX, generator hub, morph step). Complex modes with
      screen-residual text left as-is (§35.8.1). *(Stage 4 — 7e75fd8)*
- [ ] **Stage 5 — Behavioural golden-test net.** `RecordingEffects`-driven
      `tests/DispatchGoldenTest.cpp` captures current `dispatchDown`/`dispatchUp`
      output across all families before any dispatch rewrite.
- [ ] **Stage 6 — New ActionIds + handlers.** Append-only ActionIds for latch,
      escape, restore pop/floor, rec-arm overdub, play-stop, step latch, nav
      unlock, overlay-entry opens. `CommandEffects` methods. Guard test.
- [ ] **Stage 7 — Dispatch migration.** Family-by-family (7a modifiers → 7f
      steps); each sub-step routes via `resolve(..., gesture).action →
      handleAction`, deletes the imperative branch, and keeps goldens green.
- [ ] **Stage 8 — Exhaustiveness guard + cleanup.** `handleAction` switch
      exhaustive (`-Werror=switch`, no `default:`); test that every ActionId is
      handled; remove `KeyBinding::hint` field.

> **Natural ship point:** Stages 1–4 are landed. Stages 5–8 are the
> dispatch rewrite — larger scope, separate branch if warranted.

### 9.13 — Redundancy / SSOT consolidation + switch hygiene  *[active]*

Eliminate the implicit "two things meant to stay in sync" defects (a class that
caused several recent bugs) and make silent `switch` fall-through a compile error.

- [x] **Stage 1 — Switch fall-through hygiene.** `-Wimplicit-fallthrough` on all
      targets; PRINCIPLES §20 (no default-case mandate; exhaustive-enum carve-out).
- [x] **Stage 2 — `refreshSurface()`** collapses the 20 hand-paired
      `repaint()` + `keyboardArea_.repaint()` call sites.
- [x] **Stage 3 — Track-length single writer.** All edits route through
      `setTrackLength`; ownership comments on `Track.length` ↔ APVTS param.
- [x] **Stage 4 — Euclid state moves as a unit** (restoreEuclidStash /
      forgetEuclidEditorState; one teardown path).
- [x] **Stage 5 — Remove dead mirrors** (`latch.anySteps`); document SurfaceModel
      label authority.
- [x] **Stage 6 — Document state-ownership invariants** (DESIGN §4.7a).
- [x] **Stage 7/8 — Unified modal read SSOT.** Editor-dispatch harness proved
      unviable headless (component-teardown segfault); pivoted to a pure seam.
      `activeModal(ui)` (`src/ui/mode/ModalState.h`) is the one modal-priority
      query; inspector funnels through it; a drift test locks it to
      `resolveActiveLayer`. **Finding:** held-chords coexist with entered modes,
      so a single mutually-exclusive storage field is impossible — storage stays
      multi-field, the *read* is single (PRINCIPLES §18 caveat, §20). Also fixed:
      editor async self-refs now use `Component::SafePointer`.

### Future (structural)
- [x] **Collapse `timeStickyMode` / `densityStickyMode` / `velStickyMode` into a
  single `Overlay overlay` field** in `UiState` so that coexistence is unrepresentable
  at the type level. `Overlay` enum now defined in `state/UiState.h`; `Overlay.h`
  is a shim. `activeOverlay()` returns `ui.overlay`; `escapeOverlay()` guards each
  arm. *(Stage 6 — 5259b6a)*

### 9.14 — Grammar-consistency pass: Clear/Delete, FX picker, per-step inspector, move-step  *[active]*

One coherent **orchestra paradigm** (PRINCIPLES §21) makes destructive verbs
predictable, puts the FX picker under its section, brings per-step note/P-Lock
surgery to the surface, and adds a move-step primitive — all inside the
existing scope+verb grammar. Organizing principle: **hold = reveal & edit;
tap = navigate/toggle** (PRINCIPLES §5). Docs-first.

- [x] **Stage 0 — Docs.** PRINCIPLES §21 (orchestra paradigm; Clear blanks /
      Delete removes; actor = scope, stage = current phrase; `+Song` widens via
      a legal cross-column compound — *no exception needed*; confirmation
      scales by blast radius). §5 revised: section *tap* = params, section
      *hold* = picker (grid re-skin, §17 long-press); held step reveals its
      inspector. DESIGN §13.2 (Clear/Delete table + `Track+Song` all-phrases +
      confirm tiers), §13.8 (held-step inspector), §19.1 (move-step +
      Step-Position panel), §32.2/§32.3 (hold-FX picker; `Func+FX` freed).
- [ ] **Stage 1 — Clear/Delete consistency + confirm + preview.** Immediate vs
      confirm tiers (immediate auto-snapshots; confirm via `PendingConfirm`).
      `Track+Song+Clear` all-phrases (`kModTrack|kModSong` row +
      `clearTrackAllPhrases`). Armed-preview banner names target + reach.
- [ ] **Stage 2 — Hold = picker.** `Gesture::Hold` on Section idx5 →
      `OpenTrackFxPicker`; tap → params; `Song`+hold-FX → master picker;
      retire `Func+FX` row.
- [ ] **Stage 3 — Held-step inspector.** `StepInspector` layer; MZ lock badges
      + tap-to-clear (absorbs `Func+step` PLockClear); tap SRC → note editor
      (absorbs `Func+Src+step`).
- [ ] **Stage 4 — Move-step + Step-Position panel.** `swapSteps` (full Step
      travels); hold step + `←/→` bubble-swap; hold step + `Func+←/→`
      microOffset; MZ flips to position panel (encoders = move + micro-time);
      `QUANT` zeroes offset.
- [ ] **Stage 5 — Copy/paste discoverability.** Armed banners/preview for
      Record/Play; no clipboard-model change.
- [ ] **Stage 6 — Tests + README.** Confirm-tier resolution; `Track+Song+Clear`
      round-trip; move-step carries overrides round-trip; inspector lock-clear
      + note edit; hold-FX picker entry. README shortcut table; remove retired
      `Func+Src+step` / `Func+FX`.

> **Retired/relocated gestures (this item):** `Func+Src+step` (note edit) →
> hold-step inspector + SRC; `Func+step` (P-Lock clear mode) → inspector tap-to-clear;
> `Func+FX` / `Func+Song+FX` (effect pickers) → hold-FX / `Song`+hold-FX.

### 9.15 — Unified surface invalidation (events redraw, the clock only animates)  *[active]*

Recent draw fixes papered over missing redraws with per-mode "repaint every
tick" timers (e.g. the MZ `StepPosition` `area_.repaint()`, `4817f57`). The
cause: there is **no single "the surface may have changed" signal** — ~50
synchronous `refreshSurface()` sites, several polling timers, and the
controllers' brute-force 30 Hz rebuild all coexist. This item unifies *when* the
surface redraws (the §35.8 model already unifies *what*): one coalescing
invalidation channel for discrete events, one self-suspending clock for
continuous animations. Docs-first.

- [x] **Stage 0 — Docs.** PRINCIPLES §22 (one invalidation channel; discrete =
      events, never polled; the lone animation clock owns only continuous decays
      and suspends when settled). DESIGN §35.9 (`SurfaceDispatcher` +
      `handleAsyncUpdate` single build → all sinks; §35.9.2 audio→UI discrete
      bridge on active-step change, not PPQ; §35.9.3 the one animation clock).
- [x] **Stage 1 — `SurfaceDispatcher` + on-screen path.** `AsyncUpdater`-based
      dispatcher (`ui/SurfaceDispatcher.h`); `refreshSurface()` → `invalidate()`
      (all 22 sites repointed via the one header method); `onFrame` repaints
      chrome + grid (controller fold-in is Stage 2). `SurfaceDispatcherTest`
      asserts the coalescing contract (N invalidate ⇒ 1 frame; idle ⇒ 0; re-arm
      after delivery; pending-at-teardown cancelled). No behaviour change.
- [x] **Stage 2 — Fold controllers in.** `ControllerPortManager::drain` split
      into `drainInput()` (FIFO→onInput, every tick) + `renderSurface()`
      (onConnect-once + LED render). `onFrame` → `renderSurfaceFrame()` repaints
      chrome + grid then `renderControllers()` (one `buildSurfaceModel`, both
      surfaces); the unconditional per-tick rebuild is gone. The editor tick now
      `refreshSurface()`s on transport/morph/playhead change (interim ppq poll;
      Stage 3 moves it to the audio bridge). **Discipline sweep:** every bare
      `keyboardArea_.repaint()` and the `EditorEffects` `ed.repaint()` callbacks
      (mutes, machine-assign, capture, generic `requestRepaint`) now route through
      `refreshSurface()`, so a grid change can never leave a controller stale —
      the §35.8 no-divergence invariant. The only direct paint left is inside
      `renderSurfaceFrame`. Connect/disconnect triggers an initial render.
- [x] **Stage 3 — Audio→UI discrete bridge.** `surfaceDirtyFromAudio_` set in
      `drainEngineCmds` whenever the audio thread applies a queued param change
      (CC / encoder / P-Lock writes — all async via the engine FIFO); the editor
      reads-and-clears it (`takeSurfaceDirty()`) on its tick → `refreshSurface()`,
      which *settles* the value on screen + controllers (incl. the final value
      after the user stops turning). `KeyboardArea`'s PPQ/length poll **removed**
      (no timer at all now); length/divider routed through the editor's APVTS
      listener. **Refinement vs the original sketch:** no `lastActiveStep_[]` /
      discrete-step computation — the playhead's sub-step phase
      (`playheadPhase`, the X-Touch envelope) is *animation*, so playback frames
      stay continuous; discrete-step-only would freeze that envelope. **Playhead
      moved to the display vblank** (`juce::VBlankAttachment`): the 30 Hz tick
      sampled the PPQ clock too coarsely (a 16th spans 4/3 ticks → visible "fast,
      fast, slow"), so the playhead now recomputes from the live clock at refresh
      rate, repainting only grid + controllers when the PPQ moved. Test:
      `testSurfaceDirtyOnParamApply` (apply sets the flag once; idle blocks
      don't; `takeSurfaceDirty` clears it).
- [x] **Stage 4 — Eliminate the component timers.** Refined from the original
      "single animation clock": the architecture settled on **two** clocks by
      necessity (DESIGN §35.9.3) — the always-on ~30 Hz editor timer (slow chrome
      decays + hardware-facing work that must survive screen-sleep) and the
      display **vblank** (screen playhead). Component-timer disposition:
      - [x] **`ManipulationZone` timer removed** (was a perpetual 30 Hz poll):
        `refreshSliders()` is now frame-driven from `renderSurfaceFrame`; the
        `StepPosition` `area_.repaint()` hack is deleted (the encoder write routes
        through the channel — mouse via `onStepPositionChanged`, controller via
        the input-drain `refreshSurface`); the timer now runs **only during
        CC-learn** (pulse + completion) and self-suspends.
      - [x] `InPluginTransport` (15 Hz timer) removed → its `pollState()` (shadow-
        gated) is called from the editor's always-on tick, so host-driven
        play/rec/metronome still surface.
      - [x] `SamplePoolOverlay` (10 Hz) → self-suspends via `visibilityChanged`:
        polls only while the manager is on screen (was polling even when closed).
      - [x] Idle repaints: gated by `dirty`/`modelDirty`/`ppqMoved` + the vblank's
        step-change check, so a stopped, idle surface issues none by construction
        (hardware spot-check still worthwhile).
      Surviving timers: the one always-on editor timer, the display vblank, and
      the 1 Hz controller-hotplug poll. MZ + pool timers self-suspend to their
      transient states (CC-learn / overlay-visible).

### 9.16 — Performance capture: the tape deck  *[shipped]*
The 8.26 blind WAV toggle became a visible, transport-aware "separate
recording device" (key standalone workflow). Mental model: a tape deck, not a
DAW export.
- [x] **`CaptureController`** (`src/io/CaptureController.h`, pure/JUCE-free,
      unit-tested) — the emergent-tail state machine: Idle / Armed / Recording /
      JustSaved. The single finalize rule: silence finalises only while *winding
      down* (transport stop edge or tap-stop), never during active playback, so a
      musical rest can't chop a take. `double-tap = hard cut` is the reliable
      stop (drone/noise-floor). No auto-finalize "mode", no long-press toggle.
- [x] **Gesture grammar on one cell** (`Func+Song+Record`): tap = arm (rolls on
      Play; or now if already playing) / stop · double-tap = roll-now / hard-cut ·
      long-press = reveal folder (idle) / discard (just-saved window). Routed via
      `GestureRecognizer::kCaptureToken`, resolved on key-up + mid-hold timer.
- [x] **Feedback strip** under the master meter (`paintCaptureStrip`): ARMED ▸
      starts on Play / ● REC m:ss / ◐ STOPPING — waiting for silence / ✓ saved →
      path (hold REC to discard). Destination visible from arm onward.
- [x] **Exit-while-recording** dialog (`captureExitGuard`) chained ahead of the
      9.2 dirty-project guard: Stop & exit (finalise) / Discard & exit / Cancel.
- [x] Writes **directly** to the destination (crash ⇒ real partial file).
- [x] **Per-track stems** *(shipped — 6.1 Workstream D)* — every take is a
      directory `Captures/capture-<stamp>/` holding `master.wav` + one
      `track-NN.wav` per non-empty Master-routed track. No per-track arm surface
      needed: routing (CHANNEL "Out") *is* the stem grouping — buses fold in their
      feeders, feeders/Off/empty-Route-buses are skipped. Always-on; same
      arm/level/tail lifecycle as the master. (DESIGN §27.)
- [ ] **Crash-partial header refresh** *(deferred)* — a partial WAV's RIFF size
      fields are patched only on clean close; periodic header refresh would make
      a crash-partial fully playable.
- *Deferred:* metronome count-in (double-click immediate-roll is the pre-roll);
  configurable silence threshold / tail (constants in `CaptureController`).

### 9.17 — Unified launch-quantize + per-track "clip" transport  *[shipped]*
One quantize authority for every deferrable action, and a Session-View
stop/restart built from mute + a phase-reset primitive (no per-track `Stopped`
state). Docs shipped first: PRINCIPLES §25, DESIGN §4.8 / §13.4 / §16.1.
- [x] **`LaunchQuant` enum** `{Instant, Beat, Bar, Bars2, Bars4, Bars8,
      PhraseEnd}` replacing `Project.launchQuantizeBars`; generalise the
      scene-launch bar engine (`PluginProcessor.cpp` `queueScene` /
      `prepareSceneLaunch` / `stagedSwap_`, `boundary = ceil(blockStart/grid)*
      grid` at ~L1798, applied at top-of-next-block ~L1541) into one shared
      boundary helper covering beat / bar-multiple / phrase-end.
- [x] **Route Song switch + Phrase deviation through the authority** — fixes the
      §16 drift (`setActiveSong` is currently immediate; phrase deviation is
      currently immediate). Double-tap = instant, phase-preserving.
- [x] **Quantized mute/unmute** — arm `trackMute` toggles to the grid (reuse the
      declick `muteGain_` path); **phase-reset primitive** (snap
      `nextTriggerPpq_[t]` to the boundary + `rearmOneShots(track)`, mirroring
      the transport-start `freshStartPending_ → nextTriggerPpq_.fill(0)`
      pattern); **`Mute + Play + step`** relaunch/retrigger gesture. Bare unmute
      resumes in phase.
- [x] **Looper edge-arming onto the shared grid** — `loop_sync` becomes
      *length-only* (`Free | Free Len | Sync`); REC/PLAY/overdub edges arm via
      the shared authority (retire `quantPeriodSamples()` as the timing source,
      keep it for synced *length*). Double-tap REC/PLAY = instant (unchanged).
- [x] **Per-track `launchQuant` override** (`FollowGlobal` default; folds in the
      old `launchMode PhraseEnd`) + Set-level grid value on the transport-globals
      page (`Func + 7`). `PhraseEnd` is a **per-track-override-only** value —
      the Set grid offers only `{Instant, Beat, Bar, Bars2, Bars4, Bars8}`, so
      whole-band Scene/Song launches stay atomic on one shared boundary.
      *Shipped:* both grids live on `Func + 7` — slot 4 = Set grid (`LaunchQ`),
      slot 5 = focused-track override (`T-LnchQ`, Follow…Phrase), mirroring the
      per-track Scale slot already there. A Track-scope TRIG tail-slot is a
      possible later refinement.
- [x] **Serializer v24 → v25:** `Project.launchQuant` (enum; legacy int
      1/2/4/8 → `Bar/Bars2/Bars4/Bars8`), per-track `launchQuant`, `loop_sync`
      re-interpretation. Round-trip test for legacy load.
- [x] **Unit tests per modality** (CLAUDE.md rule): boundary resolution (right
      grid → right instant), scope routing (held modifier lands in the right
      scope), and a write→serialise→reload round-trip. Cover the phase-reset
      relaunch and the instant-override double-tap.

### 9.18 — Sample-pool identity re-architecture (stable ids + typed pickers)  *[shipped]*
Fixed the flat-pool-index rot found in manual testing (the "fluttering-bumblebee"
cleanup, Items 2 + 6): a sample reference was a raw array index, so it drifted when
the pool reordered (reserved volatile REC slots + reload rebasing), and every
picker showed every entry regardless of what the machine can use (a PCM-less Stream
entry offered in the Sampler; a Stream track's default `sample_id=0` resolving to
the wrong entry → silence). Plan file:
`~/.claude/plans/sample-pool-identity-rearchitecture.md`. Shipped in 6 commits.
- [x] **Two identity domains, one token.** Persistent entries (File/Stream) are
      identified by content hash (`hashXX32`) — stable across reorder *and* file
      moves (reload → hash matches → auto-relink). Volatile captures (Record/Loop)
      get a **session-local monotonic id**. `SampleId {domain, key}`; the pool
      resolves it via `idOf`/`indexOf`/`resolve`, decoupling identity from position.
- [x] **Serializer routes refs by content hash.** Sample-ref P nodes are stamped
      with `sh` (hash); `normalizeSampleRefs` (runs every load) re-resolves each to
      the pool entry's current position. Serializer **v28 → v29**; the v28 bridge
      resolves legacy flat indices via the pool node's `i` attribute. Round-trip +
      legacy-load tests.
- [x] **Capability-filtered pickers.** `IMachine::sampleClass()` (Pcm/Stream);
      `sampleAcceptedByTrack()` gates the MZ picker + pool-overlay assign. Retired
      the `ensurePcm`-on-pick path (Stream stays out of the PCM players).
- [x] **Pool overlay grouping** into FILE / STREAM / RECORD / LOOP sections.
- [x] **Save-and-promote** gesture (`promoteVolatileToFile`): write a volatile
      capture to a WAV, reload it as a durable File entry, repoint references to it.
- [x] **Missing-sample surfacing:** `missingSampleCount()` + `onStateLoaded` hook;
      the editor posts a status hint to relink. Per-entry `relink()` already exists.
- [x] **MZ slot order:** StreamMachine `sample_id` → slot 0 (Sample), `start` →
      slot 1, so the picker button renders over the Sample param.

### 9.19 — Loop-seam / runtime-sample / pool polish  *[shipped]*
Post-9.18 follow-up from manual testing. Plan file:
`~/.claude/plans/loop-seam-runtime-samples-pool-polish.md`. Six commits.
- [x] **Loop-seam crossfade.** `SamplePlayer`'s forward loop hard-wrapped with
      `fmod` (an audible click); it now crossfades the seam — **borrows real tail
      material past `loopEnd`** when the sample has it (loop period preserved, right
      for bar-synced loops), else **eats into the loop** at the sample end. New
      SampleMachine SRC slot `samp_loop_xfade` (ms, default 8; `0` = old hard wrap).
      StreamMachine, which hard-started/stopped with no declick, gained a ~5 ms
      anti-click gate (Stretch already had one; neither actually loops through
      `SamplePlayer`, so the plan's shared-path premise was corrected here).
- [x] **Stream/Stretch loop-length sizing helper.** They stay trig-gated; assigning
      a sample to a track with **no trigs** auto-fits it — sizes the track length to
      the sample's musical bar-length and seeds one trig on step 1 (no bespoke
      gesture; rides the sample-assign hook). A sequenced track is left untouched.
- [x] **Runtime missing-sample handling.** `SamplePool::rescanMissing()` re-stats
      path-backed entries on pool-manager open, so a sample deleted/moved *while
      running* shows MISSING + Relink; a persistent editor banner replaces sole
      reliance on the fading load-time toast. Decoded PCM is retained (a File keeps
      playing from RAM — no mid-set dropout); a Stream with no reader falls silent.
- [x] **Per-group stable pool numbering.** The in-machine picker numbers entries
      within their origin group (`groupOrdinal`: FILE 1, STREAM 1, REC 1 …) instead
      of the raw pool index, which jumped when the volatile REC slots re-seed at the
      pool front on reload.
- [x] **Looper default → Sync** (was Free; Free ignores tempo and is the hardest
      mode to reason about). New tracks only.
- [x] **SRC sample-picker first-paint fix.** The picker button is laid out the
      instant it becomes visible (was waiting for the next `resized()`).

### 9.20 — Section-stack unification (one resolver, four consumers)  *[shipped]*
Item 7 of the "fluttering-bumblebee" cleanup
(`~/.claude/plans/i-found-quite-a-fluttering-bumblebee.md`). The section-key
behaviour was split across two systems — the schema-derived Machine/Track param
stack (`ScopeSectionSelect`) and a separate static meta/sticky matrix
(`ScopedSectionMatrix` + `MetaBand`) — so section-key tint, the MZ header colour,
and dispatch could disagree. One resolver now feeds the editor dispatch, the
section-bar painter, and the MZ banner. Six commits.
- [x] **Pure model + static table.** `SecCandidate` gains an action
      (Param/Meta/TimeSticky), a `metaIndex`, a `funcQualified` flag and a label;
      `SectionStackTable.h` carries the non-param content a held scope maps a key
      onto (Track DIV, Phrase LEN, Scene/Song TIME, Song master FX, Func
      COND/NOTE/TRSP). Pure + unit-tested.
- [x] **`SectionResolve` resolver drives dispatch.** `resolveSectionKey()` unions
      the schema param candidates (shared with `sectionsForKey`) with the stack
      rows and returns `{winner, action, metaIndex, label, groups}`; the editor's
      Section-key dispatch collapses to resolve → switch on action.
- [x] **The stack is a true underlay.** Each key shows the layer nearest the held
      ceiling — **bare TRIG shows the Track layer's DIV** (and pressing it opens the
      DIV band), Scene+FX shows the Song master-FX layer. *(Refined in 9.21: the
      first cut fell only downward and pinned a scope-coloured param — see below.)*
- [x] **Winner-colour painting.** Section buttons tint by the resolved winning
      origin (`originColour`) — a track-DSP FILTER reads cyan, a meta row reads its
      scope hue — not a blanket held-scope wash; the dim wash for contentless keys
      generalises to every scope.
- [x] **MZ banner from the page origin.** The MZ carries `pageOrigin_`, fed once
      by the editor at selection time (never re-derived from `slotOffset_`); the
      header word (MACHINE/TRACK/PHRASE/SCENE/SONG/GLOBAL) + colour + wash come from
      it, so a scope-scoped page names its scope after the modifier releases.
- [x] **Func outline never latches.** `funcOutlineActive(funcHeld, pageFunc)`:
      the Func border shows only while Func is held and only on a func-qualified
      key/page; a latched COND/TRANSPORT band keeps its body colour but drops the
      outline on release.

### 9.21 — Section-stack underlay: final model (nearest, colour-by-winner)  *[shipped]*
Corrects the 9.20 first cut after a design discussion pinned down the intended
model (`~/.claude/plans/section-stack-underlay-final.md`). The taught **contract**
is hold-scope + section = that scope's content (Track+TRIG=DIV, Song+FX=masterFX);
fall-through is a **convenience + colour-teaching aid**, and every key is coloured
by the scope its content *truly* comes from. Five code/test commits + this block.
- [x] **Nearest, ties toward deeper — falls up OR down.** `resolveSectionKey`
      picks the layer nearest the held ceiling (`abs(origin − floor)`, ties to the
      larger `SecOrigin`). The ceiling's own layer is distance 0, so canonical
      chords are always exact; off-ceiling keys fall to the nearest real layer in
      either direction (Song+FILTER falls *up* to the machine filter; Scene+FX falls
      *down* to Song master-FX). Replaces the 9.20 downward-only peel.
- [x] **No per-scope param layer — the fiction is dead.** Params have only
      step-override ELSE track-base (OEB); holding a scope never changes the write
      target. The resolver no longer consults `ScopedSectionMatrix` and no longer
      pins a scope-coloured param candidate. Scene+FILTER edited track-base while
      painted scene-green — it now falls to a **real** layer (the machine filter, or
      a track-DSP block) and is coloured by it.
- [x] **Honest colours + up-fill.** Deep-scope keys that fall up are enabled, not
      dim (no wasted real estate); a section coloured by a scope hue genuinely has
      content in that scope. `SurfaceModel`/MZ code was already winner-driven — only
      the resolver's honesty and the stale assertions/comments changed.
- [x] **Editor pages the winner.** Section dispatch routes `selectSection(index,
      winner==Track)` so a key pages the same layer it is coloured by.
- [x] **Selected-section underlay locked.** A committed section survives entering +
      exiting a sticky Vel/Density/Time overlay (test); single owner (overlay in
      `UiState::overlay`, selection in `masterSection`).
- Deferred: **section→scope association review** (DIV@Track / LEN@Phrase /
      TIME@Scene·Song / masterFX@Song may be sub-optimal — the colour language now
      makes a bad placement visible; a `SectionStackTable` row-move is a one-line
      change) and **per-scope sections** (the table supports them; sparse scopes are
      fine today).

### 9.22 — Func colour model + `Func+Song` = Global scope  *[shipped]*
Makes the Func layer obey the 9.21 colour-by-winner rule and wires **Global** as a
real scope. Plan: `~/.claude/plans/func-scope-promotion-global.md`. Five code/test
commits + this block. Decisions: Func is context-split; promotion is Global-only
for now; Global surfaces TRSP (Func+7 shortcut kept); Global gets its own hue.
- [x] **Dedicated Global hue.** `kScopeGlobal` (azure) — `originColour(Global)` no
      longer borrows the Song gold, so a promoted Global page is visibly distinct.
- [x] **`Func` is context-split (one rule).** `sectionResolveMode(ui)` owns it:
      `Func+Song` → `{Global, primary}`; bare `Func` → `{Machine, meta hierarchy}`
      (COND/NOTE + the Func+7 TRSP shortcut); `Func` over an unwired scope falls back
      to the meta hierarchy (promotion is Global-only). Painter, dispatch and MZ all
      read this one function, so they cannot diverge.
- [x] **Global scope content = TRSP, floor-only.** A non-funcQualified Global row
      puts the transport globals on the FILTER key, reached by `Func+Song`. The
      resolver gates Global as *floor-only* in the primary layer, so it never leaks
      up into a shallower scope's FILTER (Scene/Song+FILTER keep the 9.21 machine
      filter); the `Func+7` shortcut still reaches TRSP through the func-meta layer.
- [x] **Colour-by-winner under Func + border as the modifier signal.** The section
      painter drives the resolver under Func too: COND/NOTE read machine-neutral,
      TRSP reads Global azure, `Func+Song` fall-up keys read their scope hue — with
      the func-colour border layered on every non-dim key while Func is held (never
      latched). The MZ meta bands (COND neutral, TRSP azure) + `pageOrigin_` match.
- [x] **Editor + MZ routed through the resolver.** The Func+section (MetaSection)
      dispatch resolves via `sectionResolveMode` and switches on the action, so
      `Func+Song+TRIG` fires Song TIME (was wrongly COND). `Func+Song+Record`=CAPTURE
      (VerbRecord path) is untouched.
- Deferred: the **promotion ladder** (Scene→Song, Phrase→Scene, Track→Phrase — a
      row per rung) and additional **Global / master-bus content** beyond TRSP.

---

## Phase 10 — Melodic & Harmonic Authoring  *[active]*

The tonal layer: a key-signature system built on the **circle-of-fifths
brightness line**, scale-aware manual authoring, and two supportive
(deterministic / manual — never ongoing-generative) tools. Full design in
PRINCIPLES §23, DESIGN §4.10 + §39.11–39.12; execution plan in
`~/.claude/plans/i-would-like-you-validated-bear.md`. Committed per phase.

### 10.1 — Docs  *[active]*
PRINCIPLES §23, DESIGN §4.10 + §39.11–12, this phase block, README stubs.

### 10.2 — Scale core *(src/core/Scale.h)*
`KeySig` + `Modifier`; brightness-window math; functional Add/Alter algebra
(collection-anchored, compatibility-gated); derived `pcMask`/`degrees`/
`coreTier`/`quantize`/`classicalName`; whole-tone + diminished. Pure, unit-tested.

### 10.3 — KeySig hierarchy + persistence
Project/Song/Scene fields; `effectiveKeySig()` cascade; serializer bump (one past
head); round-trip test at all three levels.

### 10.4 — KeySig editor
`Overlay::Key` + `kOverlays` row; `buildKeyBand()` (brightness / root / modifiers
/ name); KEY sub-page off the TIME band; scope routing test.

### 10.5 — Scale-aware authoring
In-scale highlighting; root-anchored keyboard layout; diatonic nav gestures
(`Nav` diatonic / `Func+Nav` chromatic / move-mode octave); diatonic transpose.

### 10.6 — Per-track scale-quantize
Opt-in, default-off transform at note-emit + live play-in; serializer flag;
audio-path test.

### 10.7 — Melodic generator ✅
Generator Hub cell 3 (MELODY); `src/core/MelodyGen.h` (deterministic/seeded);
density / core-bias / contour / octaves / step-leap / seed; prints mono line.
Metric strength is the spine: onsets land strongest-beat first, strong beats get
strong (low-tier) notes and longer durations, and a short weak note leaves the
rest that bridges into the next stronger onset. Euclid-style stash → live
preview → P prints / Func+P (or escape) reverts. `MetaBand::Melodic`,
`Overlay::Melodic` / `Modal::Melodic`, `CellState::GeneratorMelodic`.
Tests: `tests/MelodyGenTest.cpp` (determinism, in-`pcMask`, core-bias narrowing,
strong-beat→strong-note/longer, rest-bridge) + `MetaBandTest` band round-trip.
**SRC field** (lock-one-dimension transform): *Gen* generates the rhythm too;
*Keep* locks onsets to the track's existing trigs and generates pitch only
(`generateMelody` `fixedOnsets`; onset set read from the pre-entry stash).

### 10.8 — Harmonic voice-mover ✅
Generator Hub cell 4 (**CHORD**) → sticky `Overlay::Harmony`; `src/core/HarmonyGen.h`
(no chord theory — a scale-constrained multi-voice buffer). Operates on a
progression of up to 8 chord slots, each ≤4 voices. Voices are indices into the
diatonic **ladder** (the scale across octaves), so every voice stays in-key. The
8-field MZ band shows the cursor chord's four voices (`V1` bass … `V4` top; off-
detent removes the top voice, the first empty slot adds one) plus structure:
`LEN` (chord count, **growing clones the last chord**), `CUR` (cursor), `MOVE`
(shift the whole chord one scale degree) and `OCT` (octave-shift the chord) —
the last two are relative nudgers that rebuild to neutral. Euclid-style stash →
live preview (chords printed onto evenly-spaced steps, auditioned via transport)
→ **P** prints / **Func+P** (or escape / section press) reverts. `MetaBand::Harmony`,
`Overlay::Harmony` / `Modal::Harmony`, `CellState::GeneratorHarmonic`. Tests:
`tests/HarmonyGenTest.cpp` (ladder in-scale, per-slot print, even placement,
voice-count/removal, clamp-stays-in-scale, dedup, degenerate) + `MetaBandTest`
band round-trip (voice add/remove, LEN-clone, CUR, MOVE).
**Refined in 10.10:** per-voice chromatic nudge (Func+voice) and immediate
re-strike audition shipped; the idle context loop was dropped (the live preview
carries context). See 10.10 for the reel visual + bar placement + lossless grow.

### 10.9 — Phrase transpose ✅
`Phrase + ↑/↓` transposes the focused track's phrase ±octave; `Func+Phrase+↑/↓`
±semitone. `LockstepProcessor::transposeTrack(track, semitones)` shifts the base
note + both trig layers' authored notes, clamped to 0–127, snapshot-undoable.
Octave-default/Func=semitone per user (scale-degree transpose deferred). Test:
`EngineTest::testTransposeTrack`.

### 10.10 — Harmonic voice-mover refinement ✅
A UX pass on 10.8 across four axes:
- **Voice reel + half-knobs.** The `V1`–`V4` cells drop the confusing absolute
  ring for a **note-name reel** (prev/now/next, bright centre) with a half-knob
  alternating top (V1/V3) / bottom (V2/V4) so the chord reads straight across.
  Same gesture — bare turn = diatonic rung step; **`Func`+turn = a chromatic
  borrowed tone** (`HarmonyChord::chroma` per-voice semitone offset; resolved MIDI
  = `ladder[rung] + chroma`; `canonicalizeVoice` snaps an in-scale landing back to
  a rung). New `MetaFieldView`/`MetaRotary::View` reel metadata + a
  `MetaRotaryLookAndFeel` branch (mirrors `densityCell`); `nudgeHarmonyChroma`
  routes Func+voice as an incremental delta.
- **Bar-aligned placement.** `printHarmony(…, stepsPerBar)` prints one chord per
  bar of the in-scope time sig (chord *k* → bar *k*), even-spacing fallback when
  bars < K. `applyHarmonyLive` computes `stepsPerBar = round(barPpq / stepPpq)`.
- **Lossless clone-previous grow.** Progression `length` starts at 1; growing
  clones the **previous** chord into a genuinely-new slot (past `reach`),
  shrinking never overwrites so shrink→grow restores.
- **Immediate re-strike audition.** Any cursor-chord change re-strikes through
  the live-note engine (`auditionHarmonyCursorChord` / `liveNoteOn`/`Off`);
  released at the single exit chokepoint `forgetHarmonyEditorState`.

Tests: `HarmonyGenTest` (bar onsets + even fallback, borrowed-tone
resolve/canonicalize, MOVE-slides-offset), `MetaBandTest` (reel cells + knob
parity + add slot, Func→chromatic reel, Func+voice semitone nudge, bare-write
clears offset, lossless shrink/grow). No serializer bump — `harmonyProg` is
ephemeral and printed steps are already absolute MIDI.

### 10.11 — Harmonic existing-rhythm placement — planned
A placement mode that keeps the track's existing trigs in place and assigns each
the chord of the bar it falls in (harmony follows bars, rhythm preserved) — the
harmonic twin of the melodic SRC "Keep" transform (`fixedOnsets`). Needs a
placement selector (Nav-right sub-page or a repurposed slot — settle at build).

---

## Appendix — Legacy code → new id

For tracing historical commit messages and notes against the renumbered scheme.

| Legacy | New | Legacy | New |
|---|---|---|---|
| M0 | 1.1 | MGX | 2.6 |
| M1 | 1.2 | MHX | 3.1 |
| M2 | 1.3 | MHY | 3.2 |
| M3 | 1.4 | MHZ.1 | 3.3 |
| M4 | 1.4 | MHZ.2 | 3.4 |
| M5 | 1.5 | MHZ.3 | 3.5 |
| MA | 1.6 | MHZ.4 | 3.6 |
| M6 | 1.7 | MHZ.5 | 3.7 |
| M7 | 1.8 | MHZ.6 | 3.8 |
| M8 | 1.8 | MHZ.7 / MHZ.7.x | 3.9 |
| MB | 2.1 | MHZ.9 | 3.10 |
| MC | 2.2 → 7.x | MHZ.8 | 3.11 → 7.5 |
| MD | 2.3 | MH.1 | 4.1 |
| ME | 2.4 | MH.2 | 4.2 |
| MF | 2.5 | MH.3 | 4.3 |
| MH.4 / MK | 4.4 | MH.6 | 4.5 |
| MH.7 | 4.6 | MH.8 | 4.7 |
| MH.9 | 4.8 | ML | 5.1 |
| MI | 5.2 | MJ | 5.3 |
| MN | 5.4 | MO | 5.5 |
| MQ | 5.6 | MG / MM | 5.7 |
| MP | 5.8 | MR | 6.1 |
| MS | 6.2 | MT | 6.3 |
| MU | 6.4 | MV | 6.5 |
| MW | 6.6 | M10 / MH.5 | 6.7 |
| M9 | 6.8 | | |

Dissolved: old MG Keyboard mode → 3.9 (CHROMATIC); old MM 16-levels → 3.9
(LEVELS); their remainders → 5.7. Bank / Pattern / Part / Chain model (2.2 /
MC) → Phase 7 re-architecture (`Set / Song / Scene / Phrase`; shipped as
`Set / Piece / Section / Phrase`, renamed in 7.9).

---

## Play-test notes

**2026-05-08 — 1.2:** 16-step pattern, kick on every step. No clicks, stable
amplitude. Choke micro-fade working. Phase 1 sampler complete.

**1.3 test procedure:** Load one sample. Set track 1 length 16, track 2 length 7
via the step-grid length sliders. Switch tracks and observe the amber playhead
cycling at different rates; phasing is audible when both tracks share pool index 0.
