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
**Last completed:** `9.2` standalone quit guard.
**Next up:** `A3` gain-staging audit; `6.7` Machine Module ABI; Phase 9+ backlog.

Phases 1–3 took Lockstep from an empty plugin to a frozen, playable performance
surface; Phase 4 fills the machine catalogue; Phases 5–6 are the depth and
platform passes; Phase 7 built the Set/Song/Scene/Phrase musical hierarchy;
Phase 8 is the hardening/maintainability pass. The framing comes from DESIGN §1 and `PRINCIPLES.md`: Lockstep
is for both bringing existing material on stage **and** improvising new material
from a blank pool. Every milestone targets both workflows and must satisfy the
principles.

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
- **Stock catalogue = the iconic set keyed to the lineage.** Sampler/Slicer (DT),
  FM (DN), VA (A4), DrumSynth (RYTM), Digital (Monomachine), Percussion (modal),
  Static/Thru/Recorder/Looper (OT). Neighbour folds into Thru; Syntakt = voices +
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
- **Recorder buffers = volatile entries in the unified sample pool**, RAM-only,
  `REC`-badged; the §22 naming flow doubles as freeze-to-disk. → DESIGN §28.
- **Overwrite in Recorder, overdub in Looper.** No overdub state on the trig
  path. → DESIGN §29.
- **Three special trig types:** trigless/lock-only, one-shot, recorder trig.
  → DESIGN §30.
- **The Morph morphs parameters only, never trigs;** fader lerps continuous slots /
  snaps stepped slots. "Fluid mute" = morph-assigning AMP `Level`. → DESIGN §17.
- **Modifier-gated sculpting**: hold/latch `Morph` + encoder writes morph overlay at 1:1 normalised fader split; bare encoder writes kit base. `Morph + ^/v` forces pure A/B. → DESIGN §17.3/§17.6.
- **Cue = additive monitor send, never solo.** No cue output = no-op. → DESIGN §31.
- **AMP gate source `{Envelope | Held-open}`** — the basis of continuous Thru and
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
- [x] `IMachine`, `SamplerMachine` stub, `Clock`, `Sequence`, `Track`, `Step`,
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
      re-laid-out. Sampler cleanup (gate now sequencer-scope).
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
- [x] Default split: tracks 1–8 Sampler, 9–16 MIDI-out; old 8-track saves load
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

### 3.6 — Polyphonic step authoring + VA para topology  *[shipped]*  *(was MHZ.4)*
- [x] Realtime chord record (aggregate notes on the same step, cap 4).
- [x] Step-hold snapshot-currently-held capture; multi-step parallel.
- [x] Note-count badge (1–4 ticks) on step cells.
- [x] P-Lock clear mode packed + toggle-until-commit.
- [x] VA paraphonic osc-by-slot routing + shared noise.
- [x] Keyboardless note-edit mode (1-octave chromatic overlay, octave shift).
- [x] NoteSelection (TOP/BOT bias) in the TRIG meta-section.

### 3.7 — Engine hygiene (first-trig, envelopes, RETRIG, skew)  *[shipped]*  *(was MHZ.5)*
- [x] First-trig loudness fix across FM / VA / DrumSynth / Sampler.
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

### 4.2 — VAMachine  *[shipped]*  *(was MH.2)*
- [x] Dual PolyBLEP oscs + sub + noise, SVF (LP4/LP2/HP/BP + drive), filter +
      amp ADSR, LFO (6 shapes, 4 targets), portamento, Mono/Para-4.
- [x] Polyphonic-trig infrastructure (≤4 notes/step, chord capture, gate
      auto-write), backward-compatible serialization.

### 4.3 — DrumSynthMachine  *[shipped]*  *(was MH.3)*
- [x] Rytm-style per-track drum synthesis; `type` stepped slot selects KICK /
      SNARE / HAT / TOM, each with dedicated DSP.

### 4.4 — Sampler depth + SlicerMachine  *[shipped]*  *(was MH.4; absorbs the old MK)*
- [x] Sampler trim window (`samp_start/length`), four loop modes, loop region,
      edit-time zero-crossing snap; shared `SamplePlayingMachineBase`.
- [x] `SlicerMachine` (SLICE / SCRUB dual mode, 16-slice cap, transient
      detection, MONO/POLY, anti-click fade, reverse at rate < 0).

### 4.5 — StaticMachine (disk-stream)  *[planned]*  *(was MH.6)*
- [ ] Disk-streaming sampler for long-form audio (DESIGN §29). Shares Flex's
      slot vocabulary minus RAM-only manipulations; audio never decoded
      wholesale into RAM.

### 4.6 — PercussionMachine (physical model)  *[planned]*  *(was MH.7)*
- [ ] Volca-Drum-style two-layer percussion: excitation osc (+FM/ring + pitch
      env) → waveguide / modal resonator (Tube/String/Membrane/Modal); layer A↔B
      crossfade + bit/SR reduce + drive. Canonical FLTR/AMP downstream. Algorithm
      presets ship as Sound Pool entries, not schema variants.

### 4.7 — DigitalMachine (Monomachine archetype)  *[planned]*  *(was MH.8)*
- [ ] Model-based digital monosynth (`model` stepped slot): SWAVE (supersaw),
      SID (PWM+ring+sync), WAVE (single-cycle wavetable/PWM), VO (formant).
      `V1` + live Mono/Poly; canonical FLTR/AMP (no opt-out). Monomachine
      GND/FM/drum engines subsumed (Thru / FMMachine / DrumSynth). Authored
      against the 6.7 SDK + post-3.11 contract.

### 4.8 — DrumSynth voice expansion  *[shipped]*  *(was MH.9)*
- [x] Extended the DrumSynth `type` enum to eight voices: KICK, SNARE, HAT, TOM,
      CLAP, COWBELL, CYMBAL, RIMSHOT — each with dedicated DSP.
      Boundary rule: 808/909 analog/FM-metal lives in DrumSynth; modal/waveguide
      struck-metal stays in PercussionMachine. Shipped as `type` values.

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

### 5.5 — Audition + cross-track record  *[planned]*  *(was MO)*
DESIGN §21.
- [ ] Preview gestures (`Trig+Yes` fires a step once; `Track+Yes` fires the
      track's base trig once), bypassing the event stream.
- [ ] Per-track record arms in Per-Track-MIDI mode; arm-all (`Func+RecordArm`).
- [ ] Omni-mode arming behaviour documented.
- [ ] Step-as-keyboard live record composing with the trig-grid modes.

### 5.6 — Special trig types  *[planned]*  *(was MQ)*
DESIGN §30. (Trigless could pull earlier — no machine dependency.)
- [ ] Trigless / lock-only trig (`off → note → lock-only` via `Func+step`).
- [ ] One-shot trig (RAM armed/spent, auto-rearm); arm-all / disarm-all per track.
- [ ] Step-state preview integration for lock-only / spent / armed states.

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
- [x] **Accent velocity generator** (`Func+Fill` chord, §39.10): bakes
      Lerdahl–Jackendoff metric-weighted velocities into the focused track's trig
      steps. MetaBand::Accent exposes DEPTH (0–100 %) and CENTR (1–127). Live
      preview; checkpoint+commit or restore-stash cancel. Works in any time
      signature. See DESIGN §39.10. *(7fb3f01)*
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

## Phase 6 — Routing, FX & Platform  *[planned; 6.6 in progress]*

The audio-input boundary and the machines it unlocks, the effects system, the cue
bus, external controller surfaces, the machine-module ABI, and the beta polish.

### 6.1 — Audio-input boundary + routing + Thru machine  *[planned]*  *(was MR)*
DESIGN §27, §29. Gates 6.2 / 6.3.
- [ ] Optional audio-input path at the machine boundary (sequencer fills `buffer`
      from `input_source`).
- [ ] `input_source` slot (`None | External | Track N | Master`).
- [ ] Per-block topological sort; cyclic routing refused at assignment.
- [ ] Master prior-block tap (`input_source = Master`).
- [ ] ThruMachine (unity pass-through; canonical FLTR/AMP/FX process it).
- [ ] MIDI-out parity (no input source; excluded from the graph).

### 6.2 — Recorder buffers + recorder trigs  *[planned]*  *(was MS)*
DESIGN §28, §29, §30. Depends on 6.1.
- [ ] Volatile pool entries (RAM-only, `REC`-badged, unified address space).
- [ ] Fixed set of volatile buffer slots (~8, TBD).
- [ ] RecorderMachine (`input_source`, `target_buffer`, `rec_length`,
      overwrite-only).
- [ ] Recorder trig variant; freeze-to-disk via the §22 naming flow; round-trip
      test.

### 6.3 — Looper machine (overdub)  *[planned]*  *(was MT)*
DESIGN §29. Depends on 6.2.
- [ ] LooperMachine state machine (empty→record→play→overdub→stop→clear).
- [ ] Verb-driven control while focused; click-free overdub seams; transport-
      synced loop-length option.

### 6.4 — Cue bus + monitoring  *[planned]*  *(was MU)*
DESIGN §31. Adds the monitor bus + the `Cue` scope (finally bound to a key).
- [ ] Cue/monitor output bus (standalone ch 3–4 / plugin second bus).
- [ ] `Cue + track` additive send (post-FLTR/AMP/Level); `Cue + Scene` preview;
      `Cue + MIDI-out track` event copy. No cue output = no-op.

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
      rescan, MIDI-out emit-only validation, port SamplerMachine to host-services
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
- [x] **8.8c** Convert `VAMachine`.
- [x] **8.8d** Convert `DrumSynthMachine` + `SamplerMachine`.
- [x] **8.8e** Convert `SlicerMachine` + `MidiOutMachine`.
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
IEffect catalogue smoke. Envelope goldens for VA and FM with explicit ADSR values.
- [x] **8.13** `MachineDspTest.cpp`: smoke + envelope goldens for VA/FM/DrumSynth/
      Sampler/Slicer + 4 IEffects; ASan/UBSan clean.

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
      VA/FM envelope `[SUSPECTED-BUGGY]` markers removed (goldens pass); per-sample
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
- [x] **A3/gap closure** (post-audit): A0 regression test; legato goldens for VA/FM;
      retrig click-metric tests; effect smoke + specific assertions for all 13 effects;
      serializer v17 round-trip + v16 upgrade tests; Animate for the 4 master units
      (DESIGN §32.5 + Song+FX quadrant mapping; test pins `setMasterSendBypass`);
      bugfix: internal-amp machines (DrumSynth/VA/FM/Sampler) never routed sendA/sendB
      to master send buses (both transport paths fixed).

### 8.27 — Smoothing policy  *[shipped as part of 8.26-A]*
Per-sample one-pole smoothing (~5 ms) on all gain-path effect params.
See DESIGN §32.3 "Smoothing policy" addendum.

### 8.28 — Track channel/envelope split + universal filter  *[shipped]*
DESIGN §14 rewritten. Separates the conflated AMP block into a **CHANNEL** block
(always-on: level/pan/sendA/sendB) and an optional **ENVELOPE** block (AHDSR +
gate source; only for machines without `hasInternalAmp()`). Adds **OFF mode** to
the track filter (always-present, defaults to OFF on new tracks). Deletes
`hasInternalFilter()`; VA and other machines with internal filters get the track
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
- [x] **B6** Behavioural tests: channel P-Lock on VA, filter-OFF passthrough,
      filter-on-VA attenuates.
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
