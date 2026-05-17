# Lockstep — Design

> Working name. **Lockstep** is the codename used throughout the source
> tree (CMake project, namespace `lockstep`, bundle id
> `com.ChalkWalkMusic.Lockstep`); the eventual product name will replace
> it.

## 1. Vision

Lockstep is a performance-oriented step sequencer plugin (VST3 / CLAP /
Standalone, AU on macOS) for electronic musicians, sound designers, and
live performers. It treats sequencing as a live instrument rather than
a passive automation surface: discrete trigger programming, conditional
logic, and step-based parameter locking ("P-Locks") replace the
continuous DAW automation paradigm.

The reference lineage is explicit:

- **Digitakt** for the core feel — the trig grid, P-Locks, trig
  conditions, fills, performance mutes, copy/paste/clear grammar,
  temporary save/revert, and the "control-all" gesture that broadcasts
  a parameter edit across every track at once.
- **Octatrack** for the *Project / Bank / Pattern / Part* hierarchy and,
  crucially, for the model that lets every track choose its own
  machine. A Lockstep Part is the Octatrack idea translated onto
  variable-schema `IMachine`: each track slot in a Part stores which
  machine it hosts, its samples, and its base parameter frame.
- **Squarp Pyramid** for two things: (1) the seriousness with which it
  treats *external* gear — sequencing an outboard synth or a sibling
  plugin is a first-class workflow, not a degraded fallback; a MIDI-out
  track has the same trig grid, the same P-Lock semantics, and the same
  performance modifiers as a track driving an internal machine. And
  (2) for the way its workflow lets a performer *compose live* — the
  copy/paste/clear grammar, the conditional trigs, and the
  hold-scope-and-act gestures are not just for triggering pre-made
  loops; they are for *building* material on stage, in front of an
  audience, starting from nothing.

Both halves are equal citizens. A typical project mixes
sampler/synth tracks driving Lockstep's own internal machines with
MIDI-out tracks driving a Digitone, Syntakt, A4, Rytm, Tonverk,
Octatrack, or a sibling plugin in the same DAW. The sequencer behaves
identically for both.

Three non-negotiable design pillars:

1. **Realtime usability without the mouse.** The full editing workflow
   is reachable from the QWERTY keyboard alone. The eventual hardware
   controller is literally a special, fewer-key QWERTY keyboard in a
   grid layout — same key→action mapping, ergonomically denser
   package, **no extra features**. If a workflow can't be done from
   the software's QWERTY today, the hardware won't add it.
2. **A strict DSP encapsulation boundary.** Sound-generating engines —
   "Machines" — are isolated behind a tightly scoped interface so that
   a native engine, a MIDI-out adapter, and any future plugin-host
   wrapper look identical to the sequencer core.
3. **Performance is composition.** The same tools that let a performer
   manipulate pre-authored material — copy/paste/clear, conditional
   trigs, P-Locks, fills, control-all sweeps, alternate trig modes,
   checkpoint reverts, pattern chaining — are the tools that let them
   *create new material live*. Two equally-supported workflows:

   - *Bring existing material on stage.* Load a project, perform it,
     improvise variations on top via mutes / fills / chain.
   - *Improvise from scratch.* Open the plugin, point it at a sample
     pool (or a MIDI destination), and start authoring: tap in trigs,
     hold-step + tweak to P-Lock, copy a pattern and mutate it,
     checkpoint before a risky idea and revert if it didn't land.
     There is no "design mode" vs "performance mode" — the editing
     gestures **are** the performance gestures.

   There is no song timeline. The closest analogue is the *Chain* (a
   queued list of upcoming pattern changes — see §16), which is
   itself a performance tool rather than a static arrangement: it
   lets the performer plan two or three changes ahead, freeing their
   hands for the live composition itself.

The project is GPLv3, cross-platform (Linux, macOS, Windows), and
compiles to CLAP and VST3 from a single C++/JUCE codebase.

## 2. The Machine Boundary (`IMachine`)

The sequencer talks to every sound engine through a single C++ virtual
base class, `lockstep::IMachine`. A contributor adds a new engine by
inheriting from `IMachine` and implementing its half-dozen virtuals;
no sub-plugin format, no IPC, no sandbox.

The boundary is deliberately narrow but deliberately *not* fixed-shape
— each machine declares its own parameter schema and voice topology:

- **Parameter Schema (variable, machine-declared).** A machine declares
  any number of parameter slots. Each slot is a `ParamSpec` carrying:
  a stable string id, a display label, range, default, stepped flag,
  unit hint, the section index it belongs to, and an optional
  **role tag** (see below). Slot count and layout are entirely the
  machine's choice. The sequencer's P-Lock storage, MZ rendering,
  and CC mapping are all driven by the schema the machine reports.
- **Role tags (`ParamSpec::role`).** An optional enum that classifies
  what a slot *is* across machine types: `cutoff`, `resonance`,
  `attack`, `decay`, `sustain`, `release`, `lfo.rate`, `lfo.depth`,
  `pitch.fine`, `pitch.coarse`, `pan`, `level`, `drive`, `feedback`,
  `delay.time`, … The set is a closed C++ enum, deliberately small,
  and grows by patch when a new cross-machine concept emerges.
  Most slots are `role = none`. The role tag is the fallback the
  Control-All gesture (§13.1) uses when no exact `id` match exists
  on a target track. A machine author who wants their slot to be
  cross-controllable simply tags it; nothing else is required.
- **Hybrid slot identity.** At runtime, P-Locks and CC routes use
  integer indices for speed. On disk, they serialize as the slot's
  string id. On load, ids are resolved back to indices, so a machine
  author can reorder or insert slots between releases without breaking
  saved patches.
- **MIDI events + ParamFrame at runtime.** Each block, the machine
  receives `(juce::MidiBuffer events, std::span<const float> params,
  juce::AudioBuffer<float>& buffer)`. The MIDI buffer carries
  sequencer-emitted note-on/off (one note-on per fired trig, one
  note-off scheduled `gate` samples later — see §4.6) plus any external
  MIDI passed through. `params` is the resolved `ParamFrame`, sized to
  match the machine's schema.
- **Per-machine voice topology.** A machine declares `maxVoices()`:
  `1` = monophonic with sequencer-managed choke (the baseline
  sampler), `n>1` = polyphonic with self-managed voice stealing,
  `0` = unbounded / MIDI-out style. Track monophony is therefore a
  property of the chosen machine, not a universal sequencer rule.
- **Headless.** Machines ship no UI. The sequencer's Manipulation Zone
  queries each slot's metadata at display time and synthesises the
  controls itself. A machine that wants a custom visualisation (e.g. a
  wavetable view) is out of v0.1 scope.

The 48-slot fixed taxonomy that earlier drafts of this document
described has been retired: it leaked engine-specific assumptions into
the sequencer (slots-per-page = 4, sections = 6, total = 48) without
any corresponding payoff. The MZ still shows 4 slots at a time for
keyboard ergonomics, and the section bar still has 6 buttons; both
paginate within whatever the machine declares.

## 3. The Baseline Sampler Machine

The first engine to inherit `IMachine` is a monophonic sampler
designed for trip-hop / drum-machine workflows:

- **Voice topology.** `maxVoices() = 1`, so the sequencer schedules
  the 1–2 ms choke micro-fade on retrigger; the sampler itself doesn't
  manage voice stealing. Voices ring out under their AHDSR envelope
  until the sequencer-emitted note-off triggers release (see §4.6).
- **Sample pool.** Patches reference samples by id (an index into a
  user-curated pool). The id is one of the sampler's P-lockable slots,
  so a step can switch which sample plays. Sample-pool entries store a
  path plus an `xxHash32` of the PCM payload so projects survive moves
  and renames. The pool is plugin-global; other machines may ignore
  it or, eventually, expose their own resource pools the same way.
- **Slicing.** Samples may carry an array of slice points. The slice
  index is exposed as a P-lockable slot, enabling per-step retrigger
  of slice positions.
- **Pitch handling.** Incoming MIDI note number drives playback rate
  relative to a per-sample root note (stored per pool entry, default
  60). A separate `pitch_offset` slot adds a fine-tune semitone offset
  on top, P-lockable per step for vibrato-style modulation. Pitch
  recording is handled at the sequencer layer (§5.4), not by the
  machine — held-step + note-on writes the note number into the step's
  trig override, which the sequencer then emits as the trig's
  note-on next time the step fires.
- **DSP.** Linear/cubic interpolation for pitch, an AHDSR amplitude
  envelope, and a multi-mode state-variable filter (LP/BP/HP/Notch).

## 4. State Model

### 4.1 Override-ELSE-Base resolution

Every value with both a track-scope default and a step-scope override
follows the same rule:

```
Effective Value = Step Override [if present] ELSE Track Base
```

This applies to two parallel layers:

- **Machine ParamFrame.** Track Base is `Track::baseParams` (one float
  per machine slot). Step Override is `Step::paramOverrides`, a sparse
  map keyed by slot index.
- **Sequencer trig fields.** Track Base carries `defaultNote`,
  `defaultVelocity`, `gateLength`, and `baseCond`. Step Override
  carries `noteOverride`, `velocityOverride`, `gateOverride`, and
  `condition` — each independently optional. When a step's override
  for a given field is absent, the resolver falls through to the
  track-scope default.

There is no "reset" sentinel value to manage; presence/absence in the
override is the only signal. This rule is the only way values reach
either the trig event stream or the engine. Everything in the UI and
ingestion layers ultimately reduces to writing into one of the two
layers.

### 4.2 Polymetric clocking

Tracks are independent. Each carries:

- a step length in [1, 64];
- a clock divider (1, 2, 4, 8, …) against a shared 16th-note grid;
- its own base parameter frame.

The clock itself only advances a shared `samplePosition`; per-track
step indices are computed via modulo arithmetic, so two tracks with
lengths 7 and 16 phase against each other naturally without any
master-bar concept.

### 4.3 Clock sources and sync modes

Lockstep maintains a single internal timeline (bar / beat / tick)
regardless of where the clock comes from. The timeline can be driven
by:

- **Internal** — the plugin's own tempo. Available in standalone and
  plugin contexts.
- **DAW transport** — when running as a plugin, the host's playhead
  drives the timeline.
- **MIDI clock** — when running standalone, an incoming MIDI clock
  stream can drive the timeline. MIDI clock as a plugin input is not
  supported; the DAW is the host's timeline authority.

For external sources two sync modes apply:

- **Locked.** The sequencer's position is slaved to the external
  timeline beat-for-beat.
- **Auto.** The sequencer follows when the external timeline is
  running and degrades distinctly depending on *how* the timeline
  stops:
    - **MIDI clock dropout** → freewheel at the last known tempo;
      resync on clock return. (Live continuity.)
    - **Explicit transport stop** (DAW stop, MIDI Stop / MMC) →
      freeze the sequencer.

When the only source is Internal, no sync mode applies — Locked vs
Auto is a property of "is there an external timeline?" The full
matrix:

| Context     | Source        | Modes available   |
|-------------|---------------|-------------------|
| Plugin      | Internal      | (none)            |
| Plugin      | DAW transport | Locked / Auto     |
| Standalone  | Internal      | (none)            |
| Standalone  | MIDI clock    | Locked / Auto     |

### 4.4 Trig Conditions and Scope

Conditional firing follows the same Override-ELSE-Base model as
parameters: each `Track` carries a `baseCond : TrigCondition` and each
`Step` may override it. When a step's condition is trivial (probability =
100%, iteration = 1:1, no prev-dep), the track's base condition is used
instead. This allows track-level stochastic or phrase-gated behaviour
without per-step authoring.

Three condition types, and where they make sense:

**Probability (1–100%).** Meaningful at both levels. Track-level
probability mutes the entire track stochastically on a per-loop basis —
useful for fills or variation patterns. Step-level probability applies
to individual trigs only.

**Iteration rule (m:n).** Fire on loop m of every n pattern repeats. The
iteration counter is the absolute step counter divided by track length,
so it increments every complete pattern cycle. Both levels are valid
musically, though track-level m:n ("whole track fires on the 1st pass of
every 4") tends to be the more immediately useful mapping. Step-level
m:n addresses individual trig density over multiple loops.

**Previous-step dependency.** Fire only if the preceding step in the
same track's sequence did (or did not) fire. This is inherently a
step-local concept: "the previous step" has no unambiguous meaning at
track level. The `baseCond` field carries a `prevDependency` value for
structural completeness, but it is always rendered as disabled in the
manipulation zone unless a step is held — at which point it activates
and targets `step.condition`. See §6.1 for the COND section layout.

### 4.5 Deterministic evaluation and step-state preview

The probability check is a pure function of `(trackIndex, absoluteStep)`
— the same pair always produces the same result, reproducibly across
plays (`TrigEvaluator::deterministicPercent`). The m:n check is
similarly pure (`absoluteStep / trackLen % denominator`). This means
the fire state of every step visible in the grid can be pre-computed for
the current pattern loop and displayed before the sequencer reaches those
steps:

- **Certain fire** — step cell at full brightness.
- **Certain skip** — step cell significantly dimmed.
- **Probabilistic** — step cell at intermediate brightness proportional
  to its probability, giving a visual density read across the pattern.

Prev-dep steps chain from other steps, so uncertainty propagates: a step
dependent on a 50%-probability predecessor is itself shown as uncertain.
The preview walks the step sequence forward, accumulating certainty, and
rerenders at the start of each pattern loop (not every audio block).

This predictive display is a first-class feature, not an afterthought:
users need to *see* what conditional logic will do before they commit to
a variation.

### 4.6 Trig events: the sequencer→machine note stream

When a step fires, the sequencer translates it into a MIDI event pair
on the track's MIDI buffer:

- **Note-on** at the trigger sample, with note number = effective
  `note` and velocity = effective `velocity` (resolved per §4.1).
- **Note-off** scheduled `gate` samples after note-on, where `gate` is
  the effective `gateLength` for the step. Gate is stored as a count
  of samples derived from the track's clock divider and the global
  tempo, so it scales with tempo automatically. A sentinel value of 0
  suppresses the note-off (machine plays to envelope completion or
  until the next retrigger chokes).

Note-offs that fall after the end of the current block are queued and
re-emitted at the right sample of a future block. Retriggers on a
monophonic machine don't need an explicit note-off: the sequencer
emits a new note-on, the machine's `maxVoices() = 1` declaration tells
the sequencer to insert the choke micro-fade in front of it, and the
new voice supersedes the old.

This model means machines never see "trig" as a concept — they only
see MIDI. A polyphonic machine handles overlapping note-ons naturally;
a MIDI-out machine forwards events verbatim; the sampler interprets
note-on as "start a voice at this pitch, this velocity."

External MIDI (from the host or a hardware controller) is mixed into
the same buffer the sequencer writes to, so the machine sees one
unified event stream. Routing rules (Omni vs Per-Track, focus follows)
determine which track's buffer external MIDI lands on.

### 4.7 Project / Bank / Pattern / Part hierarchy

The container hierarchy follows the Octatrack model, adapted to
variable-schema machines:

```
Project
 └── Bank        × N   (default 8)
      └── Pattern × 16
           └── (references) Part × P-per-bank   (default 4)
```

- **Project.** The top-level container; one Project = one
  plugin-instance state blob. Owns: all banks, the (project-global)
  sample pool, MIDI CC mappings, focus state, channel mode, clock
  settings, and the global Sound Pool (§13.5).
- **Bank.** A namespace of patterns and parts. Banks exist purely to
  give patterns memorable addresses ("Bank A, Pattern 03") that map
  onto physical scope buttons during live performance.
- **Pattern.** The trig grid plus everything that varies *with*
  trigs: per-step trig overrides, per-step P-Locks, per-track step
  length and divider, per-track trig defaults (note/velocity/gate),
  per-track condition base. Each Pattern references exactly one
  Part within its bank.
- **Part.** The per-track *machine state*: machine identity per
  track slot, the machine's resolved base ParamFrame, per-track
  sample references and trig defaults that the machine cares
  about, the optional post-machine FLTR/AMP block state (§14),
  and the pattern-scope mute mask (§13.4). Multiple Patterns in
  a Bank can share one Part, so swapping pattern keeps the same
  sounds; or each Pattern can reference its own Part for total
  kit changes.

The split between Pattern and Part is the same one Octatrack draws,
and it is what lets a project mix heterogeneous machines per track:
the Part stores `track[i].machineId` independently per slot, and
the resolver simply asks "what machine is on track i of this Part?"
at attachment time. The 1-Part-per-Bank simplification was
considered and rejected: live "switch the kit but keep the trigs"
is one of the workflows the hierarchy exists to support.

On disk, machine identity in a Part is a stable string id
(`"lockstep.sampler.v1"`, `"lockstep.midi_out.v1"`, …). On load,
unknown machine ids cause the track to fall back to a stub machine
that preserves its base params and trigs but produces silence,
with a relink/replace dialog offered.

Pattern switching at runtime is a performance gesture, not a state
reload: the next Pattern is queued and the swap happens at the
nearest configured grid boundary (default: end of the longest
playing pattern). Banks have their own queueing semantics — see
the Chain mode in §16.

## 5. Input Layer

All parameter writes travel through the **EditContext** before
landing. The target depends on both the active section type and
whether a step is currently held:

| Active section | No step held | Step held |
|---|---|---|
| Machine section | Track Base (`baseParams`) | Step PLock (`paramOverrides`) |
| Track meta — COND | Track Base Condition (`baseCond`) | Step Condition (`step.condition`) |
| Track meta — TRIG | Track defaults (`defaultNote`, `defaultVelocity`, `gateLength`) | Step trig overrides (`noteOverride`, `velocityOverride`, `gateOverride`) |
| Track meta — TRACK | Track structural fields (length, divider) | (no step-level override) |
| Track meta — GLOBAL | Global APVTS params | (no step-level override) |

This rule is **input-source-agnostic**: encoder, QWERTY, MIDI CC, and
future hardware controls all resolve through the same gate. A CC
arriving while a step is held writes a step-level override on that
step exactly as a UI encoder twist would — regardless of whether the
active section targets machine params or sequencer conditions.

### 5.1 MIDI CC ingestion

Two CC pathways:

- **Absolute CC (0–127) with Soft-Takeover.** The incoming value must
  cross the current internal value before it starts driving the
  parameter, preventing audible zippering when a knob's physical
  position disagrees with the current state.
- **Relative CC (delta arithmetic).** Endless-encoder messages bypass
  takeover and apply +/- integer deltas directly.

### 5.2 CC mapping scopes

Each CC mapping carries a **scope** that determines its target:

- **Global** — fixed, mapping is to a global parameter (e.g. output
  gain, swing). Always agnostic of the focus state.
- **Track[N]** — fixed, mapping is to slot S on track N. Useful when
  a hardware surface dedicates physical knobs to specific tracks.
- **SelectedTrack** — follows the current focus. If focus is Track 3,
  the CC drives slot S on track 3; switch focus and the same CC
  retargets. If focus is Global, the CC is a no-op.

Transport controls (Play / Stop / Record) are not CC-mappable
through this surface; they bind to dedicated MIDI realtime / MMC
messages. Mappings are project-saved.

### 5.3 Focus state and contextual encoders

Selection is a **first-class focus state**, one of `{Global,
Track1..8}`. The focus determines what the contextual encoders
manipulate and what `SelectedTrack`-scoped CCs target.

Four **contextual encoders** parallel the 4-slot Manipulation Zone:
they are configurable CC inputs that always drive the current focus
quadrant. Switching focus reassigns what they manipulate without the
user remapping anything.

### 5.4 MIDI note routing

Two channel modes, exposed as a global setting:

- **Omni → Selected.** All channels accepted; notes route to the
  currently focused track's MIDI buffer. If focus is Global, notes
  are ignored.
- **Per-Track Channel.** MIDI channel N (1..8) hard-routes to track
  N's MIDI buffer. Channels 9..16 are ignored. Live focus changes do
  not affect note routing in this mode.

A note-on routed to a track is appended to that track's MIDI buffer
for the current block and reaches the machine through the standard
event stream (§4.6). The sequencer does not interpret note numbers
itself — they pass through to the machine.

The pitch-recording gesture is the one exception: when a step is held
(EditContext active), an incoming note-on is *consumed* by the
sequencer instead of being passed through. The note number is written
to the held step's `noteOverride` field; the next time that step
fires, it will emit a note-on with that number. For a monophonic
machine (`maxVoices() = 1`) the last note-on within the hold wins;
for a polyphonic machine the overrides accumulate into a chord (the
exact encoding for chord storage is deferred — see §12).

**Key-as-PLock for drum patterns (record arm, M7).** With record arm
engaged, individual note keys write P-Lock values to a chosen machine
slot — typically the sampler's `sample_id` slot — derived from the
key index, e.g. routing different pad keys to different sample IDs
across one drum track. This is a recording-mode-specific gesture, not
a live editing gesture, and the target slot is a per-track config
choice rather than a hardcoded behaviour. See M7.4.

### 5.5 QWERTY overlay

The QWERTY overlay translates raw scancodes into actions:

- **Bottom two rows (A–K, Z–,):** steps 1–16 in the 2×8 grid.
- **Top row (3–8):** section selection — keys select machine sections
  and cycle pages within them. With Shift (key 1) held, the same keys
  select track meta sections.
- **Key 2:** navigation (grid cursor up).
- **Space:** Play / Stop.
- **R / T / Y / U / I:** Record arm / Tap tempo / Copy / Paste / Clear.

The same physical keys will be mirrored 1:1 by the hardware
controller's mechanical grid.

## 6. UI Philosophy

The UI is built around three regions whose layout matches a future
hardware surface 1:1:

- **The Manipulation Zone.** Exactly 4 primary parameters visible at
  any time. Their labels, ranges, and visualisations come from the
  active Machine's metadata. When a section contains more than 4
  slots, repeated section-key presses cycle pages of 4 within it.
- **The Section Bar.** Six section buttons (keys `3`–`8`) with two
  layers accessed via Shift (`1`):

  *Machine sections (no Shift):* the six section buttons follow a
  **canonical-reserved + machine-extension** rule. Sections 1–6 on
  the section bar are reserved for a fixed canonical taxonomy:

  | Key | Canonical section | Typical contents |
  |---|---|---|
  | 3 | **TRIG** | Trig defaults: note, velocity, gate. (Also reachable as a track-meta section — see below.) |
  | 4 | **SRC** | Sound source: sampler controls, oscillator controls, MIDI program/channel for MIDI-out machines. |
  | 5 | **FLTR** | Filter — usually the post-machine FLTR block (§14), but machines may opt out and present their own. |
  | 6 | **AMP** | Amplitude envelope — usually the post-machine AMP block (§14). |
  | 7 | **LFO** | Modulation: LFO rate/depth/shape/destination. |
  | 8 | **FX** | Per-track FX: drive, bit-reduction, send levels. |

  A machine that has nothing to fill a canonical section leaves it
  empty (button dimmed). A machine that needs more than the canonical
  six declares *extension sections* on additional section-bar pages,
  reached by repeated press of the same section key (which cycles
  through both the canonical pages-within-section and the machine's
  extension pages). Section labels are declared by the machine but
  must match the canonical title where one applies — this discipline
  is what makes "hold FLTR section + COPY" mean the same thing across
  every machine type.

  *Track meta sections (Shift held):* sequencer and structural controls
  for the current track, machine-independent. The six meta slots have
  a fixed layout:

  | Shift + key | Meta section | Contents |
  |---|---|---|
  | 3 | COND | Trig conditions (prob, m:n, prev-dep) |
  | 4 | TRIG  | Default note, default velocity, gate length |
  | 5 | TRACK | Length, divider |
  | 6–7 | — | Reserved for future use |
  | 8 | GLOBAL | Output gain, sync mode, clock settings |

  Each section button cell shows its machine-section label at the top
  and its track-meta label at the bottom. The active layer determines
  which label renders prominently and which colour highlights the cell
  (teal for machine sections, amber for track meta).
- **The Step Grid.** A 2×8 visual matrix mirroring the bottom two
  QWERTY rows. Sequences longer than 16 paginate via dedicated keys.
  Cells carry a condition-state preview (§4.5): certain-fire,
  certain-skip, and probabilistic states are each rendered distinctly
  so conditional logic is visible at a glance without running the
  sequencer.

### 6.1 COND meta section — manipulation zone layout

When the COND track meta section is active, the four encoder slots
show the trig condition controls for the current track:

```
[ Prob %  ] [ m:n Num ] [ m:n Den ] [ Prev-dep ]
```

- **No step held:** values read from `Track::baseCond`. Prev-dep is
  rendered but disabled (dimmed, not writeable). Prob and m:n govern
  the whole track.
- **Step held:** all four controls become active and read from
  `Step::condition` (with Override-ELSE-Base fallback to `baseCond`).
  Prev-dep is now writeable. The EditContext routes writes to
  `step.condition` exactly as it routes machine-param writes to
  `step.overrides`.

This is the canonical editing surface for conditional trigs: users
can set a track-wide default condition (no hold needed) and then
override individual steps by holding them — the same gesture as
P-Locking a machine parameter.

The TRIG meta section (Shift+4) follows the same pattern for the
sequencer-emitted note stream: `[Note] [Velocity] [Gate]` plus one
spare slot. No step held → reads/writes `Track::defaultNote` /
`defaultVelocity` / `gateLength`. Step held → reads/writes
`step.noteOverride` / `velocityOverride` / `gateOverride` (with
Override-ELSE-Base fallback). This is the keyboard-and-MZ surface for
melodic step entry when the user prefers explicit numeric entry over
the live pitch-recording gesture (§5.4).

The aim is that an experienced user holds an editing context (a step
held, a section selected) and resolves all parameter changes in the
Manipulation Zone without ever leaving the keyboard.

### 6.2 Step Grid / QWERTY overlay display modes

The Step Grid and Section Bar can render in one of three overlay modes,
selectable as a persistent user preference (stored in global settings,
not in the project state):

| Mode | Name | Description |
|---|---|---|
| 0 | **Staggered** | Renders a realistic keyboard silhouette with the physical row stagger of a standard QWERTY layout. UI elements (step cells, section buttons) are positioned to sit on top of their corresponding keys. Key legends (letters, numbers) are visible beneath the UI layer. Intended for new users building muscle memory: the on-screen layout is an exact 1:1 map of the physical keyboard in front of them. |
| 1 | **Ortholinear** | Same keys and UI elements, but arranged in a uniform grid with no row offset. Key legends remain visible. Useful once muscle memory is established: the layout is compact and symmetric, and the legends still provide reference for occasional use without a hardware surface. |
| 2 | **Clean** | Ortholinear grid with no key legends shown — only the UI elements. Intended for hardware surface users, where the controller has no printed legends and showing them in software adds no value. |

The three modes share an identical key→action mapping (`QwertyOverlay::resolve` is unaffected); only the visual rendering changes.

The active mode is exposed as a right-click / settings option on the Step Grid or via a small mode-cycle button in the global UI chrome. It is not P-lockable and does not affect playback or MIDI routing.

## 7. Host Serialization

Plugin state carries:

- the APVTS (host-visible parameters);
- the full Sequence (tracks, base frames, P-Lock maps, trig defaults
  and per-step trig overrides, per-track machine identity);
- sample-pool **references** (path + `xxHash32`), never PCM payloads.

P-Lock and CC-mapping serialization uses the slot's stable string id,
not its runtime integer index. On load, ids are resolved against the
current schema of the named machine; unknown ids are dropped with a
log entry. This lets a machine author insert or reorder slots between
releases without invalidating saved patches.

Excluding raw PCM is deliberate: it keeps DAW auto-saves cheap and
prevents the audio thread from blocking on background save activity.
The hash defends against silent file substitution.

A `kCurrentVersion` constant on `PluginState` provides a forward
upgrade path. v0 ships a minimal serializer (APVTS only); the full
payload lands when P-Locks become first-class (M7).

## 8. Voice Lifecycle and Choke

For machines that declare `maxVoices() = 1`, the sequencer manages
voice lifecycle via a 1–2 ms choke micro-fade:

- Before emitting a note-on for a retrigger, the sequencer asks the
  machine to fade out its current voice over 1–2 ms, then emits the
  new note-on at the appropriate sample.
- Voices that finish their envelope without being stolen are reclaimed
  by the machine internally; the sequencer doesn't track this.
- Cross-track triggers do not interact; each track is its own choke
  group.

For machines that declare `maxVoices() > 1`, the sequencer emits
note-ons (and scheduled note-offs) without choke; the machine handles
its own voice stealing if note-ons exceed `maxVoices()`. The
sequencer's behaviour is therefore uniform — it always emits MIDI —
and the machine's voice declaration determines whether choke fades
are inserted in front of retriggers.

## 9. Future direction: wrapping arbitrary plugins

An earlier draft of this document called out a Phase 3 milestone for
hosting CLAP/VST3 plugins as Machines, with a 48-parameter contract
they would need to satisfy. With the variable-schema `IMachine`
boundary now in place (§2), wrapping an arbitrary plugin reduces to
writing one specific `IMachine` subclass — a `WrapperMachine` that
loads the host plugin via `juce::AudioPluginFormatManager`, exposes
its parameter tree as the schema, and forwards MIDI and audio across.

This is no longer a separate phase; it is one possible machine among
many, deferred until there is a demonstrated need. No core sequencer
changes are required to support it.

## 10. Phase 4 — Open-Source Hardware Companion

A consolidated, gig-ready control surface mirrors the Phase 1 software
UI 1:1. Headline points (full hardware design out of scope for this
document):

- A 256×64 SPI OLED flanked by 4 endless push-encoders for the
  Manipulation Zone.
- A 32-key transparent mechanical matrix (8×4): top row pages, second
  row navigation/transport, bottom 2×8 the trig grid.
- An RP2040-class MCU enumerates as a USB HID keyboard *and* a USB
  MIDI device simultaneously: scancodes drive the deterministic
  QWERTY mapping; MIDI carries CC and SysEx.
- Bidirectional SysEx for LED state, P-Lock indicators, and OLED
  rendering data, so the hardware is a self-sufficient face for the
  plugin during live performance.

## 11. v0.1 Scope

v0.1 is the "first usable" milestone. It includes:

- The single sampler Machine inheriting `IMachine` end-to-end, driven
  by sequencer-emitted MIDI events.
- Polymetric clocking, multi-track sequencing, base parameter frames.
- P-Lock editing model with the Override-ELSE-Base resolver, applied
  to both machine ParamFrames and sequencer-scope trig fields
  (note / velocity / gate).
- Trig conditions (probability, 1:N, prev-dep) honoured at runtime.
- MIDI ingestion: abs/rel CC with soft-takeover, scoped mappings
  (Master / Track[N] / SelectedTrack), Omni and Per-Track channel
  modes, four contextual encoders, MIDI clock + sync modes in
  standalone, edit-context routing of all input sources including
  the note-on pitch recording gesture (held-step + key writes the
  step's `noteOverride`).
- QWERTY overlay + Manipulation Zone + Section Bar + Step Grid wired
  up, with section/page taxonomy driven by the active machine's
  declared schema.
- Per-track gate length scheduling (sequencer emits note-off at
  `triggerSample + gate_samples`).
- Pattern recording (live note/CC capture into trigs and P-Locks).
- State serialization including P-Lock data, trig overrides, and
  sample references; slot identity stored as stable string ids.

The hardware companion (§10) and arbitrary-plugin wrapping (§9) are
out of v0.1 scope; the architecture is built to absorb them without
restructuring.

## 12. Open Questions / Future Work

- **Chord / polyphonic step entry.** The pitch-recording gesture
  (§5.4) writes a single `noteOverride` per step, which suits
  monophonic machines (last note wins). For polyphonic machines, a
  step should be able to fire a chord. The on-disk encoding is
  deferred — likely a `noteOverride` widened to a small list — until
  the first polyphonic machine is designed. The gesture itself is
  already correct.
- **MPE-aware machines.** The boundary is `(MidiBuffer, ParamFrame)`,
  so per-event expression already has a transport (MIDI poly-pressure,
  pitch-bend per channel). Sequencer support for authoring expression
  per step is a separate UX project.
- **Per-machine resource pools.** The sample pool is plugin-global
  today. A future synth machine might want its own wavetable pool,
  IR pool, etc. Generalising "resource pool" across machines is
  deferred.
- Bundled sample library / factory-patch shape.
- Final product name to replace "Lockstep".

## 13. Performance Features

The features in this section are the heart of the live-performance
ergonomics Lockstep inherits from the Digitakt family. They share a
single underlying grammar: hold a **scope** key (a key/button that
declares "what am I about to operate on?"), then press a **verb** key
(record / stop / play / yes / no), or — for live tweaks — turn an
encoder. The grammar is the same regardless of whether the scope is
a step, a track, a section, or a pattern.

The scope buttons are persistent first-class modifiers, complementing
the existing `Shift` and `Func`:

| Scope button | Selects | Held alongside |
|---|---|---|
| `Func` | Modifier for verb keys (Yes/No/Record/Stop/Play). | The verb. |
| `Track` | One or more track slots. | Verb, or an encoder. |
| `Pattern` | One pattern (or, in chain mode, several). | Verb, or a pattern key. |
| `Trig` (hold a step) | The held step(s); multi-step hold is allowed. | Verb, encoder, or note key. |
| Section key (3–8) | The held section's slots. | Verb. |
| `Mute` | The mute mask. | Track keys. |
| `Fill` | "While I'm holding this, fill conditions evaluate true." | (no verb needed — it's the state itself). |

The verb set is small and uniform:

| Verb | QWERTY | Meaning |
|---|---|---|
| Record | (Rec) | Capture the scope into the clipboard. |
| Stop | (Stop) | Clear the scope. |
| Play | (Play) | Paste the clipboard into the scope. |
| Yes / No | (Yes / No) | Push / pop a checkpoint (with `Func`); confirm / cancel modal dialogs. |

The same grammar drives §13.2 Copy/Paste/Clear, §13.3 Performance
Mutes, and the Checkpoint stack in §13.6. The verbs never mean
different things in different scopes — only the scope changes.

### 13.1 Control-All

Holding the `Track` scope (with no specific track selected) promotes
the next parameter edit to a *broadcast*: the new value is written to
**every** track that exposes a matching slot. Resolution is
**id-primary, role-fallback**:

1. Build the target set `T` = all tracks in the active Part.
2. For each track `t ∈ T`, look up the `ParamSpec` on `t.machine`
   whose `id` equals the source spec's id. If found, use that slot.
3. Otherwise, look up the slot whose `role` equals the source spec's
   `role` (if both are non-`none`). If found, use that slot.
4. Otherwise, the track is skipped (and visibly dimmed in the
   broadcast indicator).

The edit obeys the EditContext rule like any other write: if a step
is held, it lands as a P-Lock on that step on every matching track;
if no step is held, it updates each track's base. Control-All is
the canonical way to perform sweeping filter opens, drive ramps,
or amp-decay tightens across a kit.

The role-fallback half is what makes Control-All useful across
heterogeneous machines (Sampler + FM synth + MIDI-out): only the
`role`-tagged slots participate. A machine author opts in by tagging.

### 13.2 Copy / Paste / Clear

A single uniform grammar: **hold scope, press verb**.

| Gesture | Effect |
|---|---|
| `Trig` (hold 1+ steps) + Record | Copy those steps (trigs + condition + P-Locks). |
| `Trig` + Play | Paste clipboard onto the held steps. |
| `Trig` + Stop | Clear those steps' overrides (trig + P-Locks). |
| `Section` key + Record | Copy all of that section's params (base + P-Locks across all steps). |
| `Section` key + Play | Paste section onto current track. |
| `Section` key + Stop | Reset section to default. |
| `Track` (specific track) + Record | Copy the whole track within the Part. |
| `Track` + Play | Paste track. |
| `Track` + Stop | Clear track. |
| `Pattern` + Record | Copy the whole pattern (within or across banks). |
| `Pattern` + Play | Paste pattern. |
| `Pattern` + Stop | Clear pattern (back to empty). |

Multi-step holds copy a contiguous *or* discontinuous group: the
clipboard preserves the relative offsets and pastes them back over
the destination held step(s). Pasting a 1-step clipboard over N
held steps replicates. Pasting an N-step clipboard over 1 held step
unrolls forward from that step.

The clipboard is in-memory only (not persisted) and typed: a step
clipboard cannot be pasted into a section scope, a section clipboard
cannot be pasted into a pattern. The UI shows the clipboard type as
a small chip in the transport bar.

### 13.3 Fills

A momentary modifier and a per-step condition flag:

- The **Fill** scope button is a live momentary modifier. While
  held, the sequencer's evaluation of every step's condition treats
  `fill` as true.
- Each step's `TrigCondition` gains a `fillRule` enum:
  `Always` (default — ignore fill state), `OnlyFill` (fire **only**
  while Fill is held), `NeverFill` (fire **only** while Fill is
  *not* held).
- Combined with probability and m:n, fill rule is one more
  conjunction: a step fires iff probability passes **and** m:n
  matches **and** fillRule is satisfied by the current fill state.
- Fill state, like all conditions, is deterministic and
  pre-computable for preview (§4.5) — the grid renders fill-only
  cells in a distinct dimmer/brighter colour while fill is held to
  let the user see what's about to fire.

The Fill button can itself be a P-Locked or chain-triggered state in
later milestones (e.g. "auto-fill on the last bar before pattern
change"), but the manual hold-Fill gesture is the baseline.

### 13.4 Performance Mutes

Two mute layers, both first-class:

- **Global mute (per-track).** Lives in the Project, not in any
  Pattern or Part. Surviving across pattern changes makes it the
  natural target for live "drop the drums" performance gestures.
  Entered by `Func + Track` (Digitakt convention).
- **Pattern mute (per-track).** Lives in the Pattern. Saved with the
  pattern and recalled on pattern load — useful for arrangement-style
  pattern variants without duplicating notes. Entered by
  `Func + double-tap Track`.

Mute mask resolution: a track is muted at runtime iff
`global.muted[i] || pattern.muted[i]`. Muting is non-destructive (no
note-offs are emitted); a muted track simply has its emitted trigs
suppressed at the sequencer→machine MIDI boundary, after condition
evaluation but before machine dispatch. External MIDI input bypasses
the mute (the user can still play a muted track manually).

**Multi-select on release.** Inside either mute mode, holding `Func`
defers the mute toggle: every track key pressed while `Func` is held
is collected, and on `Func` release every selected track is toggled
atomically. This is the live-performance "kill four tracks at once"
gesture. Without `Func`, each track key toggles immediately.

A **solo** layer is deliberately not added: solo equals "mute
everything else," which the multi-select gesture already encodes.

### 13.5 Alternate Trig Modes

The trig grid (the 2×8 step row) is the densest physical surface on
the controller; reusing it for non-step roles is a major workflow
multiplier. Modes are entered by a dedicated mode chord (precise
chord deferred to the UI rethink milestone) and exit on release of
that chord — the trig grid becomes a *modal* surface, not a
permanently-reassigned one.

- **Keyboard mode.** The 16 trig keys map to 16 chromatic semitones,
  with a configurable root note. Pressing a key emits a note-on for
  the focused track's machine — same EditContext rules apply, so
  holding a step + a keyboard-mode key writes that step's
  `noteOverride`. This is the canonical melodic entry mode for
  pitched machines (synth, MIDI-out, pitched-sampler).
- **Retrig / Slice mode.** The 16 trig keys, while held, act as a
  per-tick retrigger (configurable rate: 1/16, 1/32, 1/48, 1/96).
  For sampler tracks with slice data, an alternate sub-mode binds
  the 16 keys to the first 16 slice indices, so the trig row plays
  the sample's slices like pads. Retrig is purely a playback
  gesture; it does not write into the pattern unless record-arm is
  active, in which case the retriggers are quantised and captured
  as P-Locks of the same rate slot.
- **Sound Pool mode.** A *Sound Pool* is a Project-scope library of
  saved per-track "sounds" — i.e. a (machineId, base ParamFrame,
  sample-pool refs) bundle. In Sound Pool mode, the 16 trig keys
  page through the pool and, while held, swap the focused track's
  current sound to that pool entry. Pressing a key with record-arm
  active writes a `sound_id` P-Lock onto the next-emitted step (or,
  with a step held, onto that step), so a single track can fire a
  different sound per step. This is the Digitakt "preset pool"
  workflow translated onto the Octatrack Part model: the Pool lives
  at Project scope, individual Parts pull entries out of it.

All three modes share the property that they are *playback / capture*
gestures, not destructive edits — leaving the mode never alters the
authored pattern.

### 13.6 Checkpoint Stack

A bounded LIFO stack of Pattern + Part snapshots, scoped per pattern,
capped at 8 entries (configurable). Two gestures:

- `Func + Yes` — push the current Pattern+Part state onto the
  pattern's checkpoint stack.
- `Func + No` — pop the top of the stack and restore.

Behaviour notes:

- The stack is RAM-only and does not persist across project save /
  reload. (This is intentional: checkpoints are a "scratch take"
  tool; the project save is the canonical state.)
- Pushing while the stack is full evicts the **oldest** entry,
  preserving the most recent 8.
- Pop restores Pattern + Part *only* — it never replaces the active
  Bank, the Project's global mute mask, or the Sound Pool. The
  checkpoint is a pattern-scoped scratchpad.
- A small UI chip in the transport bar shows the stack depth so the
  user can see how many undos remain.

Checkpoints emerge naturally as a live performance undo: experiment
with a destructive copy/paste or a Control-All sweep, then revert
with `Func + No` if it didn't land. The stack depth gives a few
levels of "two-mistakes-deep" recovery without bloating into a full
DAW-style history.

## 14. Signal Path and Post-Machine FLTR / AMP

The sequencer-side signal path for an internal-audio track is:

```
machine.process() → [FLTR] → [AMP] → [FX send taps] → track sum → master
```

Where `[FLTR]` and `[AMP]` are sequencer-side DSP blocks owned by the
track (not the machine):

- **FLTR.** A multi-mode state-variable filter (LP / BP / HP / Notch),
  parameterised by Cutoff, Resonance, Drive, and an
  Envelope-amount-to-cutoff that follows AMP's envelope.
- **AMP.** The track's amplitude envelope (AHDSR), plus Pan and
  Level. AMP responds to the sequencer-emitted note-on/off pair —
  this is what gives every track a consistent envelope feel
  regardless of which machine it hosts.

This is the Elektron model: a machine focuses on producing raw
audio at unity gain; the surrounding FLTR + AMP is uniform. The
canonical FLTR (key 5) and AMP (key 6) section buttons drive these
blocks regardless of which machine the track hosts.

Machines may opt out:

- `IMachine::hasInternalFilter()` returning `true` causes the
  sequencer to bypass the FLTR block; the FLTR section button on
  that track is repurposed as a passthrough to the machine's
  internal filter slots.
- `IMachine::hasInternalAmp()` similarly bypasses AMP.

Opt-out is rare and reserved for analog-emulation engines where
filter and envelope are tightly coupled to the sound (e.g. an
Analog Four-style synth). Most machines accept the default.

For a MIDI-out track (§15), both FLTR and AMP are bypassed
implicitly — there is no audio to filter or amplitude-shape — and
the FLTR/AMP section buttons are repurposed to expose
MIDI-output-relevant CC banks for that track instead.

## 15. MIDI-out Machine (First-Class)

`MidiOutMachine` is a built-in `IMachine` subclass that translates
sequencer-emitted MIDI events into output on a configured
destination (a JUCE MIDI output device, a host MIDI bus, or a
plugin-side MIDI output port). It is a *peer* of the internal audio
machines, not an afterthought.

Schema highlights:

- `dest` — destination device / port (stepped slot, declared at
  machine attachment time, persisted by id).
- `channel` — 1..16. P-lockable per step so a single track can
  drive multiple channels.
- `program` — bank+program change, optionally emitted on pattern
  start.
- 16 generic `cc[0..15]` slots, each with a per-track-configurable
  CC number and label. These are the MIDI-out equivalent of a
  synth's parameter knobs — P-lockable per step, broadcastable via
  Control-All when `role`-tagged.
- An optional `cc_name_table` resource: a per-destination JSON file
  mapping CC numbers to human-readable names (e.g. "DN_FLT_CUT" =
  CC 74 on Digitone), which the UI surfaces in place of "cc[3]"
  labels.

A MIDI-out track participates in all performance features
identically: Control-All can sweep the same CC across every MIDI-out
track in the Part; the Sound Pool can store and recall full
(channel + program + CC-mapping + base params) bundles per
destination device; Fills, Mutes, Copy/Paste, and Checkpoints apply
unchanged.

When the host transport stops, the MIDI-out machine emits an
All-Notes-Off + Reset-All-Controllers on each channel it has been
driving. This prevents stuck notes on external hardware.

`MidiOutMachine` reports `maxVoices() = 0` (unbounded), which
disables sequencer-managed choke entirely — note-offs are scheduled
by gate length as for any track, and overlapping note-ons are
forwarded verbatim.

## 16. Banks and Chain Mode

Banks (§4.7) hold up to 16 patterns each, addressed `A01..A16` and
so on. The default project has 8 banks. Bank selection is a
two-key gesture: `Pattern + bank_letter`, then a step-row key for
the pattern within the bank.

**Pattern switching** at runtime is *queued*, not immediate:

- `Pattern + <stepkey>` queues the named pattern to start at the
  next configured grid boundary (default: end of the longest
  playing track in the current pattern; configurable to bar / 2 bars
  / pattern-end).
- The transport bar shows the queued pattern as a "next" chip.
- A second `Pattern + <stepkey>` before the boundary replaces the
  queued pattern (the user can "change their mind").
- `Pattern + Stop` cancels the queued change.

**Chain mode** extends this to a *queue of upcoming changes*. While
holding `Pattern + Chain` (chord TBD in UI rethink), each pattern
key pressed *appends* to the chain rather than replacing the queued
one. The chain then plays in order, each pattern occupying one
"chain slot" worth of time (default: one pattern length).

Chain semantics deliberately stay light:

- The chain is RAM-only; it is not part of the project save. Chains
  are performance plans, not arrangements.
- A chain has no repeat count per slot in v1 (one slot = one
  play-through); a per-slot repeat is a likely v1.1 addition.
- The chain loops by default; a single-shot mode is a toggle.
- The chain is interruptible at any time by a plain `Pattern + key`
  — that discards the rest of the chain and queues just the new
  pattern.

The Pyramid's full arrangement / song view is intentionally out of
scope. The chain is the entire song-level surface, and it exists to
let the performer plan two or three pattern changes ahead while
their hands are busy with other modifiers.

## 17. Scenes and the Crossfader

The Octatrack's crossfader is the one continuous-axis performance
control in the design. It is not a duplicate of the Checkpoint
stack (§13.6): checkpoints are *discrete, whole-state, stack-shaped*
("snapshot now, jump back later"); the crossfader is *continuous,
selective, two-pole* ("morph smoothly between two curated parameter
sets"). They complement each other — a typical workflow is to
checkpoint a pattern before authoring a Scene A/B pair, then perform
the live morph itself with the fader.

### 17.1 Data model

Scenes live on the **Part** (not the Pattern). A Part carries a pair
of scenes:

```
Part.sceneA : map<(trackIdx, slotIdx) -> float>
Part.sceneB : map<(trackIdx, slotIdx) -> float>
```

Each scene is a sparse map covering whichever (track, slot) pairs the
user has assigned to that scene. The map shape is identical to a
P-Lock map but addressed across the whole Part rather than per-step.

Attaching scenes to the Part (rather than the Pattern) means
patterns sharing a Part also share scenes — consistent with the
"swap pattern, keep the kit" gesture (DESIGN §4.7). Authoring a
different scene pair requires forking the Part, same as authoring
different base params.

### 17.2 Runtime state and resolution

The fader value `f ∈ [0, 1]` is RAM-only runtime state (it does not
serialize with the Part — it is a controller axis, like a held key,
not a stored field).

Resolution order, per (track, slot):

```
effective(track, slot, step) =
    step.pLock[slot]                                  // wins if present
  ∨ lerp(sceneA_val, sceneB_val, f) [if in any scene] // otherwise mix
  ∨ track.baseParams[slot]                            // otherwise base
```

where `sceneA_val = Part.sceneA[(track,slot)] ?? track.baseParams[slot]`
(and likewise for B). A slot not present in either scene is
unaffected by the fader; its base value resolves directly.

Stepped slots (those with `ParamSpec::stepped == true`) snap at
`f = 0.5` instead of lerping. This includes MIDI-out
`channel` and `program` slots (§15) — they are scene-assignable
but morph discretely, with a clean note-off on the previous channel
emitted at the snap point to prevent stuck notes downstream.

P-Locks still win at the step level: a step that locks a slot
bypasses the fader on that slot for that step. This makes scenes
non-destructive to authored intent at the step layer.

### 17.3 Assignment gesture

The scene assignment gesture follows the scope+verb grammar (§13):

- Hold `Scene A` (a scope button) + turn an encoder → adds the
  current value of that slot to Scene A's map.
- Hold `Scene B` + turn an encoder → adds to Scene B's map.
- Hold `Scene A` + press the trig-`Stop` verb on an assigned slot
  → removes it from Scene A.
- Hold both `Scene A` and `Scene B` and turn → assigns the same
  value to both scenes (rarely useful by itself, but the natural
  "make this the rest position" gesture).

The MZ renders assigned slots with a small A / B indicator and the
two captured endpoint values. Slots in both scenes morph; slots in
only one effectively go from "base" to "scene value" as the fader
crosses (because the missing side falls back to base).

### 17.4 MIDI-out parity

Scenes apply identically to MIDI-out tracks (§15): the generic
`cc[0..15]` slots are continuous and lerp smoothly; `channel` and
`program` are stepped and snap. The live-morph use case
("crossfade between two Digitone patches by morphing 16 CCs at
once") is one of the headline workflows that justifies treating
MIDI-out as a first-class machine.

### 17.5 Hardware and software surfaces

The hardware controller carries one physical fader. In software,
the same axis appears as a vertical slider in the transport chrome,
mouse-draggable, plus an automatic CC mapping (default CC number
TBD; user-remappable) so any external surface can drive it.

There is no QWERTY mapping for the fader axis: continuous gestures
on a typing keyboard are a poor fit and would only invite muscle
memory the hardware can't satisfy. The QWERTY mapping for
**Scene A** / **Scene B** scope buttons exists (for the
assignment gesture), but the fader's continuous value comes from
mouse / CC / hardware fader only — consistent with pillar 1
("hardware = fewer-key QWERTY"): the QWERTY layer omits the one
axis the hardware can't reduce to a button.

## 18. Roadmap Reference

The milestone plan and progress checkboxes live in `ROADMAP.md`.
After the current M8 (state serialization), the next milestones
expand on the performance vision in this document — UI/input
rethink, the Project/Bank/Pattern/Part hierarchy, the performance
modifier cluster, canonical sections + post-machine FLTR/AMP, the
MIDI-out machine, alternate trig modes, and the machine catalogue
expansion (FM, VA, DrumSynth, Slicer).
