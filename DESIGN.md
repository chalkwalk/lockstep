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

### 4.4 Trig Conditions

Per-step conditional firing combines:

- **Probability** (1–100%);
- **Iteration rules** (e.g. fire on iteration 1 of every 4
  playthroughs);
- **Previous-dependency** (fire only if the previous step did / did
  not fire).

The data model carries these from M0; the evaluator and prev-state
machine land in M4.

## 5. Input Layer

All parameter writes travel through the **EditContext** before
landing:

- If `ActiveForEditing` is **true** (a step is held — by QWERTY, by
  MIDI note, or by hardware), incoming writes go to that step's
  `PLock` (Step Override).
- Otherwise, writes go to the Track Base.

This rule is **input-source-agnostic**: encoder, QWERTY, MIDI CC, and
future hardware controls all resolve through the same gate. A CC
arriving while a step is held writes a P-Lock on that step exactly
as a UI encoder twist would.

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

- **Master** — fixed, mapping is to a master parameter (e.g. master
  gain, swing). Always agnostic of the focus state.
- **Track[N]** — fixed, mapping is to slot S on track N. Useful when
  a hardware surface dedicates physical knobs to specific tracks.
- **SelectedTrack** — follows the current focus. If focus is Track 3,
  the CC drives slot S on track 3; switch focus and the same CC
  retargets. If focus is Master, the CC is a no-op.

Transport controls (Play / Stop / Record) are not CC-mappable
through this surface; they bind to dedicated MIDI realtime / MMC
messages. Mappings are project-saved.

### 5.3 Focus state and contextual encoders

Selection is a **first-class focus state**, one of `{Master,
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

The QWERTY overlay translates raw scancodes into one of: a step in
the 2×8 grid (bottom two rows), a page selector (top row + Shift
modifier for pages 1–12), or a transport command. The same physical
keys are the ones a future hardware controller's mechanical grid
will mirror.

## 6. UI Philosophy

The UI is built around three regions whose layout matches a future
hardware surface 1:1:

- **The Manipulation Zone.** Exactly 4 primary parameters visible at
  any time. Their labels, ranges, and visualisations come from the
  active Machine's metadata.
- **The Section Bar.** Six section buttons (keys `3`–`8`) group the 48
  parameter slots into machine-defined sections, each with 1–n pages of
  4. Re-pressing a section key cycles pages within that section.
  Holding Shift (`1`) then pressing a section key selects a master
  section (global/per-track controls outside the 48-slot machine
  frame). Section labels and page counts are declared by the machine,
  not hardcoded in the UI.
- **The Step Grid.** A 2×8 visual matrix mirroring the bottom two
  QWERTY rows. Sequences longer than 16 paginate via dedicated keys.

The aim is that an experienced user holds an editing context (a step
held, a page selected) and resolves all parameter changes in the
Manipulation Zone without ever leaving the keyboard.

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
