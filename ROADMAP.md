# Lockstep — Roadmap

The long-running plan. Work is grouped into six **phases**; each milestone has a
stable `phase.item` id (e.g. `3.4`) and a status flag. Sub-tasks are checkboxes
so the state of the project is visible on every return to the repo. Tick items as
they land; flip an item's status when its checklist completes.

For architecture see `DESIGN.md`. For the guiding principles every feature must
satisfy, see `PRINCIPLES.md`. **Before adding a milestone here, confirm it is
expressible within those principles and within the existing scope+verb grammar
(DESIGN §13).**

**Active focus:** `7.8` — Stage H: verification sweep (Stages 0–G shipped);
`7.9` — vocabulary rename (`Set/Song/Scene/Phrase` + Morph): docs landed, code pending.
**Last completed:** `3.10` — Latch (virtual-hold) + Track+Nav mode cycle.
**Next up:** `7.1–7.8` (full musical hierarchy re-architecture, absorbs `3.11`),
then `6.7` — the Machine Module ABI.

Phases 1–3 took Lockstep from an empty plugin to a frozen, playable performance
surface; Phase 4 fills the machine catalogue; Phases 5–6 are the depth and
platform passes. The framing comes from DESIGN §1 and `PRINCIPLES.md`: Lockstep
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
  a master-drive `IEffect`. Anything more specialised is a third-party module.
  → DESIGN §29.
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
- **Morph-aware editing (PolyBrute-style), 1:1 normalised**, coexists with the
  explicit `Morph + ^/v` assignment; no auto-assign at endpoints. → DESIGN §17.6.
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

### 7.5 — Stage E: Surface model + UI  *[shipped (core); pending: nav chrome, tests]*
- [x] `LengthInRun(90)`, `LengthBoundary(91)`, `LengthOutRun(92)`, `SelectorDeviated(95)`
      CellState tokens (add-only); `compatColour()` entries.
- [x] Pattern scope re-skin → 16 phrases per track; deviation badge (`SelectorDeviated`).
- [x] Part scope re-skin → 16 sections; queued-section `SelectorNext` badge.
- [x] Phrase-length re-skin: `Pattern+Func` (purple) / `Scene+Func` (orange) momentary
      branch; `LengthInRun/LengthBoundary/LengthOutRun` per absolute step index.
- [x] `isTrackDeviated(t)` / `deviationPhraseIdxForTrack(t)` public accessors.
- [ ] Double-tap-NavRight scroll-past-end unlock (pending).
- [ ] `SurfaceModelTest.cpp` new length-edit assertions (pending).

### 7.6 — Stage F: Serialization (clean break)  *[shipped]*
- [x] `kCurrentVersion = 5`; `upgrade_v4_to_v5` drops old Project node.
- [x] `writeNewHierarchyNode` / `readNewHierarchyNode`: Piece → Lane(Kit+Phrases),
      Section(phraseIdx, activeMask, coreTime). Legacy node preserved as fallback.
- [x] `writePhraseNode/readPhraseFromNode`, `writeKitNode/readKitFromNode` helpers.

### 7.7 — Stage G: Scene hooks  *[shipped]*
- [x] `Section.sceneA/sceneB` maps serialized under `<SceneA>/<SceneB>` children.
- [x] Runtime crossfader resolver deferred to ROADMAP 5.2.

### 7.8 — Stage H: Verification  *[partial]*
- [x] Build clean under `-Werror`; all pre-existing tests pass.
- [x] Standalone binary built; no crashes / NaNs on cold start.
- [ ] Live play-test: Section launch (quantized), phrase swap (sticky deviation),
      re-sync, two-layer mutes, Piece switch, polymeter.
- [ ] Phrase-length re-skin: momentary `Pattern+Func` / `Scene+Func` re-skin.
- [ ] Double-tap-NavRight scroll-past-end (pending Stage E item).
- [ ] VST3/CLAP save → reload round-trips the new format.
- [ ] Update "Active focus" to next milestone on completion.

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
- [ ] **7.9e** Checkpoint scope-respecting stacks (DESIGN §13.6: default Song;
      Track/Scene/Phrase; floor = saved state; reload-on-release). **Gated on the
      legacy-state consolidation below** — a faithful Song/Track/Phrase snapshot
      needs the live sound-state to be Song/Kit-owned, not legacy `Part`.

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
- [ ] **Wire processor to Arrangement** (stage 2b): hold an `Arrangement`; repoint
      `song()/section()/kit()/activePhrase()/sequence()` + the switch gestures at
      it; drop `activeSongIdx_/activeSceneIdx_/deviated_` members + the inline
      `syncSequenceFromCurrentScene`. Keep legacy `Part` for FLTR/AMP for now.
- [ ] **Serializer onto Arrangement** (stage 2c): persist only the new hierarchy;
      seed the working buffer on load; bump state version `6→7`; round-trip tests.
- [ ] **Go direct** (stage 3): move FLTR/AMP + machineId into `Kit` (retarget the
      machine picker off `setActivePatternPart`); audio reads Kit for sound; drop
      `Bank/Pattern/Part` structs + the legacy serializer nodes + dead nav.
- [ ] Then build **7.9e** (DESIGN §13.6) on the consolidated model.

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

### 4.8 — DrumSynth voice expansion  *[planned]*  *(was MH.9)*
- [ ] Extend the DrumSynth `type` enum: CLAP, CYMBAL/METAL, COWBELL, RIMSHOT.
      Boundary rule: 808/909 analog/FM-metal lives in DrumSynth; modal/waveguide
      struck-metal stays in PercussionMachine. Ship as `type` values.

---

## Phase 5 — Performance Depth  *[planned]*

The depth pass on top of the frozen surface: timing feel, scenes, pattern/part
management, sampling, audition, special trigs, the remaining trig-grid modes, and
the UI-polish/palette pass.

### 5.1 — Microtiming + swing + quantize  *[planned]*  *(was ML)*
Completes the record-time capture story (gate / velocity / microtiming).
- [ ] `Step::microOffset ∈ [-0.5, +0.5]`; resolver shifts trig sample position.
- [ ] Realtime record writes `microOffset` (residual delta to nearest step).
- [ ] Per-track `swing ∈ [0,1]` (default 0.5), delaying odd-indexed steps.
- [ ] `Quantize` verb (`<scope> + No`) zeroing microOffsets in scope.
- [ ] Step-grid nudge-direction tick indicator.

### 5.2 — Morph + crossfader  *[planned]*  *(was MI)*
DESIGN §17. *(Morph A/B snapshot fields are carried on the Scene after Phase 7
Stage G — shipped as `Section.sceneA/B`, renamed `Scene.morphA/B` in 7.9; a
placeholder crossfader slider exists from 3.1. The full crossfader
implementation ships here.)*
- [ ] `Scene::morphA / morphB` sparse `map<(track,slot)->float>`, serialized
      (fields already present from 7.7; this stage wires the runtime resolver).
- [ ] `faderValue` (RAM-only, smoothed).
- [ ] Resolver morph-pair consult (lerp continuous / snap stepped at 0.5).
- [ ] `Morph + ^/v` assignment gesture + `Morph+Stop` removal; MZ A/B indicators.
- [ ] MIDI-out parity (cc lerp, channel/program snap + All-Notes-Off).
- [ ] Hardware fader axis 1:1 + auto CC map; no QWERTY axis.
- [ ] P-Lock dominance over Morph mix.
- [ ] Morph-aware editing (1:1 normalised through the fader position).
- [ ] Fluid mute (`Morph+Mute` captures `Level→silence`).

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

### 5.7 — Alternate trig modes: Retrig/ratchet + Sound Pool  *[planned]*  *(was MG remainder + MM generic-role)*
The trig-grid modal surface beyond CHROMATIC/LEVELS (which shipped in 3.9).
*(Code today: `SoundPool` data model + `SoundBankOverlay` UI and live-retrig
machinery in the processor exist; the `TrigGridMode` enum is present but unwired —
the modal-grid surface and the ratchet redesign are deferred here.)*
- [ ] Retrig / ratchet trig-grid mode (record-arm captures the rate as a P-Lock);
      slice sub-mode for sampler/slicer tracks.
- [ ] Sound Pool mode: grid pages through the pool and live-swaps the focused
      track's sound; record-arm captures the pool index as a `sound_id` P-Lock.
- [ ] Mode-chord UX consistent with the surface model; clean exit on release.
- [ ] Generic role-tagged LEVELS sub-mode (extend 3.9's velocity-first LEVELS to
      a closed eligible role set: cutoff, attack, pan, … — the surviving MM.1).

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

### 6.5 — Insert + master effects (FX system)  *[planned]*  *(was MV)*
DESIGN §32. Depends on 2.4 + the §14 path (independent of 6.1–6.4).
- [ ] `IEffect` interface (reuses `ParamSpec`/`role`/P-Lock; stub fallback) +
      starter catalogue.
- [ ] Per-track 2-insert chain (post-AMP, Part-scope).
- [ ] FX canonical-section rendering + effect-load gesture.
- [ ] Two master FX slots (`Insert | Send`), Master-focus edited, Project-scope.
- [ ] Send routing (per-track Send A/B in the AMP mix).
- [ ] Performance-grammar parity (P-Lock / scene / Control-All / section copy).
- [ ] MIDI-out tracks carry no inserts/sends.

### 6.6 — External controller surfaces  *[in progress]*  *(was MW)*
DESIGN §35. Generic third-party MIDI controllers as augmentation surfaces (worked
example: Behringer X-Touch Mini — see `XTOUCHMINI_MCU.md`). The load-bearing piece
is the surface model (§35.8): one pure `buildSurfaceModel()` both screen and
controllers render from. Contributors add controllers via `IControllerSurface` +
`ControllerRegistry`; the JSON profile is the default data-driven impl.
- [ ] **6.6.1** `IControllerSurface` seam + `ControllerRegistry` + profile loader
      + JSON schema + validation; graceful unknown-device / malformed / unknown-
      token handling.
- [ ] **6.6.2** `ControllerPortManager` (dedicated `MidiInput`/`Output`, disjoint
      from the host bus; hotplug; three-thread marshalling).
- [ ] **6.6.3** `ControllerInputRouter` (encoders→CC, buttons→`ControllerEvent`,
      fader→interim slider).
- [ ] **6.6.4** Ship the X-Touch Mini built-in profile; manual pass.
- [~] **6.6.5** Surface model + feedback. **(a)** Extract pure
      `buildSurfaceModel()` → `SurfaceModel` and re-point the screen at it —
      **done**: slices 0–6 (`SurfaceModel.{h,cpp}`, `CellState`, decoration
      channels, `tests/SurfaceModelTest.cpp`) + the **6.6.5a UX-consistency pass**
      (unified hint-band rule, note-edit → `Func+Src`, CPC under-scope relabel,
      `TrigGridMode` removed, `Func+arrow` rotate/×2/÷2). **(b)** [pending]
      `ControllerFeedbackEmitter` (30 Hz, diff/throttle, dedicated-port output).
- [ ] **6.6.6** Feedback colour / state mirroring (token-aware + dumb-device
      fallback).
- [ ] **6.6.7** Finalise scene-fader binding once 5.2 lands.
- [ ] **6.6.8** Adaptive `layoutMode` (opt-in, deferred-most).

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
