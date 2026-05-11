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

Two non-negotiable design pillars:

1. **Realtime usability without the mouse.** The full editing workflow
   is reachable from the QWERTY keyboard alone; an open-source hardware
   controller will later mirror that workflow 1:1.
2. **A strict DSP encapsulation boundary.** Sound-generating engines —
   "Machines" — are isolated behind a tightly scoped interface so that
   a native engine and a third-party sub-plugin look identical to the
   sequencer core.

The project is GPLv3, cross-platform (Linux, macOS, Windows), and
compiles to CLAP and VST3 from a single C++/JUCE codebase.

## 2. The Encapsulation Boundary (`IMachine`)

The sequencer talks to every sound engine through a single C++ virtual
interface, `lockstep::IMachine`. The interface is deliberately narrow:

- **Parameter Schema Contract.** Exactly **48 parameter slots**, laid
  out as **12 logical pages × 4 parameters per page**. Slot indices
  (0..47) are the *only* thing the sequencer knows about; the engine
  attaches semantics. This keeps the P-Lock storage and editing model
  completely engine-agnostic.
- **Headless Operation.** Machines ship no UI. The sequencer's
  Manipulation Zone queries each slot's metadata (label, range,
  default, stepped/continuous) at display time and synthesises the
  controls itself.
- **State Ingestion.** At the start of each audio block the sequencer
  resolves a `ParamFrame` (a `std::array<float, 48>`) per active track
  and hands it across the boundary. The engine renders additively into
  the supplied output buffer.

A baseline sampler (§3) implements `IMachine` natively in v0. Phase 3
generalises this to a `juce::AudioPluginFormatManager`-backed sub-host
(§9) that can load arbitrary CLAP/VST3 plugins as Machines, provided
they honour the same 48-slot contract.

## 3. The Baseline Sampler Machine

The first engine to satisfy `IMachine` is a robust monophonic sampler
designed for trip-hop / drum-machine workflows:

- **Track Monophony.** Each track has exactly one ringing voice at a
  time. New triggers on the same track choke the previous voice via a
  short (1–2 ms) micro-fade, never an instantaneous cut. Voices ring
  out under their AHDSR envelope; they are not bound to MIDI note-off.
- **Sample Pool.** Patches reference samples by id (an index into a
  user-curated pool); the id is one of the 48 P-lockable parameters,
  so a step can switch which sample plays. Sample-pool entries store a
  path plus an `xxHash32` of the PCM payload so projects survive moves
  and renames.
- **Slicing.** Samples may carry an array of slice points. The slice
  index is exposed as a P-lockable parameter, enabling per-step
  retrigger of slice positions.
- **Melodic pitch.** A dedicated *note* slot (MIDI note number 0–127)
  determines playback rate relative to a per-sample root note. This
  makes the pitch-recording gesture (§5.4) first-class: holding a step
  and playing a key P-Locks the note slot of that step to the key's
  MIDI pitch. The existing semitone-offset slot becomes a fine-tune
  layer on top of the note slot. Root note is stored per sample-pool
  entry and defaults to 60 (middle C).
- **Gate length.** A *gate* slot (0 ms – full step duration, stored in
  ms) sets the point at which the voice transitions to its AHDSR
  release phase, independent of choke or retrigger. Gate = 0 means the
  release is triggered only by a subsequent trigger on the same track
  (the current behaviour). Gate > 0 imposes an explicit timed release,
  enabling staccato and legato articulations per step via P-Lock.
- **DSP.** Linear/cubic interpolation for pitch, an AHDSR amplitude
  envelope, and a multi-mode state-variable filter (LP/BP/HP/Notch).

## 4. State Model

### 4.1 Override-ELSE-Base resolution

Every parameter has two storage layers:

```
Effective Value = Step Override [if present] ELSE Track Base
```

The Track Base is the patch-level default for that slot. The Step
Override is a sparse, per-step entry stored in a `PLock` map keyed by
slot index. When a step has no override for a slot, the resolver falls
through to the base — there is no "reset" sentinel value to manage.

This rule is the only way values reach the engine. Everything in the UI
and ingestion layers ultimately reduces to writing into one of the two
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

## 5. Input Layer

All parameter writes travel through the **EditContext** before
landing. The target depends on both the active section type and
whether a step is currently held:

| Active section | No step held | Step held |
|---|---|---|
| Machine section | Track Base (`baseParams`) | Step PLock (`overrides`) |
| Track meta — COND | Track Base Condition (`baseCond`) | Step Condition (`step.condition`) |
| Track meta — TRACK | Track structural fields | (no step-level override) |
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

- **Omni → Selected.** All channels accepted; notes trigger the
  currently focused track. If focus is Master, notes are ignored.
- **Per-Track Channel.** MIDI channel N (1..8) hard-routes to track
  N. Channels 9..16 are ignored. Live focus changes do not affect
  note routing in this mode.

A note-on triggers the destination track's machine.

Two distinct note-driven P-Lock gestures exist, separated by intent
and by whether record arm is active:

**Pitch recording gesture (live, no record arm required).** When a
step is held (EditContext active), a note-on from any source writes
the note's MIDI pitch to the pitch slot of that step as a P-Lock.
This is the same single-input-gate rule applied to note events — no
different in principle from turning an encoder while holding a step.
For a monophonic machine (e.g. the baseline sampler), the last
note-on received within the hold gesture wins. For a polyphonic
machine, multiple simultaneous notes can map onto a chord-capable
slot set; the exact encoding is machine-defined. This is the primary
mechanism for melodic step entry and for building chord patterns when
polyphonic machines are available (§12).

**Key-as-PLock for drum patterns (record arm, M7).** With record arm
engaged, individual note keys write P-Lock values — not pitches but
*parameter values* — derived from the key index, e.g. routing
different pad keys to different sample IDs across one drum track.
This is a recording-mode-specific gesture, not a live editing
gesture. See M7.4.

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
  active Machine's metadata.
- **The Section Bar.** Six section buttons (keys `3`–`8`) with two
  layers accessed via Shift (`1`):

  *Machine sections (no Shift):* group the 48 parameter slots into
  machine-defined sections, each with 1–n pages of 4. Re-pressing a
  key cycles pages within that section. Section labels and page counts
  are declared by the machine, not hardcoded in the UI.

  *Track meta sections (Shift held):* sequencer and structural controls
  for the current track, machine-independent. The six meta slots have a
  fixed layout:

  | Shift + key | Meta section | Contents |
  |---|---|---|
  | 3 | COND | Trig conditions (prob, m:n, prev-dep) |
  | 4 | TRACK | Length, divider, gate default |
  | 5–7 | — | Reserved for future use |
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
- the full Sequence (tracks, base frames, P-Lock maps);
- sample-pool **references** (path + `xxHash32`), never PCM payloads.

Excluding raw PCM is deliberate: it keeps DAW auto-saves cheap and
prevents the audio thread from blocking on background save activity.
The hash defends against silent file substitution.

A `kCurrentVersion` constant on `PluginState` provides a forward
upgrade path. v0 ships a minimal serializer (APVTS only); the full
payload lands when P-Locks become first-class (M7).

## 8. Voice Lifecycle and Choke

Track monophony plus tail-ringing voices implies a bounded but
non-trivial voice manager:

- A new trigger on a track schedules a 1–2 ms linear micro-fade on
  whichever voice that track currently owns, then steals it for the
  new trigger.
- Voices that finish their AHDSR release without being stolen are
  reclaimed automatically.
- Cross-track triggers do not interact; each track is its own choke
  group.

## 9. Phase 3 — Modular Sub-Hosting

The native `IMachine` contract is the foundation; Phase 3 adds a sub-
host so third-party DSP can satisfy the same contract. The plan:

- The plugin scans an application-specific directory for sandboxed
  CLAP/VST3 binaries.
- A sub-plugin must conform to a strict shape: 0/2 or 2/2 audio buses,
  exactly 48 host-exposed parameters, no proprietary UI window.
- A `juce::AudioPluginFormatManager` instance instantiates the sub-
  plugins and routes the resolved `ParamFrame` into their parameter
  tree each block.

This lets specialised DSP nodes — 4-op FM, modal synthesis, dedicated
MIDI CC transmitters — be developed and tested in any DAW as ordinary
plugins, then dropped into Lockstep to be driven by its P-Lock engine.

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

- The single sampler Machine implementing `IMachine` end-to-end.
- Polymetric clocking, multi-track sequencing, base parameter pages.
- P-Lock editing model with the Override-ELSE-Base resolver.
- Trig conditions (probability, 1:N, prev-dep) honoured at runtime.
- MIDI ingestion: abs/rel CC with soft-takeover, scoped mappings
  (Master / Track[N] / SelectedTrack), Omni and Per-Track channel
  modes, four contextual encoders, MIDI clock + sync modes in
  standalone, edit-context routing of all input sources including
  note-on pitch recording gesture (held-step + key = pitch P-Lock).
- QWERTY overlay + Manipulation Zone + Section Bar + Step Grid wired up.
- Gate length as a P-lockable machine slot; machine enforces timed
  release when gate > 0.
- Pattern recording (live note/CC capture into trigs and P-Locks).
- State serialization including P-Lock data and sample references.

Sub-hosting (Phase 3) and the hardware companion (Phase 4) are
explicitly out of v0.1 scope; the architecture is built to absorb them
without restructuring.

## 12. Open Questions / Future Work

- **Chord / polyphonic step entry.** The pitch-recording gesture (§5.4)
  is defined for monophonic machines as "last note wins." For
  polyphonic machines, multiple simultaneous notes should populate a
  chord. The `PLock` model currently stores one `float` per slot; chord
  encoding (e.g. a set of note slots per step, or a compact bitmask
  slot) is deferred until the first polyphonic machine is designed.
  Conceptually the gesture is already correct — only the storage
  encoding needs resolving.
- Per-track voice count above 1 (e.g. for sampler chord stabs) —
  currently strict track-monophony.
- MIDI-out machine for sequencing external gear from inside the same
  pattern grid.
- MPE-aware Machines — the boundary is currently ParamFrame only; a
  per-step expression layer may follow.
- Bundled sample library / factory-patch shape.
- Final product name to replace "Lockstep".
