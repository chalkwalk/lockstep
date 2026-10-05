# Lockstep — Roadmap

The long-running plan. Work is grouped into eight **phases**; each milestone has a
stable `phase.item` id (e.g. `3.4`) and a status flag. Sub-tasks are checkboxes
so the state of the project is visible on every return to the repo. Tick items as
they land; flip an item's status when its checklist completes.

For architecture see `DESIGN.md`. For the guiding principles every feature must
satisfy, see `PRINCIPLES.md`. **Before adding a milestone here, confirm it is
expressible within those principles and within the existing scope+verb grammar
(DESIGN §13).**

**Active focus (refreshed 2026-10-04):**

*Lockstep is public* (`6.8`, 2026-10-04): `github.com/chalkwalk/lockstep`, CI
green on Linux, macOS and Windows in one run, docs at
`lockstep.chalkwalkmusic.com` mirrored to the wiki. Published the way
antiphon, arps-euclidya and star-canopy are — no tags or GitHub releases; the
builds are CI's artefacts. *The modality arc (`6.9`) is also done*: one stuck
mode found and fixed (Melodic could not be escaped), and a standing 150-case
sweep. What remains is product work and a short list of debts, both below.

1. **`5.3` — Song/Scene management UI.** Names, colours, browser and phrase
   copy/fork all ship (two play-test rounds landed 2026-07-16/17). What remains is
   the **recall unit**, which needs its own brainstorm/design session first —
   "Kit" is retired as a term (`9.29`), so the story is re-derived, likely atop
   `SoundPool`. Must fit the picker paradigm (step grid is the selection surface;
   no popups) and the scope+verb grammar. Smaller open holes are itemised in the
   milestone.

   *(`9.36` and `9.38` are both closed — **every** catalogue row is implemented (the
   last, standalone wind/scrub, landed 2026-07-25) and every defect the suite found is
   fixed. It is now a standing net rather than an arc: run it, and add a journey when a
   new flow ships.)*

**Shipped since the 2026-07-14 review** (all cross-referenced against the code):
the housekeeping sweep (`9.14` stage-6 audit; pool content-hash comments), the
`9.23` Stream→pool wiring gap (`9.23` S9), **`9.4` snapshot/undo in full** (A–H:
phantom cluster fixed, simple per-scope model, undo on `Func+O`; DESIGN §13.6,
elaborate model rejected-but-preserved), **`6.4` cue balance + the access pass +
`6.4a` overlay tiers** (2026-07-15 — per-track cue *balance* crossfade rather than
an additive send, DESIGN §31/§31.3/§31.5; `Cue + Scene` and `Cue + MIDI-out` remain
deferred), the **`5.3` play-test follow-up and round 2** (2026-07-16/17 — grid-mode
default, name visibility, create-on-select Songs/Phrases, the uniform-scaled
resizable window, and the VU-freeze fix that gave `repaintLogical()` a single
owner), and the **CUJ suite's Phase-0 harness + catalogue + seed trio** (2026-07-24,
`9.36`).

**Earlier:** the capture arc closed 2026-07-12 (`11.10`–`11.12` — see their
compressed entries below the seam) and Phase 11 (the deck engine) is complete,
core, tail and all play-test rounds. Phase 10 is shipped through 10.10 except
`10.5` (partial), `10.6` and `10.11`. The 2026-07-14 review also flipped five
milestones whose `[active]` flag had gone stale (`9.5`, `9.6`, `9.8a`, `9.13`,
`9.27` — all complete in code).

**Gated on the user (schedule a dogfooding session or mark waived):** ear
tests (`9.23` S1 Bungee, `9.24` A/B matrix, `9.25` R4 varispeed texture);
manual verification sweeps (`3.10`, `7.8`, `7.9e`, `8.24`, `9.9`); Push 1
on-hardware palette check.

**Revised decision (2026-07-12):** the "catalogue waits on the ABI" gate is
lifted — see the locked-decisions list. `4.6`/`4.7` are unblocked and ship
first-party; `6.7` waits for a concrete second consumer.

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

## Maintenance — how shipped history is compressed

This file is forward-looking; shipped work is **compressed in place** so the
live plan stays small. Git history is the archive — `git log -p ROADMAP.md`
and the blame view recover any compressed detail. The rules:

- **The seam.** One marker line containing the token `SHIPPED-HISTORY-SEAM`
  divides the file: everything above is the live plan (phases with open
  work, active first), everything below is shipped history. It is unique and
  greppable — `sed -n '/SHIPPED-HISTORY-SEAM/,$p' ROADMAP.md` tails the
  history, `sed '/SHIPPED-HISTORY-SEAM/,$d' ROADMAP.md` heads the live plan.
- **Compression rule.** A milestone is compressed only when *every* checkbox
  in it is checked. Keep the `### id — title *[shipped]* *(was …)*` header
  exactly (stable ids keep cross-references and git archaeology working);
  replace the body with 1–3 sentences: the intent, what actually shipped, and
  any load-bearing decision made along the way. Unchecked and user-gated
  items survive **verbatim** — compression never deletes an open box.
- **Phase moves.** When a phase's last open item closes, compress the
  stragglers and move the whole phase below the seam (typically during an
  alignment review — see `/align-roadmap`). A phase keeps its intro
  paragraph; it already carries the intent. Exception: user-gated
  verification items and parked *(Later)* items do **not** hold an
  otherwise-shipped phase above the seam — the phase moves, the milestone
  carrying them stays verbatim, and the preamble's gated-on-the-user list
  is the live index into them.
- **External plan docs.** While a milestone is pending, a plan too detailed
  for this file lives as its own doc in `docs/`, referenced from one line in
  the milestone. When the milestone completes, fold a one-paragraph summary
  into the compressed milestone and **delete the doc** — the commit that
  deletes it is the archive pointer.
- **Invariants to verify before committing a compression pass:** the set of
  unchecked `- [ ]` lines is unchanged; no `## Phase` or `### <id>` header is
  lost (adding headers is fine; status flags may be corrected); the seam
  heading (`^## SHIPPED-HISTORY-SEAM`) appears exactly once; the Legacy-code
  appendix is untouched.

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
  Morph/Song | Mute/Fill` + held-step + section keys; verbs `Snapshot/Record/
  Play/Clear/Confirm` (each with a `Func`-layer secondary `Restore/Copy/Paste/
  Delete/Cancel`; `Stop` retired — transport stop is `Play` double-press).
  Cross-column compounds only; `Func` is the universal qualifier; `Cue`
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
- *(roadmap)* **First-party machines ship statically; the ABI waits for a real
  second consumer.** *(Revised 2026-07-12; previously "the catalogue waits on
  the SDK".)* The original gate — author 4.5+ against the Machine Module ABI so
  they ship as modules from day one — was being routed around in practice (4.5
  Stream shipped statically) while blocking 4.6/4.7. Since PRINCIPLES §9 already
  keeps capture machines first-party, and an add-only ABI frozen with no
  third-party consumer is a forever-cost with no payer, the gate is lifted:
  catalogue machines author against the frozen surface + `sdk::MachineBase`
  and link statically; 6.7 lands when a concrete second consumer exists.
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
- **"Lockstep" is the product name** *(roadmap, 2026-10-03)*. Decided, not
  deferred: the 6.8 rename item is closed, the docs domain is
  `lockstep.chalkwalkmusic.com`, the repo is `chalkwalk/lockstep`. README's
  "working codename … final name not yet chosen" caveat goes with it.
- **The first public release is a beta with its gaps named** *(roadmap,
  2026-10-03)*. `5.3`'s recall unit, `9.23`, `10.6`, `10.11`, the deferred
  `Cue + Scene` / `Cue + MIDI-out`, freeze-to-disk, and every user-gated ear
  test do **not** block publication; they are listed openly instead. The
  register is antiphon's: "builds and tests clean" is a real result and is not
  the same as "supported". → `6.8`.
- **Publication precedes the modality arc** *(roadmap, 2026-10-03)*. Not because
  it matters more, but because nothing in this codebase has ever been compiled
  on macOS or Windows, and that is the only unbounded risk on the path to a
  beta. `6.9`'s cost is bounded; `6.8`'s is not, until CI runs once. → `6.8`.

---

## Phase 9 — Standalone & Files  *[active]*

### 9.1 — Project file flow  *[shipped]*
`.lockstep` plain-XML project files over the shared serializer + upgrade
chain: `writeToFile`/`readFromFile` (fail-safe on parse errors),
new/save/load + state hash dirty tracking on the processor, the
`StandaloneFileBar` with the three-way dirty guard and last-project
auto-open, and `Project::soundPool` serialization (v16).

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

### 9.4 — Snapshot / undo model  *[shipped 2026-07-14]*
The 2026-07-14 design session (spec: **DESIGN §13.6**, rewritten) both fixed a
phantom cluster and settled the model. **Phantoms fixed (A–C):** `Track+Y` /
`Phrase+Y` snapshot had never been wired (no scoped row, so the bare "SNAP" label
dispatched to nothing), `Scene+Y`→`Scene+O` rehomed SYNC, and an editor
cancel-a-queued-launch intercept had shadowed both `Scene+O` SYNC and `Phrase+O`
CLEAR PHRASE into dead code — cancel moved to `Func+P` (the pending-action verb).
The empty-stack restore that silently reverted a track to the project baseline is
now a no-op saying `NOTHING TO RESTORE`. **Load-bearing lesson:** the binding table
is the dispatch golden's enumeration domain (one scenario per *row*), so a verb that
leans on a barer row's label is never tested — every scoped verb now gets its own
row.

**Model settled (D–H):** the first pass specced a globally-consistent model (global
epoch, ancestor-overlap invalidation, vim-style undo tree) and it was **rejected** —
every checkpoint payload is a complete valid overlay for its scope, so a mixed
restore is a real playable state, not corrupt; the overlap machinery solved a
consistency problem a live performer never has. The rejected design and its
revival-trigger CUJs are preserved in `docs/snapshot-undo-rejected-elaborate-model.md`.
What shipped: **independent per-scope mark stacks** (no epoch, no cross-scope rule),
bounded by a **64 MB/scope memory budget** not a flat count (payloads span Scene
192 B → Song 2.77 MB, so a count is the wrong shape — Song self-limits ~20, cheap
scopes effectively unbounded); a **separate shallow undo stack on `Func+O`** (the
seat 9.29 vacated) armed by the ~24 destructive-op auto-captures, kept apart from
marks so a flurry of `Y`s never buries the pre-mistake point; **a restore arms undo**
(it overwrites live state), so `Func+O` takes a mis-fired restore back — no separate
unrestore gesture. REDO is designed but deferred (no grammar seat yet). Surface: a
mark-depth pip + count on each scope key, `CK:N` kept as the held-scope summary with
a `UNDO:n` note. REDO and the elaborate model can be revived from the preserved
rejected-design doc if a CUJ ever demands them.

### 9.5 — Velocity overlay polish  *[shipped]*
Mix-blend baseline fix (swings around velCenter on unauthored steps), the
Phrase velocity mode (accents anchored to the phrase start so they don't
drift against bar-co-prime lengths), skip-disabled sub-pages as a general
modal-band rule, and the `ModalEntryInert` dim-amber affordance for
available-but-inert entries. Serializer v21.

### 9.6 — Contextual parameter-name aliasing  *[shipped]*
`ParamSpec::contextLabel` (mode-dependent labels), applied to DrumSynth's
type-dependent slots and Sample/Slice loop annotations; also fixed the
latent `return "—"` const-char assert.

### 9.7 — Hierarchical time signature  *[shipped]*
Time signature as a first-class hierarchical value (Set default +
Song/Scene overrides, `effectiveTimeSig()` cascade consumed by
launch-quantize/metronome/velocity/density/phrase seeding), grammar-edited
via the TIME band. Serializer v21.

### 9.8 — Hierarchical tempo + top-display rework  *[shipped]*
Tempo as a hierarchical peer of time signature (Song x Scene ratios over
the host/root BPM via `effectiveTempoRatio()`), edited on the unified TIME
page; the mouse-driven standalone tempo bar deleted for a scope-coloured
readout. Serializer v21.

### 9.8a — TIME page cleanup pass  *[shipped]*
Structural cleanup of 9.7/9.8: one TIME page (Tempo + Sig), one
`timeStickyMode` + entry scope, a pure transition layer
(`applyTimeEntry`/`escapeTimeSticky`), per-control INHERIT floor,
bar-length-ordered time-sigs, CUJ tests.

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
The affordance data model (`tapLabel`/`holdLabel`/`doubleTapLabel` on
`SurfaceCell`), 4-slot KeyButton affordance rendering, the full 22-entry
affordance table, top-chrome consolidation, and the **context inspector**
(`InspectorModel` pure builder + `InspectorBar`, KEY/HELD/OVERLAY/EDIT).
Fixed the stale `Func+3` MET binding and the unpainted generator-hub cells.
(The seed table this built, `KeyAffordances`, was deleted by 9.12 when
display started deriving from the grammar.) PRINCIPLES §19, DESIGN §6.11.

### 9.12 — Unified Gesture Grammar (table-driven dispatch + derived affordance display)  *[SHIPPED 2026-07-13]*
Display and dispatch now read the **same** binding table: every key frame is
derived from `resolveBinding(..., Gesture)` and `handleAction` is exhaustive
over `ActionId` with no `default:` (a new action is a build failure, not a
dead key). The migration ran family-by-family behind a widened behavioural
golden net (`lockstep_dispatch_tests` driving a real headless editor — the
"headless is unviable" belief was a misdiagnosis of a stack-local ~47 MB
`Arrangement`), and found **five live display/dispatch divergences** plus one
undisplayable gesture — none findable by reading either side alone. Two
routing rules coexist by design: a modifier resolves on its **bare** row (a
press means "enter this scope" whatever else is held) while verbs resolve
most-specific-wins on the full held set. `KeyBinding::hint` was deleted
(derived via `hintFor()` from the Func row), and the discipline that made it
safe is recorded: widen the net before moving code, read every golden diff,
move bodies rather than rewrite them. (The execution plan lived at
`docs/dispatch-migration-plan.md`; folded here and deleted per the
Maintenance convention — git blame this line for the full text.)

### 9.13 — Redundancy / SSOT consolidation + switch hygiene  *[shipped]*
The two-things-in-sync defect class attacked structurally:
`-Wimplicit-fallthrough` everywhere (PRINCIPLES §20), `refreshSurface()`
replacing 20 hand-paired repaints, track-length writes single-routed through
`setTrackLength`, Euclid state moving as a unit, dead mirrors removed, state
ownership documented (DESIGN §4.7a), the three sticky-mode booleans collapsed
into the single `UiState::overlay` field (illegal co-existence
unrepresentable), and `activeModal(ui)` as the one modal-priority read
(storage stays multi-field; held chords coexist with entered modes). Stage
7/8's "headless dispatch unviable" diagnosis was later corrected by 9.12.

### 9.14 — Grammar-consistency pass: Clear/Delete, FX picker, per-step inspector, move-step  *[shipped]*
The orchestra paradigm (PRINCIPLES §21): Clear blanks / Delete removes,
confirmation scales by blast radius, **hold = reveal & edit, tap =
navigate/toggle**. Shipped: confirm tiers + armed-preview,
`Track+Song+Clear` all-phrases, hold-FX = picker (tap = params; `Func+FX`
retired), the held-step inspector (absorbing `Func+step` P-Lock clear and
`Func+Src+step` note edit), move-step + the Step-Position panel
(`swapSteps`, micro-offset, QUANT), and copy/paste discoverability on the
status lane. Load-bearing lesson (st.5): for the verb family the key table
carries only labels — the **scope x verb matrix** (`verbs::clipAffordance`)
is the authority, and it exposed Song COPY/PASTE rows as silent no-ops
wearing labels; paste guards, key glow and the lane all read the one matrix
now. St.6 closed with an exhaustive ConfirmKind audit (every kind must arm
the pop-over, name itself, and be unable to fade).

### 9.15 — Unified surface invalidation (events redraw, the clock only animates)  *[shipped]*
One coalescing invalidation channel for discrete events
(`SurfaceDispatcher`; `refreshSurface()` = invalidate; one build feeds
screen + all controllers) plus two clocks by necessity (the always-on ~30 Hz
editor timer and the display vblank playhead); the audio->UI bridge is a
dirty flag set when the engine FIFO applies a change; every component
timer was removed or self-suspends. Stage 5 made the rule **structural**:
`tests/SurfaceInvalidationGuardTest.cpp` fails the build on any `repaint()`
outside a frame producer not marked `// chrome only: <reason>` — the audit
that motivated it found ~45 convention violations leaving controller LEDs
stale. PRINCIPLES §22 is descriptive; DESIGN §35.9.

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
One quantize authority (`LaunchQuant {Instant .. Bars8, PhraseEnd}`) for
every deferrable action — scene/song launch, phrase deviation, quantized
mute/unmute with the phase-reset primitive (`Mute+Play+step` relaunch),
looper edge-arming (loop_sync becomes length-only) — with per-track
override + Set grid on `Func+7`, double-tap = instant. PhraseEnd is
per-track-only so whole-band launches stay atomic. Serializer v25.
PRINCIPLES §25, DESIGN §13.4/§16.1.

### 9.18 — Sample-pool identity re-architecture (stable ids + typed pickers)  *[shipped]*
Fixed flat-pool-index rot: `SampleId {domain, key}` — content hash for
persistent entries (stable across reorder and file moves; reload
auto-relinks), session-local monotonic ids for volatile captures. Serializer
v29 routes refs by hash (`normalizeSampleRefs` on load; v28 bridge for
legacy indices). Capability-filtered pickers (`IMachine::sampleClass()` —
Stream stays out of PCM players), pool overlay grouped by origin,
**save-and-promote** (volatile -> WAV -> durable File entry, references
repointed), missing-sample surfacing.

### 9.19 — Loop-seam / runtime-sample / pool polish  *[shipped]*
Loop-seam crossfade (borrows real tail material past loopEnd when present,
else eats into the loop; `samp_loop_xfade`, default 8 ms), Stream anti-click
gate, trigless auto-fit on sample assign (sizes track length + seeds one
trig; sequenced tracks untouched), runtime missing-sample rescan + banner
(decoded PCM retained — no mid-set dropout), per-origin-group stable pool
numbering (`groupOrdinal`), looper default -> Sync, picker first-paint fix.

### 9.20 — Section-stack unification (one resolver, four consumers)  *[shipped]*
One `SectionResolve` resolver (`resolveSectionKey()`: schema param
candidates unioned with the static `SectionStackTable` of meta/sticky rows)
now feeds editor dispatch, the section-bar painter, and the MZ banner —
section tint, header colour, and dispatch can no longer disagree.
Winner-origin colour painting, the MZ `pageOrigin_` banner, and the
never-latching Func outline shipped with it. (The underlay direction was
refined by 9.21.)

### 9.21 — Section-stack underlay: final model (nearest, colour-by-winner)  *[shipped]*
The taught contract: hold-scope + section = that scope's content;
fall-through is a convenience that falls to the **nearest** real layer in
either direction (ties toward deeper), and every key is coloured by the
scope its content truly comes from. Killed the per-scope param-layer
fiction (params are OEB only; holding a scope never changes the write
target). Deferred: the section->scope association review and true per-scope
sections.

### 9.22 — Func colour model + `Func+Song` = Global scope  *[shipped]*
Global became a real scope with its own azure hue (`kScopeGlobal`):
`sectionResolveMode(ui)` context-splits Func (`Func+Song` -> Global primary
layer carrying TRSP floor-only; bare Func -> the meta hierarchy), the
painter/dispatch/MZ all read that one function, and the func-colour border
is the modifier signal (never latched). Deferred: the promotion ladder and
more Global content.

### 9.23 — Sample-playback coherence: Bungee engine + player family + pool metadata  *[in progress]*
Unify the sample-player param family, adopt a third-party stretch engine, make
loops seamless, and make detected sample metadata user-editable. Plan:
`~/.claude/plans/i-was-working-on-jiggly-planet.md`. **Serializer v29 → v30.**
Engine strategy (locked with user): **Bungee** (github.com/bungee-audio-stretch,
MPL-2.0; vendors Eigen + PFFFT) is the single real-time stretch engine — its
pull-based grain API gives free reverse/scrub/zero-speed, seamless looping by
source-position wrapping, and native input↔output rate conversion. A future
max-quality offline render-to-pool engine can sit behind the same seam (engine TBD
— chosen on measured quality when built, not assumed); signalsmith-stretch (MIT)
is the named real-time fallback if Bungee fails the ear test. WSOLA
`dsp/TimeStretch` was deleted.
- [x] **S1 — Bungee + pull-model `IStretchEngine` seam** (`src/dsp/IStretchEngine.h`,
      `BungeeStretchEngine`, `PcmStretchSource`, `tools/stretch_audition`). Granular
      pull loop → planar FIFO; proportional run-in discard aligns onset to startPos;
      loop = monotonic position + modulo fetch (folding the position would glitch the
      seam). ⚠ **EAR-TEST GATE pending user listening** on the audition renders.
- [x] **S2 — Stretch param family + WSOLA deletion.** Appended player_tune (±50 c),
      player_loop (Off), player_reverse (Fwd); StretchMachine drives Bungee in fold
      mode. Deleted `dsp/TimeStretch.{h,cpp}` + its test.
- [x] **S3 — Stream native-rate fix + stretch pipeline.** ITempoAware + player_pitch/
      tune/timestretch/loop; `ReaderStretchSource` over BufferingAudioReader; engine
      rebuilt at the file rate in `withQuiescedEngine` (the missing-resample fix).
      Shared `StretchMath::stretchTimeRatio` drives both players.
- [x] **S4 — Loop=On via position-wrap + phase anchor.** Tempo loops the full musical
      length (phase-locked by exact bars×samplesPerBar period, kept matched by
      per-block setRatios); autoFit seeds a one-shot trig + player_loop=On.
- [x] **S5 — Pool metadata user overrides + effective values + v30.** Detected
      one-shot (was lost) + userBpm/Key/Tuning/OneShot overrides; effective =
      override-else-detected consumed everywhere (stretch ratio, autoFit, sync-slice,
      displayHint with `*`). Serialised (osh/ubpm/ukeyR/ukeyB/utune/uosh), incl. on
      Stream entries; upgrade_v29_to_v30 stamp.
- [x] **S6 — Pool "sample properties" MZ editor.** Sticky `Overlay::SampleProps`
      + `MetaBand::SampleProps`; `Props…` button on the pool row enters it with
      `samplePropsPoolIndex`. 8-field band (BPM / BPMx spring / Root / Mode / Tune /
      1Shot / Revert spring / read-only Name) routes each slot to the single-owner
      `SamplePool` setters; effective values + `hasOverride` drive the readout. Exit
      on Func double-tap or any foreign scope/section. Mandated modality triple
      shipped (ModeReducer / ModalState / MetaBand — resolution, scope routing, and a
      write→serialize→reload round-trip). Milestone code-complete pending the S1
      ear-test gate (the Stream→pool wiring follow-up shipped 2026-07-14 — see S9).
- [x] **S7 — A440 Auto/Raw + fine-tune on Sample/Slicer** (+ Stretch tune mode).
      Auto cancels the detected deviation; Raw plays as recorded.
- [~] **S8 — Docs** (this block; DESIGN/README/THIRDPARTY).
- [x] **S9 — Stream→pool wiring.** *(Shipped 2026-07-14.)* The gap the 2026-07-14
      review found hiding behind shipped prose: S5 gave the pool effective (override-
      else-detected) tempo/tuning and Stretch read them, but **Stream hardcoded
      `effBpm = 0.0`** and so ignored the pool completely — a tempo-stamped song
      streamed at its native rate no matter what the project was doing, which is
      precisely the machine that hosts tempo-stamped long-form material. Stream now
      holds the pool, the pool entry travels *with* the path into `setFilePath` (one
      call, so an open reader cannot read its tempo off a different entry), and the
      A440 Auto/Raw slot from S7 is appended for parity with Stretch.
      Second half of the fix: a Stream entry never decodes, so it had no way to *learn*
      a tempo. `addStreamRef` now reads metadata (ACID/BWF) and filename hints — which
      cost no PCM — so a tagged file works without the user typing anything. Detection
      proper stays off; PCM is the one thing a stream reference exists to avoid.
      Tested on the audio path by duration (half the tempo ⇒ ~2× the time, same pitch),
      with a control proving an *untagged* stream is still played native.
- Follow-ups: player unification part 2 (Looper→Bungee tape/scrub/glide, Stream
      reverse, Sample/Slicer engine-optional + Hermite interp), a max-quality offline
      render-to-pool engine behind the seam (engine TBD, benchmark before adopting),
      FX third-party swap (juce::dsp DelayLine/Oversampling; Signalsmith basics) —
      now scheduled as **9.24**.

### 9.24 — FOSS DSP overhaul: FX-catalogue quality + machine-DSP + new effects  *[code complete — pending ear-test A/B]*
Quality-fix the weak bespoke effects and grow the catalogue using the FOSS DSP
already available: `juce::dsp` (Oversampling/LadderFilter/Limiter/Convolution —
linked but until now unused) + vendored Signalsmith DSP primitives (9.24 S1). Plan:
`~/.claude/plans/we-discussed-the-possibility-silly-panda.md`. **Serializer v30 → v31.**
Discipline: `tools/fx_audition` before/after render matrix is the ear gate (baseline
stashed before any DSP change); measured alias/purity/click assertions in
`tests/MachineDspTest.cpp`. Param ids are save-format — swapped internals keep ids +
ranges; new params appended only. All new DSP is zero-latency-configured (no PDC
exists and this milestone does not add one).
- [x] **S1 — Vendor Signalsmith DSP.** Submodule + SYSTEM includes + THIRDPARTY row +
      cubic fractional-delay compat proof. signalsmith-basics dropped (dsp primitives
      + juce::dsp suffice).
- [x] **S2 — `tools/fx_audition` ear gate + stashed baseline** (impulse / dual-sine /
      sweep / drum-burst × catalogue × tier × preset). 120-WAV baseline in gitignored
      `fx_baseline/`.
- [x] **S3 — Measurement helpers** (spectrumOf / aliasRatioDb / maxSampleStep /
      sinePurityDb) in shared `tests/SpectralMeasure.h`.
- [x] **S4 — SamplePlayer 2-point linear → 4-point Hermite** (highest audible win) via
      shared `src/dsp/Interpolation.h`. **LoopMachine read folded in** (it was on plain
      2-point linear too — circular-wrapped Hermite now; overdub writes stay
      nearest-integer, unchanged).
- [x] **S5 — Saturation oversampling** (both faces via `juce::dsp::Oversampling`,
      min-phase IIR, one mono instance per channel; block up/tanh/down restructure).
      **4x not 2x** — a hot HF tanh is near-square; 2x still folds the 5th harmonic.
      Alias drops ~26 dB vs no-OS (LQ now oversampled too; HQ moved off the homegrown
      Oversampler2x). Absolute floor ~-34 dB at the 10 kHz/full-drive worst case.
- [x] **S6 — FMMachine oversampling: assessed, `Oversampler2x` KEPT** (plan said
      delete). FM is a *generator* (synthesises two sub-samples then `decimate`s);
      `juce::dsp::Oversampling` is block up→process→down with no standalone decimate
      entry, so it can't serve a generator without a hacky zero-upsample or a risky
      rewrite of the working 16-voice synth. The homegrown FIR halfband decimator is
      the right tool and stays; FM keeps 2x (poly-CPU tradeoff, no aliasing complaint).
- [x] **S7 — Distortion oversampling** (`juce::dsp::Oversampling`, block up/tanh/
      down like S5). **8x** — drive reaches 20x (near-square); 4x left ~-24 dB
      alias. Per-insert (not per-voice) so the stages are affordable. Beats a no-OS
      20x-tanh reference by >20 dB, clears -30 dB absolute.
      **Ear-test bugfix (post-S18):** Distortion's tone LP used the raw `tone` param
      as its coefficient, so `tone=0` froze the filter at a dead DC block — with
      `mix=1` (the fx_audition "extreme" preset) the wet path went silent and only
      the mix-smoothing ramp leaked the input transient (the "distortion is just a
      click" report). This was pre-existing, not caused by the oversampling. Fixed by
      flooring the coefficient (`0.02 + tone²·0.98`, mirroring Saturation) so `tone=0`
      is dark, not dead. Regression test: extreme preset now sustains (tail RMS
      audible, not a transient). Also added `fx_audition --ab` (dry→gap→wet in one
      file) for quick effect-vs-bypass A/B.
- [x] **S8 — Delay fractional taps + tape-style retune slew.** LQ `DelayEffect`
      read tap is now 4-point Hermite (fractional delay); HQ `HQDelayEffect` reads
      a fractional length with a one-pole tape-bend slew (~50 ms) toward the target
      and a short (~20 ms) crossfade for jumps >50 ms (division switches) instead
      of sweeping the whole distance. Test: 440 Hz sine while the tempo ramps
      120→121 over 64 blocks keeps `maxSampleStep < 0.1` (whole-sample retune
      would spike far higher). `--bpm-ramp` audition available.
- [x] **S9 — Chorus rebuild** (hand-rolled multi-voice; `juce::dsp::Chorus` is
      single-voice, not an upgrade). Three 4-point-Hermite voices per channel on
      phase-offset LFOs (0/120/240 deg), right channel rotated a quarter cycle for
      a wide stereo image. Appends `chorus_fb` (default 0 = legacy no-feedback
      sound on old projects; write is exactly the dry input at fb=0). Test: smooth
      (`maxSampleStep < 0.1`) at max depth+rate, and L/R correlation < 0.98.
- [x] **S10 — HQ FDN reverb: Hermite on modulated reads only** (topology kept).
      The per-line modulated read is now a 4-point Hermite fractional tap; the old
      integer read quantised the LFO sweep to whole samples (stepped shimmer at
      high mod depth). Feedback matrix, damping, decay, allpass diffusers untouched.
      Test: steady tone at max mod depth keeps `maxSampleStep < 0.1`; the existing
      tail assertion is unchanged.
- [x] **S11 — SVF → TPT: assessed, kept** (docs-only verdict, folded into S10's
      commit). The homegrown state-variable filters already use the Cytomic TPT
      (topology-preserving trapezoidal) formulation — the same structure a
      `juce::dsp::StateVariableTPTFilter` swap would bring — so the swap is a pure
      regression risk with zero payoff. Future simplification candidates that
      Signalsmith/`juce::dsp` could tidy without changing sound: envelope
      followers (scattered one-poles) and LFO shape generation.
- [x] **S12 — FX picker pagination** (single pure helper; catalogue passes 16 cells).
      New `src/machine/EffectPickerModel.h`: pure `fxPickerEntries(ctx)` /
      `fxPickerPageCount(ctx)` / `fxPickerCellToCatalogue(ctx,page,cell)` over
      `{TrackInsert, MasterInsert, MasterSend}`. Track drops masterOnly+sendOnly
      (deliberate compaction — no greyed gaps), master insert drops sendOnly, send
      keeps the External sentinel. `UiState::fxPickerPage` (reset on open +
      `resetFxPickers`); Nav L/R pages ±1 clamped while a picker is open; the
      picker status line shows `P1/2` when >1 page. SurfaceModel render,
      KeyboardArea paint/labels, and PluginEditor apply-guards all route through
      the helper. Tests: context filtering, External send-only, page-offset
      cell→catalogue mapping, out-of-range → -1.
- [x] **S13 — New effects: Ladder (`juce::dsp::LadderFilter`) + Freq Shifter
      (FIR-Hilbert SSB), track+master.** `LadderFilterEffect` (id
      `lockstep.ladder.v1`, Cutoff/Reso/Drive/Mode LP·BP·HP 12/24). `FreqShifterEffect`
      (id `lockstep.freqshift.v1`, Shift ±Hz / Mix / Feedbk) — Signalsmith ships no
      Hilbert, so built from a 201-tap Type-III antisymmetric Hilbert FIR
      (deterministic, SR-independent, ~2 ms latency, no PDC). Both appended to
      `kEffects` + `makeEffectForId` (no tier split). Tests: ladder LP24 kills a
      12 kHz probe > 30 dB; shifter 1 kHz +200 Hz peaks at 1.2 kHz with lower
      sideband and carrier leakage both < -30 dB.
- [x] **S14 — New effect: Limiter** (master-only, zero-lookahead safety limiter).
      `LimiterEffect` (id `lockstep.limiter.v1`) wraps `juce::dsp::Limiter` —
      Gain (drive dB) / Ceiling (dB) / Release (ms). masterOnly. No lookahead =>
      no PDC (drops onto the zero-latency master chain); documented as a safety
      limiter, not a brickwall maximiser. Test: +6 dB sine over a -1 dB ceiling is
      held to <= ceiling+0.5 dB while still passing signal.
- [x] **S15 — Serializer v31: optional `SampleId irRef` on insert slots** (upgrade
      = stamped copy). `TrackKit::InsertSlot` (and thus master inserts/sends) gain a
      `SampleId irRef`; `SampleId` extracted to JUCE-free `machine/SampleId.h` so
      core carries it. Serialised as a content hash (`kIrHash`) on the Ins/MIns/MSnd
      node, written only for a Persistent ref (Volatile/None dropped, same policy as
      sample refs); `readIrRef` reconstructs `{Persistent, hash}` (reorder-safe).
      `kCurrentVersion = 31`; `upgrade_v30_to_v31` = stamp bump (v30 slot loads with
      empty irRef). Processor get/set API (`setTrackInsertIrRef`/`…IrRef` + master
      equivalents) added for S16's pool-IR gesture. Test: track/master irRef
      round-trips, Volatile dropped, appended `chorus_fb` survives.
- [x] **S16 — Convolution reverb** (`juce::dsp::Convolution`, zero-latency; IR-select
      Bundled | Pool via `SamplePoolOverlay` pick-IR mode). `ConvolutionEffect`
      (id `lockstep.conv.v1`, track+master): IR-select (Pool default | Bundled 1..4),
      Pre-delay, Damp, Mix. IEffect gains a `setImpulseResponse` seam; the processor
      `pushInsertIr` resolves a slot's v31 irRef to pool PCM and pushes it (on load +
      on the pick gesture). Pool pick: `SamplePoolOverlay` pick-IR mode
      (`onPickIr` + `setPickIrMode`), opened with **Confirm** while an FX picker
      shows a convolution slot; routes to `setTrackInsertIrRef`/master equivalents.
      `loadBundledIr` synthesises a deterministic decay until S17 ships baked WAVs.
      Bypass cuts the tail (documented). Tests: latency == 0; known sparse IR ==
      direct convolution (within 3e-3, spinning the background IR swap + settling the
      crossfade); missing IR → finite no-crash. **UI gesture (Confirm→pick) is
      compile-clean but wants hands-on GUI verification.**
- [x] **S17 — bundled starter IRs (rendered from our own HQReverbEffect presets).**
      *Deviation:* instead of a `tools/ir_bake` generator writing committed WAVs
      embedded via `juce_add_binary_data`, the 4 bundled IRs (Room / Plate / Hall /
      Long-Dark) are **rendered at load time** by driving a unit impulse through
      `HQReverbEffect` at curated presets. Same source + determinism the plan wanted
      (HQReverb's mod phase is fixed at `prepare()`), with no binary assets in the
      repo and no build-system change — cheaper and fully unit-testable. A
      bake-to-WAV + `BinaryData` embed remains a future option. Test: each bundled
      preset loads, is finite, non-silent, and decays.
- [~] **S18 — Docs + full A/B sign-off** (ROADMAP / README / THIRDPARTY). Docs done:
      ROADMAP S1–S17 checkboxes with per-stage deviation notes; README FX table (Ladder /
      FreqShift / Convolve + chorus Feedbk + Limiter caveat + conv-IR workflow + picker
      paging/compaction); THIRDPARTY corrected (Signalsmith is validated/available, not
      load-bearing — shipped effects use the project's own `hermite4` + FIR Hilbert).
      **Remaining: the user's `fx_audition` A/B ear-test vs the S2 baseline** (the ear
      gate — Saturation/Distortion oversampling, chorus rebuild, delay retune, reverb
      mod; flag any "before" that already sounded wrong). The GUI pick-IR gesture (S16)
      also wants a hands-on pass. Code + measured assertions are green under -Werror.

### 9.25 — Sample-rate correctness + bandlimited resampling  *[R1–R6 shipped; R4 varispeed texture pending ear-test]*
One shared bandlimited `Resampler` (`src/dsp/Resampler.h`, Signalsmith
windowed-sinc; rate-aware cutoff) behind: the sample-rate-correctness bug
fix (file rate folded into Sample/Slice playback rate — a 44.1k sample in a
48k session finally plays true), anti-aliased pitch-up reads, the looper
varispeed **scatter-add overdub** into a fresh add-only layer folded at the
wrap (`A = A*decay + B` — decouples fractional writes from decay feedback;
unity stays bit-exact), SR-scaled oversampling factors, and SR-normalised
one-pole corners. The R4 varispeed overdub *texture* stays user-gated for
the parallel ear test.

### 9.26 — Play-test fixes: live loop re-latch, held-step COND, universal page dots  *[Stages A–C shipped]*
Live `player_loop` re-latch mid-voice (was note-on-latched and inert on
sustaining auto-fit loops), held-step COND promotion (bare TRIG + held step
-> COND via a stepQualified resolver row, all four consumers agree), and
universal re-press page dots routed through the same section-selection path
the dispatcher pages with.

### 9.27 — Capture-round: two bugs, four features  *[shipped]*
The play-test round that produced the deck-engine design (DESIGN §40) also
shipped: onset-seeded start for Stretch/Stream (pre-roll-like onsets only —
a pad's swell is never trimmed), lock-only trigs made live while stopped
(`idleResolveStep()` single owner + the parked step; `noteOnLatched` marks
un-movable locks `*!`), **live P-lock motion recording** (pure
`core/MotionRecorder.h`, one seam for MZ + CC paths; a recorded motion
promotes an empty step to a trigless trig), HQ-delay musical detents
(`ParamSpec::detents` + `snapToDetents` — bare turn snaps, Func-turn free;
v32), 16 adjustable lazily-committed volatile slots (used-length as the
invariant; exhaustion returns -1, never silently slot 0), and metronome
Level/Cue-bus routing + record pre-roll count-in (standalone/Auto only).

### 9.28 — Varispeed head-law correctness (deck pre-work)  *[shipped]*
Audit of every read/write path against the head signal law (DESIGN §40.10)
before Phase 11 built on it: scatter-write gain scaled by |rate| (fixes
-6 dB at rate 2, +6 dB at half speed, unbounded pile-up at rate->0),
looper reads above unity routed through the shared polyphase (they aliased
on Hermite), and `Resampler::readCircular` + rate-axis coverage (zero,
negative, seam continuity).

### 9.29 — The Machine scope (naming the operand the grammar lost)  *[SHIPPED 2026-07-13]*
Fell out of 9.12 st.7a: section keys always edited the machine, but no
modifier could *say* "machine", so no verb could act on one — the grammar
was missing an **operand**. Shipped: `Func+Track` = Machine scope (promoted
on the primary layer exactly like `Func+Song` = Set/Global; announced via
banner, not a fake grid layer), the machine picker moved to `Track` +
hold(`SRC`) (tap = navigate, hold = picker), verbs on the machine
(copy/paste/init; `Machine+Snapshot` reserved), and the compound-scope
latch (`Func` + double-tap survives releasing Func, holding it virtually —
load-bearing for verb resolution). **Grammar change it forced:** Delete
moved off the Func qualifier onto the gesture axis uniformly — `scope +
tap(Clear)` clears contents, `scope + hold(Clear)` deletes the entity —
which revived the dead `Trig+Func+Clear` (clear P-Locks, keep trig). `KIT`
retired as a user-facing term. PRINCIPLES §2, DESIGN §13.9. Known gap: the
picker holds are still armed by hardcoded index checks (section-family
migration territory).

### 9.30 — Chrome regroup: one concern per band, one status organ  *[SHIPPED 2026-07-13]*
The bands above the MZ were grouped by accretion order, and status had six
homes. DESIGN §42 wrote the **status taxonomy** (*state / alert / event*,
distinguished by what makes them go away; anything that changes what the
next key press does is STATE and must render while armed). Shipped:
Inspector 2.0 directly above the MZ with the full-width STATUS lane — the
confirm prompt is now *derived from the armed state*
(`confirmPromptFor(kind, target)`) as a double-height pop-over, so
armed-but-invisible is unrepresentable (the old 1.5 s fading toast guarded a
sticky delete); the duplicate status homes deleted (~320 lines; badges
became HELD-text qualifiers); the hot transport+time block (with key-sig
displayed at last, and the capture indicator off the VU strip); the cold
project rail (+ controller indicator, killing status home six); the master
VU gone vertical; and a headless layout test that measures at the shipping
window size.

### 9.31 — Play-test batch: levels, seeds, outline, tempo-relative time, two bugs  *[SHIPPED 2026-07-13]*
Six items: fractional trig conditions now mean what they say (`m:n` = m
fires per n cycles, maximally even — one helper owns the rule for audio AND
the grid preview, which carried a second copy of the buggy formula); the
legato dropped-trig bug fixed at the source (`Envelope::isSilent()` — a
spent sustain-0 envelope parked active; Analog mono + para and FM all had
the hole); the MZ Func outline latches with the page; **levels made
visible/playable/on-the-wire** (VU level tick, `Track`+hold(`AMP`) mixer
page, master-VU drag, CC7 for MIDI-out); generator seeds stopped cloning
(effective seed = hash of placement + a serialized **project epoch**, v33;
SEED stays the musical dial); and temporal FX went **tempo-relative**
(delay in beats, mod rates in period-beats, detent snapping, migration
through saved BPM, v34; `IMachine::setTimeInfo` added).

### 9.32 — The status lane at rest: the MZ's write target  *[SHIPPED 2026-07-13]*
A fourth `StatusKind::Idle` (always true, so always outranked) captions the
MZ's **write target** in dispatch-precedence order — fill -> morph ->
control-all -> held step -> base, plus the meta bands — surfacing the OEB
rule where it is decided and announcing the traps (Morph writes a
deviation; Control-All writes sixteen tracks; Fill with no step held drops
the value). DESIGN §42.2.

### 9.33 — The chrome look has one owner  *[SHIPPED 2026-07-13]*
`ChromeLookAndFeel` — one font/height/radius/border/hover installed on the
editor so every chrome child inherits it; a base, not a straitjacket
(semantic colour ids still win). `ChromeStyleGuardTest` fails the build on
a per-component colour override that doesn't claim `// semantic colour:
<reason>`. DESIGN §42.2a.

### 9.34 — The UI, driven like a human drives it  *[SHIPPED 2026-07-16]*
`tests/UiDriver.h` (in `lockstep_dispatch_tests`): interaction tests written as
gesture verbs against a real headless editor — `press/tap/doubleTap/longPress/
chord/step` at the ControllerEvent layer, `keyTap`/`clickStep`/`clickDesign`
through the REAL scancode and mouse paths, `surface()` for what the editor would
actually paint. Closes the two layers the golden net enters below (QWERTY resolve
+ repeat/release, and mouse hit-testing through Item E's UI-scale transform — the
bug class no headless test could see).

Rests on the editor having ONE clock (`nowMs()`, enforced by
`ClockFunnelGuardTest`): time is frozen and advanced, never slept through, so a
gesture boundary can be asserted from both sides (kDoubleTapMs ±10) — which a
wall-clock test cannot do at all. Dispatch golden 12.3s → 6.2s, byte-identical.

Also pins **LATCH IMPLIES HELD**: `keyPressed` doesn't OR the latch into
`qwerty_.resolve` (everything else does) and is correct only because
`CommandCore::handleUp` never clears `xxxHeld` while latched. The suspected
divergence was not real; the invariant holding it up now has a test. Tier 3
(offscreen pixel rendering) deferred — shipped as 9.35.

### 9.35 — The UI, seen  *[SHIPPED 2026-07-16]*
9.34's Tier 3, plus the geometry duplication it existed to protect. Nothing
verified what was *drawn*: a lost transform, a paint/hit-test drift or a colour
regression stayed invisible because no test rendered pixels.

**Inter, embedded, product-wide** (`assets/fonts/`, OFL-1.1 — see THIRDPARTY.md).
Every Font in `src/` names no family, so the surface rendered in whatever each
machine called "sans": a look nobody chose, and glyphs that were never the same
twice. The hook must be `getTypefaceForFont` on the **default** LookAndFeel — a
typeface-less Font never consults the component's — hence
`installProductLookAndFeel()`. Bold resolves to the real Bold cut; one face for
both would render every bold label regular, legibly and wrongly.

**Three visual layers, cheapest first.** `surface()` (the model) → the **region
oracle** (per-cell: the model's colour vs the rendered median at the cell the
product's own hit test claims — no golden file, so a deliberate restyle needs no
re-blessing; runs 3 scales × 3 display modes) → **scene goldens** (6 blessed
frames for the chrome that has no model; fuzzy by necessity, `LOCKSTEP_REGEN_GOLDEN=1`).
Rests on renders being reproducible: `ManipulationZone`/`TimelineStrip` no longer
read the wall clock mid-paint (`setAnimClockMs`, now guarded).

**Step geometry has one owner** (`boundsForStep`/`gridCellBounds`). Paint and hit
test each computed the layout, hand-mirrored across all three display modes;
`stepCellAt` is now their inverse *by construction*. Landed with the region test
green before and after. Function row is the same disease, untouched.

**Drags** (`dragPhysical`/`dragMZSlider`): the MZ arms P-Lock capture in
`onDragStart`, so `setValue` can never test where a value lands. The parity sweep
went 19/22 → 22/22 — and the plan's explanation was wrong: measured, the signature
(mzOff/mzOrg) closes it alone, the fixture never mattered. Both numbers anyone had
asserted in this arc turned out wrong when measured; the scene budget likewise
cleared an obvious canary by only 1.2× until tightened 10×.

### 9.36 — Critical User Journeys, grown in waves  *[SHIPPED 2026-07-25 — standing net]*
The automated equivalent of the manual test sheet a team runs before every
release. 9.34/9.35 gave the harness the *ability* to drive and see the product;
nothing yet walked a whole user **task** end to end. A journey is described the
way a person performs it — an ordered gesture script — and asserts both the
durable processor state and the visible affordance on `surface()`, with
`expectReached` guarding that the setup truly landed (a precondition that silently
failed must fail loudly, not assert against a wrong start state).

**`tests/CUJ_CATALOGUE.md` is the authoritative tracker** and the durable artifact
that outlives any one wave: 26 journeys in groups A–H, each with its gesture
script, assertions, harness deps and a ☐/~/☑ status. Per-journey status lives
*there* — this milestone tracks waves only, so there is no second source of truth.

Arc shape (set 2026-07-24): document every journey, build the harness for all of
them, implement a few, add the rest in waves. Tests and docs only — a journey that
reveals a product bug **files** it, never fixes it inline.

- [x] **Phase 0 — the harness** (2026-07-24). `AudioRig.h` as the single owner of
      how a live processor is stood up (`EngineHarness` now delegates to it), the
      `UiDriver` live-audio bridge (`runBlocks`/`play`/`lastRms`/`hasNaN` — what
      lets a UI test assert on state the audio thread owns), MIDI note-in/out verbs,
      the semantic `setParam` through the real armed MZ path, and `expectReached`.
- [x] **Wave 1 — the seed trio** (2026-07-24). A1 two-track drum beat, B1 Euclid
      generate/commit/cancel, F1 realtime record. Proved the assertion pattern and
      corrected the catalogue where it was wrong (polymeter length is
      `Func+Phrase+step`, not a TRIG field).
- [x] **Wave 2** (2026-07-24) — A4 copy/paste/clear across scopes, C1 P-Lock (the
      OEB invariant), C3 machine picker (guards the `numSections()` trap), D1 mute,
      D5 checkpoints, E1 scene, E3 song. D5 and E3 land **partial** (`~`): each has a
      leg the product cannot currently reach, listed below. Seven journeys, five
      defects — the ratio the arc exists for.

**Defects wave 2 found.** Filed here rather than fixed inline (the arc is tests +
docs; a journey that finds a bug files it). Four of the five are the same disease
9.14 st.5 named: something *declares* a behaviour that dispatch never reaches.

- [x] **The machine catalogue was a `static`** — `commandContext()`'s
      `ProcessorCatalog` bound the FIRST editor's processor forever, so a second
      plugin instance read the first's machine schema and outliving it dangled
      (a segfault on a section copy). **Fixed on the spot**, being a crash rather
      than a wart: the catalogue is owned by the editor that uses it.
- [x] **`Func+O` never reaches UNDO.** *(closed by `9.37` item B.)* `KeyBindings` declares the row,
      `CommandCore` handles the action, DESIGN §13.6 makes it the safety net under
      every destructive op — but `clearVerbTap` routes to the table only when
      `primaryScope()` is neither `None` nor `Func`, and with only Func held it *is*
      `Func`. The press falls through to "clear the active P-Lock slot". Blocks D5's
      undo leg and all of C6.
- [x] **A scoped mark cannot be popped.** *(closed by `9.37` item A.)* §13.6 says `Func+Y` walks the held scope's
      stack; dispatch reserves Snapshot *and* Restore whenever a section-suite scope
      is held (`sectionSuiteScopeHeld`), so Track/Scene/Phrase marks push with no
      gesture to pop them — the mirror of the phantom `9.4` fixed on the push side.
      Decide which side wins: the reserve, or §13.6.
- [x] *(closed by `9.38`)* **`Mute+Song+step` (blank song) could not fire — and muted a track instead.** The
      Mute layer rewrites every step key to `ToggleMute` before the Step case can read
      `songHeld`. `resolveBinding` matches on a *subset* of held mods and no
      `ToggleMute` row requires Song, so the plain `{ToggleMute, kModMute}` row wins:
      the press arms a mute on the track with that index (measured). Dispatch's own
      documented branch is dead code. The worst of the four — the others do nothing,
      this one does the wrong thing silently. (5.3's prose claimed it; corrected
      there.)
- [x] **`Trig+Func+CLEAR` is shadowed by UNDO.** *(closed by `9.37` item C — it
      moved to `Trig` + hold(`O`).)* The documented "clear every P-Lock, keep the trig" (DESIGN §13.2)
      never reaches `verbs::trig` — `routeVerb` matches the table on the full held-mod
      set and `{VerbClear, kModFunc} → VerbUndo` outscores the bare row. 9.29 freed
      this chord *for* this gesture; 9.4 then took it for undo.
- [x] *(closed by `9.38`, and far wider than filed — it took down the whole Func layer)* **The microtiming nudge needs two Func presses.** The first `Func` over a held
      step is the W7 latch and is consumed ("no funcHeld", says the branch), so
      `hold step + Func + →` performs the step MOVE — a different documented gesture —
      until Func is pressed again. Found by A3; a gesture collision rather than dead
      code, but it silently does the wrong thing.
- [x] *(closed by `9.38`)* **A held step used as a copy/paste operand still authors on release.**
      `verbs::trig` marks the edit context param-written for Clear but not for
      Record/Play, so copying a step turns its trig off and pasting onto one inverts
      what just landed. Also noted: the copy-key glow is wired for section-*suite*
      scopes only, so a held step or section copies without lighting the key.
- [x] **Wave 3** (2026-07-24) — A2 ☑, C4 ☑, C5 ☑, D2 ☑, E2 ☑, E4 ☑; A3 and C6 `~`.
      Found: the microtiming nudge needs two Func presses (below), `Trig+Func+CLEAR`
      is shadowed by the UNDO row (→ `9.37`), the per-track `Phrase+<diagonal>` badges
      a track as deviated onto its own home phrase, the "sticky" delete picker needs
      Track still held for a Track slot tap, and the audio-census rolls were short
      enough to miss the trigs they measured. C2 (Sound Pool) deferred to wave 4.
      Three more wrinkles found, all small and all the same shape (two paths that
      write one fact disagree): per-track `Phrase+<diagonal>` badges a track as
      deviated onto its own home phrase while `deviateAllToPhrase` clears it; the
      "sticky" delete picker only accepts a slot tap for Track while Track is still
      held (the step→SelectTrack remap is what it matches on); and the audio-census
      rolls were short enough to miss the trigs they were measuring.
- [x] **Wave 4** (2026-07-24) — B2 density, B3 velocity, B4 melodic, B5 harmonic,
      C2 sound pool, D3 morph, D4 cue, H1 TIME page, H2 ratchet. **24 of 26 journeys
      now land**; only F2–F5 and G1/G2 remain, and those wait on the committed CC0
      audio the catalogue names (real transients, a musical loop, a >30 s bed) —
      procedural fixtures cannot honestly exercise transient detection or key/tempo
      analysis.
- [x] **Wave 5** (2026-07-25) — assets committed (`tests/assets/`, CC0, own licence
      statement), plus the audio-input enabler the rig never had: `AudioRig` takes a
      per-block input fill, `AssetAudio.h` decodes a fixture, `UiDriver::feedAudio`
      streams it in like a cable. F2 tape punch, F3 loop, F4 record-to-pool, F5
      MIDI-out, G1 the anchor capture flow, G2 routing-as-stem-grouping.
      **Every row in `tests/CUJ_CATALOGUE.md` is now implemented.**
- [x] **Deferred-leg sweep** (2026-07-25) — the legs each journey had parked: A3's
      note editor (flipping A3 to ☑), E1's scene bake, E2's phrase clipboard, E3's
      panic, C2's live audition, D3's fluid mute, D4's cue console, B5's LEN growth,
      B2's per-track detents, F4's `Save…` promotion, F2's markers, F3's four-sub
      widening + take-group promote, and H1's count-in. The only thing still parked is
      standalone wind/scrub, which landed with `9.38`'s close-out (F2b — the tape as a
      REEL: audible wind, reel-is-truth, the leader clamp, and the jog reel on MZ slot
      0). **Every catalogue row is now implemented, with no partial rows.** (`E3`
      flipped to ☑ when `9.38` closed the leg it was waiting on.)

Invariant for every CUJ commit: `-Werror` clean, both suites green, and
`git diff --exit-code tests/goldens/dispatch.txt` — a journey must not move the
dispatch digest.

### 9.38 — The CUJ defect backlog  *[SHIPPED 2026-07-25]*
Everything the journey suite found and did not fix, closed in one arc. The arc's own
rule is that a journey **files** a bug rather than fixing it inline, so they had piled
up here first; the fixes then went in by **cause**, not by symptom.

Six defects were filed. They turned out to be **two causes and a documentation debt**,
and closing them properly turned up a seventh instance nobody had noticed.

**Cause 1 — a layer remap rewrites a key's identity, and code matches on the rewritten
identity.** `kLayerRemaps` rewrites `Step → SelectTrack` / `Step → ToggleMute` at the
input source, from a `LayerContext` of exactly three bools. Every other modifier is
invisible to it, and because `resolveBinding` matches a *subset* of held mods, the bare
row wins by **default** rather than failing. The rule now lives in DESIGN §37.1 with
`tests/LayerRemapReachabilityTest.cpp` as its keeper — which also turned up that there
are **three** delivery mechanisms for a compound, not two (Row / Imperative /
EffectReads).

- [x] **`Mute+Song+step` muted a track instead of blank-creating a song.** *(The worst
      of the set: the others did nothing, this did the wrong thing silently.)* The rule
      moved into `handleSongSlotPress`, reached from both buttons. `Mute` qualifies the
      **create** only; on an occupied slot it is ignored and the press switches, as
      `Func` already was — a qualifier with nothing to qualify must not turn a working
      gesture into a silent no-op. **Flipped `E3` to ☑.**
- [x] **`Mute+Play+step` (relaunch / retrigger) was dead for the same reason** — the
      seventh instance, found by applying the rule rather than by a journey. It is now
      read imperatively in the `ToggleMute` case, because **no binding row can express
      it**: Play-held is not one of the eight modifier bits.
- [x] **The "sticky" delete picker was not sticky for Track.** It matched `SelectTrack`,
      the name a step key wears only *while Track is held*, so releasing the arming
      chord — which PRINCIPLES §16 explicitly requires to be safe — made the tap arrive
      as `Step` and cancel the picker. The per-scope button test is gone entirely; the
      scope decides *what* is deleted, the key only says *which* slot.
- [x] **The `Func` step latch was eating the entire Func layer.** Filed as "the
      microtiming nudge needs two `Func` presses"; it was far wider. The press was
      *consumed* before `uiState_.funcHeld = true`, and both `heldModsFromUiState` and
      the Func layer of `kLayerRemaps` derive from that one flag — so while a step was
      held, **every `kModFunc` binding row and every Func remap was unreachable**. The
      two meanings now separate on the press-duration axis, resolved at key-up: `Func`
      qualifies normally, and only a `Func` released having qualified nothing latches.

**Cause 2 — one fact written in two places, and the copies disagree.**

- [x] **`Phrase+<diagonal>` badged a track as deviated onto its own home phrase.**
      `deviated` was never a decision — it is *derived*. `Arrangement::setDeviation`
      owns it, and the **two defensive `cur != home` recomputations in the readers are
      deleted**: a workaround kept past its fix is just a quieter bug.
- [x] **A held step used as a copy/paste operand still authored on release.** Marking
      the step consumed was an obligation on fifteen writers, thirteen of which
      honoured it. Moved to `CommandCore::handleVerb`'s `PS::Trig` case.
- [x] **The copy-key glow was wired for section-*suite* scopes only.** It asked
      `firstHeldSectionSuiteScope` where dispatch asks `primaryScope()` — in which
      `Trig` is rank 0, the *highest*. A held step copied, the status lane said
      `REC=COPY`, and only the key stayed dark. **Half-fix, stated:** a held *section*
      is still uncovered, because `UiState` carries no section-held flag; closing that
      means deciding who owns the fact.

**Cause 3 — the docs were a third writer, and they were stale.** `9.29` moved DELETE to
`scope + hold(O)` and `9.37` moved clear-locks to `Trig + hold(O)`; both old spellings
were still stated as current across all three root docs — README told a reader that
`Func+O` opens the deletion picker, two milestones after it stopped. Corrected, and
DESIGN §13.7 stopped documenting the retired double-tap-step latch.

**On the "undocumented preconditions"** the journeys had collected: one was a defect
wearing a precondition's clothes (the TRIG band "must" be opened before the step hold —
that was the Func collision), and almost all the rest were **already documented**. Only
one was genuinely missing: a track is offered as an `Out` destination only when its
machine exposes an input source. Recorded rather than padded.

**Method note.** Every fix was verified by breaking it on purpose. That caught an
escape-hatch assertion in the glow leg (`!disabled` passes in both worlds, because with
no scope recognised the block is skipped and the key is not dimmed either) — the same
mistake this arc had already learned once.

### 9.37 — Snapshot / restore / undo: the reachability review  *[SHIPPED 2026-07-24]*
Wave 2 and 3 of the CUJ suite drove the checkpoint family the way a finger drives it
and found that two of its three gestures could not be reached at all. The causes were
not in the 9.4 model — they were in who else already owned the keys. The session ran
2026-07-24; **the rulings are folded into DESIGN §13.6** ("The three rulings of 9.37")
and README §5.15. What is left is the build.

The knot, for the record: `Func+O` was claimed by *three* features at once — UNDO
(9.4), the "clear the active P-Lock slot" fallback in `clearVerbTap`, and
`Trig+Func+O` = clear every P-Lock keep the trig, which **9.29 had revived from dead
code by retiring the old `Func+O → VerbDelete` remap, six milestones before 9.4 took
the seat**. Neither session saw the other, and the golden net could not see either:
it covers binding-table *rows*, while these verbs dispatch imperatively.

- [x] **A — Restore reads the held scope.** Remove the `sectionSuiteScopeHeld` reserve
      from `CB::Restore` (and from the keyboard-dead `CB::Snapshot` case, for
      consistency): `Func+Y` under a held Track/Phrase/Scene walks *that* scope's
      stack. Keep 9.4 item A's guard — an empty stack says `NOTHING TO RESTORE` and
      never falls through to the floor.
- [x] **B — `Func+O` reaches UNDO in every state.** `clearVerbTap` routes to the table
      only when `primaryScope()` is neither `None` nor `Func`; with only Func held it
      *is* Func, so the press falls through to the active-slot clear and undo never
      fires. Delete that fallback (`Trig`+slot+`O` already clears one slot) and let the
      table's `VerbUndo` row through.
- [x] **C — The per-step lock-clear moves to `Trig` + hold(`O`).** Same tap/hold split
      9.29 gave the Clear key for delete; the hold is the wider blast radius. Arm the
      hold when a step is held (today `deleteHoldCapable()` gates it on a deletable
      scope), and keep the delete rail untouched.
- [x] **D — The checkpoint family reads SUITE scopes only.** `ckScope()` reads
      `primaryScope()`, where `Trig` and `Section` outrank `Track` — so holding a step
      while marking a track silently marks the **Song**. Read the held section-suite
      scope instead (`firstHeldSectionSuiteScope`), else Song.
- [x] **E — Undo's scope: fingers optional.** A bare `Func+O` reverts the newest
      destructive op across *all* scopes (entries carry the scope the op armed); a held
      suite scope narrows to that scope's newest. Needs a global ordering the per-scope
      stacks do not have today — a small push-order log in `Arrangement`, kept in sync
      with `evictToBudget`.
- [x] **F — Cash it out in the journeys.** D5's undo leg and C6's third radius land;
      both flip from `~` to ☑ in `tests/CUJ_CATALOGUE.md`. Every gesture §13.6 names
      gets a journey that drives it end to end — the acceptance test for this milestone
      is reachability, since that is precisely what nothing checked.

## Phase 5 — Performance Depth  *[partial: 5.1/5.2/5.7/5.7c/5.10 shipped; 5.3 next (active arc); 5.4/5.8/5.9 open]*

The depth pass on top of the frozen surface: timing feel, scenes, pattern/part
management, sampling, audition, special trigs, the remaining trig-grid modes, and
the UI-polish/palette pass.

### 5.1 — Microtiming + swing + quantize  *[shipped]*  *(was ML)*
`Step::microOffset` with a sample-accurate look-ahead scheduler (also fixed
the single-emit-per-block limitation), record-time microtiming capture,
signed additive **hierarchical swing** (Song-all + Song-track + Scene-all,
moved out of APVTS into musical state — no longer host-automatable, v10),
the `Quantize` verb (`scope+No`), and the qualifier-driven authoring UI +
nudge-direction ticks.

### 5.2 — Morph + crossfader  *[shipped]*  *(was MI)*
DESIGN §17. The full crossfader: three-tier resolution (P-Lock > morph-lerp >
kit base) with **mirror resolution** for absent poles, modifier-gated
sculpting (normalised split writes), pole-forcing, stepped snap + MIDI-out
parity, **fluid mute** (`Morph+Mute` captures level->silence into the near
pole), and fader MIDI-learn. RAM-only fader; inert until A != B.

### 5.3 — Song/Scene management UI  *[active]*  *(was MJ; re-scoped for Phase 7)*
DESIGN §23 (re-derived for the Phase 7 model). The old Pattern/Part management UI
is re-scoped to manage Songs and Scenes.

Items 1–5 shipped (identity model+serialize, generative naming overlay, entry
gestures, browser, phrase copy/move/fork — see the plan doc). A **playtest
follow-up** then shipped (2026-07-16): grid-mode defaults to STG and stops
resurrecting CLN; Song/Scene names show in a widened transport indicator + the
hold-selectors; the MOD naming affordance is promoted to a lit "NAME SONG/SCENE"
primary with a hold glyph; **Songs and Phrases create-on-select** (real slot model
— `Song::initialised`, serializer v36 + backfill; empty `Song+step` copies /
`Mute+Song+step` blanks — **but see 9.36: that variant cannot fire and mutes a track
instead**, the Mute layer rewrites the step key to `ToggleMute` before dispatch can
read `songHeld`; phrase rows
are diagonal-aware, `Phrase+step` inert on
un-created rows); and the whole UI is a **uniform-scaled, aspect-locked resizable
window** (default 1.2×, persisted). Items 6–7 (SoundPool full-bundle, docs) open.

A **second playtest round** shipped (2026-07-17), all three from the uniform-scale
window landing:
- **VU meters froze** (track + master). Item E's logical→physical gap: `resized()`
  caches every chrome region in design-canvas coords but `repaint(rect)` takes
  physical pixels, so at the 1.2× default the invalidation missed the paint entirely.
  Fixed with a single-owner `repaintLogical()` (`src/ui/DesignCanvas.h`) through which
  all seven scoped invalidations route; `RepaintRegionTest` spies the invalidation
  channel itself (a `CachedComponentImage`), and `SurfaceInvalidationGuardTest` now
  fails the build on a raw `repaint(rect)` in `PluginEditor.cpp`.
- **Sg:Sc transport pill** narrowed 402→356px, right-justified and width-matched to
  the project-rail control row above; the capture indicator moved to the vacated
  middle (gaining resting room, 150→204px). Alignment is measured in
  `runChromeLayoutTests`, not asserted.
- **Song selector** now shows names + identity colours (Scene already did): the screen
  renderer consumed a hand-copied predicate that had dropped `songHeld`; it now reads
  the model's own `activeLayer` SSOT, and occupied Song/Scene cells fill from their
  identity colour (Browser pattern) with the scope on the border.
Checkbox state below corrected against the code on 2026-07-24 (the prose above had
run ahead of the boxes; each open box now names what is actually missing).

- [x] Song + Scene names (≤16 chars) — `Song::colour`/`name`, `Scene::colour`/`name`,
      persisted; authored through the generative identity overlay (`src/ui/NameGen.h`,
      `openIdentityOverlay`).
- [ ] **Decide the typed name path.** `identityRawActive`/`identityRawText`
      (`src/state/UiState.h`) are *read* by the commit path but never set true
      anywhere in `src/` — the only writer is a unit test. Either build host-keyboard
      text capture behind them or delete the fields; dead state that looks live is
      the worse of the two.
- [x] Song + Scene colours (palette tied to §24) — palette-index picker page in the
      identity overlay; occupied cells fill from their identity colour.
- [ ] Song + Scene **tags** — no `tags` field exists on `Song` or `Scene`. Deferred:
      it needs a filter/browse story to be worth anything.
- [x] Non-modal browser overlay, navigable while playing (`Func+Song+MOD` →
      `consumeBrowserKey`, `BrowserCell*` tokens). *Pages are Scenes and Phrases,
      not Songs → Scenes* — there is no Song-level browser page, which is a
      correction to this item's original wording, not an omission to chase.
- [x] Copy / paste / duplicate Phrases within a track's slot pool (snapshot on
      `Record`, paste on `Play`, fork-pick placement; `phraseSlotSnapshot` /
      `writePhraseSlot` / `forkPhraseIntoSlot`).
- [ ] Phrase **move** (and an explicit cross-track / cross-Song affordance). No
      source-clearing op exists, and the browser is scoped to `activePieceIdx()`,
      so cross-Song is unproven. Cross-track works today only incidentally, because
      the clipboard survives a `Nav` track switch.
- [ ] In-browser Scene queue cue. Cueing works but is bound to the **step press**
      (`queueScene`, double-tap = floor); `Yes` is bound to *rename*, and `No` never
      calls the existing `cancelQueuedScene()`. Settle the binding, then wire it.
- [ ] The **recall unit** (was "Kit as a recall unit", DESIGN §4.7.2). Nothing of
      this exists: `TrackKit` has no name or library identity, `Project` holds only
      a `SoundPool`, and the `Func+Track` picker is machine-only with no
      machine-vs-library paging. Blocked on a design session — "Kit" was retired as
      a term by `9.29`, so the unit must be re-derived (likely a named `TrackKit`
      bundle atop `SoundPool`, which today captures machine + `baseParams` + sample
      ref but *not* `fltrState`/`channelState`/`envState`/inserts). Recall stays a
      `Track`-scope Checkpoint floor (§13.6) — no separate gesture.

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
DESIGN §30. Trigless/lock-only trigs (`Trig+step` cycles off -> note ->
lock-only; the running path advances the fired step so overrides ride onto a
sustaining voice with no note; v24), one-shot trigs (RAM-only spent flag,
auto-rearm on transport/scene, COND "1Shot" field), and the contextual
Record trig (a trig on a Record track captures; lock-only disallowed there).
One-shot armed/spent chrome remains a follow-up.

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
Full management UI over the 5.7 pool (recall/delete/inline-rename rows,
machine-mismatch guard, `remapSoundIdsAfterRemoval` under
`withQuiescedEngine`, auto-naming). Critical bug fixed: `Project::soundPool`
was never serialized (v16). Also added the `tools/check.sh` format gate.

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
The `Func`-held section row gained the same glow/dim colour grammar the
scopes use (reachable secondaries were invisible), and the §6.2 relocations
landed: `TRACK` meta -> `Track+TRIG`, `GLOBAL` -> `Song+FX`, with `Func`
pinned to COND/NOTE.

## Phase 6 — Routing, FX & Platform  *[6.1/6.2/6.3/6.5 shipped; 6.4 shipped incl. access pass + 6.4a overlay tiers (Cue+Scene / Cue+MIDI-out deferred); 6.6 in progress; 6.7 waits for a second consumer; **6.8 shipped** (public); **6.9 shipped** (modality)]*

The audio-input boundary and the machines it unlocks, the effects system, the cue
bus, external controller surfaces, the machine-module ABI, and the beta polish.

### 6.1 — Audio-input boundary + routing + Route machine  *[shipped]*  *(was MR)*
DESIGN §27, §29. The audio-input path at the machine boundary
(`input_source` outside-world tap), **output-directed routing** (per-track
CHANNEL "Out" `{Master | Track N | Off}`; a bus reads the sum routed into
it; cycles refused at the write), the per-block topological sort
(`core/RoutingGraph.h`), the master prior-block tap, the unity RouteMachine,
MIDI-out parity — and **stem export** (Workstream D): every capture take is
a directory of `master.wav` + one `track-NN.wav` per non-empty Master-routed
track; routing IS the stem grouping.

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

### 6.4 — Cue + Aux output buses + monitoring  *[balance core + 6.4a overlay tiers shipped 2026-07-15; Cue+Scene / Cue+MIDI-out deferred]*  *(was MU)*
DESIGN §31 / §31.1. Static output complement + the `Cue` scope.

> **Scope correction (2026-07-14 alignment review).** Two boxes below were stale:
> the `Cue`-scope key *is* allocated (it shipped with 5.5 as `Func`+`3` →
> `enterCueScope()`, `PrimaryScope::Cue`), and the Cue bus *is* fed — but only by
> the metronome (`processMetronome`, the bus's sole writer).
>
> **Design reframe (2026-07-14 design session).** The remaining work was
> re-specced from an "additive monitor send" to a **per-track cue *balance*
> crossfade** (DESIGN §31 rewrite; spec:
> `docs/superpowers/specs/2026-07-14-cue-balance-6.4-design.md`). Cue balance
> `b ∈ [0,1]` crossfades a track between its normal route/sends `× (1 − b)` and
> the cue bus `× b`, applied as a fan-out gain *after* Level (so direct track
> taps bypass cue, master taps reflect it, sends fade with it). It is a
> persistent performance overlay (not scene state), morphable, and drives the
> DJ "cue-then-bring-in" workflow. `Cue + Scene` and `Cue + MIDI-out` are
> deferred to follow-on items.
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
- [x] Cue bus **declared and fed** (standalone ch 3–4 / plugin Cue bus). The
      metronome is currently its only writer (`processMetronome`), which is what
      proves the bus is live end-to-end.
- [x] `Cue`-scope key allocated: `Func`+`3` → `enterCueScope()` (`PrimaryScope::Cue`),
      shipped with 5.5. `Cue`+step already auditions.
- [x] **Per-track cue balance** overlay state (`b ∈ [0,1]`, default 0):
      `ParamIDs::cueBalance`, an APVTS param mirroring `trackMute` (persistent
      performance overlay, free serialization round-trip). `get/set/toggleCueBalance`.
- [x] **Distribution-stage crossfade**: `prepCueRamp` builds a per-sample `b`
      trajectory (~5 ms declick); route sums + `depositRoutedToCue` split by
      `(1 − b)` / `b` per-sample; sends fade block-rate by `(1 − b)`. Applied to
      deposited copies only — never in place on `trackBuffers_`. `cueEngaged_`
      keeps the uncued path at the pre-6.4 unity cost. `outputReachesMaster()`
      stays static.
- [x] **Tap semantics verified**: a RouteMachine tapping a fully-cued track still
      relays its full signal to main (upstream bypass); master tap reflects cue
      (crossfade test). Composition invariant asserted static.
- [x] **`Cue + Mute` direct gesture**: while the `Cue` scope is held, the Mute
      cluster key toggles the focused track's cue balance — a cross-column
      compound that never opens mute-view; leaves the momentary `Cue+step`
      audition untouched.
- [x] **Cue console (two pages)**: `Overlay::Cue` (opened by `Cue`-held +
      NavRight). Flip page — a step key arms that track's quantized cue flip
      (`queueCueFlip`, next quantum via the 9.17 authority, with the declick);
      param page — the MZ `Cue` band puts the bank's eight cue balances under the
      encoders (mirrors MIXER, writes the overlay, never a P-Lock). Nav pages
      between them; foreign scope / Func double-tap exits.
- [x] ~~AMP/CHANNEL page per-track balance param~~ — **folded into the `Cue`
      band** (2026-07-15). A CHANNEL slot would double-store cue (channelState
      serialization *and* the APVTS param); the `Cue` band already gives per-track
      continuous editing under the encoders, so no redundant conflict-prone cell.
- [x] Surface **cue indicator**: non-frozen `SurfaceCell::cued` / `cuePending`;
      on the console flip page the 16 step cells show cued (cyan top strip) +
      armed-flip (hollow cyan dot). Controllers ignore the new fields.
- [x] New-modality unit tests: state round-trip, crossfade / send-fade /
      tap-bypass, quantized flip (immediate + armed), `Cue`+`Mute` gesture and
      console entry/flip/page/exit (dispatch binary), `Cue` band build/write
      (MetaBand), overlay entry/exit (ModeReducer), indicator (SurfaceModel).
- [x] **6.4a — Cue overlay tiers (P-Lock + morph over a global base)**
      *(shipped 2026-07-15)*. DESIGN §31.5. Cue joins the `P-Lock ▷ morph ▷
      base` ladder every channel param uses, **keeping the global base** (§31.2):
      base = the `cueBalance` APVTS param; per-scene **morph** endpoint
      (equal-power); per-step **P-Lock** written only from the AMP cell with a
      step held (the cue console still writes the base only —
      single-writer-per-context enforced by construction). `prepCueRamp`
      resolves `step-override ▷ morphBlend(equal-power) ▷ getCueBalance` (the
      5 ms declick smooths per-step changes for free). **Storage ≠ display:** the
      cue cell displays at `ampCueSlot` but stores P-Locks/morph on a frozen
      `kCuePLockSlot` sentinel — `ampCueSlot` aliases `insertParamOffset(_,0)`, so
      it could never be the storage key. The audit resolved cleanly: whole-step
      copy, clear-all, the grid dot, and the empty-step check enumerate the
      override map generically (sentinel included); only the per-cell lock
      indicator + clear button needed the sentinel wired. `reserve(np+1)` keeps
      the audio-thread `set()` alloc-free. Tests: `CueBalanceTest`
      (`testCuePLockResolveLadder` incl. the collision guard,
      `testCueMorphEqualPower`, `testCuePLockWriteAndClear`,
      `testCuePLockSerializeById`).
- [ ] *Deferred (own follow-on items):* `Cue + Scene` double-resolve
      pre-listen; `Cue + MIDI-out` copy to a cue MIDI destination.
- Design spec (kept): `docs/superpowers/specs/2026-07-14-cue-balance-6.4-design.md`.
      Build plan folded into the ticked boxes above and deleted (shipped 2026-07-15).
- [x] Live stem capture via Aux outs documented as the blessed stem-export path
      (DESIGN §31.1) — the offline per-take stem-export item is demoted.

### 6.5 — Insert + master effects (FX system)  *[shipped]*  *(was MV)*
DESIGN §32. `IEffect` (reuses ParamSpec/role/P-Lock) + the starter
catalogue, per-track 2-insert chains, the **Animate** momentary bypass
(FX-held + step), two master FX slots + two send buses (see 8.26),
performance-grammar parity for inserts (P-Lock, Control-All, section-copy),
transport globals relocated to `Func+7`, serializers v13/v14. MIDI-out
tracks carry no inserts/sends.

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

### 6.7 — Machine Module ABI  *[deferred — waits for a concrete second consumer (2026-07-12 decision)]*  *(was M10; supersedes the old MH.5)*
> No longer gates the catalogue: 4.6/4.7 ship first-party statically (see the
> revised locked decision). Freezing an add-only ABI with no third-party
> consumer is a forever-cost with no payer; build 6.7 when one exists.
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

### 6.8 — Publication: CI, site, public  *[SHIPPED 2026-10-04 — remaining items are debts]*  *(was M9)*

Getting Lockstep out, in the shape the three sibling projects already use:
multi-platform CI, a Docusaurus site on GitHub Pages behind a CNAME, docs
mirrored to the wiki. The build plan
(`docs/superpowers/plans/2026-10-03-publish-beta-build.md`) is folded into
this entry and deleted, per the maintenance convention; `git log -p` on it
recovers the detail. The template is
antiphon (`.github/workflows/{build,deploy,wiki-sync}.yml`) — read its workflow
comments before writing ours; they record measured failures (MinGW vs MSVC, the
`-g` artefact-size blowup, clang-format version skew) worth not rediscovering.

**Baseline measured 2026-10-03:** build clean; **6/6 ctest suites pass, 172s**
(Debug); 465 tracked files, 170 MB `.git`, largest blob 11 MB (no LFS needed);
~30 TODO/FIXME across `src/` + `libs/`; **33/33 CUJ rows implemented**.

- [x] **P0 — agent tooling.** AGENTS.md documented six slash commands and two
      hooks that existed in no repo in the ecosystem. Written from its own spec
      and wired; the staging guard is proven live. AGENTS.md's stale Status
      section is now a pointer to this file, its durable half kept as
      "Load-bearing lessons", and its phantom `libs/music/` submodule struck.
- [x] **P1 — licence + contributor docs.** *(2026-10-04.)* `LICENSE` (GPLv3 verbatim; JUCE is
      used under its GPL option) and `CONTRIBUTING.md`. `THIRDPARTY.md` already
      audits every dependency and is the best of the four sibling repos — it
      just has no `LICENSE` beside it.
- [x] **P2 — the repository.** *(2026-10-04: public, `main`, verified against the remote that no agent file travelled.)* `chalkwalk/lockstep`, public. Keep the existing
      `pi@192.168.1.31` origin under another name; 1487 commits live against it.
      Decide `master`→`main` (every sibling workflow triggers on `main`).
      Verify after pushing that no agent file travelled.
- [x] **P3 — build workflow.** *(2026-10-04. **Gate answered: Linux and macOS green; Windows compiles under MSVC and passes 4/6 suites.** Seven runs, eight distinct real bugs, none in sequencer logic — the biggest being that Linux had never built with Clang, so `-Werror` was enforced nowhere.)* **The schedule gate: read the macOS/Windows
      result before committing to a beta date.** Expect genuine portability
      errors there, not warnings — the strict set and `-Werror` are Clang-gated,
      so neither new compiler fails on warnings. Verify the JUCE patch path
      (root `CMakeLists.txt` 45–88) works with `CHALKWALK_JUCE_DIR` unset and a
      fresh submodule, which is the CI case and never the local one. Plus the
      lint job: clang-tidy advisory, and **clang-format advisory too**, which
      is a departure from the sibling template and was measured, not assumed —
      see below.
- [ ] **Reformat the tree, then make clang-format blocking.** `233 of 402`
      files differ from `.clang-format` — ~24k lines of 120k. The count is
      identical at the pinned 20.1.8 and at 21.1.8, so it is **not version
      skew**: this tree was never run through clang-format. Antiphon can gate
      on format because its codebase was written in the style its config
      describes; ours was not.
      - Doing it is one commit that rewrites more than half the source: it
        destroys `git blame` across the codebase and puts 24k unreviewed lines
        through a `-Werror` build. That is its own piece of work with its own
        risk, and it was deliberately **not** smuggled into CI setup.
      - The gate flips to blocking in the same change that takes the count to
        zero, and not before.
- [ ] **Work down the clang-tidy backlog. Real baseline: 16,926 findings**
      across all 67 translation units in `src/` (measured 2026-10-04).
      Antiphon's comparable number is 142.
      - The CI step reported **2227**, and that figure was flattering rather
        than wrong: its glob `src/*.cpp tools/*.cpp` is not recursive and
        covered about five top-level units plus their headers. Widening it
        measured 7.6x more. Findings in a shared header are counted once per
        translation unit that includes it, so the number of *distinct* issues
        is smaller than 16,926 — but it is the number a blocking gate would
        have to reach zero against, so it is the one recorded.
      - *(Done 2026-10-04.)* The CI step now runs every translation unit in
        `src/` and `tools/`, in parallel, with a Clang compile database, and
        writes the count to the run summary. Its first run jumps from 2227; that
        is the same code counted honestly, not a regression.
      - Read that figure carefully before reacting to it: the step runs
        `clang-tidy -p build src/*.cpp tools/*.cpp`, and that glob is **not
        recursive** — it matches only the handful of top-level translation
        units (`PluginProcessor.cpp`, `PluginEditor.cpp`, `Parameters.cpp`, …),
        not `src/ui/`, `src/machine/`, `src/core/` and the rest. So 2227 is the
        count from ~5 files plus every header they pull in, and the true
        whole-tree figure is unknown and larger.
      - Widening the glob and re-measuring is step one; the number will get
        worse before it gets better, and that is the honest baseline rather
        than a flattering one.
      - The gate goes blocking (with `WarningsAsErrors` restored in
        `.clang-tidy`) in the same change that takes the count to zero.
- [x] **clang-format baseline confirmed in CI: 233 / 402** at the pinned
      20.1.8 — identical to the local measurement, so the figure is not an
      artifact of one machine's toolchain.
- [x] **Sign-conversions in `chalkwalk-tape`'s `Resampler.h` fixed upstream**
      *(2026-10-04, chalkwalk-tape `8138297`; pin bumped to it)*. Five
      `size_t * int` stride computations, safe at runtime but fatal to any
      consumer building with `-Wsign-conversion -Werror` — Lockstep's first
      macOS build died on them. The stride is now cast explicitly, matching
      how the file already casts its table size. The library itself does not
      enable `-Wsign-conversion`, which is why its own CI never saw them; that
      policy is its own and was left alone.
      - Lockstep keeps including the chalkwalk libraries as `SYSTEM`. That was
        never only a workaround: a vendored library's warnings are not this
        repository's to enforce.
- [x] **`libs/tape` pin bumped to `63da1d4`, and the override cleared**
      (2026-10-04). The submodule pinned `80d9adf` while this tree's `build/`
      cache pointed `CHALKWALK_TAPE_DIR` at a working checkout two commits
      ahead — so every local build and test run, including the baseline this
      arc opened with, was against the override and not the pin. CI has no
      override and builds the pin, so the two were never the same claim.
      - `cmake/ChalkwalkLibrary.cmake` warns about exactly this in capitals in
        its own header ("THE SUBMODULE SHA NO LONGER DESCRIBES WHAT YOU BUILT")
        and says to bump and re-verify before calling anything done.
      - **CI is what forced the issue.** macOS died on `std::cyl_bessel_j` in
        `LossEffects.h`, which Apple's standard library has never shipped —
        and the upstream commit sitting unpinned (`e3d52bd`, "Build on macOS
        and Windows, which it has not done since the promotion") had already
        replaced it with `dsp::besselJ0`, *and* fixed the same `M_PI` problem
        for MSVC via `dsp::kPi`. The fix for both platforms was a pin the
        project already had locally and had never recorded.
      - `CHALKWALK_JUCE_DIR` was checked for the same hazard and is clean: the
        shared checkout is at `501c076`, exactly the submodule pin.
- [x] **Engine/editor link cycle broken** *(2026-10-04)*. The engine no longer
      names a symbol it does not own: `createEditor()` is defined in
      `lockstep_engine` and asks a factory the consumer registers, so the UI
      depends on the engine and the engine depends on nothing. The registration
      lives in `createPluginFilter()`, a translation unit the JUCE wrappers
      guarantee is linked — a static initialiser elsewhere would have
      reproduced the original problem, since an archive member only links if
      something already references it. `hasEditor()` answers honestly rather
      than always `true`, and `tests/TestEditorStub.cpp` is deleted.
- [ ] **LTO stays off, and the cycle was not why.** This is a correction. The
      cycle was real and is fixed; restoring LTO still failed on Clang 18.1.3,
      and the undefined reference is now **inside a single archive** —
      `createPluginFilter()`'s lambda needs `LockstepEditor`'s constructor, and
      both translation units are members of `libLockstep_SharedCode.a`. A
      linker is expected to re-scan an archive for symbols newly required by a
      member it just pulled in; under that LTO plugin it does not.
      - So it is archive-member resolution in **Clang 18's LTO**, not our
        dependency direction, and restructuring targets cannot move it.
        Measured across four toolchains: GCC links it, Clang 21 links it,
        **macOS/AppleClang links it with LTO on**, Clang 18.1.3 does not.
      - The lever, if revisited, is the link line rather than the source:
        either link the plugin's own objects unconditionally instead of through
        an archive, or wrap the static libraries in a rescan group. Both are
        linker-specific, which is why neither was done for an optimisation
        nicety. Cost of leaving it off: cross-TU inlining and ~4 MB.
- [ ] **Run the two GUI suites on a real Windows desktop and say which way they
      go.** `SurfaceModelTest` and `DispatchGoldenTest` both stand up a JUCE
      component tree through `EditorRig`, and both SegFault on a Windows CI
      runner — while passing on Linux and macOS, and while the four vendored
      suites pass on Windows. A Windows runner has no interactive desktop
      session; antiphon's GUI audit fails on the same runners for the same
      reason, and macOS images do have a window server, which is why macOS runs
      all six.
      - They are excluded on Windows so that job reports honestly on what it
        *can* check (MSVC compiles the whole project; vendored suites pass)
        rather than being uniformly red. **This is not a claim they would fail
        on a real desktop, nor that they would pass — nobody has run them
        there.** It is a debt, and the exclusion comes off the moment someone
        says which it is.
- [x] **P4 — documentation site.** *(2026-10-04.)* Docusaurus at
      `lockstep.chalkwalkmusic.com`. The README question is settled: it was
      2,371 lines and **is now 97** — paradigm, glossary, tutorial, the
      1,400-line feature reference, implemented-vs-planned and the gesture tree
      all moved to `website/docs/`, which is the manual now. Split at top level
      only; the reference stays one page because most of its 42 anchors point
      within it. Docs serve at the root rather than `/docs`, because the
      siblings' React landing page wants artwork Lockstep does not have yet.
      - Two Docusaurus traps, both now in the config with reasons:
        `markdown.format` must be `'detect'` or `.md` is parsed as MDX and every
        `<reason>` placeholder is a JSX error; and a frontmatter title
        containing a colon must be quoted or the build fails with an unrelated
        metadata error.
- [x] **P5 — wiki sync.** *(2026-10-04.)* Wiki enabled and synced; one source
      renders two ways, and `wiki_transform.py` mirrors `website/docs/` as
      flat wiki pages with a generated `_Sidebar`.
- [ ] **Turn on Enforce HTTPS** once GitHub finishes issuing the certificate
      for the custom domain. Enabling it before the cert exists breaks TLS,
      which is how it was found; enforcement is off until then.
- [x] **P6 — release honesty.** *(2026-10-04.)* `/docs/not-done` names every
      gap — platforms as what is actually true, unfinished features, unrun ear
      tests, internal debts — and ends with what is solid. Linked from the docs
      front page.
      - **"Then tag the beta" is dropped, by decision.** None of the sibling
        projects has a single tag or GitHub release; their model is a public
        repo with CI artefacts on every push, a docs site and a wiki, and
        Lockstep now matches it. The version in `CMakeLists.txt` stays `0.0.1`.
        A release ritual the rest of the ecosystem does not use would have been
        invented here rather than followed.
- [ ] Performance pass (voice CPU profile, choke-fade SIMD, voice cap).
- [ ] Factory patch library.
- [ ] Bundle ids, icons, About box. *(The rename item is closed — "Lockstep" is
      the name; see the locked decisions.)*
- [ ] AU is built on macOS but has never been through `auval`. Treat as
      untested-in-a-host alongside VST3 and CLAP, and say so.

### 6.9 — Modality: one owner for entry, and a sweep that proves it  *[SHIPPED 2026-10-04]*

The modal architecture was sound and this was not a rewrite. The gap was that
**entry and exit were enforced asymmetrically**: `escapeOverlay()` was the only
way out, while entry was six raw `ui.overlay = Overlay::X` assignments that
skipped the outgoing overlay's parameter reset.

- **M1** — `enterOverlay()` owns entry, escaping whatever is active first.
  `OverlayEntryGuardTest` keeps it. The guard found a seventh site the same day:
  `MetaBand.cpp` had its own `escapeTimeSticky()` that *disagreed* with
  `escapeOverlay` — it never reset `sigPage`, so whether TIME reopened on its own
  page depended on which exit you last used.
- **M2** — `ModalSweepTest`: every `Modal` entered by its real gesture, then ten
  interruptions each, asserting the surface cannot show a layer ranked below the
  active modal and that a bounded escape reaches rest with **every flag clear**.
  **16 of 16 driven, 160 cases**; the table is exhaustive over `Modal` and fails
  if a value is missing. `SampleProps` was last — it opens from the pool
  overlay, so the sweep loads a sample, opens the pool from the rail, selects
  the row and presses Props... with real clicks through the component tree.
- **M3** — **the find: the melodic generator could not be escaped.** `kOverlays`
  had rows for Euclid and Harmony and none for Melodic, so `handleOverlayEvent`
  answered `NotConsumed` to everything and the universal escape did nothing. A
  `constexpr` check now requires every `Overlay` except `None` to have a row.
  Also: the guard's own pattern missed `ui.overlay = cond ? A : B`, found while
  looking up a gesture; and `ModalState.h`'s entry comments were corrected
  against `kBindings` — three had been wrong since 9.29 moved the pickers off
  the Func layer.
- **M4** — landed as Group I in `tests/CUJ_CATALOGUE.md`. A standing net: run it,
  add a row when a new modal ships.

*(The one open item, driving `SampleProps`, is done — 16 of 16.)*

---

## Phase 4 — Machine Catalogue  *[partial]*

DESIGN §1 (lineage), §29. Each machine is a contributor-sized engine inheriting
the SDK base. 4.1–4.4 shipped; 4.5+ are authored against the frozen surface and
the Machine Module ABI (6.7), so they ship as loadable modules.

### 4.1 — FMMachine  *[shipped]*  *(was MH.1)*
4-op FM with a free 4x4 matrix, per-op ADSR/ratio/fine/mix, macro scalars,
Mono/Poly (4-voice pool, oldest-steal).

### 4.2 — AnalogMachine  *[shipped]*  *(was MH.2)*
Dual PolyBLEP oscs + sub + noise, SVF with drive, filter + amp ADSR, 6-shape
LFO, portamento, Mono/Para-4 — and the polyphonic-trig infrastructure
(<=4 notes/step, chord capture, gate auto-write) the rest of the catalogue
rides on.

### 4.3 — DrumMachine  *[shipped]*  *(was MH.3)*
Rytm-style per-track drum synthesis; the `type` stepped slot selects the
voice, each with dedicated DSP (expanded to eight voices in 4.8).

### 4.4 — Sample depth + SliceMachine  *[shipped]*  *(was MH.4; absorbs the old MK)*
Sample trim window, four loop modes, edit-time zero-crossing snap, the shared
`SamplePlayingMachineBase`, and `SliceMachine` (SLICE/SCRUB dual mode,
16-slice cap, transient detection, MONO/POLY, reverse below rate 0).

### 4.5 — StreamMachine (disk-stream)  *[shipped]*  *(was MH.6)*
Disk-streaming player for long-form audio via a background
`BufferingAudioReader` — audio never decoded wholesale into RAM; gated
stream with level/pan via CHANNEL. (Pool metadata wiring and native-rate
stretch landed later in 9.23.)

### 4.6 — PercussionMachine (physical model)  *[planned — unblocked 2026-07-12: ships first-party, no longer waits on 6.7]*  *(was MH.7)*
- [ ] Volca-Drum-style two-layer percussion: excitation osc (+FM/ring + pitch
      env) → waveguide / modal resonator (Tube/String/Membrane/Modal); layer A↔B
      crossfade + bit/SR reduce + drive. Canonical FLTR/AMP downstream. Algorithm
      presets ship as Sound Pool entries, not schema variants.

### 4.7 — DigitalMachine (Monomachine archetype)  *[planned — unblocked 2026-07-12: ships first-party, no longer waits on 6.7]*  *(was MH.8)*
- [ ] Model-based digital monosynth (`model` stepped slot): SWAVE (supersaw),
      SID (PWM+ring+sync), WAVE (single-cycle wavetable/PWM), VO (formant).
      `V1` + live Mono/Poly; canonical FLTR/AMP (no opt-out). Monomachine
      GND/FM/drum engines subsumed (Route / FMMachine / DrumSynth). Authored
      against the 6.7 SDK + post-3.11 contract.

### 4.8 — DrumSynth voice expansion  *[shipped]*  *(was MH.9)*
The DrumSynth `type` enum grew to eight voices (KICK, SNARE, HAT, TOM, CLAP,
COWBELL, CYMBAL, RIMSHOT). Boundary rule kept: 808/909 analog/FM-metal lives
here; modal/waveguide struck-metal stays in PercussionMachine (4.6).

### 4.9 — Sample analysis metadata  *[shipped]*
Key + tuning detection (`dsp/KeyEstimate.h`: chroma over 60-2000 Hz, A440
deviation, circle-of-fifths scoring — root + brightness only, Unknown when
unconfident), filename/ACID hints fused detection-first, the v26 per-entry
analysis cache keyed by sample hash, the SliceMachine SYNC source (beat-grid
slicing at the detected tempo, anchored on the first transient), and pool
hint display ("128 bpm  Amin" / "one-shot"). Excluded by design: Stream
files (no PCM in RAM) and volatile captures. Documented follow-up:
key-synced Stretch playback.

### 4.10 — Tone (FluidLite + a bundled SF3 GM bank)  *[SHIPPED 2026-08-14]*

**The thought.** A General MIDI machine — **`Tone`** — with its *own* bundled sound bank —
not because GM sounds good, but because 128 named instruments in the box
opens genres the current catalogue can't reach (orchestral, jazz combo,
pop-band sketching) without leaving the Lockstep paradigm. Every GM voice
inherits step conditions, P-Locks, morph, cue, track FX and capture for free.
This is a *first-party statically-linked* machine — no 6.7 ABI involvement.
(Named in the design session below; it was captured as "GMMachine", which PRINCIPLES
§24 does not allow.)

**Research findings (2026-08-13) — the idea holds up.**

- **Engine: FluidLite** (`divideconcept/FluidLite`), a stripped FluidSynth:
  settings + synth only, no MIDI file reader, no MIDI input, no audio output.
  Standard C, zero external deps. Exactly the seam Lockstep wants — we
  already own transport, MIDI and audio. API is the familiar
  `new_fluid_settings` / `new_fluid_synth` / `fluid_synth_sfload` /
  `noteon` / `noteoff` / `cc` / `pitch_bend` / `program_select` /
  `nwrite_float`.
- **Why not TinySoundFont** (single-header, MIT, also reads SF3): TSF does
  not implement SF2.01 **modulators**, and its author has said he won't.
  GeneralUser GS leans hard on modulators — under TSF it plays, but wrong.
  Modulator support is the whole reason FluidLite is the pick.
- **Licence: clean under GPLv3.** FluidLite's headers carry LGPL v2
  *"or (at your option) any later version"*, so LGPL-2.1+ → GPLv3
  compatible; static linking into a GPLv3 Lockstep is fine. The SF3 path can
  be built with vendored `stb_vorbis` (`-DENABLE_SF3=YES -DSTB_VORBIS=YES`),
  keeping the "no external dependency" property and avoiding libogg/libvorbis
  licence + build surface entirely.
- **Bank: GeneralUser GS** (v2.0.3, `mrbumpy409/GeneralUser-GS`). 30.82 MB SF2,
  **261 presets, 13 drum kits** (corrected 2026-08-14 against the real files). Its licence permits use and modification in
  software projects and bundling; it asks that we host our own copy rather
  than hotlink. Caveat to note in the shipped licence file: it is *permissive
  but not an OSI/DFSG-free licence*, and the author states he cannot fully
  vouch for every sample's origin. It is bundled **data**, not linked code,
  so it does not entangle the GPLv3 binary.
- **Size: measured, and the earlier estimate was wrong.** SF3 = SF2 with
  Ogg-Vorbis sample data. This entry used to predict "~15% of the SF2 at
  quality 0.6 → ~4.5 MB". The whole ladder was actually converted and
  listened to (2026-07-25); the real ratio is roughly **twice** that, so the
  `<10 MB` target is a constraint rather than the comfortable margin claimed:

  | q | size | % of SF2 |
  |---|---|---|
  | 0.4 | 7.26 MB | 23.5% |
  | 0.5 | 8.00 MB | 26.0% |
  | 0.6 | 8.70 MB | 28.2% |
  | 0.7 | 9.23 MB | 29.9% |
  | **0.8** | **10.07 MB** | **32.7%** |
  | 0.9 | 11.34 MB | 36.8% |
  | 1.0 | 13.38 MB | 43.4% |

  (SF2 = 30.82 MB. Files kept outside the repo at `~/Programming/GeneralUser-GS`.)

  **Decision: ship q0.8**, chosen by ear, not by the size column. It costs
  0.84 MB over q0.7 and lands *just* over the old `<10 MB` line — so the
  target is restated as **~10 MB**, deliberately, rather than the bank being
  quietly degraded to defend a number nobody had tested. q0.4, which the
  previous note floated as the artifact threshold, is 23.5% and audibly worse.
- **RAM is *not* reduced.** FluidLite decodes every Vorbis sample to 16-bit
  PCM at load (`FLUID_SAMPLETYPE_OGG_VORBIS_UNPACKED`), so the resident cost
  stays ~30 MB. SF3 buys **distribution size**, not footprint. Acceptable —
  but it is a fixed ~30 MB whenever any track holds a Tone.

**Design session 2026-08-14 — settled.** Four decisions, and the architecture
question is answered.

**1. The machine is called `Tone`.** `Rompler` is an agent noun, which is the
form PRINCIPLES §24 outlaws — it is why "Sampler" became **Sample** and "Looper"
became **Loop**; `GMMachine` breaks the other half (no `Synth`/`Machine`/`Engine`
suffix). `Tone` is the word these keyboards literally printed on the button that
picks the instrument, so it names the role in the idiom being homaged, in one
scannable token. Ruled out on collision: `Voice` (polyphony), `Bank` (the mixer's
group of 8), `Kit` (retired by 9.29), `Module` (the 6.7 ABI), `Arrange` (the
Arrangement). **`Style` is reserved** — that is what those keyboards call the
auto-accompaniments, so it is the name to use if that ever becomes a feature.
*Accepted cost:* `Tone` already exists as a **param label** (Distortion's knob,
DrumMachine slot 5). Different namespace, never on screen together; it only makes
prose slightly awkward.

**2. The target is a cheap keyboard, deliberately.** The brief is a home keyboard
with a questionable GM bank — the cheesiness is the *point*, not a defect to be
engineered out. This is a standing constraint: do not "improve" the bank's voicing
later, and do not treat preset quality as a bug report.

**3. Internal reverb and chorus are OFF.** PRINCIPLES §9 — machines generate,
effects process — and Lockstep already gives every track its own FILTER/AMP/FX,
which is per-track where FluidLite's is not. This also **dissolves the main
objection to the shared-instance shape** below, which was that its reverb/chorus
are global to the instance. *Consequence:* the GM reverb/chorus **send** CCs
(91/93) are inert, so `Tone` does not claim the **FX** section at all — the track
chain owns it. (`Analog` already returns `numSections() == 5`, i.e. TRIG..MOD with
no FX, so this is a precedent rather than an exception.) The bank is voiced
expecting some reverb and will sound drier unaided; that is accepted.

**4. Bundled bank only.** No user `.sf2`/`.sf3` loading. The program picker stays
a fixed grid, and none of the sample pool's missing-file / relink / content-hash
story is inherited.

**The architecture: one shared instance, channel = track index.** This is
option (b), and it wins more clearly than the entry above suggested once the
internal-FX objection is removed. It carries one bank in RAM *by construction*, so
option (a)'s sfont-ownership spike disappears entirely — with (a), if
`delete_fluid_synth` frees a shared font it is a use-after-free across tracks, and
if sharing does not work at all it is 16 × ~30 MB resident. The design should not
rest on that coin-flip. Shared also buys one voice pool, one render call, one
`sfload`.

- **Channel = track index, statically.** Not allocated from 1. There is no
  allocation table to drift (PRINCIPLES §20 — the track index *is* the channel), a
  MIDI monitor reads straight across, and it lines up with MIDI-out tracks. It
  spends the 16-channel space exactly, so it **assumes `kNumTracks == 16`** — stated
  here rather than discovered later.
- **Requires lifting the channel-9-drums constraint** (FluidLite has the setting),
  or track 9 would be forced to drums. Any track then reaches a kit via bank 128.
- **The inversion is smaller than feared.** `trackMidi` is a
  `std::array<MidiBuffer, kNumTracks>` assembled **completely before** any machine
  renders, so the pre-pass needs no new plumbing to get what it wants. But
  `processTrackChain` runs in **routing (topological) order**, so "render on the
  first Tone track's `process()`" is *not* safe — it must be a pre-pass:
  1. feed every Tone track's MIDI + param→CC writes to `engine->channel(trackIdx)`;
  2. `engine->render(n)` **once** into 16 group buffers;
  3. each `ToneMachine::process()` copies its own group out — so the `IMachine`
     contract is untouched.
- **Known trade-off:** a shared voice pool means a busy pad track *can* steal
  voices from a drum track. Hardware GM modules behave exactly this way; mitigated
  by a large pool (size to be measured in the spike).

**The spike is ANSWERED — source read 2026-08-14** (`divideconcept/FluidLite`,
cloned and inspected; not yet vendored). Option (b) is viable, and the
channel-per-track shape is what the engine already does natively:

```c
/* fluid_synth.c:2308, in fluid_synth_one_block */
auchan = fluid_channel_get_num(fluid_voice_get_channel(voice));
auchan %= synth->audio_groups;
left_buf = synth->left_buf[auchan];
```

MIDI channel N routes to audio group N, and `fluid_synth_nwrite_float` copies
`synth->audio_channels` separate stereo pairs out. Set **audio-channels =
audio-groups = 16** and `channel == track == group` is identity end to end.

*Settings confirmed registered* (`fluid_synth.c:107-123`): `synth.audio-channels`
and `synth.audio-groups` (1–256), `synth.polyphony` (default **256**, 16–4096),
`synth.midi-channels` (min 16), `synth.sample-rate` (22050–96000, default 44100 —
and `fluid_synth_set_sample_rate` exists, so `prepareToPlay` can retune it),
`synth.gain` (**default 0.2** — low; raise it), `synth.reverb.active` /
`synth.chorus.active`, and **`synth.drums-channel.active`** — the setting that
frees channel 9. With it `"no"`, `fluid_synth_program_change` honours the
channel's own bank, so any track reaches a kit via bank 128.

**Findings that harden decisions already taken.**

- **Internal FX off is now *mandatory*, not merely principled.**
  `nwrite_float`'s `fx_left` / `fx_right` parameters appear **only in its
  signature** — never in the body — and it always calls `one_block(synth, 1)`
  (*do not mix fx to out*). On the multi-group path reverb and chorus are
  therefore computed and then **discarded**. Leaving them enabled burns CPU on
  inaudible effects.
- **Option (a) was genuinely unsafe.** `delete_fluid_synth` deletes every sfont
  in its list (`fluid_synth.c:616-618`), so handing one font to two synths is a
  double-free. The fallback is worse than the entry implied.
- **`stb_vorbis.c` is vendored in FluidLite's own tree**, so SF3 with zero
  external dependencies is one flag pair (`ENABLE_SF3` + `STB_VORBIS`). The
  file-static `vorbisData` that the earlier note worried about is inside
  `#if SF3_SUPPORT == SF3_XIPH_VORBIS`; the stb path uses stateless
  `stb_vorbis_decode_memory`, so on our path the reentrancy concern **does not
  exist**.
- **A pluggable file API exists** (`fluid_set_default_fileapi`, with
  fopen/fread/fseek/fclose callbacks), so the bank can be served from an
  **embedded memory block** — no temp file, no install-path hunting. It is
  process-global state, which is fine with a single engine.
- **Real-time safety is otherwise good.** Voices are pre-allocated at
  `new_fluid_synth` from `synth.polyphony` and `alloc_voice` picks or kills from
  that pool, so **note-on does not allocate**; all SF3 decoding happens during
  `sfload` (hence the ~30 MB resident), not lazily on first note.
- **FluidLite has NO internal locking — every mutex in `fluid_synth.c` is
  commented out.** Hard constraint: one thread only. The load must be strictly
  fenced from the audio thread and published with an atomic.

**The one real hazard, and its fix.** `fluid_defsfont_sfont_get_preset` does
`FLUID_NEW(fluid_preset_t)` on **every** lookup, and `fluid_channel_set_preset`
frees the old one — so a program change is a free + a malloc. That lands on the
audio thread twice over: a P-Locked program change, and (more commonly) turning
the program knob while the pattern rolls, since `writeParam` → `EngineCmd` is
drained in `processBlock`. Browsing instruments live is a normal gesture, so this
is not waveable-through.

**Fix — pre-resolve, then swap without owning (no fork).** Resolve all 128
melodic presets plus the drum kits **once at load**, on the message thread, into
a cache the engine owns; on the audio thread set `chan->preset` **directly**
(`fluid_channel_t::preset`, `src/fluid_chan.h:37`) rather than through
`fluid_channel_set_preset`, so nothing is ever freed and nothing is allocated.
The ownership audit is small and provably complete — presets are freed in exactly
**three** places, all in `fluid_chan.c`: `fluid_channel_reset` (:59),
`delete_fluid_channel` (:162) and `fluid_channel_set_preset` (:176). Therefore:

- never call `fluid_channel_set_preset`; write the member;
- never call `fluid_synth_system_reset` — it is the *only* caller of
  `fluid_channel_reset` (`fluid_synth.c:1233`) — nor `fluid_synth_program_reset`;
- pass `reset_presets = 0` to `fluid_synth_sfload`;
- at teardown, **null every `chan->preset` before `delete_fluid_synth`** so
  `delete_fluid_channel` cannot free cache entries, then free the cache.

Use the sfont iteration API (`iteration_start` / `iteration_next`) to discover
which drum kits the bank actually contains rather than hard-coding kit numbers.

*Accepted cost:* this couples us to `src/fluid_chan.h`, a private header of the
vendored submodule. The submodule is pinned, and the four rules above are the
whole of the coupling — but a submodule bump must re-check those three free sites.

**Sections (canonical by meaning, PRINCIPLES §8).** `numSections()` returns 5
(highest index + 1 — the trap in CLAUDE.md).

- **SRC** — `program` as slot 0: stepped, 0–127, `valueLabels` = the GM names.
  It is an ordinary param, so it **P-Locks like any other** and a step can change
  the instrument mid-pattern, which is squarely the cheap-keyboard move. Bank
  selects the drum kits.
- **FILTER** — CC 74 brightness, CC 71 harmonic content.
- **AMP** — CC 73 attack, CC 72 release. **Not** CC 7/10: level and pan belong to
  Lockstep's own channel strip, and duplicating them would give one fact two owners.
- **MOD** — CC 1, plus portamento / vibrato from the GM2 set.

**The program picker — settled 2026-08-14: two presses, on the console rail.**
The earlier design said "16 families × 8 programs = exactly 2 pages of 64 cells",
which assumed a 64-cell grid; `KeyboardArea::kPageSteps` is 16. The fix is not a
different page mapping but a different *interaction*: **press one for the family,
press two for the program**. 16 families fills the 16-cell page exactly, a family
holds 8 — so 128 instruments are reachable in **two presses with no paging**,
where 8 pages of 16 would have been the same information behind a hunt.

**It needs no new gesture.** `ConsoleMode::OnDemand` plus the default
`consoleSectionIndex() == kSrcSecIdx` means bare `hold(SRC)` already toggles a
machine console (re-hold closes) — the rail Route's matrix and the deck consoles
already ride. The section rule is untouched: *tap pages, hold asks what fills it.*
`Track + hold(SRC)` still picks the machine; bare `hold(SRC)` picks what fills the
machine's SRC. Drum kits stay on the `Kit` slot rather than competing for a family
cell.

Program is fully usable meanwhile — SRC slot 0 is a stepped param with the GM
names as `valueLabels`, so the encoder browses instruments by name like any other
stepped slot. The picker is ergonomics, not what makes Tone playable.

- [x] **Built.** `consoleMode() -> OnDemand`, a Tone branch in `SurfaceModel`'s
      `MachineConsole` layer (families / programs-of-family), the editor's
      cell-press handling, and `UiState::toneConsoleFamily`. The console layer
      gained its second consumer and took it without a refactor — the Route
      branch was already gated on the focused machine, so Tone slots in beside
      it. The console always opens on the family page: resuming mid-drill would
      make the gesture sometimes one press and sometimes two, which is worse
      than either. Program selection goes through `writeParam`, so **a held step
      P-Locks the instrument** — the step-changes-instrument move, for free.

**Remaining details, settled here.**

- **Loading** is lazy — on the first `Tone` track, off the message thread
  (`sfload` does file I/O), the track silent until it lands. Cost is a fixed
  ~30 MB resident whenever *any* track holds a Tone; SF3 buys distribution size,
  not footprint.
- **Sample rate**: FluidLite registers 22050–96000. Run the engine at the session
  rate when it is in range; outside it, run at 48 kHz and resample through the
  self-built polyphase `Resampler.h` from 9.25 rather than refusing the machine.
- **Persistence**: program + bank per track; serializer bump; the standard
  new-modality test triple (resolution / scope routing / round-trip).

**Build order.**

- [x] ~~Spike: does stripped FluidLite still do multi-group rendering?~~ **Answered
      by reading the source: yes, natively, and channel = group = track is
      identity — then PROVEN against the real bank by `tests/ToneEngineTest.cpp`,
      which plays one channel and asserts every other group is silent.**
      **Measured** (Debug, third-party at `-O2`): bank load **~495 ms** (10 MB SF3
      decoded to ~30 MB PCM plus every preset resolved) — which is why loading is
      async and off the audio thread, not a nicety; steady-state render with all
      16 channels sounding 3 voices each (48 voices) **77× realtime**. Polyphony
      stays at FluidLite's default 256.
- [x] **Docs pass (2026-08-14).** The PRINCIPLES check did **not** pass unamended
      as predicted: §9's consequence ("a specialised engine is a third-party
      module, not a reason to grow the in-box catalogue") would, read literally,
      push `Tone` out of the catalogue. §9 gains **"Breadth is not speciality"** —
      `Tone` earns its place by reach rather than iconic parentage, and *cannot*
      be a module anyway, because the ABI is a per-machine vtable with no
      cross-instance hook and a shared engine has no way to express itself through
      it. DESIGN gains the lineage-table row (marked as deliberately outside the
      lineage) and **§29.3**, the full architecture.
- [ ] Vendor FluidLite as a submodule; SF3 + vendored `stb_vorbis` build; a
      `deck_core`-style JUCE-free wrapper (`src/tonecore/`, `ToneEngine`).
- [ ] Convert + commit the q0.8 SF3 bank as a build asset (10.07 MB, see the table
      above); licence file alongside, naming the permissive-but-not-OSI caveat.
- [ ] `ToneEngine`: the preset cache (resolve-all-at-load) + the direct
      `chan->preset` swap, with the four "never call" rules above enforced in one
      place. A submodule bump re-checks the three free sites in `fluid_chan.c`.
- [x] `ToneMachine : IMachine`, `kMachineId = "lockstep.tone.v1"`; internal
      reverb/chorus off (mandatory, not optional); `drums-channel.active = "no"`;
      `synth.gain` raised from its 0.2 default; the engine stands up on a
      message-thread tick and loads in the background.
- [x] **Split the running-transport track loop into scheduling and render
      passes.** The pre-pass design assumed `trackMidi` was fully assembled
      before any machine rendered; that held for the IDLE path and not for the
      running one, where scheduling and `processTrackChain` were the same
      iteration of one routing-ordered loop. Found by the test that asserted
      sound and got silence, with a Drum control track proving the transport
      innocent. The render body moved **verbatim** (its only loop-local
      dependencies were `track`, re-derived, and `curFillActive`, carried), so
      the diff is reviewable as a move; the render pass keeps routing order and
      scheduling does not need it. Proven by putting the pre-pass back in front
      of the scheduling pass: both Tone assertions fail. Golden unmoved, ASan
      clean.
- [ ] Bank served from an embedded memory block via a custom `fluid_fileapi_t`.
- [x] Round-trip test: `program`, `drumkit` and an ordinary CC param survive
      save/load. It failed on the first run and was right to — poking
      `kit().baseParams` directly is discarded, because the save flushes working
      state over the kit. Writes go through `writeParam` + a block, which is the
      documented rule and now has a test that would catch its being forgotten.
- [x] Program picker: two-press family → program, as Tone's OnDemand console
      (`tests/ToneConsoleTest.cpp`).

*Not now, but named so the space is reserved:* auto-accompaniment (`Style`), which
is the other half of what those keyboards did.

## Phase 10 — Melodic & Harmonic Authoring  *[mostly shipped: 10.1–10.4, 10.7–10.10 done; 10.5 partial; 10.6, 10.11 open]*

The tonal layer: a key-signature system built on the **circle-of-fifths
brightness line**, scale-aware manual authoring, and two supportive
(deterministic / manual — never ongoing-generative) tools. Full design in
PRINCIPLES §23, DESIGN §4.10 + §39.11–39.12; execution plan in
`~/.claude/plans/i-would-like-you-validated-bear.md`. Committed per phase.

### 10.1 — Docs  *[shipped]*
PRINCIPLES §23, DESIGN §4.10 + §39.11–12, this phase block, README stubs.

### 10.2 — Scale core *(src/core/Scale.h)*  *[shipped]*
`KeySig` + `Modifier`; brightness-window math; functional Add/Alter algebra
(collection-anchored, compatibility-gated); derived `pcMask`/`degrees`/
`coreTier`/`quantize`/`classicalName`; whole-tone + diminished. Pure, unit-tested.

### 10.3 — KeySig hierarchy + persistence  *[shipped]*
Project/Song/Scene fields; `effectiveKeySig()` cascade; serializer bump (one past
head); round-trip test at all three levels.

### 10.4 — KeySig editor  *[shipped]*
`Overlay::Key` + `kOverlays` row; `buildKeyBand()` (brightness / root / modifiers
/ name); KEY sub-page off the TIME band; scope routing test.

### 10.5 — Scale-aware authoring  *[partial]*
In-scale highlighting *(shipped — `chromScaleMask` in the chromatic layout +
note editor reads `effectiveKeySig()`)*; root-anchored keyboard layout; diatonic
nav gestures (`Nav` diatonic / `Func+Nav` chromatic / move-mode octave) and
diatonic transpose *(open — Phrase+Nav transpose shipped octave/semitone in
10.9; scale-degree mode deferred)*.

### 10.6 — Per-track scale-quantize  *[open]*
Opt-in, default-off transform at note-emit + live play-in; serializer flag;
audio-path test.

### 10.7 — Melodic generator ✅
Generator Hub cell 3 (MELODY); `src/core/MelodyGen.h`, deterministic/seeded.
**Metric strength is the spine**: onsets land strongest-beat first, strong
beats get strong (low-tier) notes and longer durations, short weak notes
leave rests that bridge to the next stronger onset. Euclid-style stash ->
live preview -> print/revert. The SRC field is a lock-one-dimension
transform: *Gen* generates rhythm too, *Keep* locks onsets to existing
trigs and generates pitch only.

### 10.8 — Harmonic voice-mover ✅
Generator Hub cell 4 (CHORD) -> sticky `Overlay::Harmony`;
`src/core/HarmonyGen.h` — no chord theory, a scale-constrained multi-voice
buffer: up to 8 chord slots x 4 voices, each voice an index into the
diatonic **ladder** so everything stays in-key. MZ band = the cursor
chord's voices + LEN/CUR/MOVE/OCT structure fields; stash -> live preview ->
print/revert. Refined in 10.10 (reel visual, chroma, bar placement).

### 10.9 — Phrase transpose ✅
`Phrase + Up/Down` transposes the focused track's phrase +-octave;
`Func+Phrase+Up/Down` +-semitone (`transposeTrack` shifts base note + both
trig layers, snapshot-undoable; scale-degree mode deferred).

### 10.10 — Harmonic voice-mover refinement ✅
The 10.8 UX pass: note-name **reel** cells with half-knobs (bare turn =
diatonic rung; `Func`+turn = a chromatic borrowed tone via per-voice
`chroma`, canonicalized back to a rung on in-scale landings), bar-aligned
placement (chord k -> bar k), lossless clone-previous grow, and immediate
re-strike audition through the live-note engine. No serializer bump.

### 10.11 — Harmonic existing-rhythm placement — planned
A placement mode that keeps the track's existing trigs in place and assigns each
the chord of the bar it falls in (harmony follows bars, rhythm preserved) — the
harmonic twin of the melodic SRC "Keep" transform (`fixedOnsets`). Needs a
placement selector (Nav-right sub-page or a repurposed slot — settle at build).

---

----------------------------------------------------------------------

## SHIPPED-HISTORY-SEAM — everything below this line is shipped history

The live plan ends here. Phases below are complete and compressed per the
Maintenance section in the preamble; full detail lives in git history
(`git log -p ROADMAP.md`).

----------------------------------------------------------------------

## Phase 1 — Core Sequencer  *[shipped]*

The empty-plugin-to-playable-sequencer foundation: clocking, the P-Lock model,
MIDI ingestion, the variable-schema machine boundary, the QWERTY editor, and
state serialization.

### 1.1 — Skeleton + buildable empty plugin  *[shipped]*  *(was M0)*
CMake skeleton: `lockstep_core` static lib under Standalone/VST3/CLAP (+AU on
Apple) targets, all `core/machine/io/state/ui` headers compiling under strict
warnings, `processBlock` exercising the full pipeline (silent, NaN-free) and
APVTS round-tripping through host save/load.

### 1.2 — Audible sampler  *[shipped]*  *(was M1)*
First audible path: mono sampler voice over the pool (`{path, xxHash32}` refs),
linear interpolation, AHDSR, 1–2 ms choke micro-fade, DC-block + soft-clip +
gain-smoothed output stage. Verified click-free with a kick on every step.

### 1.3 — Polymetric clocking + multi-track  *[shipped]*  *(was M2)*
Per-track length `[1..64]` + divider resolved modulo a shared position (7-vs-16
phasing verified), step-grid pagination past 16 steps, eight tracks onto one
stereo bus.

### 1.4 — P-Locks + trig conditions  *[shipped]*  *(was M3 + M4)*
Hold-step `EditContext` routing writes to the step-override layer; probability
(deterministic per-pattern seed), iteration `m:n`, previous-dependency, and a
track-level base condition all resolving through Override-ELSE-Base.

### 1.5 — MIDI ingestion layer  *[shipped]*  *(was M5)*
Absolute CC (soft takeover) + relative CC delta routers, MIDI Learn with
per-mapping scope `{Master|Track[N]|SelectedTrack}`, channel modes, pitch
recording onto held steps, and standalone MIDI clock input with Locked/Auto
sync (freewheel-on-dropout, freeze-on-stop). `EditContext` interception is
identical for CC / encoder / QWERTY.

### 1.6 — Variable-schema machine pivot + MIDI boundary  *[shipped]*  *(was MA)*
The refactor that retired the 48-slot fixed `IMachine`: per-machine `ParamSpec`
schema, machine-sized `ParamFrame`, hybrid slot identity (index at runtime,
string id on disk), per-machine voice topology (`currentVoices()` per trig),
and the `(MidiBuffer, ParamFrame, AudioBuffer)` process signature — trigs
become injected note-ons, machines never see "trig". Trig fields
(`defaultNote/Velocity/gateLength`) became sequencer-scope with per-step
overrides.

### 1.7 — QWERTY overlay + Manipulation Zone  *[shipped]*  *(was M6)*
`QwertyOverlay::resolve` scancode mapping, MZ widgets from machine metadata,
SectionBar with per-machine labels + page cycling, COND/TRACK/TRIG/GLOBAL meta
sections, StepGrid with trig toggle/hold/P-Lock indicators and fire/skip/
probabilistic step-state preview, transport keys, three grid display modes.

### 1.8 — Pattern recording + serialization  *[shipped]*  *(was M7 + M8)*
Record-arm quantised trig capture (CC obeys EditContext; key-as-PLock),
sequence + P-Lock serialization, pool refs as `{path, xxHash32}` with relink
UX, forward-compatible version guard, CC/channel/focus/clock persistence.

---

## Phase 2 — Performance Grammar  *[shipped]*

The non-DSP backbone: the scope+verb input model, the Project/Bank/Pattern/Part
hierarchy, the performance modifier cluster, canonical sections + post-machine
FLTR/AMP, the first-class MIDI-out machine, and the 16-track expansion.

### 2.1 — Scope+verb grammar  *[shipped]*  *(was MB)*
The input model's first cut: a `ControllerEvent` stream into an `EditMode`
state machine (held scopes → implied target set), verb keys dispatching
through it, N-held-step support, and the step-grid modal-surface scaffold.

### 2.2 — Project / Bank / Pattern / Part hierarchy  *[shipped → superseded by Phase 7]*  *(was MC)*
The Octatrack-style container model (data model, v2 serialization, pattern
queue, part sharing/fork, chain mode, `StubMachine` fallback). Fully replaced
by Phase 7's musical hierarchy (`Set/Song/Scene/Phrase`); this code was the
refactor's starting point.

### 2.3 — Performance modifier cluster  *[shipped]*  *(was MD)*
Scope-typed clipboard with copy/paste/clear across step/section/track/pattern,
global + pattern mutes, the `Fill` momentary modifier, Control-All (id-match
broadcast), and the first checkpoint stack (RAM-only LIFO, depth 8).

### 2.4 — Canonical sections + post-machine FLTR/AMP + role tags  *[shipped]*  *(was ME)*
`ParamSpec::role` closed enum, canonical section reservation, and per-track
post-machine FLTR (multi-mode SVF) + AMP (AHDSR/pan/level/gate-source) blocks.
The machine opt-outs shipped here were later revised: `hasInternalFilter()`
was deleted in 8.28 (FLTR is always-present with an OFF mode);
`hasInternalAmp()` survives to gate the ENVELOPE block only.

### 2.5 — MIDI-out machine (first-class)  *[shipped]*  *(was MF)*
`MidiOutMachine` (`V0`): destination/channel/program/16 configurable CCs, clean
note-offs on channel change, All-Notes-Off + Reset-All-Controllers on stop,
FLTR/AMP bypass repurposing section keys to CC banks, and factory tables for
the Elektron lineage (Digitakt/Digitone/Syntakt/A4/Rytm/Octatrack/Tonverk).
Verified as an equal citizen in Control-All / Fills / Mutes / clipboard /
checkpoints.

### 2.6 — 16-track expansion + header pagination  *[shipped]*  *(was MGX)*
`kNumTracks = 16` (arrays scale off the constant), default split 8 Sample +
8 MIDI-out, old 8-track saves load cleanly, track-header pagination with
keyboard auto-flip. Runtime machine reassignment shipped later via the picker
(3.5).

---

## Phase 3 — Control Surface  *[shipped]*

The 10×4 control-surface and grammar revamp, frozen so the catalogue (Phase 4)
authors against a stable contract. 3.1 froze geometry; 3.2 froze the cluster +
section matrix; 3.3–3.10 closed chrome and grammar gaps; 3.11 (pattern length)
was absorbed into Phase 7.5 and shipped there. Only the 3.10 scripted
verification sweep remains (user-gated).

### 3.1 — The 10×4 surface revamp  *[shipped]*  *(was MHX)*
DESIGN §33. Widened 9×4 → 10×4: the eight-key one-hand modifier cluster + the
8-wide functional block, the cross-column-only compound-chord engine (`Func`
universal), `kMZSlots` 4 → 8, and the surface freeze.

### 3.2 — Section matrix + modifier-cluster rethink  *[shipped]*  *(was MHY)*
DESIGN §6, §13, §33. The cluster identity that still stands (`Func/Track |
Pattern/Part | Scene/Master | Mute/Fill`, since renamed by 7.9), the
`ScopedSectionMatrix` scaffold, `LFO → MOD`, the `Yes/Rec/Play/Stop/No`
utility row, and machine-select on `Func+Part`.

### 3.3 — Surface chrome (typography, label rule, scope colour)  *[shipped]*  *(was MHZ.1)*
One label rule — `resolveKeyLabel(KeyDef, UiState, EditContext)` — replacing
ad-hoc branches; 6-character ceiling; the scope colour grammar in `UITheme.h`.

### 3.4 — Contextual modes (scope re-skin, top bar, MZ streamline)  *[shipped]*  *(was MHZ.2)*
Scope re-skins (1-of-16 selector grids), the top-bar dashboard + held-context
preview off one view-model, and the MZ streamline (one rotary + value display,
`valueLabels`, double-click-to-default).

### 3.5 — Note capture, P-Lock clear, step-driven edit  *[shipped]*  *(was MHZ.3)*
Step-hold MIDI capture as the canonical chord-edit path (commit on release),
P-Lock clear gestures + the orange step-driven clear re-skin, and the machine
picker on `Func+Part` — which also delivered 2.6's deferred runtime machine
reassignment.

### 3.6 — Polyphonic step authoring + Analog para topology  *[shipped]*  *(was MHZ.4)*
Realtime chord record (cap 4) + snapshot-currently-held capture, note-count
badges, keyboardless note-edit (1-octave chromatic overlay), `NoteSelection`
(TOP/BOT bias), and the Analog paraphonic osc-by-slot routing.

### 3.7 — Engine hygiene (first-trig, envelopes, RETRIG, skew)  *[shipped]*  *(was MHZ.5)*
First-trig loudness fixed across all machines, `ParamSpec::skew` (raw on
disk), envelopes re-authored, per-track RETRIG mode (`LEGATO/RETRIG/FREE`),
and the trig/notes decoupling gesture.

### 3.8 — Record-time capture parity (velocity + musical gate)  *[shipped]*  *(was MHZ.6)*
Quantised realtime record captures velocity + gate; gate length became musical
time (`gateValue`, plain/dotted/triplet, resolved at emit; serializer upgraded
from `gateMs`); per-note velocity in `TrigOverride`.

### 3.9 — Per-track input modes (CHROMATIC, LEVELS) + overwrite/overdub  *[shipped]*  *(was MHZ.7 + MHZ.7.x)*
`TrackInputMode {PLAY, EDIT, CHROMATIC, LEVELS}`: step cells as a 1-octave
keyboard or velocity buckets, record defaulting to overwrite with double-tap
Record = overdub. Subsumed the old 16-levels and Keyboard modes; the generic
role-tagged LEVELS target deferred to 5.7.

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
Fully absorbed into Phase 7 Stage E (7.5): per-track `Phrase.length`, the
momentary re-skin, scroll-past-end, and the Length CellState tokens all
shipped there (§34.4).

---

## Phase 7 — Musical Hierarchy Re-architecture  *[shipped; 7.8 manual play-test + 7.9e sweep user-gated]*

Full replacement of the `Project > Bank > Pattern > Part` (Octatrack-style)
container model with a musically-derived model (DESIGN §4.7, §4.8, §16).
Superseded **2.2**; absorbed **3.11**; rescoped **5.2** and **5.3**. State
format: clean break + version bump (pre-release; no faithful legacy
migration). Stages 7.0–7.7 shipped under the working names
`Set/Piece/Section/Phrase`; the 7.9 vocabulary pass renamed everything to
**`Set / Song / Scene / Phrase`** (+ **Morph**, `SongTrack`).

### 7.0 — Stage 0: Documentation  *[shipped]*
Docs first: PRINCIPLES gained *"More specific scope wins"* (§13); DESIGN §4.7/
§4.8/§16/§17.1/§13/§34.4 rewritten for the musical hierarchy before any code.

### 7.1 — Stage A: Core data model  *[shipped]*
The new `src/core/` structs (`TrackKit/Phrase/Section/Piece/TimeSig`,
16×16×16 constants) added alongside the legacy model, with processor accessors
shadowing the old ones until Stage D+.

### 7.2 — Stage B: Processor state + resolvers  *[shipped]*
Deviation arrays, mutes → `!section().activeMask[]`, FLTR/AMP re-homed to the
kit, section/piece switch methods, and dual-seeded startup state.

### 7.3 — Stage C: Core time + launch engine  *[shipped]*
`Metronome` parameterized by time signature; the queued-section atomic +
bar-boundary launch (`ceil(blockStart / barPpq) * barPpq`).

### 7.4 — Stage D: Gestures / dispatch  *[shipped]*
The launch/deviation gesture family (`Part+step` queue, `Track+Pattern+step`
sticky deviation, `Pattern+step` swap-all, re-sync, `Master+step` piece
switch, commit, cancel); legacy fork/chain/queue gestures removed.

### 7.5 — Stage E: Surface model + UI  *[shipped]*
Phrase/section re-skins with deviation + queued badges, the phrase-length
re-skin **and write gestures** (`Phrase+Func+step` focused / `Morph+Func+step`
broadcast → `setTrackLength`), Length CellState tokens (add-only),
double-tap-NavRight scroll-past-end, and `SurfaceModelTest` coverage. This is
where 3.11 shipped.

### 7.6 — Stage F: Serialization (clean break)  *[shipped]*
v5 with `upgrade_v4_to_v5` dropping the old Project node; Piece → Lane(Kit +
Phrases) / Section node shape with the legacy node as fallback.

### 7.7 — Stage G: Scene hooks  *[shipped]*
`Section.sceneA/sceneB` serialized; the runtime crossfader resolver deferred
to 5.2 (where it shipped).

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

### 7.9 — Stage I: Vocabulary rename  *[shipped]*
The `Set/Song/Scene/Phrase` + Morph rename, docs-first then build-verified
sub-stages (labels → enums → core structs + serializer v6 → picker re-home),
plus the flagged DESIGN redesigns (§6.1.2 matrix rows, whole-Scene copy verb,
§23 management UI re-derivation). Its 7.9e-pre sub-arc retired the legacy
`Bank/Pattern/Part` model entirely: the lost-edits shadow-projection bug was
fixed by making `src/core/Arrangement.h` own Songs + playhead + working
Sequence with write-back on every switch (`ArrangementTest` pins it), sound
state single-sourced into `Kit`, legacy structs deleted, serializer v7 — and
7.9e's scope-respecting checkpoint stacks (`Func+Yes` / `Func+No`, CK:N badge)
were built on the consolidated model. The 7.8 host round-trip sweep it feeds
remains user-gated.

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

### 7.11 — Manual-test bug fixes  *[shipped]*
Two rounds of play-test fixes: scene persistence + version chain,
Track-compound dispatch, FLTR/AMP→Kit audio, AMP held-open, transport
pause/resume, scene-mute grammar + visuals, P-Lock clear/latch fixes, and the
phrase-content persistence bug (load clobbered non-active scenes via a stale
write-back — fixed with `Arrangement::loadPosition`, regression-tested).

### 7.12 — Colour vocabulary + verb-row rethink  *[shipped]*
The UITheme rewrite (one hue per modality, role-neighbourhood map, Func=amber /
structural cool arc / Morph=magenta) and the verb-row redesign to
Snapshot/Record/Play/Clear/Yes with Func-layer Restore/Panic/Delete/No
(`VerbClear/VerbDelete/VerbPanic` appended add-only). DESIGN §6.6, §13.

### 7.13 — Scene commit-and-bake + placeable payloads + Scene clipboard  *[shipped]*
`Scene+Record` = commit-and-bake (Yes/No-guarded: bakes live deviations into
phrase content, severs sharing — the scratch-pad workflow), placeable scene
payloads on empty steps, the Scene clipboard (capture/baked-paste/floor-only
paste, conflict-gated), omni copy `Func+U` + unqualified paste `Func+I`, and
Panic re-homed to `Song+Clear`. The `globalPhrase` routing field added here
was removed again in 7.14. DESIGN §4.7/§16/§13.2/§23.3.

### 7.14 — Pin Scene→Phrase diagonal; remap gestures; morph drag fix  *[shipped]*
`Scene::globalPhrase` deleted (serializer v11; v10 states materialise onto the
diagonal at load): Scene N always plays phrase row N as a structural
invariant. Gesture remap (`Phrase+step` deviate focused, `Scene+Phrase+step`
deviate all, diagonal clears), three create variants on empty slots,
overwrite guard, and the Morph drag fix.

### 7.15 — Song selector display; meta-band split; scope-surfaced swing  *[shipped]*
Song-held 16-slot selector grid, auto-dismiss meta on track change, the band
split (`Track+TRIG` = DIV, `Phrase+TRIG` = PHRASELEN — each band owns its
hierarchy level), and swing surfaced directly by held scope.

### 7.16 — MetaBand unification: controller exposure + transient dismissal  *[shipped]*
New pure `src/ui/MetaBand.{h,cpp}` (`resolveMetaBand` / `buildMetaBand` /
`writeMetaField`) shared by screen and controllers so they cannot diverge;
fixed the picker leak, the dead DIV/PHRASELEN bands, and made the swing band
transient (`swingDismissed`).

### 7.17 — Swing anchored rotary + reusable reference-mark element  *[shipped]*
One `"Swing"` rotary showing cumulative groove at the held scope with
scope-coloured `ReferenceMark` ticks for inherited floors (reusable for any
layered/delta parameter; morph is the next consumer). Fixed the track-scope
cumulative-swing semantic (was off by the scene delta) and bipolar ring
rendering (`MetaRotaryLookAndFeel`). DESIGN §19.2.

---

## Phase 8 — Hardening & Maintainability  *[shipped]*

DESIGN §37 (Command Core), §35.8.7 (Cell appearance table), §37.4 (text SSOT),
§37.5 (ParamRow). Root causes addressed: dispatch duplication across three input
paths; off-model UI text; appearance data trapped in three switch statements;
serializer silent-loss risk; test gaps in EditMode, dispatch, and serializer
round-trips.

### 8.1 — Docs-first milestone definition  *[shipped]*
DESIGN §35.8.7 + §37 (Command Core, §37.4 Status SSOT, §37.5 ParamRow) added
before any code moved.

### 8.2 — Characterization tests before moving code  *[shipped]*
`EditModeTest` (priority order, compound scope, verbs under chords),
`LayerResolveTest` (golden scancode × layer table — the oracle for 8.3), and
`SerializerRoundTripTest` (sentinel round-trip per serialized field).

### 8.3 — Single `resolveLayer`  *[shipped]*
`src/command/ButtonLayers.{h,cpp}` + `kLayerRemaps[]` replaced three diverged
layer-resolution implementations (QWERTY, mouse, controller) with one, under
the golden test.

### 8.4 — Command core extraction  *[shipped]*
`CommandCore` / `CommandContext` / `CommandEffects` seams extracted from the
editor; all verb scopes migrated into `VerbCommands`; section/meta/transport
handling moved (`handleDown/Up`), shedding ~1,500–2,000 editor lines. The two
`[-]` items (8.4f/g — scope-modifier and step-semantics migration) were
deliberately left in `dispatchDown` and later completed by the 9.12 dispatch
migration.

### 8.5 — Gesture-level test harness  *[shipped]*
`tests/GestureHarness.h` + `GestureTest.cpp`: named end-to-end gesture
scenarios against the real core model, no processor.

### 8.6 — CellState appearance table  *[shipped]*
`src/ui/CellStates.def` + `CellAppearance.h` (X-macro, literal values)
consumed by `XTouchMiniSurface`, `Push1Surface`, and `KeyButton` — appearance
data left the switch statements.

### 8.7 — Status & contextual text SSOT + transport fix  *[shipped]*
`src/command/StatusText.h` (40 `setStatus` sites swept), `gridBanner` +
`pageDots` into `SurfaceModel`, and `TransportModel` +
`InPluginTransport::refresh()` killing label desync.

### 8.8 — ParamSpec constexpr tables  *[shipped]*
`MachineParamTable.h` (`ParamRow` + `toParamSpec`, LsmParamSpec-shaped —
pre-shaping the 6.7 ABI); every machine converted under `ParamSpecTest`
golden ids.

### 8.9 — Serializer hardening  *[shipped]*
`src/state/StateKeys.h` (every property name a `constexpr` constant) + the
round-trip mutation self-test; silent-loss bugs fixed.

### 8.10 — Final docs pass  *[shipped]*
DESIGN reconcile, README shortcut sweep, ROADMAP ticks, orientation-file
layout map + gotchas.

### 8.11 — A-series: key-cell label/action SSOT  *[shipped]*
One table answering both "what does this key say" and "what does it do":
`ScopePriority.{h,cpp}` (`kScopePriority`, the single scope-precedence
encoding), `SurfaceLayer.{h,cpp}` (`resolveActiveLayer()`, the modal-layer
SSOT), and `KeyBindings.{h,cpp}` (binding table: `ActionId` + label +
`CellState`, most-specific-wins). A4 wired the table into dispatch itself
(`handleAction` pilot, scope-up dedup, layer-routed overlays, direct
`handleVerb` calls, Panic/TapTempo migration) — leaving `dispatchDown` as a
staged shim for what became 9.12. Accepted residuals recorded: no runtime
JSON for internal tables, no registries, no per-node serializer descriptors
(possible 6.7-era follow-up).

---

## Phase 8 — Quality: threading, dispatch, dedup (8.12–8.27)  *[shipped]*

Code-quality audit findings across four workstreams (safety net, threading
architecture, dispatch SSOT, dedup/cleanup). DESIGN §38 holds the threading
contract.

### 8.12 — Sanitizer support  *[shipped]*
`LOCKSTEP_SANITIZE` CMake option (`OFF|asan|tsan`) on `lockstep_core` + tests,
`-Werror` kept; suite verified ASan/UBSan clean.

### 8.13 — Machine DSP smoke + envelope characterization  *[shipped]*
`MachineDspTest.cpp`: per-machine trigger/render/release smoke (non-silence,
no NaN/Inf, decay-to-silence), block-size invariance, IEffect catalogue smoke,
envelope goldens for Analog and FM.

### 8.14 — Engine testability: headless processBlock harness  *[shipped]*
`lockstep_engine` static lib + `EngineHarness`/`EngineTest` driving
`processBlock` hostless. Fixed the latent `preparedSampleRate_` bug (machines
were prepared at sampleRate 0 until the host called
`setRateAndBufferSizeDetails` — the root of the EngineTest silence gotcha).

### 8.15–8.19 — Threading contract + engine-command queue + dispatch mop-up  *[shipped]*
DESIGN §38 written and enforced: member thread-annotations, `PLock` as a
sorted flat vector, the JUCE-free `EngineCommand` SPSC queue migrating all
message-thread param writes (audio thread drains at block top), scene switch
via pre-staged double-buffer swap at the bar boundary (`prepareSceneLaunch` /
`applySceneLaunch`, O(N) bounded, no allocation), and `withQuiescedEngine(fn)`
wrapping every structural mutation site. 8.19 closed the post-A4
precedence/duplication mop-up (binding-table lookups + `kScopePriority`
SSOT ordering everywhere). Residual THREADING-DEBT tags: `slicePositions_`,
`applySceneLaunch` index writes.

### 8.20 — Envelope unification  *[shipped]*
DrumSynth's hand-rolled AHD replaced with `dsp::Envelope` (hat keeps a 1 ms
release for the open-hat choke); FM per-operator envelopes deferred.

### 8.21 — Controller-surface dedup  *[shipped]*
`src/controller/SurfaceShared.h`: the two encoder-delta decodes as `constexpr`
functions (X-Touch signed-magnitude vs Push 1 two's-complement — the old
comment claiming they matched was wrong), golden-tested; X-Touch double-click
onto `DoubleTapDetector`. Hardware verification stays user-gated.

### 8.22 — UiState gesture-group reset methods  *[shipped]*
Bundled reset helpers (`resetNoteEdit/PLockClear/FxPickers/Euclid`) so no
field of a gesture's state can be silently omitted; full per-gesture struct
grouping deferred (~160 access-site renames for no added safety).

### 8.23 — Serializer v15: P-Lock string ids  *[shipped]*
P-Locks written as `("id", paramId)` instead of slot ints, honouring the
documented contract in `PLock.h`; dual-path loader keeps v14 saves loading;
unknown ids drop with a log. The 7.8 host round-trip stays user-gated.

### 8.24 — UI/UX consistency pass  *[shipped]*
Label/action mismatch sweep + the confirm lifecycle: label-length conventions
pinned by test, scope×Func binding rows fixed (Scene/Morph verb mislabels),
the hint = Func-variant rule pinned, `ConfirmState` in UiState with sticky
Yes/No confirm (survives chord release; any foreign press cancels), the
scope+Func+Clear deletion picker, and scope-glow tint gaps closed. Standalone
visual smoke stays user-gated.

### 8.25 — Format/lint gate  *[shipped]*
`tools/check.sh`, house-style `.clang-format` (mechanical whole-repo
reformat), `.clang-tidy` header-filter fix, compile-commands pinned.

### 8.26 — Master-bus re-arch: 2 inserts + 2 sends; FX catalogue expansion  *[shipped]*
DESIGN §32.3 rewritten. The master bus became 2 inserts + 2 send buses
(serializer v17, AMP send slots, `Song+FX` 4-unit pagination); five new track
effects (TiltEQ/Compressor/Bitcrusher/Flanger/Phaser) and four HQ master
effects (FDN reverb, tempo-synced ping-pong delay, bus compressor, master
utility); `CaptureRecorder` WAV capture on `Func+Song+Record`; plus the
quality pass (master-chain-silent fix, FM legato, per-sample smoothing) and a
post-audit gap closure (legato/retrig/effect/serializer tests, Animate for
the master units, the internal-amp send-routing bug).

### 8.27 — Smoothing policy  *[shipped as part of 8.26-A]*
Per-sample one-pole smoothing (~5 ms) on all gain-path effect params (DESIGN
§32.3 addendum).

### 8.28 — Track channel/envelope split + universal filter  *[shipped]*
DESIGN §14 rewritten: the conflated AMP block split into always-on **CHANNEL**
(level/pan/sends) + optional **ENVELOPE** (only without `hasInternalAmp()`);
the track filter became always-present with an OFF mode and
`hasInternalFilter()` was deleted (~25 conditional-offset sites collapsed);
serializer v18; behavioural tests (channel P-Lock, OFF passthrough,
filter-on-Analog).

---

## Phase 11 — The Deck Engine: Record, Loop, Tape  *[shipped — core, tail, and all play-test rounds]*

Record, Loop, and the new **Tape** face as three faces of one four-sub-track
deck engine defaulting to a single stereo sub-track — day-one behaviour
unchanged, depth opt-in. Full design in **DESIGN §40**; rests on the
**PRINCIPLES §25.1** transport amendment (absolute position joins the one
authority; hosted, the DAW timeline *is* the tape timeline). Built as the
**JUCE-free `deck_core` library** (`src/deckcore/`) behind a thin `deck_juce`
adapter (§40.11); the standalone partner app remains a concept brief only
(`docs/partner-app-concept.md`).

Rejected en route, recorded so they are not re-proposed: scene-scoped audio
clips (session-view drift), a forward-only reel, a master-bus tape fixture,
tape-as-an-effect, and **loop groups** — cross-machine coordination lost to the
4-track deck, which keeps the coordination local to one machine.

### 11.0–11.8 — Core arc  *[shipped]*
In build order: docs (11.0 — the free-standing multi-head/tap model: a tape
delay is a *configuration* of heads, feedback caller-side; one-window rule for
sub-tracks); the `deck_core` factoring (11.7 — JUCE-free STATIC lib: `Medium`
(linear/circular, f32 **or i16**, non-owning over caller storage),
`ReadHead/WriteHead/EraseHead` under the §40.10 head signal law,
`LayerStack` (fold / one-level undo / decay / punch-as-layer), `Deck` +
`TransportSnapshot`; acceptance = a pure test builds a working tape delay from
the library alone); transport position (11.1 — `Clock::locate(ppq)`, absolute
position single-sourced, no state travels with position); the engine re-seat
(11.2 — **Loop + Record re-seated on `dc::Deck` with the suite passing
unchanged — the risk gate the whole arc pivoted on, and it held**); the
tap-only input matrix (11.3 — `routing::TapEdges`, `IMultiInput`); Loop's
4-track face (11.8 — 8-channel slot, per-sub mix + TRACKS console page,
polymeter designed out via the shared window); the Tape face (11.4 — linear
position-addressed medium, non-destructive punch + undo, **dumb markers**
(auto-drop on scene/song switch, cue = locate, never fire a launch; marker
*serialisation* dropped by decision — audio is promote-or-lose, §40.8),
serializer v33); console pages + the display-only timeline strip off a pure
`buildTimelineModel` (11.5); and take-groups + channel policy (11.6 — group
promote = N sub-track WAVs + downmix under one take-group id, the **6/5
picker rule** (`isDeckClass`), load-onto-sub-track + the **FIT** verb
(pitch-preserving Bungee stretch to the window), and `ChannelPolicy.h` —
which also fixed two latent 4-track bugs hiding in `min(2, …)` clamps).

### Phase 11 tail — chase-locked Tape, i16, proxies, wind, multi-sub overdub  *[shipped]*
Design-settled 2026-07-09, reversing an earlier real-time-reel lean: the Tape
is **chase-locked** — head = `ppq × K`, calibration `K` latched at first
record onto an empty reel, chase rate `r = K / samplesPerPpq(now)` reading and
writing through the head law (`r == 1` bit-exact; kills the mid-record
head-jump). Plus the i16 reel (`medium_depth`, depth-erased `dc::Store`), the
§19 hardware proxies (REC pulse, marker-approach brighten — editor chrome,
palette stays static), Tape scrub + FF/RW wind (standalone only,
`transportWindable()` — never half-working hosted), and multi-sub-track
overdub (armed fan-out, whole-deck undo, suite unchanged).

### Phase 11 play-test round — inputs, record model, console/timeline polish  *[shipped]*
Two bugs (Tape console text never drawn; only sub 0's SRC slot existed —
declaring `input_source_2/3/4` was the whole fix), four features (four stereo
Ext input buses with append-only encoding; the ARM-gated Loop record model
with hold-REC punch-replace; retroactive double-tap for Loop and Tape;
the permanent timeline strip), and the **S6 stretch spike** whose measurement
reshaped the plan: Bungee Basic alone is ~160×RT, so **one engine** is both
the fast and the quality path (two-tier design dropped; signalsmith stays
fallback-only; `bungee_library` + `pffft` pinned to `-O2` even in Debug
because Eigen at -O0 collapses it to 2.7×RT). S7 then shipped the FreeLen
pitch-preserved fit on that result: record-close rounds the loop length up to
the next launch-quant multiple and a realtime `BungeeStretchEngine` streams
the stretched loop instantly while a background worker bakes the full PCM and
swaps it in seamlessly (`adoptBakedFit` under `withQuiescedEngine`); the FIT
verb unified onto the same machinery (`FitScope::SingleSub`).

### Phase 11 play-test round 2 — Tape UX  *[shipped]*
The round that articulated **one deck engine, three faces** (Record = 1-track
linear, Loop = 4-track circular, Tape = 4-track linear). Reel units in
seconds; Bits (`16i`/`32f`) now *converts* the reel in place; the tiny-loop
buzz and release-slew fixes; **the Tape lost its own transport** — it chases
the song playhead and detaches only for scrub/wind while parked; the spinning
reel scrub widget; the Tape widened to a full four-sub-track deck (6a–6e:
wide reel, per-sub sources/mix/punch, shared TRACKS page, take-group
promote); retroactive double-tap client-side for both faces (pre-roll ring
backfill to tap 1, close-trim; DESIGN §40.13); TRACKS discoverability hint.

### Phase 11 play-test round 3 — streaming loop fit + Record port  *[shipped]*
The deferred **Record → deck-medium port**: RecordMachine captures through a
host-owned stereo reel bound as a LINEAR `dc::Medium` with a
transport-chasing write head (`r == 1` unity path bit-identical to the old
copy; a mid-take tempo change warps the take like tape — deliberately *not*
pitch-preserved, that's Loop's FreeLen). Commit lands at close into the
volatile pool slot; one-level deck-native undo. Bits, retro double-tap and
markers deferred for Record.

### Phase 11 round 4 — `deck_core` runtime width  *[shipped]*
`dc::kMaxSubTracks` deleted — the library's last product constant.
`dc::Deck` takes runtime capacity (`explicit Deck(int capacity = 1)`,
allocation once at construction; `setSubTrackCount` stays noexcept and
allocation-free; `subTrack(i)` clamps to defend the invariant). The 4 and
the 2 live only on the Lockstep side (`kMaxInputSubTracks` /
`kMaxDeckChannels` / `kEngineChannels`); Record's default capacity 1 *is* the
linear one-track face — the honest test that the abstraction was right. Pure
refactor, suite green unchanged.

### 11.10 — Capture-family CUJ design session  *[shipped 2026-07-12]*
Design-only session writing the capture family's combined CUJs end-to-end
against the shipped surface. Anchor: the product's core flow — **improvise a
full live set and record it as stems**, then its studio and in-DAW
replications; positioning between Octatrack, Digitakt, and Squarp Pyramid.
Key discovery: per-track stems had already shipped (6.1 Workstream D) while
DESIGN §31.1 still called them a non-goal. Decisions: **routing = the stem
grouping** (Route bus = one stem, feeders fold), stems dry-per-track with
`master.wav` carrying inserts + send returns (sends not separately captured),
capture deck = the zero-thought archive vs Aux outs = timeline-locked DAW
tracks, Tape = flexible instrument (archive only if promoted). Output:
DESIGN §41 + README §5.23; twelve seams logged, the load-bearing ones filed
as 11.11.

### 11.11 — Stems completion: the alignment invariant, arm preview, take sheet  *[shipped 2026-07-12]*

The follow-ups the 11.10 session committed to (DESIGN §41.3), plus the shipped
defect planning uncovered. These make the defining CUJ (improvise from blank →
stems) actually hold — and make the stems it produces *trustworthy*.

> **Design pivoted during planning (2026-07-12).** The session's sketch was
> "late-arm a stem, silence-pad it back to take start." Planning found a
> **second, shipped defect** that the sketch would not have caught:
>
> - **S13 — the skip-path shortfall.** Both track loops `continue` past a
>   fully-faded muted track (`PluginProcessor.cpp` ~2803 running, ~2502 idle;
>   also MIDI-out and `divPpq<=0`), which skips the stem write at
>   `processTrackChain` (~1181). Every skipped block **shortens that stem
>   file** — so muting a track mid-take (the most common performance action)
>   time-shifts everything after the mute in that stem against `master.wav`.
>   Shipped stems are already misaligned; nobody would have noticed until the
>   morning after.
>
> Auditing skip paths would leave the next one to be forgotten (§20), so
> alignment becomes a **central invariant**: *after every block, every stem
> file holds exactly as many samples as `master.wav`.* Record-all → top-up
> sweep → prune at close (DESIGN §41.3). This fixes S12 and S13 with one
> mechanism and needs no late-arm race.

- [x] **Stage 1 — the alignment invariant (S12 + S13).** SHIPPED (`d0879d3`). Arm **all 16** stem
      recorders at capture start (Stub + MIDI-out included — a machine assigned
      mid-take must yield a file reaching back to the start). After each master
      `writeBlock`, a bounded, allocation-free **top-up sweep** pads any stem
      that wrote fewer samples than the master. The audio thread latches
      "was ever stemmable"; `stopCapture()` **prunes** the files of tracks that
      never were (feeders, Off, never-assigned, MIDI-out) — so *routing is the
      stem grouping* survives unchanged. One shared `TimeSliceThread` for
      master + stems (17 always-armed recorders must not be 17 threads).
      Tests: from-blank capture → assign mid-take → aligned padded stem;
      **mute-alignment regression** (stem length == master length, sample-exact,
      across a floored mute ramp and the idle path); feeder-fold prune. Both new
      tests verified to FAIL with the sweep disabled — the regression is real and
      the net catches it.
- [x] **Stage 2 — Arm-time stem preview (S1).** SHIPPED (`aece2aa`). Arming announces the outcome in
      the capture strip — "ARMED ▸ master + N stems"; the count live-updates
      while recording (same predicate the prune latches).
- [x] **Stage 3 — Take sheet (S4 / the morning after).** SHIPPED (`a19a0f0`) —
      `src/io/TakeSheet.h` (pure formatter + wait-free bounded ring). Each capture directory
      gains a plain-text sheet: project, date, sample rate, tempo root, the
      stems it kept, and the launch log — the time of every Scene/Song launch
      during the take. Events are stamped on the audio thread into a fixed-size
      ring at the same site the Tape drops its markers (§40.4) and drained to
      text at close, so a discarded take takes its sheet with it. Dumb like Tape
      markers: places, never cues (NON-GOALS #1 tripwire applies).
- [x] **Stage 4 — Verification rider (S6) + docs close.** **Finding:** four
      stereo Ext buses are declared and `isBusesLayoutSupported` accepts any of
      them enabled, but only `In` (Ext1) is enabled by default and **nothing in
      the standalone enables the other three** (JUCE's standalone holder takes
      the default layout and offers no bus-enable UI). So: *one* stereo hardware
      return standalone, four in a host that enables them. Recorded in DESIGN
      §41.3 + README §5.23 rather than implying all four work; the fix is filed
      as **11.12** (standalone plumbing, not capture). README's "known gap"
      callout removed. (Host Aux port exposure stays tracked under 6.4.)
- [ ] *(Parked, not scheduled)*: the "External" master-insert placeholder —
      declare a master slot as processed-outside so the captured master is dry
      for DAW post-processing (DESIGN §41.3). Build only on real demand.

### 11.12 — Every bus enabled by default  *[shipped 2026-07-13]*
The fix was the *removal* of a concept: play-testing showed a DAW makes no
observable distinction between declared-enabled and declared-disabled buses,
so the "host opts in" flag bought nothing while costing the standalone its
inputs — all buses now default-enabled (Ext1–4 in; Master/Cue/Aux/Sends out),
and bus enable/disable plumbing is explicitly not being built. Killed a third
shipped defect on the way out: the capture writers were sized from the
*total* output channel count, so any host with an extra bus enabled made them
read off the end of a stereo array (`getMainBusNumOutputChannels()` now); the
test harness sizes its buffer from the bus layout, which is what surfaced the
OOB. User-gated: verify four returns → four stems on a real multi-input
interface.

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
