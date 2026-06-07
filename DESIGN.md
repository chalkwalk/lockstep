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
   is reachable from the QWERTY keyboard alone. In-context selection
   — machine type, P-lock target slot, track, pattern, part — is
   always a step-key press; section keys navigate the MZ to a
   parameter page and never launch a picker or popup. The eventual
   hardware controller is literally a special, fewer-key QWERTY
   keyboard in a grid layout — same key→action mapping, ergonomically denser
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
inheriting from the SDK base (`sdk::MachineBase : IMachine`) and
implementing its half-dozen virtuals. There is **one authoring model
with two link paths**: first-party machines are statically linked into
the core, while third-party machines compile to a loadable module
behind a stable, JUCE-free C ABI and are fronted by a `WrapperMachine`
that presents as an ordinary `IMachine` — the sequencer never learns
which path a machine took. This is a bespoke contract for machines
purpose-built for Lockstep, **not** a CLAP/VST3 sub-host: still no IPC,
no sandbox, no embedded interpreter. The full ABI, host-services
interface, registry, and discovery model are specified in §36.

The boundary is deliberately narrow but deliberately *not* fixed-shape
— each machine declares its own parameter schema and voice topology:

- **Parameter Schema (variable, machine-declared).** A machine declares
  any number of parameter slots. Each slot is a `ParamSpec` carrying:
  a stable string id, a display label, range, default, stepped flag,
  unit hint, the section index it belongs to (0..5; see §6.1.1), a
  **variant** (`Primary` or `Secondary` — 3.2) declaring whether the
  slot lives on the no-modifier page or under `Func+section`, an
  optional **role tag** (see below), and an optional **value-label
  table** (`valueLabels`, 3.4) supplying textual names for stepped /
  enum positions (`LP24 / LP12 / HP / BP`, `MONO / PARA`, sine / saw /
  pulse / tri…). Slot count and layout are entirely the machine's
  choice; only the section-key it sits under is constrained by the
  snap-to-canonical discipline (§6.1.1). The sequencer's P-Lock
  storage, MZ rendering, and CC mapping are all driven by the schema
  the machine reports. When `valueLabels` is populated the MZ
  renders the textual name in the single value display (§26.2);
  empty = numeric. An optional **skew** (`ParamSpec::skew`, 3.7,
  default `1.0` = linear; JUCE `NormalisableRange::skew` semantics)
  gives non-linear encoder mapping for parameters whose useful
  resolution is bunched at one end of the range — envelope times
  (`≈ 0.25–0.35`, so 1–10 ms covers as much knob travel as 1–10 s)
  are the canonical case. Skew is applied uniformly on the rotary,
  double-click reset, Control-All broadcast, and P-Lock read/write
  paths; on disk values are still stored unskewed.
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
- **Optional audio input.** Most machines treat `buffer` as
  output-only — they synthesise into it. A machine may instead
  *consume* audio by declaring an `input_source` slot
  (`{None | External bus | Track N | Master}`); when set, the
  sequencer fills `buffer` with the chosen upstream signal before
  calling `process()`, and the machine reads-then-overwrites (Thru) or
  reads-and-captures (Recorder). This is the capability behind Thru,
  Recorder, and Looper machines and the realtime resampling chain —
  see §27. Routing is forward-only by topological sort with cycles
  refused; `input_source = Master` is the one sanctioned prior-block
  tap (§27).
- **Per-machine voice topology, pulled live.** A machine returns
  `currentVoices(baseParams) -> Polyphony { V0..V4 }`. The sequencer
  calls this for each fired trig (so a parameter-driven mode flip such
  as VA Mono↔Para or FM Mono↔Poly takes effect on the next trig). `V1`
  = monophonic, `V2..V4` = self-managed polyphony, `V0` = unbounded /
  MIDI-out style. Track monophony is therefore a property of the chosen
  machine *in its current configuration*, not a universal sequencer
  rule. When a chord step holds more notes than `currentVoices()`, the
  sequencer applies the per-track `NoteSelection` (`TopBias` default,
  `BottomBias`) using a "spread-with-bias" picker: top + bottom voices
  first, then interior positions evenly spaced, with the bias resolving
  ties. The step's note storage cap (`kMaxNotesPerStep = 4`) matches
  the machine ceiling, so the selector only fires when a machine is
  currently sub-4 and the step holds more notes than it can voice.
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

### 2.1 What is a machine — and what is an `IEffect` instead

A machine occupies a track's sound-*source* slot. The boundary admits
exactly three kinds of machine:

- **Generators** — synths and samplers that *originate* sound from trig
  events: `SamplerMachine`, `FMMachine`, `VAMachine`, `DrumSynthMachine`,
  `SlicerMachine`, `StaticMachine`, `PercussionMachine`, `DigitalMachine`.
- **Routers** — a machine that *carries* audio from an `input_source`
  into the track's own signal path. There is exactly one: `ThruMachine`
  (§29). It is near-empty by design — the actual shaping is done by the
  canonical post-machine FILTER / AMP / FX (§14), not by the machine.
- **Capture engines** — machines whose value is *stateful audio
  capture*: `RecorderMachine` (overwrite) and `LooperMachine` (overdub).

Pure timbre *processing* is **not** a machine. A filter, EQ, distortion,
bitcrusher, reverb, delay, compressor, or any other "audio in → audio
out" colourist is an **`IEffect`** (§32) — inserted post-AMP or on the
master bus, reusing the same `ParamSpec` / `role` / P-Lock
infrastructure. Building such a processor as a machine would waste a
generator slot, duplicate the FX system, and break the §6.1 promise that
FILTER / AMP / FX mean the same thing on every track (`PRINCIPLES.md` §8).
The litmus test: *does it originate or capture sound, or merely colour an
existing signal?* Originate/capture → machine; colour → `IEffect`. Thru
is the single deliberate exception, and it earns it by doing no colouring
of its own.

This is also where catalogue scope is drawn. A machine ships **stock**
only when it is iconic and foundational to the reference lineage (§29);
anything more specialised is a **third-party module** authored against
`sdk::MachineBase` and loaded via the 6.7 ABI (§36) — including engines
the author maintains in their own separate repositories. "Would this be
better as a separate download?" is a real question for every proposed
machine, not a formality.

## 3. The Baseline Sampler Machine

The first engine to inherit `IMachine` is a monophonic sampler
designed for trip-hop / drum-machine workflows. It is the **Flex**
archetype in the Octatrack lineage — samples are decoded into RAM and
fully manipulable (pitch, slice, trim, loop). Its disk-streaming
sibling (**Static**) and the input-consuming machines (**Thru**,
**Recorder**, **Looper**) are described in §29:

- **Voice topology.** `currentVoices() = V1`. The sampler's own
  `VoiceChoke` applies the 1–2 ms micro-fade on retrigger; the sampler
  doesn't manage voice stealing. Voices ring out under their AHDSR envelope
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

### 3.1 Sample trim, loop, and the envelope-bound tail

(Details deferred to 4.4 in `ROADMAP.md`.)

The baseline sampler exposes four additional P-lockable slots
governing playback region: `start_sample`, `end_sample`,
`loop_start`, `loop_end`. The first two trim the playable region
within a sample; the second two define an optional loop region
within (or overlapping) that trimmed region.

The playback model is deliberately tight:

1. On note-on, voice playback begins at `start_sample` and proceeds
   forward through the trimmed region.
2. If a loop region is set (i.e. `loop_end > loop_start`) and
   playback reaches `loop_end`, playback wraps to `loop_start` and
   continues looping.
3. The AHDSR amplitude envelope governs voice termination: note-off
   triggers release, and the loop continues feeding the envelope
   until amplitude reaches zero (or the voice is choke-stolen for
   retrigger).
4. If no loop region is set, playback proceeds to `end_sample` and
   the voice falls silent (envelope continues normally through
   release).

A fixed small crossfade (≤4 ms) is applied at the loop seam to
suppress clicks. The crossfade is not user-controllable in v1.

This is the most flexible model that does not introduce a new
concept: a sample is "play through, then loop a region, all subject
to the envelope." It covers one-shots (no loop region), straight
loops (loop region = entire trimmed region), and looping tails
(loop region restricted to the tail of the sample).

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
emits a new note-on, and the machine — knowing it's monophonic from
its `currentVoices() == V1` declaration — runs its own choke micro-fade
before starting the new voice. (All current machines self-manage the
choke; the sequencer just picks the chord and emits notes.)

This model means machines never see "trig" as a concept — they only
see MIDI. A polyphonic machine handles overlapping note-ons naturally;
a MIDI-out machine forwards events verbatim; the sampler interprets
note-on as "start a voice at this pitch, this velocity."

External MIDI (from the host or a hardware controller) is mixed into
the same buffer the sequencer writes to, so the machine sees one
unified event stream. Routing rules (Omni vs Per-Track, focus follows)
determine which track's buffer external MIDI lands on.

### 4.7 Set / Song / Scene / Phrase hierarchy

The container hierarchy follows **musical purpose**: a performer
delivering a live set of songs, where each song has scenes, each scene
specifies which phrases its musicians play. The hierarchy was re-derived
from this model in ROADMAP Phase 7; it supersedes the earlier
Octatrack-style `Project > Bank > Pattern > Part` (ROADMAP 2.2).

```
Set                                (the live set / document)
 └── Song              × 16        (a song)
      ├── [per Track]
      │     ├── Kit                (the musician's instrument for this song)
      │     └── Phrase  × 16       (the musician's repeating musical ideas)
      └── Scene         × 16       (a launchable moment: intro / verse / chorus / …)
```

The vocabulary maps onto the performance-modifier cluster
(`Func/Track | Phrase/Scene | Morph/Song | Mute/Fill`):

| Cluster key | Selects / scopes |
|---|---|
| **Phrase** (was Pattern) | Phrase — one musician's musical idea |
| **Scene** (was Part) | Scene — a launchable moment in a song |
| **Morph** (was Scene) | the A/B fader morph within a Scene (§17) |
| **Song** (was Master) | Song select; `Func+Song` = Global/project params |
| **Track** | the musician (channel); `Func+Track` = machine/Kit picker |
| **Mute** | live global mute layer |
| **Fill** | phrase variation |

**Naming, vs. the reference instruments.** Lockstep keeps Elektron's
words *only where the behaviour still matches*, and adopts the dominant
DAW word where the behaviour moved toward a clip-launcher. This keeps
false friends out of the surface:

| Lockstep | Octatrack | Ableton / clip-launcher DAWs | Same behaviour? |
|---|---|---|---|
| **Set** | Project | **Set** | ✅ document / live set |
| **Song** | Bank | *(Arrangement)* | renamed (Bank was non-musical) |
| **Scene** | Pattern | **Scene** | ✅ a launchable cross-track row |
| **Phrase** | *(per-track length)* | Clip | per-track content (kept "Phrase": pure notes, no audio) |
| **Kit** | Part | Patch / Instrument | the per-song instrument (Part's kit role) |
| **Morph** | Scene (crossfader) | *(macro variation)* | ✅ A/B two-pole morph |

The notable trade: an Octatrack user's "Scene = crossfader" now reads
**Morph**, and **Scene** moves to the launchable row — matching every
clip-launcher DAW (Ableton, Bitwig, Logic Live Loops, Studio One). The
Octatrack "Part" (kit container) is dissolved into the per-Song **Kit**;
see §4.7.1 for what that costs and how it is recovered.

- **Set.** Top-level container; one Set = one plugin-instance state blob.
  Owns: all Songs, the project-global sample pool, MIDI CC mappings,
  focus state, channel mode, clock settings, the global Sound Pool
  (§13.5), and `launchQuantizeBars` (global launch-quantize amount in
  core-time bars; default 1). Matches Ableton's "Live Set" and the
  product's "perform a live set" framing.
- **Song.** Holds, for each of the 16 tracks, a **Kit** and a 16-entry
  **Phrase** pool; plus 16 **Scenes**. All the musical content of one
  song lives here. (The internal per-`(Track, Song)` container that
  bundles a track's Kit + Phrase pool is `SongTrack`; it is not a
  user-facing concept — the performer only ever thinks "this track, in
  this song.")
- **Track.** The channel / the musician — a whole-Set identity (one of
  16). It owns the `Track` scope (Control-All, focus) and, under
  `Func+Track`, the machine/Kit picker. A Track plays a (possibly)
  different Kit and Phrase pool in every Song.
- **Kit.** The musician's instrument *for this song* (`TrackKit`):
  machine identity, destination, base parameter frame, post-machine
  FILTER/AMP state (§14), MIDI CC config, and the track's clock divider.
  It stays fixed for the Song and may differ between Songs. Machine
  identity is a stable string id; unknown ids fall back to a stub
  machine that preserves base params and trigs but produces silence. The
  Kit is a **named, recallable unit** — name / save / load / reload under
  `Func+Track` (§4.7.2) — but it is **not** selectable per-Scene (one Kit
  per track per song). The Kit is the track's fixed *sound*, not its
  sequencer feel or content: swing is per-Track (§19); phrase length /
  steps / notes are per-Phrase; Scene assignment is per-Scene.
- **Phrase.** One musician's repeating musical idea. Carries: `length`
  (1–64 steps; see §34.4 and §4.8 for the default-seeding rule), the
  step array (trig + P-Locks), trig defaults, base trig condition, and
  note selection. Phrases are **pure musical content** — they carry no
  machine or kit info. (We keep "Phrase" rather than the DAW-standard
  "Clip" precisely because a Clip implies bundled audio/warp state; a
  Phrase is notes only.)
- **Scene.** A launchable moment in a song (intro / verse / chorus /
  bridge / …). Launched live; **no intrinsic length** — it plays until
  the next Scene is launched. A Scene is *assignment + context*, not
  content. Its phrase assignment is a single **global pattern** row (the
  grid mental model: columns = tracks, rows = phrase index; a Scene is, by
  default, a row):
  - **Global pattern** `N` — one phrase-pool row index, the Scene's
    "home" row. Default: Scene `N` → global `N`; freely changeable. All
    non-deviated tracks play their phrase from this row. **This is the
    only phrase-routing field in the saved floor** — per-track phrase
    deviations are session-RAM-only and are never serialized.
  - `activeMask[track]` — which musicians play in this scene (replaces
    the old per-Pattern mute mask; runtime silence =
    `globalMute[t] || !activeMask[t]`).
  - `coreTime` — the Scene's time signature; see §4.8.
  - Morph A/B snapshot fields; full crossfader implementation: §17/5.2.

**Scene = saved floor + live overlay.** The fields above (globalPattern,
activeMask, coreTime, Morph snapshot) are the Scene's **saved floor** —
its identity, serialized, and the §13.6 Scene-scope checkpoint floor. On
top sits a **live overlay**: in-the-moment global-pattern changes and
per-track phrase deviations. Per-track phrase deviations are **always**
part of the overlay and are **never** promoted to the floor *as routing*
(commit instead bakes them into phrase content — see below). The effective
live phrase for a track is `liveDeviation(t) ?? globalPattern`. Each Scene
**remembers its own** overlay while the set is running. **`Scene + Record` =
commit-and-bake** (Yes/No-confirmed, §16): it writes the live global-pattern
and mask changes into the floor *and* **bakes** the per-track deviations down
into content — for each deviated track it copies that track's effective live
phrase into its home-row (`globalPattern`) slot, then clears the deviation.
The floor itself still stores **no per-track routing** (`globalPattern` stays
the only phrase-routing field); a hand-curated heterogeneous arrangement
persists by **materialising content onto the diagonal**, not by storing a
routing vector. Because the bake **overwrites phrase content and severs
sharing** — the baked slot becomes a snapshot, disconnected from the phrase
it copied (the Phrase/Scene split, below) — it is destructive and prompts a
**Yes/No confirmation** before writing. Discarding the overlay: re-launching
the active Scene
(`Scene + active-step` single-tap, or `Scene + Stop`) returns to the floor;
`Func + Scene + step` launches any Scene at its clean floor (§16). This
split is the same machinery as the scope-respecting Checkpoint (§13.6):
the floor is the saved state, the overlay is working state above it.

**The Phrase / Scene split.** A Phrase is *material* — a musical idea
that may be referenced by multiple Scenes (the chorus phrase reused in
two different moments of the song). A Scene is *assignment + context* —
who plays, which row/deviations they play, the meter, and the morph
state. Editing a Phrase ripples instantly to every Scene that references
it. (This sharing is load-bearing for the snapshot model: because content
is Song-owned and only *referenced* by Scenes, the natural unit of a
whole-state snapshot is the Song, not the Scene — see §13.6.)

**Kit is per-(track, Song), not per-Scene.** An instrument change
(machine swap, base params, divider) applies to a musician throughout
the whole Song. A Song may give the same track a different instrument
from another Song. The musician plays the same kit throughout the song;
they may play a different kit in a different song.

**Live phrase grammar.** `Track` selects breadth = *one musician*, `Scene`
selects breadth = *the whole band*; `+ Phrase + step` jumps to the phrase
you pick. So:

- `Track + Phrase + step` — **deviate** the focused musician (the track
  highlighted in the VU bar) to the picked phrase. A live overlay change.
- `Phrase + step` — **unison / set-global**: set the Scene's global pattern
  to the picked phrase; the **focused** musician un-deviates and joins the
  unison; other deviated musicians keep their deviation (*"more specific
  scope wins"*, `PRINCIPLES.md` §13). A live overlay change.

Deviation management needs no separate re-sync or force-all gesture:
"rejoin one musician" = pick their **home** (global) phrase, which the
selector marks with a border (below); "clear all deviations / revert the
whole Scene" = re-launch the active Scene (`Scene + active-step`) or
`Func + Scene + step` for a baseline launch to any Scene's clean floor
(§16). The old `Track + Scene` / `Scene + Yes` re-sync gestures and the
`Scene + Phrase + step` force-all chord are **dropped** — the re-launch
gesture covers the use case with no three-key chord required. See §16 for
the launch model and §13 for all gesture bindings.

**Deviation must be visible.** Two surface affordances make the floor /
overlay legible (both add-only `CellState` / per-track-flag work on the
surface model, §35.8, so an external controller mirrors them):

- **Dual-marker phrase selector** — while `Phrase` is held, the step grid
  is the focused track's phrase selector. A **border** marks the Scene's
  global / home phrase for that track; a **fill** marks the track's
  *current* (possibly deviated) phrase. A deviation reads as fill ≠ border;
  "rejoin" is picking the bordered cell.
- **Deviation badge** — a persistent per-track indicator (on the track
  strip / VU area and the track-select cells), driven by `isTrackDeviated`,
  so you can see *which* musicians are off their home row without holding
  `Phrase`.

**Scenes are launched, not chained.** There is no stored Scene
arrangement or Song arrangement — the order is performed live. See §16.

#### 4.7.1 What the dissolved Part costs, and how it is recovered

The Octatrack **Part** bundled a kit + sound + the crossfader scenes,
with four Parts per Bank and a *reload* gesture. Lockstep moves the kit
role into the per-`(Track, Song)` **Kit** and the morph role onto the
**Scene**. That trade gives up the Octatrack performance moves that
depend on a *swappable* Part:

- **Live Part-swap** (same sequence, a different sound bundle, mid-song)
  — not available, because Kit is fixed per Song. The substitutes are
  the **Morph** (continuous A/B, §17), per-step **P-Locks**, or a **Song**
  change.
- **Per-section instrumentation** ("verse uses sound A, chorus sound B"
  *at the kit level*) — a Scene's floor is a global phrase row + masks,
  not a kit assignment, so a kit-level change cannot be pinned to a Scene;
  approximate it with Morph, `activeMask`, or P-Locks.
- **Part reload** (revert live sound-mangling to the saved baseline) —
  recovered, but generalised: it is **not** a bespoke kit feature. It is
  the floor of the scope-respecting Checkpoint stack — "reload saved" =
  walk a scope's stack down to its persisted floor (§13.6).

What the trade *buys*: Phrases stay pure content and are freely reusable
across Scenes; the "band" mental model stays clean (one instrument per
musician per song); OEB resolution gains no Pattern→Part indirection.
The recovery lever, if live re-instrumentation is later wanted, is a
small per-Track **Kit pool** plus a `kitIdx[track]` on the Scene — a
model extension, not a renaming. Deferred; not in the current model.

#### 4.7.2 Kit as a recall unit (under `Func+Track`)

The Kit is *light first-class*: a real, **named** object you can save,
recall, and reset — but with **no dedicated cluster key and no per-Scene
selection** (the noun stays off the surface; cf. the dropped "Lane").
Every affordance lives inside the existing `Func+Track` **machine/Kit
picker** — the same place machine identity is chosen — so the picker is
"this track's whole sound for the song," not just its engine.

This mirrors the Digitakt Kit (name / save / load / **reload**), which
Elektron users reach for, without a new key:

- **Name.** Each Kit carries a short name (≤16 chars), edited inline in
  the picker chrome (no modal dialog; cf. §23). Default = the machine's
  display name.
- **Save / load.** The picker has two faces, toggled by Nav: **machines**
  (pick an engine) and the **Kit library** (a Set-level pool of saved
  Kits). In the library face, a step press **loads** that saved Kit into
  the focused track's Kit for this Song; `Func+Track + Record` **saves**
  the current Kit into the library. Loading copies the whole bundle
  (machine + base params + FILTER/AMP + CC config + divider).
- **Reload (revert to saved).** Not a bespoke gesture: it is the
  `Track`-scope floor of the Checkpoint stack (§13.6) — hold `Func+No`
  with `Track` held to revert the live Kit to its on-disk saved state.
  This is the generalised Octatrack "Part-reload" (§4.7.1).

> ⚑ **Provisional gesture detail.** The machine-vs-library paging of the
> picker and the exact save/load verbs are a UI-design detail to settle
> when the picker is built (it ships as part of the §23 management pass);
> the *model* commitment here is only that the Kit is a named, library-
> recallable unit reached through `Func+Track`, not a selectable per-Scene
> object.

### 4.8 Core time and launch quantize

**Core time** is a per-Scene `TimeSig { int numerator; int denominator; }`
(default `{4, 4}`). It serves three roles:

1. **Launch-quantize grid.** The global `launchQuantizeBars` setting
   (Set-level; default 1) measures in core-time bars. A bar =
   `numerator × (4.0 / denominator)` quarter-note PPQ. A Scene or
   Song launch fires at the next multiple of
   `launchQuantizeBars × barPpq` past the current playhead.
   Example: 4/4, 1 bar → fires at each 4-beat boundary. 7/8 core time,
   1 bar → fires at each 3.5-beat boundary. Polymeter within a Scene is
   unrestricted — phrase lengths are independent of core time (§4.2).
2. **Metronome downbeat.** The metronome accent pattern is derived from
   core time; the "1" fires at `barPpq` intervals. Core time is the
   only place the metronome reads a sense of a "bar".
3. **Default phrase length.** When a new Phrase is created while a
   Scene is active, its length is seeded from core time:
   `numerator × (4 / denominator)` steps at the default step resolution
   (1/16 note = 1 step), so a 4/4 Scene seeds 16-step phrases and a
   7/8 Scene seeds 7-step phrases. This is a *default only* — the
   phrase length is freely editable afterward and never constrained by
   core time.

**Per-track phrase-end override.** Each track has a `launchMode` flag
(`GlobalBar` default | `PhraseEnd`). Tracks set to `PhraseEnd` switch to
a new Scene assignment at the *end of their current phrase cycle*
rather than at the shared core-time boundary. This is the per-musician
flexibility layer on top of the global grid — a musician may finish their
phrase before snapping to the new Scene's assignment.

`Clock.h` has no time-signature state today. Core time is new state held
in the Scene and consumed by the launch engine in `PluginProcessor`.

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
Track1..16}`. The focus determines what the contextual encoders
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
- **Per-Track Channel.** MIDI channel N (1..16) hard-routes to track
  N's MIDI buffer. Live focus changes do not affect note routing in
  this mode.

A note-on routed to a track is appended to that track's MIDI buffer
for the current block and reaches the machine through the standard
event stream (§4.6). The sequencer does not interpret note numbers
itself — they pass through to the machine.

The pitch-recording gesture is the one exception: when a step is held
(EditContext active), an incoming note-on is *consumed* by the
sequencer instead of being passed through. The note number is written
to the held step's `noteOverride` field; the next time that step
fires, it will emit a note-on with that number. For a monophonic
machine (`currentVoices() = V1`) the last note-on within the hold wins;
for a polyphonic machine the overrides accumulate into a chord (the
exact encoding for chord storage is deferred — see §12).

**Key-as-PLock for drum patterns (record arm, 1.8).** With record arm
engaged, individual note keys write P-Lock values to a chosen machine
slot — typically the sampler's `sample_id` slot — derived from the
key index, e.g. routing different pad keys to different sample IDs
across one drum track. This is a recording-mode-specific gesture, not
a live editing gesture, and the target slot is a per-track config
choice rather than a hardcoded behaviour. See 1.8.4.

### 5.5 QWERTY overlay

The QWERTY overlay uses a **10×4** layout (revamped in 3.1 §33, with
identities re-shuffled in 3.2 by measured chord-value). The **left two
columns** are an eight-key modifier cluster reachable by one hand; the
**right eight columns** are the functional block — function/section
keys (top two rows) and step keys (bottom two rows). Keeping the
functional block 8 wide preserves the 16-step grid and the six
canonical sections unchanged.

```
 MODIFIERS    │  FUNCTIONAL BLOCK                                 keys
 [Func][Track]│ [TAP ][ ^  ][TRIG][SRC ][FILTER][AMP ][MOD ][ FX ]  1 2 3 4 5 6 7 8 9 0
 [Phr ][Scn ]│ [ <  ][ v  ][ >  ][Yes ][REC ][PLY ][STP ][ No ]  Q W E R T Y U I O P
 ─────────────┼──────────────────────────────────────────────────
 [Scn ][Mstr ]│ [ steps 0 - 7 ]                                   A S D F G H J K L ;
 [Mute][Fill ]│ [ steps 8 - 15 ]                                  Z X C V B N M , . /
```

(The two left columns in each row hold the cluster; the next two
slots on row 0 are `3=TAP` and `4=NavUp`; sections fill `5–0`. Row 1's
right side is `E=NavLeft / R=NavDown / T=NavRight` followed by the
verb cluster `Y U I O P`. The 16 step keys live on rows 2 and 3.)

**Modifier cluster (left two columns, 3.2 identities).** Eight
persistent first-class scopes, six of which are "section scopes"
(each owns a six-cell row in the scope-section matrix, §6), and two
of which are performance specialists.

| Key | Modifier | Role | Chord notes |
|---|---|---|---|
| `1` | Func    | section scope + universal qualifier | composes with every other scope to give the "secondary variant" |
| `2` | Track   | section scope | focused-track edits, post-machine FILTER/AMP cells |
| `Q` | Phrase  | section scope | phrase length / scale |
| `W` | Scene   | section scope | scene launch (single/double-tap) / commit / revert (§16). The Kit / machine picker is on `Func+Track`. |
| `A` | Morph   | section scope | morph-assign per scene |
| `S` | Song    | section scope | song select; `Func+Song` = Global / master FX / gain cells |
| `Z` | Mute    | performance specialist | hold-and-tap-many multi-mute |
| `X` | Fill    | performance specialist | `Fill+step` marks fill-only |

Column 1 (`1 Q A Z`) and column 2 (`2 W S X`) compound only **cross-
column** per the §13 compound-chord rule. The two specialists on row 3
signal the semantic split: they don't take a section. `Cue` is
reserved as a scope (DESIGN §31) but is not bound to a cluster key
until 6.4; cue-scene functionality folds under `Func+Scene` and
master-scope cells in the interim.

Frequency-of-use rationale: Func and Track are the most-touched modifiers
(row 0); Phrase and Scene hold the structural-recall slots (row 1);
Morph and Song hold the performance-bus slots (row 2); specialists
sit at row 3 where they're easy to find but don't compete for prime
real estate.

**Function strip (top two rows of the functional block).** As of 3.2:

- `3` = Tap Tempo (TAP/SPL on the Func layer).
- `4 / E R T` = inverted-T navigation (Up / Left / Down / Right).
- `5–0` = the six canonical sections TRIG / SRC / FILTER / AMP / MOD / FX
  (note `LFO`→`MOD` rename from 3.2; see §6.1.1).
- `Y U I O P` = verbs `Snapshot / Record / Play / Clear / Yes` (colour-rethink).
  `Func+Y` = Restore (pop checkpoint); `Func+U` = Omni copy; `Func+I` = Unqualified paste; `Func+O` = Delete (+ confirm);
  `Func+P` = No / cancel. `Record-Arm` / `Play-Stop` chords on `9 / 0` are deferred —
  see §33.1.

**Step keys.** Row 3 `D F G H J K L ;` = steps 0–7; row 4
`C V B N M , . /` = steps 8–15.

The `Func` layer (hold `1`) reaches the secondary assignments for
every key it composes with (snapshot push/pop, metronome via
`Func+I`, stop-and-reset, etc.) and — via the scope-section matrix
(§6.1.2) — the secondary variant of any held scope's cells. The old
`Func+R = MachineSelect` gesture is retired in 3.2; machine selection
is `Func+Track` (hold `1`, tap `2`; the Track key relabels to `MACH`) —
the 16 step cells re-skin to show available machine names and a step
press confirms the selection (scope-select pattern, PRINCIPLES §5).
Holding any other modifier
reinterprets the functional block per the **compound-chord rule**
(§13): a second held modifier *qualifies* the scope, it never invents
a new verb. `Track + step` = select track; `Mute + step` = toggle
that track's mute; `Scene + ^` / `Scene + v` = assign to Scene A / B
(§17.5).

The overlay emits `ControllerEvent` structs (button-down, button-up,
encoder-delta) that are source-agnostic — hardware controllers wire
onto the same stream.

The same physical keys will be mirrored 1:1 by the hardware
controller's mechanical grid.

## 6. UI Philosophy

The UI is built around three regions whose layout matches a future
hardware surface 1:1:

- **The Manipulation Zone.** `kMZSlots` primary parameters visible at
  any time — **8** as of 3.1 (§26.2, §33), laid out 4×2. Their labels,
  ranges, and visualisations come from the active Machine's metadata.
  When a section contains more than `kMZSlots` slots, repeated
  section-key presses cycle pages within it.
- **The Section Bar.** Six section buttons (keys `5`–`0`) that
  resolve through a **scope-indexed matrix** (§6.1.2, introduced in
  3.2). The key meaning depends on which scope modifier is held: no
  modifier = the focused track's machine sections; a section-scope
  modifier (`Track / Phrase / Scene / Morph / Song`) opens that
  scope's six-section row; `Func` is the universal qualifier that
  flips any held context to its "secondary variant." Pages within a
  given `(scope, section)` cell are cycled by repeated presses of
  the section key.

**Section keys expose parameters; step keys select.** A section key
press always navigates the MZ to a parameter page — it never opens a
picker or popup. When the UI needs "pick one of N," the 16 step keys
are the selection surface and the step cells re-skin to show the
options. There are two picker patterns (see also PRINCIPLES §5):

- *Scope-select* (machine, track, pattern, part): hold modifier →
  step cells re-skin for the duration of the hold → press step to
  select → release.
- *Step-driven edit* (P-lock slot edit, step note edit): hold
  non-step modifier(s) + press the target step → step cells re-skin
  → interact → release modifiers → mode exits. The modifier hold is
  the mode; nothing is left armed after release.

### 6.1 Canonical sections

#### 6.1.1 The canonical six (3.2)

The six section keys carry a fixed canonical taxonomy:

  | Key | Idx | Canonical section | Typical contents |
  |---|---|---|---|
  | 5 | 0 | **TRIG** | Trig defaults: note, velocity, gate. Also the host of track-meta `COND` / `TRIG` content (track-default conditions + step overrides). |
  | 6 | 1 | **SRC**  | Primary sound source: sampler controls, oscillator controls, FM ratios, MIDI program/channel for MIDI-out machines. |
  | 7 | 2 | **FILTER** | Filter — usually the post-machine FILTER block (§14), but machines may opt out (`hasInternalFilter()`) and present their own. |
  | 8 | 3 | **AMP**  | Amplitude envelope + output mix (Level, Pan, Sends) — usually the post-machine AMP block (§14). |
  | 9 | 4 | **MOD**  | Modulation, *deliberately shallow*: a minimal, performable set of live-tweakable modulators (e.g. one or two LFOs with canonical targets) at the primary page. Deep modulation — full matrices, per-operator envelopes, custom shapes — is machine-internal on MOD *extension* pages, not a uniform canonical promise (see the shallow-contract note below). (Renamed from `LFO` in 3.2.) |
  | 0 | 5 | **FX**   | Effects: machine-intrinsic (drive / bit-reduction) at primary; foundation-owned inserts on `Track+FX`; master FX on `Song+FX`. |

  A machine that has nothing to fill a canonical section leaves it
  empty (button dimmed). A machine that needs more than the canonical
  six declares *extension sections* on additional section-bar pages,
  reached by repeated press of the same section key.

  **Snap-to-canonical discipline (3.2).** A machine's no-scope pages
  belong to the machine: it may relabel any section (a wave-folder
  might print `MORPH` over the FILTER-key label). But canonical
  *placement* governs — a filter-like control belongs under FILTER
  (key 7) even when relabelled; a modulation matrix under MOD (key 9);
  an envelope under AMP (key 8). The discipline lets cross-machine
  workflows survive — `hold FILTER + COPY` always means "copy whatever
  the focused machine treats as its filter stage," and Control-All
  by `role` still finds matching slots across renamed labels.

  **MOD is deliberately shallow (PRINCIPLES *"Reward mastery"*).** The
  canonical MOD section promises only a *minimal, performable* modulation
  surface — live-tweakable, P-lockable modulators with canonical targets that
  a performer reaches for mid-set. A machine's *deep* modulation (FM's full
  4×4 matrix, per-operator envelopes, any drawable shape) stays machine-
  internal on MOD *extension* pages; it is **not** hoisted into a uniform
  cross-machine contract every user must learn. Snap-to-canonical still
  governs *placement* (a matrix lives under MOD key 9); the demotion is about
  the canonical *promise*, not the key. Lockstep ships no custom-LFO designer
  and no free automation lanes — stepped P-Locks and Morph (§17) are the
  modulation model (NON-GOALS).

#### 6.1.2 The scope-section matrix

Held scope modifiers reinterpret the six section keys. The cell map
below is normative; concrete content accretes through the milestones
that own each row (2.4 for Track-scope, 7.x for Scene-scope (was Part),
5.2 for Morph-scope (was Scene), 6.5 for FX cells, 6.4 for Cue-scope
reactivation).

  | Scope     | 5 TRIG | 6 SRC | 7 FILTER | 8 AMP | 9 MOD | 0 FX |
  |-----------|--------|-------|--------|-------|-------|------|
  | *(none)*  | machine trig | machine SRC | machine FILTER (opt) | machine AMP (opt) | machine MOD | machine FX (drive/bit) |
  | `Func`    | conditions / fill | machine SRC alt | machine FILTER alt | machine AMP alt | machine MOD alt | machine FX alt |
  | `Track`   | per-track condition defaults | input_source / Thru | post-machine FILTER | post-machine AMP + sends | per-track LFO (if any) | IEffect insert 1+2 |
  | `Phrase`  | length / scale lock | (dim) | (dim) | (dim) | (dim) | (dim) |
  | `Scene`   | launch / commit · coreTime | global + deviations | (dim) | active-mask | Morph snapshot | (dim) |
  | `Morph`   | (renamed `CXFD`) | morph-assign SRC | morph-assign FILTER | morph-assign AMP | morph-assign MOD | morph-assign FX |
  | `Song`    | (dim) | (dim) | master FILTER (if any) | master gain + sends | (dim) | master FX 1+2 |

The `Phrase` and `Scene` rows follow directly from the §4.7 ownership
split (this is the resolved content pass — the rows are no longer
provisional):

- A **Phrase** is pure per-track *content* — steps, P-Locks, length, a
  per-phrase scale lock (§34.2 — a *playable layout*, not auto-correct) — and
  carries **no sound**. So only the timing-
  domain `TRIG` cell is live (length / scale lock); every sound cell
  (`SRC`–`FX`) dims, because the kit-base params a Pattern used to share
  with its Part now live in the **no-scope / Track-base** row (machine
  params write to the track's **Kit** base, §4.7.2).
- A **Scene** owns *assignment*, not sound: its global pattern (the only
  stored routing field — per-track deviations are a live overlay, baked into
  content on commit, §4.7), the `activeMask[]`, `coreTime`, and the Morph
  snapshot. Those map onto the section keys by domain — `TRIG` =
  launch / commit · coreTime (timing); `SRC` = the global pattern + the
  per-track deviation map (which phrase each musician plays, and which
  deviate from the home row); `AMP` = active-mask (the mute/level domain);
  `MOD` = the Morph snapshot. The remaining cells (`FILTER`, `FX`) dim — a
  Scene has no DSP of its own.

Three rules govern the matrix:

1. **`Func` is the universal qualifier.** `Func + section` alone =
   the machine's secondary page (`ParamSpec.variant = Secondary` —
   §2). `Func + scope + section` = the secondary variant of the
   scope's cell (e.g. `Func+Morph+FILTER` = the *other* endpoint's filter
   assignments, `Func+Song+FX` = master FX 2 vs FX 1, etc.).
2. **N/A cells are dim or renamed.** A scope+section combination
   with no content dims its key. Where a near-canonical alternative
   exists, the section bar relabels the key live under that scope
   (e.g. `Morph+TRIG` becomes `CXFD` for crossfader curve, since
   the Morph morphs parameters but never trigs — §17.2).
3. **The section bar is reactive.** Holding a scope relabels the
   key chrome to that scope's row; holding `Func` in addition flips
   to the secondary variant. This is the operational form of
   `PRINCIPLES.md` §10 ("chrome must announce state").

### 6.2 Track-meta content lives on `Func+TRIG` (3.2)

Pre-3.2, the section bar had a separate "track meta" layer reached
by Shift (`COND` / `TRIG` / `TRACK` / `GLOBAL`). 3.2 folds that
layer into the matrix: track-meta `COND` content (probability, m:n,
prev-dep) lives under `Func+TRIG`; trig defaults (default note,
default velocity, gate length) live under `TRIG` itself. Track
length / divider migrate into `Track+TRIG`. Global output gain,
sync mode, and clock settings migrate to `Song`-scope cells.

Each section button cell still shows its primary label at the top
and its `Func`-secondary label at the bottom; the active layer
determines which renders prominently and which colour highlights
the cell.
- **The Step Grid.** A 2×8 visual matrix mirroring the bottom two
  QWERTY rows. Sequences longer than 16 paginate via dedicated keys.
  Cells carry a condition-state preview (§4.5): certain-fire,
  certain-skip, and probabilistic states are each rendered distinctly
  so conditional logic is visible at a glance without running the
  sequencer.

### 6.3 COND meta section — manipulation zone layout

When the COND track meta layer is active (`Func+TRIG` post-3.2, §6.2),
the four encoder slots show the trig condition controls for the
current track:

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

### 6.4 Display preferences: granular feedback toggles

The UI's visual density is governed by a set of **granular feedback
toggles** stored in global settings (not in project state). Every
toggle defaults to *on* so a first-run user gets the loudest, most
annotated experience; power users turn individual toggles off as
fluency grows. Crucially, *the key→action mapping is identical at
every toggle configuration* — the underlying gestures never change.
This is the operational form of `PRINCIPLES.md` §1 ("Vim, not nano")
and §8 ("chrome must announce state"): beginner mode adds chrome, it
never simplifies grammar.

Representative toggles (the set grows as new chrome lands; defaults
in **bold**):

| Toggle | Default | Effect |
|---|---|---|
| `show-key-legend` | **on** | Render key letters/numbers under each StepGrid + SectionBar cell. New users learn the mapping; hardware-surface users turn it off. |
| `show-row-stagger` | **on** | StepGrid + SectionBar rows visually mirror the QWERTY row stagger. Off = ortholinear grid (better with a hardware surface or once muscle memory is set). |
| `show-scope-help` | **on** | While a scope key is held, the chrome shows "next verb will do X" hints. |
| `show-pending-paste-preview` | **on** | While a clipboard is loaded and a paste target is held, the destination steps render a ghost overlay of what the paste will write. |
| `show-mode-banner` | **on** | Trig-grid alt modes (Keyboard / Retrig / SoundPool / 16-levels) overlay a full-width banner naming the mode. |
| `show-microtiming-ticks` | **on** | Steps with nonzero `microOffset` (§19) show direction-of-nudge ticks at all times. |
| `show-fillrule-preview` | **on** | Fill-only steps render in their distinct hue even when Fill is not held, so authoring is visible. |

The three earlier "overlay modes" (Staggered / Ortholinear / Clean)
recover as **preset bundles** of these toggles: Staggered =
`row-stagger + key-legend`; Ortholinear = `key-legend` only; Clean =
all-off. The preset bundle picker remains in the chrome as a
quick-start, but power users edit individual toggles directly.

Toggle state is not P-lockable and does not affect playback or MIDI
routing.

### 6.5 Contextual chrome and label resolution (3.3) ✓

The 3.2 rollout introduced enough contextual relabelling
(`COPY/PASTE/CLEAR` on verb keys when a scope is held; the scope-section
matrix relabelling six section keys under five different scopes;
section-bar dimming for `hasContent=false` cells) that the original
"primary label / fixed secondary label" cell layout no longer carries
the meaning a held modifier puts on the key. 3.3 consolidates the
contextual chrome under a single rule:

**One resolver decides every key's label.** A pure helper

```cpp
// src/ui/KeyLabel.h
KeyLabel resolveKeyLabel(const KeyDef& def,
                         const UiState& ui,
                         const EditContext& ec);
struct KeyLabel { juce::String primary; juce::String hint; bool disabled; };
struct KeyDef   { KeyRole role; const char* natural; const char* funcLayer;
                  int sectionIdx; bool machineHasSection; };
```

is the only path that produces the strings painted on a key. The former
ad-hoc branches — the `COPY/PASTE/CLEAR` swap, the `scopedCell()` matrix
lookup, the verb-key dimming under section scope — collapse into this
one function. **Step-hold is just another modifier flag in the input**
(`ec.isActiveForEditing()`), so the historical "section scope held OR
step held" special case disappears.

The resolver follows three policies:

1. **Primary label swaps when a modifier is held.** When any held
   modifier reinterprets a key, the resolver returns that
   contextual label as `primary`. Without held modifiers, the
   `primary` is the key's natural identity.
2. **An always-on `hint` survives only for genuinely-invariant
   secondary meanings.** Verb keys (`KeyRole::VerbCopy/Paste/Clear`)
   always show COPY/PASTE/CLEAR as a permanent bottom-strip hint
   regardless of state. When a scope is held that hint promotes to
   `primary` and the hint band is cleared (no duplication). Every
   other secondary appears only when its modifier is held.
3. **Empty / disabled is a first-class return.** The resolver may
   return `disabled=true` for cells the held scope reinterprets to
   nothing (e.g. `Phrase + SRC`). The paint pipeline trusts
   `disabled` and never adds its own relabelling.

**Label expansion (3.3):** canonical section names and modifier-key
labels now use up to 6 characters where they benefit: `FILTER` (was
`FILTER`), `TRACK` (`TRK`), `SCENE` (`SCN`), `MASTER` (`MST`), `MUTE`
(`MUT`), `PART` (`PRT`), `FUNC` (`FNC`), `PLAY` (`PLY`), `STOP`
(`STP`), `FILL` (`FIL`), `COPY`/`PASTE`/`CLEAR` (`COP`/`PST`/`CLR`),
`RETRIG` (`RTG`), `POOL` (`SPL`).

### 6.6 Scope colour grammar (3.3 / colour-rethink) ✓

The scope identity that a held modifier puts on the surface is
**visible**, not just functional. Each modality owns **one hue** used at
three brightness levels (resting / active / accent), so a modifier
always wears its own colour — no generic violet borrowed when unlit.

**Role-neighbourhood hue map** (UITheme.h):

| Family | Members | Hue band | Notes |
|--------|---------|----------|-------|
| **Func** (qualifier) | Func | amber ~36° | signature |
| **Structural scopes** (cool arc) | Scene ~150°, Track ~192°, Phrase ~225°, Song ~50° | green→cyan→indigo→gold | cool = hierarchy depth |
| **Morph** | Morph | magenta ~315° | expressive crossfader; apart from cool arc |
| **Performance** | Mute red ~2°, PMute rose ~345°, Fill chartreuse ~78° | warm+chartreuse | Mute removes / Fill adds |
| **Verbs** (neutral) | Y U I O P | slate H~215 S~0.18 at rest | conventional on-active: Record→red, Play→green, Clear→warm-red, Snapshot→violet-blue |
| **Edit sub-modes** | Note-edit azure, P-lock violet, Machine lime | own band | mutually exclusive with scope-hold |
| **Sections** (TRIG…FX) | all six | steel-teal | uniform; takes scope colour under scope-hold |

Three brightness levels per modality (starting points; tuned by eye against screenshots):
- **resting** ~B 35% (clearly hued, not near-black)
- **active** ~B 80% (full brightness on press / mode-on)
- **accent** ~B 94% (border highlight)

The helper `scopeColour(PrimaryScope, machinePicker=false)` in
`src/ui/KeyLabel.h` maps a scope enum value to its bright colour in one
place; `groupForCell()` in `KeyButton.cpp` picks resting/active/accent
from the UITheme constants for each button identity.

Used by:
- modifier keys at rest (scope dim), pressed (scope bright), and latched (scope bright + 2px border);
- key tints when a modifier is held — the held key, and any key it
  *reinterprets* (relabelled section keys, scope-combining verbs),
  glow fill+border in its scope colour via `SurfaceCell::scopeTint`;
- the step-grid scope re-skin (§6.7) — wired in 3.4;
- the held-context preview band (§6.8) — wired in 3.4;
- any badge or chrome that needs to say *what scope am I in?*.

**Taxonomy is fixed; specific RGB values are tunable.** Hardware LEDs
and the software surface share the same `UITheme.h` constants.

### 6.7 Scope-driven step-grid re-skin (3.4)

When a scope modifier maps to a 1-of-16 selector, the 16 step keys
are re-skinned for the duration of the hold:

| Held modifier        | Re-skin                                  |
|----------------------|------------------------------------------|
| `Track`              | Track 1 … 16 (numeric)                    |
| `Phrase`             | Phrase 1 … 16 (numeric)                   |
| `Scene`              | Scene 1 … 16 (numeric)                    |
| `Func + Track` (machine picker) | Machine names (textual)         |

Three rules govern the re-skin:

1. **Pagination is suppressed.** In the re-skinned mode only "which
   key was pressed" matters, and there are at most 16 entries
   directly addressable by D … `/`. The page bar is dimmed; cells
   never wrap.
2. **Unavailable indices dim.** If only N entries exist (e.g. 8
   tracks), indices > N render in the disabled state. The user sees
   immediately how many slots are populated without consulting
   another part of the UI.
3. **Cells tint with the scope colour** (§6.6) so the surface tells
   the user *which scope is being picked from*.

Implementation: an extended scoped-cell table (sibling of
`ScopedSectionMatrix.h`) maps `Scope → ReskinSpec { count,
labelStyle, labelForIndex }`. The paint pipeline consults the table;
no scope-specific paint code.

### 6.8 Top-bar dashboard and held-context preview (3.4)

The pre-Phase 3 "mode chips" row duplicated information already encoded
in the held cluster keys. 3.4 replaces it with two zones reading a
single view-model:

- **Left dashboard.** Persistent performance state: BPM,
  Song / Scene identity, transport position,
  checkpoint depth (`CK:N`).
- **Right held-context preview.** Derived from currently-held
  modifiers — e.g. `TRACK 3 + …`, `FUNC + PART → machine picker`.
  Acts as a live cheat sheet without being authoritative: the
  *behaviour* is set by the cluster keys, the preview just *shows*
  what those held keys mean.

Both zones read the same view-model so what the bar says and what
the next verb does cannot drift.

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
payload lands when P-Locks become first-class (1.8).

## 8. Voice Lifecycle and Choke

Each machine owns its own voice lifecycle. The sequencer's
responsibility is bounded: it asks the machine for its live
`currentVoices()`, runs the `NoteSelection` picker to choose which
notes from a chord step survive, then emits those note-ons (and
scheduled note-offs) as plain MIDI.

- **Mono (`V1`).** The machine fades its active voice over a 1–2 ms
  choke before starting a new voice for the next note-on. `SamplerMachine`,
  `FMMachine` (in Mono mode), and `VAMachine` (in Mono mode) all do this
  with a per-voice `VoiceChoke` helper.
- **Poly (`V2..V4`).** The machine manages its own voice pool and steals
  the oldest voice when note-ons exceed the live voice count (with a
  brief fade on the stolen voice). `FMMachine` (Poly) and `VAMachine`
  (Para) use this path.
- **MIDI-out (`V0`).** The sequencer passes chord notes through unclamped.
- Cross-track triggers do not interact; each track is its own choke
  group.

The sequencer's behaviour is uniform — it always emits MIDI — and the
machine's `currentVoices()` declaration determines how many of the step's
notes are emitted. Choke is an implementation detail of each machine,
not a sequencer feature.

## 9. Machine modules and arbitrary-plugin hosting

The successor to the earlier "wrap arbitrary plugins" sketch is the
**Machine Module ABI** (milestone 6.7), specified in full in §36. It
is *not* a CLAP/VST3 sub-host: it is a purpose-built, JUCE-free C ABI
for machines designed specifically for Lockstep, loaded as native
modules and fronted by a `WrapperMachine`. That is the sanctioned
extensibility path, and the one we build.

Hosting *arbitrary* third-party CLAP/VST3 instruments (Serum, Diva, …)
via `juce::AudioPluginFormatManager` is explicitly **not planned**. If
it ever happened it would be just one more machine module written
against the §36 SDK by whoever wanted it — no core sequencer changes —
but it is not a goal and ships no first-party support.

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
- Phrase recording (live note/CC capture into trigs and P-Locks).
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

The scope buttons are persistent first-class modifiers, the eight-key
cluster in the left two columns of the 10×4 QWERTY layout (see §5.5,
§33):

| Scope button | QWERTY key | Selects | Held alongside |
|---|---|---|---|
| `Func` | `1` (col 1) | Modifier for verb keys and meta sections; the universal qualifier. | Any. |
| `Phrase` | `Q` (col 1) | Phrase selection (§4.7/§16): `Phrase + step` = unison/set-global (focused musician un-deviates and joins; others kept); `Track + Phrase + step` = deviate the focused musician. To clear all deviations, re-launch the active Scene or use `Func + Scene + step` (§16). | Verb, step key, `Track`. |
| `Morph` | `A` (col 1) | Morph assignment; `Morph + ^/v` picks endpoint A/B (§17.5). | Nav, encoder, `Song`, `Fill`. |
| `Mute` | `Z` (col 1) | Live global mute layer (hold and tap many); `Scene + Mute + step` = per-scene mute. | Track/step keys, `Scene`. |
| `Track` | `2` (col 2) | One or more track slots; none selected = Control-All. `Func+Track` activates the machine/Kit picker via step-cell re-skin. | Verb, encoder, or a col-1 modifier. |
| `Scene` | `W` (col 2) | Scene launch + commit/revert (§16): `Scene + step` on a *different* Scene = carry overlay; on the *active* Scene = revert to floor; on an *empty* slot = create a **baked copy** (current effective layout, deviations included). `Func + Scene + step` = **baseline** (existing Scene → floor-only launch; *empty* slot → create a fresh default/empty Scene). `Scene + Stop` = revert active Scene; `Scene + Record` = **commit-and-bake** (Yes/No-confirmed): folds global-pattern + mask into the floor and bakes per-track deviations into home-row phrase content (§16, §23.3). *(Undeviated duplication lives on the clipboard, not a create chord — `Mute + Scene + step` is reserved for scene-mute; §23.3.)* | Verb, step key, `Track`. |
| `Song` | `S` (col 2) | Song select: `Song + step` queues a Song change (§16). `Func+Song` = Global/project params (incl. master-bus / FX focus, §32.3). | Verb, step key, section key, `Morph`. |
| `Fill` | `X` (col 2) | "While I'm holding this, fill conditions evaluate true." | Step keys; (no verb needed — it's the state itself). |
| `Trig` (hold a step) | `D–;` / `C–/` | The held step(s); multi-step hold is allowed. | Verb, encoder, or note key. |
| Section key | `5–0` | The held section's slots. | Verb, scope modifier (scope-section matrix, §6.1.2). |

The cluster identities above are the 3.2 layout (frequency-of-use
ordered, with the two performance specialists on row 3). `Cue` is
reserved as a scope (§31) but is not bound to a cluster key until
6.4; its functionality currently lives under `Func+Morph` (cue morph)
and `Song`-scope cells (cue routing) — see §31.

**The compound-chord rule.** Modifiers may be held together to qualify a
scope, but under hard rules so compounds never become bespoke chords and
so cost stays graduated (PRINCIPLES §15 — *gesture cost is graduated*):

1. **Cross-column only.** A column compound holds *at most one modifier
   from each column*. Two column-1 modifiers (e.g. `Phrase+Mute`) is
   meaningless and ignored; the legal column-compound space is exactly
   "one key from each column."
2. **A modifier+modifier compound never fires on its own** — it only
   sets a *compound scope*, still awaiting a verb or an encoder turn.
   Nothing happens from two modifiers alone, so there is no surprise.
   An action occurs only when a verb is pressed (`Track+Record`) or an
   encoder moves.
3. **`Func` is the universal qualifier** — the one sanctioned crossing
   of clause 1, *and the cheapest modifier* (PRINCIPLES §15): it
   composes with anything as the secondary/advanced layer, including on
   top of a cross-column pair (`Func + mod + mod + key`, the rung-6
   ceiling).

The compound *qualifies* the scope; it does not change what a verb
means. `Track + Section` = the section verb scoped to *this* track
rather than current/all; `Scene + Mute` = assign the AMP-level slot to
a scene (fluid mute, §17.2); `Cue + Scene` = preview that scene (§31).

**The cost ceiling.** Per the PRINCIPLES §15 ladder, the maximum is
**four simultaneous keys** (`Func + mod + mod + key`), admitted only
when the grammar and value earn it; **five is forbidden**. Cost counts
held scopes plus one operand — repeated same-class targets (multi-step
holds, hold-tap-many mutes) are one operand, not many keys. The
rung-by-rung inventory of every live gesture is §13.0.

**Exceptions table** (high-value chords that knowingly bend clause 1
*or* sit at the rung-6 ceiling; starts empty and grows only when the
obvious meaning is clearly worth it):

| Chord | Meaning | Why it earns the exception |
|---|---|---|
| *(none yet)* | — | — |

The verb set is small and uniform:

| Key | Idle (no scope) | Under scope | Func-secondary |
|---|---|---|---|
| `Y` | **Snapshot** — push checkpoint; **Yes** in dialog contexts | scope-specific snapshot (confirm) | *(same as bare — push checkpoint)* |
| `U` | **Record** — arm / toggle overdub | **Copy** — clipboard capture | *(fork Part, placeholder)* |
| `I` | **Play/Pause** — double-tap = Stop-to-top | **Paste** — clipboard write | **Panic** — kill voices + hard stop |
| `O` | **Clear** — clear active P-Lock; inert if nothing to clear | **Clear** scope contents | **Delete** — remove entity (+ confirm) |
| `P` | **No** — cancel a pending action | *(reserved / dim)* | **Pop** checkpoint (tap) / **Floor** (hold+release) |

- Under any section-suite scope (Track / Phrase / Scene / Song / Morph), `Y U I O` glow
  in the scope colour and take their scoped meaning. `P` dims (reserved for the
  confirm/cancel channel).
- `Func+O` = Delete sets a **pending-confirm** visible in the status band
  ("Delete X?  Y=Yes  P=No"). `Y` = confirm; `P` = cancel.
  No modal popup — PRINCIPLES §5.
- Solo is `Func + Mute + step` (rung 4 — solo reads as "the
  secondary/advanced layer of mute"; PRINCIPLES §15). No verb acts as a
  held modifier: `Y` is only ever a verb (snapshot / dialog-confirm).

The same grammar drives §13.2 Copy/Paste/Clear, §13.3 Performance
Mutes, and the Checkpoint stack in §13.6. The verbs never mean
different things in different scopes — only the scope changes.

**Scope-combining vs ambient (no silent pass-through).** Keys split
into two classes under a held scope. *Scope-combining* keys — the
section keys, the step grid, and the verbs — show **only** their
scoped meaning; if the scope defines no meaning for that key it is
**reserved** (dim, inert), never silently doing its no-scope thing.
*Ambient* utilities — navigation and tap/metronome — keep working and
render neutral. In-scope keys glow in the scope colour (§6.6);
reserved verbs dim. This is the operational form of PRINCIPLES §10
("a held scope recolours the keys it rebinds").

### 13.0 Gesture cost reference

Every live gesture, grouped by its PRINCIPLES §15 rung. The rung is the
held-scope count plus one operand; repeated same-class targets (multi-step
holds, hold-tap-many mutes) do not add cost. This table is the audit made
durable — a new gesture must slot into a rung here, and a *common* action
must not sit deeper than a *rare* one. The user-facing, by-scope
(press-order) view of this same gesture set is README §7; keep both in
sync when a gesture is added or changed.

**Rung 1 — `key` (1 key).** Step tap = toggle trig; verb tap (`Y` (SNAP)
push checkpoint / `U` record-arm / `I` play-pause, double-tap = stop-to-
top / `O` clear active P-Lock / `P` (YES) confirm a pending prompt);
navigation; tap-tempo.

**Rung 2 — `Func + key` (2 keys, Func cheapest).** `Func+Y` (RESTORE)
checkpoint pop (tap) / floor (hold+release); `Func+P` cancel a pending
prompt; `Func+U` omni copy; `Func+I` unqualified paste; `Func+O` delete
(+confirm); `Func+step` P-Lock clear mode; `Func+section` secondary
section layer; `Func+Song` global/project params. (Checkpoint **push** is
the bare `Y` — rung 1.) **Panic** (kill voices): `Song + Clear (O)` —
rung 3.

**Rung 3 — `mod + key` (2 keys).** `Mute+step` global mute (hold-tap-
many); `Scene+step` launch (carry overlay; double-tap = floor);
`Scene+verb` bake (Rec) / revert (Clear) / re-sync-all (Snap);
`Song+step` queue song; `Song+Clear` Panic; `Phrase+step`
unison/set-global; `Fill+step` mark fill; `section+verb` copy/paste/clear
a section; `Trig+verb` step copy/paste/clear; `Track+Nav` cycle input
mode (PLAY/CHROM/LEVELS); `Morph+^`/`Morph+v` pick endpoint A/B (5.2,
planned).

**Rung 4 — `Func + mod + key` (3 keys).** `Func+Track+step` machine/Kit
picker assign; `Func+Scene+step` baseline launch (floor, discard
overlay); `Func+Scene+Rec`/`Func+Scene+Play` Scene copy / paste;
`Func+Section+step` note-edit entry; `Trig+Func+Stop` clear
all P-Locks on the held step(s); **`Func+Mute+step` solo** (the
secondary layer of mute).

**Rung 5 — `mod + mod + key`, cross-column (3 keys).** `Scene+Mute+step`
per-scene mute (active-mask); `Track+Phrase+step` per-track phrase
deviation; `Track+section+verb` track-scoped section copy/paste/clear;
`Track+track-key+Nav` set input mode on a specific track; `Morph+^+v`
assign both endpoints at once.

**Rung 6 — `Func + mod + mod + key` (4 keys, the ceiling).**
`Mute+Func+Scene+Play` floor-only Scene paste (strip the content overlay;
apply floor metadata only). Reserved otherwise; admit a new gesture here
only via the §13 Exceptions table.

**Forbidden — 5+ keys.** No gesture may require a fifth simultaneous
scope key (e.g. `Func + mod + mod + section + step`).

### 13.1 Control-All

Holding the `Track` scope (with no specific track selected) promotes
the next parameter edit to a *broadcast*: the new value is written to
**every** track that exposes a matching slot. Resolution is
**id-primary, role-fallback**:

1. Build the target set `T` = all tracks in the active Song.
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

A single uniform grammar: **hold scope, press verb**. **Copy is always a
deep clone** — all content is duplicated independently. There is no
shallow (structure-only) copy path; machine-only track copies are
done via the Kit picker (`Func+Track`) instead.

| Gesture | Effect |
|---|---|
| `Trig` (hold 1+ steps) + Record | Copy those steps (trigs + condition + P-Locks). |
| `Trig` + Play | Paste clipboard onto the held steps. |
| `Trig` + Stop | Clear those steps' overrides (trig + P-Locks). |
| `Trig` + `Func + Stop` | Clear **all** P-Locks on the held step(s), leaving the trig itself intact. The `Func` qualifier narrows `Stop`'s scope from "clear the step" to "clear locks only". |
| `Trig` + `(MZ slot)` + Stop | Clear **only that slot's** P-Lock on the held step. Targeted by the held slot (the same slot the MZ would write). |
| `Func` + step (P-Lock clear mode) | Hold Func then press a step → step cells re-skin orange: bright for P-locked slots, dim for empty. Press any step cell to clear that slot's P-Lock on the target step. Release Func to exit. Slots 0-15 are shown; each cell maps to one machine slot by index. |
| `Section` key + Record | Copy all of that section's params (base + P-Locks across all steps). |
| `Section` key + Play | Paste section onto current track. |
| `Section` key + Stop | Reset section to default. |
| `Track` (specific track) + Record | Copy the whole track within the Song. |
| `Track` + Play | Paste track. |
| `Track` + Stop | Clear track. |
| `Phrase` + Record | Copy the focused track's phrase. |
| `Phrase` + Play | Paste phrase. |
| `Phrase` + Stop | Clear phrase (back to empty). |
| `Func` + `Scene` + Record | Copy the active Scene to the typed clipboard. The grab is the full **effective** layout (floor + live deviations as content), losslessly. **No move** (cf. the create-and-move `Scene + step`). Baked-vs-floor is chosen at *paste* (§23.3). Type tag = `Scene`; badge = `CPY:SCN`. |
| `Func` + `Scene` + Play | Paste the Scene clipboard onto the active Scene — lays down the **baked** layout. Conflict-gated (§23.3). |
| `Mute` + `Func` + `Scene` + Play | Paste **floor only** (deviations stripped). `Mute` = the "floor-only / strip the overlay" qualifier; distinct from verbless scene-mute by the `Play` verb (§23.3). |
| `Func` + `Scene` + Stop | Clear the Scene to empty / default. |
| `Func + U` (no scope) | **Omni copy** — captures the full live stack: scene + active track + pattern. Type tag = `All`; badge = `CPY:ALL`. |
| `Func + I` (no scope) | **Unqualified paste** — stamps the single captured layer by reading the clipboard's type tag. Rejects with "Paste: pick a scope" if type is `All` (omni grab has no single default). `None` = "Nothing copied". |

> **Why `Scene` copy is `Func`-qualified.** Every other scope's bare
> copy/paste/clear triad is `Record`/`Play`/`Stop`. `Scene` is the one
> exception: its *bare* triad is reserved for live launch-unit verbs —
> `Scene + Record` = **author** (commit-and-bake live state into the Scene —
> folds global/mask into the floor and bakes deviations into home-row content,
> Yes/No-confirmed; §16),
> `Scene + Play` = **launch now**, `Scene + Stop` = **revert to stored**
> (§16). `Func` therefore *lifts* the clipboard triad up one level
> (`Func + Scene + Record/Play/Stop` = copy/paste/clear of a Scene as
> data), exactly as `Func` narrows `Trig + Stop` (clear step) to
> `Trig + Func + Stop` (clear locks only). The clipboard is typed: a
> Scene clipboard pastes only into a Scene slot.

Multi-step holds copy a contiguous *or* discontinuous group: the
clipboard preserves the relative offsets and pastes them back over
the destination held step(s). Pasting a 1-step clipboard over N
held steps replicates. Pasting an N-step clipboard over 1 held step
unrolls forward from that step.

The clipboard is in-memory only (not persisted) and typed. A qualified
copy (`scope + Record`) tags the clipboard with that scope; a qualified
paste (`scope + Play`) accepts that scope or `All`. An omni copy
(`Func + U`) tags it `All`; an unqualified paste (`Func + I`) stamps
the single captured layer using the type tag, and rejects `All` ("pick
a scope"). The UI shows the clipboard type as a small chip in the
transport bar (`CPY:STP` / `CPY:SEC` / `CPY:TRK` / `CPY:PHR` /
`CPY:SCN` / `CPY:ALL`).

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

- **Global mute (per-track).** Lives in the Set (APVTS
  `trackMute` param), not in any Scene or Phrase. Surviving across
  scene changes makes it the natural target for live "drop the
  drums" performance gestures. Entered by `Z + step` (MuteScope +
  step key), toggles immediately.
- **Scene mute (per-track).** The inverse of the Scene's `activeMask`
  (§4.7), serialised as a bitfield. Saved with the Scene and recalled on
  Scene launch — useful for arrangement-style scene variants without
  duplicating notes. Entered by `Func + Z + step`; toggles are deferred
  and applied atomically on `Func` release (see multi-select below).

Mute mask resolution: a track is muted at runtime iff
`global.muted[i] || !scene.activeMask[i]`. Muting is non-destructive (no
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

#### Euclidean print-on-release (deterministic generator)

Distinct from the three modes above — which never alter the pattern — the
**Euclidean generator** is an authoring aid that *writes*. Held as a scope
chord (precise chord at the UI milestone), it re-skins the focused track's
step row and turns the encoders into Euclidean parameters against that track's
`Phrase.length` slots:

- **Pulses** — how many hits are distributed evenly across the length.
- **Rotation / offset** — rotate the pattern's starting position.
- *(optional)* **Accent / velocity** of the generated hits.

While the chord is held, the generated rhythm is **audible and visible** in
real time — the performer dials it in against the playing sequence. On
**release** it *prints*: it **replaces** the trigs within the track length
with the generated pattern (existing trigs in range are overwritten; a
checkpoint pushes first — §13.6 — so the prior pattern is recoverable). The
printed output is **ordinary trig data**, indistinguishable from hand entry
and fully hand-editable afterward.

This is deliberately the deterministic, *prints-to-real-state* form sanctioned
by PRINCIPLES *"Pragmatic determinism"* and *"Reward mastery"*: Lockstep
refuses stochastic / generative authoring (NON-GOALS), but a clocked,
repeatable generator that the performer drives live and commits is welcome.
Euclidean density is not a "make it good for me" button; it is a rhythm you
shape and time.

#### Arpeggiator — DRAFT (PRINCIPLES-cleared, DESIGN-pending)

> **Status:** admitted in principle, *not yet specified*. Recorded here so the
> ROADMAP gate (PRINCIPLES → DESIGN → ROADMAP) has a home to point at; it does
> **not** earn a ROADMAP checklist until the grammar fit below is resolved.

An arpeggiator is admitted **only as a performable engine**, never a
set-and-forget noodler (PRINCIPLES *"Reward mastery"*). Any spec must satisfy:

- **Per-track, deterministic.** Rate, order (up / down / up-down / as-played),
  range / octaves, gate — all deterministic; *no* "random" order (that would
  be stochastic authoring, NON-GOALS).
- **Driven live, P-lockable.** Its parameters live on a section (candidate:
  TRIG or MOD) and are P-lockable per step, scene-assignable, and Control-All-
  able like any slot — so the arp is *played*, not armed and forgotten.
- **Composes, doesn't replace.** It transforms the track's chord-step notes
  (§3.6) and coexists with Morph; it is not a separate authoring mode.
- **Open questions (close before ROADMAP):** which scope/gesture toggles and
  edits it; whether it emits to the pattern (print-on-release, like Euclidean)
  or stays a live transform; interaction with retrig (§13.5) and microtiming
  (§5.1).

### 13.6 Checkpoint Stack

Snapshot is **not** a bespoke global feature — it is a scope-respecting
verb, exactly like every other gesture on the surface (PRINCIPLES §13).
`Func+Yes` snapshots **whatever scope is currently held**, onto *that
scope's own* LIFO stack; `Func+No` walks that same scope's stack back
down. With **no scope held, the scope is the Song** — the default
working unit.

| Held scope | `Func+Yes` snapshots | `Func+No` restores |
|---|---|---|
| *(none)* | the whole **Song** | the Song |
| `Track` | that track's Kit + current Phrase + base params | that track |
| `Scene` | that Scene's saved floor (`globalPattern`, `activeMask[]`, `coreTime`, Morph snapshot) | that Scene |
| `Phrase` | that Phrase (steps, P-Locks, defaults) | that Phrase |

Why these scopes and no finer: content is **Song-owned** and only
*referenced* by Scenes (§4.7), so each scope above captures a distinct,
well-defined slice — a Scene snapshot is *assignment only*, which is
exactly what a Scene owns.

This is the **same floor/overlay split** a Scene already runs (§4.7/§16):
the Scene-scope checkpoint floor **is** the Scene's saved floor, and a
Scene's live overlay is working state above it. So the Scene-level reverts
are one behaviour reached several ways — double-tap a Scene launch,
`Scene + Stop`, or `Func + No` walked to the Scene's floor.

**The floor is the saved state.** Each scope's stack is seeded, on Song
load / Song switch, with a single **floor** entry = that scope's
on-disk saved state. `Func+Yes` pushes working snapshots above the
floor; the floor itself can never be popped away. This folds the
Octatrack "Part-reload" into the stack:

> **"Reload saved" = walk a scope's stack down to its floor.** `Func+No`
> resolves **on key release**, and the hold duration picks the action
> (reusing the tap/hold threshold of §13.7): a brief **tap** pops one
> entry; a **hold** then release jumps straight to the floor (the live
> "reset this to saved, now" move). Resolving on *release* rather than
> press is deliberate — it guarantees a single press produces exactly
> **one** action, never a pop immediately followed by a reload. At the
> floor the gesture is idempotent.

Behaviour notes:

- Only the *floor* is derived from persisted state; entries above it are
  RAM-only and do **not** survive a project save / reload (checkpoints
  are a "scratch take" tool — the project save is the canonical state).
  After reload, the floor is re-seeded from disk, so "reload saved"
  still works even though the scratch pushes are gone.
- Pushing while a stack is full evicts the **oldest** *non-floor* entry,
  preserving the most recent 8 plus the floor.
- A pop is a **deliberate, exact restore** of its scope — it is *exempt*
  from the §13 "broadcast skips deviated tracks" rule (that rule governs
  launch/unison gestures, not explicit undo). You get back exactly what
  the entry held, even if it clobbers a finer edit made afterward.
- A restore never reaches above its scope: a Song-scope pop restores the
  whole Song but never the Set-level sample pool, CC maps, or global
  mute mask — those sit above Song.
- A small UI chip in the transport bar shows the **currently-scoped**
  stack's depth (`CK:N`), so the user sees how many undos remain for
  whatever they are holding.

Checkpoints emerge naturally as a live performance undo: experiment with
a destructive copy/paste or a Control-All sweep, then revert with
`Func+No` if it didn't land — at the grain you were working at. The
stack depth gives a few levels of "two-mistakes-deep" recovery without
bloating into a full DAW-style history.

### 13.7 Latch — virtual hold ✓

The grammar so far is built on *holding*: a held scope modifier sets a
scope, a held step opens its P-Lock editor, a held `Func` raises the
secondary layer. Latch adds one gesture that turns any of those holds
hands-free, **without inventing a new per-key meaning**:

> **Tap** = a momentary action (a step tap toggles `step.trig`; a lone
> modifier tap does nothing). **Hold** = a momentary mode (hold a step →
> P-Lock edit; hold `Func` → secondary layer; hold `Track` → Control-All
> scope). **Double-tap = a virtual hold** — the *same* mode, held for you,
> exactly as if the keys stayed down.

A latched element reads identically to a physically held one (effective-held
= physical OR latched), so nothing downstream of the hold needs to know the
difference. Latch is persistence, not a new clause.

- **Modifiers** (`Track`, `Scene`, `Phrase`, `Morph`, `Song`, `Mute`,
  `Fill`). Double-tap latches the scope; double-tap the **same** modifier
  releases it. Latches obey the §13 compound-chord rules unchanged: at most
  one latch among the left column {`Phrase`, `Morph`, `Mute`} and one among
  the right column {`Track`, `Scene`, `Song`, `Fill`}; latching a second key
  in a column releases the first. Cross-column latches *compose* (latching one
  from each column builds a compound scope), exactly as holding both would.
- **`Func` never latches.** `Func` stays the momentary universal qualifier
  (§13). Its double-tap is reserved as the **universal escape**: it clears
  every latched modifier and every virtual-held step in one gesture. This is
  the literal reading of "the *modality* latches, not the key" — you latch a
  `Func`-combo (e.g. the machine picker, `Func + Track`) by double-tapping its
  *operand* (`Track`), and you leave via `Func`. Because `Func` is the lone
  exception, the escape always lives in the same place; the user never hunts
  for the key that started a mode. (When no latch is engaged, `Func` behaves
  exactly as today — the escape is a no-op and never pre-empts `Func`'s normal
  key-up commits such as the §13.4 deferred pattern-mute multi-select.)
- **Steps are operands, never the exit.** Double-tapping a step virtual-holds
  it into the edit context (P-Lock / trig override), so encoder edits land on
  it hands-free. A **single tap still toggles that step's trig**, even while
  an edit is latched — a tap is a momentary action, unchanged. Double-tapping
  an already-latched step removes just that operand. The *session* is exited
  with `Func` (a step is transient content and makes a poor exit affordance).

**Entering a new modality exits the current one.** Latch persists a *mode*,
so committing to a different mode ends it. In particular, switching a track's
input mode (§34.1, `Track + Nav` cycling PLAY ↔ CHROMATIC ↔ LEVELS) always
clears any latched edit or scope — exactly as the `Func` escape would. There
is never a stale modality lurking under a freshly chosen one.

**Holds latch, verbs amplify.** The five verbs keep their own double-press
meaning — the established one being a double-press of Play = stop + reset
phase (§13 transport). Verbs are instantaneous, so latching them is
meaningless; that they instead carry the "amplified action" reading keeps the
two double-tap families cleanly separated.

| Gesture | Effect |
|---|---|
| Double-tap a modifier | Latch (virtual-hold) that scope |
| Double-tap the same modifier | Release that latch |
| Double-tap a step | Virtual-hold it into the edit context (add operand) |
| Single-tap a step (latched edit active) | Toggle its trig (unchanged) |
| Double-tap a latched step | Remove that operand |
| Double-tap `Func` | Universal escape — clear all latches |
| Double-press a verb | Amplified action (e.g. Play = stop + reset) |

## 14. Signal Path and Post-Machine FILTER / AMP

The sequencer-side signal path for an internal-audio track is:

```
[input fill] → machine.process() → [FILTER] → [AMP] → [FX1] → [FX2] → track sum
track sum → [master FX 1] → [master FX 2] → master gain → out (+ cue split)
```

The optional `[input fill]` stage (§27) is present only for machines
that declare an `input_source`; for an ordinary synth or sampler the
machine synthesises into an empty buffer and the stage is a no-op.
`[FX1]`/`[FX2]` are the per-track insert effects and `[master FX 1/2]`
the global effects — both described in §32.

`[FILTER]` and `[AMP]` are sequencer-side DSP blocks owned by the
track (not the machine):

- **FILTER.** A multi-mode state-variable filter (LP / BP / HP / Notch),
  parameterised by Cutoff, Resonance, Drive, and an
  Envelope-amount-to-cutoff that follows AMP's envelope.
- **AMP.** The track's amplitude envelope (AHDSR), plus the track's
  **output mix**: Level, Pan, and two send levels (Send A / Send B) to
  the master Send-mode effects (§32). AMP responds to the sequencer-
  emitted note-on/off pair — this is what gives every track a
  consistent envelope feel regardless of which machine it hosts.
  - **AMP gate source** (`{Envelope | Held-open}`). Default `Envelope`:
    the amplitude stage follows the note-on/off envelope, so the track
    needs trigs to sound. `Held-open` keeps the amplitude stage open
    continuously — the basis of a continuous **Thru** (gated external
    audio becomes always-on pass-through) and of **drones** on synth
    tracks. For ordinary trig-driven machines `Held-open` has no
    audible effect (they still need a trig to produce sound). It
    composes with trigless/lock-only trigs (§30): an open-AMP track
    still accepts lock-only steps that sweep parameters without
    retriggering.

This is the Elektron model: a machine focuses on producing raw
audio at unity gain; the surrounding FILTER + AMP + FX is uniform. The
canonical FILTER (key 5), AMP (key 6), and FX (key 8) section buttons
drive these blocks regardless of which machine the track hosts.

Machines may opt out:

- `IMachine::hasInternalFilter()` returning `true` causes the
  sequencer to bypass the FILTER block; the FILTER section button on
  that track is repurposed as a passthrough to the machine's
  internal filter slots.
- `IMachine::hasInternalAmp()` similarly bypasses AMP.

Opt-out is rare and reserved for analog-emulation engines where
filter and envelope are tightly coupled to the sound (e.g. an
Analog Four-style synth). Most machines accept the default.

For a MIDI-out track (§15), both FILTER and AMP are bypassed
implicitly — there is no audio to filter or amplitude-shape — and
the FILTER/AMP section buttons are repurposed to expose
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

`MidiOutMachine` reports `currentVoices() = V0` (unbounded). The
sequencer passes the step's full chord through unchanged, with no
clamp; note-offs are scheduled by gate length as for any track, and
overlapping note-ons are forwarded verbatim.

## 16. Songs and Scenes — Live Launch Model

The performance model is **launch-based, not arrangement-based**. The
performer navigates the Set by launching Scenes (within the active
Song) and switching Songs (to change songs), improvising deviations on
top via per-track phrase swaps. This is the Ableton Session View /
Deluge / Polyend Tracker model: rows are triggered live, not played back
from a written timeline.

Each Scene is a **saved floor + a live overlay** (§4.7): the floor is the
Scene's stored global pattern, active mask, and timing/morph fields.
Per-track phrase deviations are **never** part of the floor — they are
session-RAM-only and exist only in the overlay. Each Scene remembers its
own overlay while the set runs.

**Scene launch.** Three modes, two gestures:

- **`Scene + step` on a different Scene** — carry the current live overlay
  to the new Scene (deviations and any live global-pattern changes persist
  above the new Scene's floor).
- **`Scene + step` on the active Scene** (or `Scene + Stop`) — revert:
  discard the active Scene's overlay and return to its saved floor. This is
  the "reset to stock" move.
- **`Func + Scene + step`** — **baseline launch**: switch to any Scene at
  its clean saved floor, discarding all overlay state regardless of which
  Scene you pick.
- **Double-tap** (kept as a quick shortcut) — same effect as baseline for
  the tapped Scene; equivalent to `Func + Scene + step` on that Scene.

At the launch boundary the band takes the Scene's effective phrases
(floor, or floor+overlay for a single-tap), the active mask
(`activeMask[t]`) takes effect, and core time + Morph snapshots update. A
pending Scene shows as a badge in the top bar; a second `Scene + step`
before the boundary replaces the queue ("change your mind").

**Song switch.** `Song + step` queues a Song change, quantized by the
*currently playing* Scene's core time. At the boundary, the incoming
Song's first Scene establishes the new meter and phrase assignments, and
all tracks' kits update. **Deviations are cleared on a Song switch** — a
song change is a full performer reset; the musician steps up with fresh
assignments.

**Phrase swaps and deviation** (the live overlay; full grammar in §4.7):

- `Track + Phrase + step` — **deviate** the focused musician to the picked
  phrase.
- `Phrase + step` — **unison / set-global**: set the Scene's global pattern
  to the picked phrase; the focused musician un-deviates and joins; other
  deviated musicians keep their deviation (*"more specific scope wins"*).

There is **no force-all or separate re-sync gesture**. To rejoin one
musician, pick their **home** (global) phrase — the phrase selector borders
it (§4.7). To clear all deviations, re-launch the active Scene
(`Scene + active-step`) or use `Func + Scene + step` (baseline launch,
above). The old `Track + Scene`, `Scene + Yes`, and `Scene + Phrase + step`
gestures are removed.

**Scene commit / revert** (the bare `Scene` triad; the copy/paste/clear
meanings are `Func`-qualified, §13.2):

- `Scene + Record` — **commit-and-bake** (Yes/No-confirmed): write live
  global-pattern and mask changes into the Scene's saved floor, *and*
  **bake** the per-track deviations into content — each deviated track's
  effective phrase is copied into its home-row (`globalPattern`) slot, then
  the deviation clears. This is the **only persistence path for a
  hand-curated heterogeneous arrangement**: the floor stores no routing
  vector, so the curated phrases are materialised onto the diagonal *as
  content*. Because it overwrites phrase slots and severs phrase sharing
  (the baked slot is a snapshot, §4.7), it is destructive and **prompts a
  Yes/No confirmation** — the standard guard for destructive ops, and the
  reason the surface carries a Yes/No pair. *Scratch-pad workflow:* park on
  one Scene, audition ideas into the high phrase slots via
  `Track + Phrase + step` deviations, then `Scene + Record` → `Yes` bakes
  the keepers onto the home row.
- `Scene + Play` — **launch now**: an unquantized re-fire of the active
  Scene immediately (apply its phrases + active mask now, not at the next
  core-time boundary).
- `Scene + Stop` — **revert**: discard the live overlay, return to the
  Scene's saved floor. Same behaviour as a double-tap launch of the active
  Scene, and the same restore the §13.6 Checkpoint floor reaches via
  `Func + No` walked to a Scene scope's floor — one behaviour, several
  doors.

**No stored arrangement.** There is no song timeline, no arrangement
track, and no chain queue (the RAM-only pattern chain of earlier versions
is removed). The performer's plan for the set lives in muscle memory and
rehearsal: queuing the next Scene now, and adjusting in real time. The
Checkpoint stack (§13.6) provides the last-second undo layer.

> ⚑ **Pattern chaining (planned, future).** If a hands-off "play these
> phrases in sequence" tool is later wanted, it rides *on top* of this
> model as **automation that drives the global pattern / deviations over
> time** — not a return of the fixed pattern chain. It changes nothing in
> the floor/overlay model; it just scripts the same live gestures.

## 17. Morph and the Crossfader

> **Naming.** The continuous A/B crossfader morph was called a "Scene"
> in earlier drafts (after the Octatrack). It is now **Morph**, because
> "Scene" was reassigned to the launchable row (§4.7) to match every
> clip-launcher DAW. The Morph lives *on* a Scene.

The Octatrack's crossfader is the one continuous-axis performance
control in the design. It is not a duplicate of the Checkpoint
stack (§13.6): checkpoints are *discrete, whole-state, stack-shaped*
("snapshot now, jump back later"); the crossfader is *continuous,
selective, two-pole* ("morph smoothly between two curated parameter
sets"). They complement each other — a typical workflow is to
checkpoint a Scene before authoring a Morph A/B pair, then perform
the live morph itself with the fader.

### 17.1 Data model

The Morph lives on the **Scene** (DESIGN §4.7). Each Scene carries a
pair of morph snapshots:

```
Scene.morphA : map<(trackIdx, slotIdx) -> float>
Scene.morphB : map<(trackIdx, slotIdx) -> float>
```

Each endpoint is a sparse map covering whichever (track, slot) pairs the
user has assigned to that endpoint. The map shape is identical to a
P-Lock map but addressed across the whole Scene rather than per-step.

Attaching the Morph to the Scene (rather than the Phrase) means all the
musicians playing a given Scene share one morph pair — consistent with
the musical meaning: "verse" has its own sound space that applies to
everyone in that scene, regardless of which specific phrase each
musician happens to be playing. Authoring a different morph pair means
authoring a different Scene.

### 17.2 Runtime state and resolution

The fader value `f ∈ [0, 1]` is RAM-only runtime state (it does not
serialize — it is a controller axis, like a held key, not a stored
field). Default rest = A (f = 0).

Resolution follows a three-tier precedence per (track, slot, step):

```
effective(track, slot, step) =
    step.pLock[slot]           // P-Lock — phrase layer wins
  ∨ morphBlend(track, slot, f) // morph tier — only if slot in A ∨ B
  ∨ kit.baseParams[slot]       // kit base — song layer
```

`morphBlend` uses **mirror resolution**: an absent endpoint reads as the
*other* endpoint (then falls to kit base if both are absent):

```
aVal = morphA[(t,s)] ?? morphB[(t,s)] ?? kit.baseParams[s]
bVal = morphB[(t,s)] ?? morphA[(t,s)] ?? kit.baseParams[s]
morphBlend = stepped ? (f < 0.5 ? aVal : bVal) : lerp(aVal, bVal, f)
```

Mirror semantics mean the fader is **inert until A and B genuinely
differ** — a half-authored morph (one pole only) does nothing to the
sound, so incomplete edits never cause unintended fades. Both poles
must be explicitly sculpted before the fader does anything.

Stepped slots (`ParamSpec::stepped == true`) snap at `f = 0.5` instead
of lerping. MIDI-out `channel` and `program` (§15) are stepped and snap
with a clean note-off on the previous channel at the crossing point.

**P-Locks still dominate.** A step that P-Locks a slot bypasses the
fader for that step — the Morph is non-destructive to phrase-layer
intent.

**The Morph morphs parameters only — never trigs.** Morphing trigs is
ill-defined and is served by scene switching (§16) and mutes (§13.4).
This keeps the resolver a clean OEB-plus-lerp model (`PRINCIPLES.md` §7).

**Kit invariant.** A track runs the same machine in every scene of a
song (the Kit is Song-scoped, §4.7). Scene-scoped morph is coherent
because the slot address space is identical across all scenes —
morph overlays address the same slots regardless of which scene plays.

**Shadowing footgun.** Morph values are absolute (not deltas). A kit
edit of a morphed slot is overshadowed by the morph overlay in any
scene that assigns that slot. The MZ flags morph-owned slots with A/B
chips so the performer can see which slots are morph-controlled
(PRINCIPLES §10).

### 17.3 Modifier-gated sculpting

The `Morph` key is the **scene-layer selector**: holding or latching it
routes encoder edits into the morph overlay, exactly as holding a step
routes edits into a P-Lock. Bare encoder edits (no `Morph` held) always
write to the kit base.

**Sculpting at fader position.** Hold or latch `Morph`, turn an
encoder → the edit is split across A and B at the normalised
proportional split for fader position `f`:

```
D  = (1−f)² + f²
da = Δ · (1−f) / D   // added to morphA[(track, slot)]
db = Δ · f / D        // added to morphB[(track, slot)]
```

This ensures the **heard value tracks the gesture 1:1** at every
position — `da·(1−f) + db·f = Δ`. At f = 0 the whole edit lands in A;
at f = 1, in B; at f = 0.5 both endpoints move by Δ together.

If either endpoint map entry does not yet exist, it is initialised from
the resolved value (via mirror resolution) before the delta is applied —
so the first edit at any fader position is always heard as Δ.

**Pole-forcing with `^`/`v`.** QWERTY has no fader; Nav qualifiers
override the ratio:

- `Morph + ^` + encoder → pure A write (ignores fader position)
- `Morph + v` + encoder → pure B write (ignores fader position)
- `Morph + ^` + `Stop` on a slot → removes from A's map
- `Morph + v` + `Stop` on a slot → removes from B's map
- `Morph + Stop` on a slot → removes from both maps (clears assignment)

On a hardware surface with a physical fader, `^`/`v` are natural
"commit this exact position" shortcuts; on QWERTY they are the primary
assignment path.

**Stepped slots.** Stepped slots cannot be split; a sculpt write goes
to the resolved side (f < 0.5 → A, else B).

**P-Lock dominance.** When a step is held (EditContext active), the
edit targets the step's P-Lock regardless of Morph scope.

The MZ renders morph-assigned slots with A/B chips showing the captured
endpoint values, flagging that kit edits of those slots are shadowed
here (the footgun from §17.2).

**Fluid mute.** `Morph + Mute` on a track captures `AMP Level →
silence` into the near pole (A if f ≤ 0.5, B if f > 0.5) and writes
unity into the far pole, giving a continuous fade path across the
fader. Binary performance mutes (§13.4) remain a separate,
instantaneous mechanism.

### 17.4 MIDI-out parity

The Morph applies identically to MIDI-out tracks (§15): the generic
`cc[0..15]` slots are continuous and lerp smoothly; `channel` and
`program` are stepped and snap. The live-morph use case
("crossfade between two Digitone patches by morphing 16 CCs at
once") is one of the headline workflows that justifies treating
MIDI-out as a first-class machine.

### 17.5 Hardware and software surfaces

The hardware controller carries one physical fader. In software,
the same axis appears as a vertical slider in the transport chrome,
mouse-draggable, plus a user-assignable CC mapping (MIDI-learn, 5.2) so any external
surface can drive it.

There is no QWERTY mapping for the fader axis: continuous gestures
on a typing keyboard are a poor fit and would only invite muscle
memory the hardware can't satisfy. The fader's continuous value comes
from mouse / CC / hardware fader only — consistent with pillar 1
("hardware = fewer-key QWERTY"): the QWERTY layer omits the one
axis the hardware can't reduce to a button.

The assignment gesture uses a **single `Morph` modifier** (one key,
not a Morph A / Morph B pair — 3.1, §33). Endpoint selection is
a compound: `Morph + ^` (NavUp) targets **endpoint A**, `Morph + v`
(NavDown) targets **endpoint B** — mirroring the vertical fader's A-top /
B-bottom throw. On hardware the fader position picks the near endpoint
directly, so the explicit `^`/`v` qualifier is the QWERTY-only path.
The software fader sits as a vertical slider on the right of the
encoder band (§26.1), spatially aligned with the encoder rows.

### 17.6 Write routing summary

| Morph held? | Step held? | Edit destination |
|---|---|---|
| No | No | Kit base (`track.baseParams`) |
| No | Yes | Step P-Lock |
| Yes | No | Morph overlay at fader split (§17.3) |
| Yes | Yes | Step P-Lock (P-Lock always wins) |

The `Morph` scope selects the **scene layer** for encoder edits.
Everything else (base edits, P-Lock authoring) is unaffected by whether
Morph is engaged. This is consistent with the modifier grammar: a scope
declares what you are editing, not a separate mode.

## 18. Roadmap Reference

The milestone plan, phase grouping, status flags, and progress
checkboxes live in `ROADMAP.md` — the single source of truth for *what
ships when*. This document specifies the *what* and *why*; the roadmap
sequences it. (`ROADMAP.md` cites the relevant section here from each
milestone.)

## 19. Microtiming, Swing, and Quantize

Two independent mechanisms shape sub-step timing: a per-step
microtiming offset, and swing. They compose linearly. Swing itself
operates at two granularities — a project-global master pocket and a
per-track groove — summed additively into a single effective value.

### 19.1 Per-step microtiming offset

Each `Step` carries `microOffset : float ∈ [-0.5, +0.5]`, expressed
as a fraction of the step's length (which itself follows the track's
clock divider). At resolve time, the trigger sample for that step
is shifted by `microOffset × step_samples` from its grid position.
A value of `0` (the default) leaves the trig on-grid; `+0.25` pushes
a quarter of a step late; `-0.5` lands the trig exactly between the
step and its predecessor.

The range is intentionally ±50%, not ±100%: a value beyond ±50%
would push the trig into another step's cell, which makes
"the step at position N" mean different things in different
contexts. Capping at ±50% keeps the mental model "every step fires
within its own cell, possibly nudged."

Microtiming is captured automatically by live record (1.8): a
note-on arriving between step boundaries records to the nearest
step with the residual delta stored as the offset. It is also
P-lockable per step via the usual EditContext gestures (held step +
encoder turn on the appropriate slot — the TRIG meta section gains
a `MicroTime` column when extended).

### 19.2 Swing (global + per-track, additive)

Swing is a **signed** off-beat displacement, expressed as a fraction
of the step length. `0` (the default) is perfectly straight. `+0.5`
pushes every odd-indexed step a full half-step late (classic
maximum-shuffle). `−0.25` pulls odd steps a quarter-step early
(slightly rushed, ahead-of-the-beat feel). The range mirrors
`microOffset`: ±50% of the step length.

Two levels contribute:

| Level | APVTS parameter | Range | Default |
|---|---|---|---|
| Global | `swing` | `[-0.5, +0.5]` | `0` |
| Per-track | `track_t_swing` | `[-0.5, +0.5]` | `0` |

They are **composed additively** at resolve time and clamped:

```
effectiveSwing(t) = clamp(globalSwing + trackSwing[t], −0.5, +0.5)
```

Both levels live in APVTS (instance-global, like `track_t_length`
and `track_t_divider`), so they persist across scene and song
changes. This matches the same "feel is a global/track property,
not a phrase property" reasoning that puts per-track `divider` in
APVTS rather than in `TrackKit`.

**Summation seam.** The implementation is a single summation:
`effectiveSwing = Σ (levels present)` clamped at the end.
Reserved slot for Scene-level swing (the sanctioned next level
to add): `effectiveSwing(t) = clamp(globalSwing + sceneSwing +
trackSwing[t], ±0.5)`. Song- and Phrase-level are further deferred;
if added, they each contribute one addend to the same sum.

**Combined cap.** At emit time the total sub-step shift on a step is:

```
totalShift = clamp(effectiveSwing(t) × {1 if odd step, 0 if even}
             + step.microOffset, −0.5, +0.5)
```

This preserves §19.1's "every step fires within its own cell"
guarantee and bounds the look-ahead scan to ≤ half a step.

**Effective-swing readout.** The TRACK meta section shows the
current `effectiveSwing(t)` value so the additive composition is
never opaque. The readout updates live as either level changes.

Swing is per-track (and per-track different from the global addend)
because polymetric tracks have independent step lengths; the global
value anchors the project groove and the per-track value allows
independent depth for layered parts (e.g. hi-hat at deeper swing
than bass).

### 19.3 The Quantize verb

Quantize zeros microtiming offsets in scope. It composes onto the
existing verb set via `<scope> + No`:

| Gesture | Effect |
|---|---|
| `Trig` (1+ steps held) + `No` | Zero those steps' `microOffset`. |
| `Track` (single track held) + `No` | Zero all `microOffset` values on that track. |
| `Phrase` + `No` | Zero `microOffset` across every step of the active phrase. |

(`No` was previously used only for checkpoint pop. Pop is scoped to
"no scope held + Func + No" — see §13.6 — so the two readings of
`No` do not collide: with any scope held, `No` quantizes; without
a scope, `Func + No` pops a checkpoint.)

Quantize never touches trigs themselves, P-Locks, or trig
overrides — only the timing offset. To remove a trig entirely, use
`Stop`.

Swing is not affected by Quantize. To reset swing, set it via the
TRACK meta section like any other parameter.

### 19.4 Musical gate values (3.8)

Gate length is **musical**, not absolute. `TrigOverride.gateValue`
is a closed enum: note value `{1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1,
2, 4}` × modifier `{plain | dotted | triplet}`, plus a `None`
sentinel meaning "use the track default." The resolver converts to
samples at emit time using the current BPM.

The motivation is the same as for swing being per-track: once
tracks can carry different step subdivisions (polymetric clocking,
§4.2; future per-track step subdivisions beyond polymetric ratios),
a gate expressed as "3 steps" stops meaning anything portable. A
1/8-dotted gate sounds like a 1/8-dotted gate on every track, at
every tempo, regardless of step length. MZ renders the value
textually via `valueLabels` (`1/8`, `1/8.`, `1/8T`).

The realtime record path rounds the observed note-on→note-off span
to the nearest musical value at capture time (using the BPM at
capture, not at playback — captures are committed to a definite
musical interval). The step-hold capture path does the same on the
captured chord's gate.

## 20. 16-levels Trig-Grid Mode

**Implementation note (3.9).** 16-levels lands as the **LEVELS**
input mode on a per-track basis (see §34, `TrackInputMode`), not as
a momentary chord on top of the trig grid. The §20 design below
still describes the *behaviour* of the mode; what's changed is the
*entry*: instead of "hold a mode chord," the user puts the focused
track into `LEVELS` mode via the `Track + Nav` mode selector
(§34.1), and the step cells reinterpret as quantised value
buckets while the mode is active. 3.9 ships **velocity-first**
(binding fixed to `velocity`); §20.1's eligibility set and §20.2's
encoder-binding selector remain the design target for the follow-up
generic sub-mode but are not in 3.9 itself.

The 16-levels mode (DESIGN §13.5 extension) repurposes the trig
grid into a value-selection surface. While the mode is active,
pressing trig key `i ∈ [0..15]` writes the value
`v = lerp(spec.minValue, spec.maxValue, i / 15)` to a
**bound parameter**.

### 20.1 Eligible parameters

16-levels is intentionally restricted to role-tagged slots whose
roles are in a closed eligibility set: `velocity`, `pitch.coarse`,
`cutoff`, `resonance`, `attack`, `decay`, `release`, `lfo.depth`,
`level`, `pan`, `drive`. The eligibility set lives next to the role
enum and grows by patch as new roles emerge.

A slot is eligible iff (a) its `role` is in the set and (b) its
machine declares it on the focused track. Machines opt into
16-levels purely by tagging their slots with canonical roles — no
extra schema field is required.

### 20.2 Binding and writes

While the mode is active, one role is the current binding. It
defaults to `velocity`. Holding the mode chord (see §13.5) + turning
an encoder cycles through eligible roles present on the focused
track. The chrome shows the active binding loudly.

A key press writes per the standard EditContext rules:

- **No step held, no record-arm:** writes the value to the focused
  track's `baseParams[slot]`.
- **Step held:** writes a P-Lock onto the held step(s).
- **Record-arm engaged, no step held:** writes a P-Lock onto the
  next emitted step on the focused (or armed-set) track.

### 20.3 MIDI-out parity

16-levels works identically against MIDI-out tracks: any `cc[i]`
slot that has been tagged with an eligible role (via the per-track
CC name table) participates. This is the principled way to set
"pick one of 16 velocities" or "pick one of 16 filter cutoffs"
across a Digitone or Syntakt without leaving the trig grid.

## 21. Audition and Cross-Track Live Record

The performance grammar already provides everything needed for
audition; this section pins down the concrete gestures.

### 21.1 Preview (non-recording)

Two preview gestures, both expressed in the existing scope+verb
grammar:

| Gesture | Effect |
|---|---|
| `Trig` (one step held) + `Yes` | Fire that step's *resolved* trig once, off the sequencer's schedule. P-Locks, trig overrides, and the OEB-resolved note/velocity/gate all apply, so the user hears exactly what that step will produce when it next fires. |
| `Track` scope held (no track-key) + `Yes` | Fire the focused track's *base* trig once: `Track::defaultNote`, `Track::defaultVelocity`, `Track::gateLength` at the current `baseParams`. Useful for "what does this track sound like right now?". |

Both bypass the sequencer event stream: the sequencer injects a
one-shot note-on/off pair directly onto the target track's
MidiBuffer for the current block. The audio path is otherwise
identical (post-machine FILTER/AMP applies). Neither gesture writes
into the pattern.

### 21.2 Cross-track live record

Live-record behaviour follows the channel mode (DESIGN §5.4):

- **Omni mode.** Recording is global: the existing 1.8 record-arm
  state is the single toggle, and every note-on captures to the
  *focused* track's pattern only. Switching focus mid-record is
  the gesture for capturing across tracks.
- **Per-Track-MIDI mode.** Each track carries a `recordArmed`
  boolean. Only armed tracks capture incoming MIDI on their own
  channel. Default arm is the focused track; arms persist with
  the project. UI on the TRACK meta section.
- **Arm-all.** In Per-Track-MIDI mode, `Func + RecordArm`
  (`Func + T`) toggles every track's arm in lockstep. The chord
  cleanly composes with the existing `T` (arm focused) and `Q`
  (Track scope) keys.

In all modes, EditContext rules apply unchanged: a note-on arriving
while a step is held writes to that step's `noteOverride`, even with
record-arm off — the single-input-gate rule (DESIGN §5) doesn't
fork for audition.

### 21.3 Step-as-keyboard live record

When 3.9's CHROMATIC input mode (the shipped form of the old keyboard
trig-grid mode) is active and record-arm is on, each trig-key press
writes the corresponding note onto the *next emitted step* on the
armed-set (or focused) track. This is
the canonical "play in a melodic line" gesture, and it composes
identically with Retrig (5.7), Slice (5.7), and Sound Pool
(5.7) modes — each writes its respective payload (rate-P-Lock,
slice-index, sound-id) onto the next emitted step. The grammar is
the same; only the payload differs.

### 21.4 Step-hold capture window (3.5 — implemented)

Hold-step + play notes + release is the **canonical chord-edit path**
on a step. The transport need not be running and record-arm need not
be on: holding a step opens a *capture window* on that step; every
MIDI note-on that arrives while the step is held accumulates into a
temporary chord buffer; releasing the step commits the buffer.

Commit semantics:

- The committed chord replaces the step's
  `TrigOverride.notes[] / noteCount` (reusing the 4.2 chord-step
  model, up to `kMaxNotesPerStep = 4`). Notes are written
  immediately as they arrive; the chord is finalised (velocity,
  gate) on step release or last note-off.
- **Empty buffer = no change.** Holding a step without playing any
  notes preserves existing data — the gesture is non-destructive
  unless notes are actually played.
- **Velocity** = highest velocity among all captured note-ons
  (written to `TrigOverride.hasVelocity / velocity`).
- **Gate** captured from the note-on→note-off span: if all captured
  notes are released before the step is released, the gate is the
  span from first note-on to last note-off. Otherwise (notes still
  held when step is released) the track default `gateLength` is
  used.

Coexistence:

- **Replaces** the per-playhead behaviour as the *canonical* editing
  path. The earlier transport-time record-arm capture (§21.2)
  remains as the **live performance** flow: while record-arm is on
  and the transport is running, played notes still land on the
  currently-firing step on the armed-set's tracks. The two are
  orthogonal and never run simultaneously on the same step.
- Composes with all other modifiers — holding a step is just another
  modifier in the single-input-gate rule (§5) — so a chord captured
  during step-hold can still be augmented with section-page
  P-Locks before release.

There is no per-note add/remove/swap editor in Phase 3; **replace on
hold** is the editor. A finer-grained editor only lands later if
play-testing proves replace-only too coarse.

#### 21.4.1 Velocity and gate (3.8 amendment)

The 3.5 implementation captured **one** velocity (max across the
chord) and a raw-ms `gateMs`. 3.8 lifts both:

- `TrigOverride.velocities[kMaxNotesPerStep]` (`uint8`, gated by
  `hasNoteVelocities`) gives each note its own velocity. **Realtime
  record** preserves per-note velocity (each MIDI note-on writes
  its own). **Step-hold chord-snapshot** uses the **mean** of
  currently-held MIDI velocities and writes it uniformly across the
  captured chord — coarse but matches the gesture (the user picked
  a chord, they didn't perform it). Emitted MIDI note-ons carry the
  per-note velocity; machine API is unchanged.
- `TrigOverride.gateValue` replaces `gateMs`. It is a musical note
  value (see §19.4) rather than absolute time, so gates survive BPM
  changes musically and decouple from any future per-track step
  subdivision (a "3-step gate" stops meaning anything once track A
  is 1/16 and track B is 1/4). Step-hold capture rounds the
  observed note-on→note-off span to the nearest musical value;
  realtime record does the same.

#### 21.4.2 Trig / notes / P-Locks decoupling

`step.trig`, `trigOverride.notes[]` (with velocities + gateValue),
and the step's P-Locks are **three independent storage axes**.
Toggling any one of them does not touch the others. This was
latent in the data model from 3.5 but only becomes visible in
3.7:

- Step cells with `trig == false` but `noteCount > 0` or P-Locks
  present render dim while still showing the note-count badge and
  P-Lock dot, so authored-but-muted material is discoverable at a
  glance.
- Three clear gestures cover the three axes:
  `Trig + step` toggles `step.trig`;
  `Trig + Func + No` clears notes + velocity + gateValue;
  `Trig + Func + Stop` clears P-Locks.

This supports the "sketch a chord progression, mute trigs to find
the part" workflow without losing authored chords.

#### 21.4.3 Realtime record: overwrite vs overdub

The live/quantized record path (record-arm + transport running, no
step held) uses an **absolute** quantized step number to track visits:

- **Same absolute step number** within a loop = same visit. Notes
  aggregate into a chord (dedup, `kMaxNotesPerStep` cap). This is how
  simultaneous MIDI notes land on one step chord.
- **New absolute step number** = new visit. In **overwrite mode**
  (default), the step is cleared before the first note of the new
  visit, so each pass through the pattern replaces whatever was there.
  In **overdub mode**, the clear is skipped: notes accumulate across
  passes until the chord is full.

**Gesture:** single-tap Record arms overwrite record (Record button
red). **Double-tap Record** toggles overdub mode on top of record arm
(Record button amber, labelled "Overdub"; QWERTY `U` shows "OD" in
amber). Single-tap while in overdub disarms overdub and returns to
plain record; a second single-tap disarms record entirely.

Overdub is automatically cleared when record is disarmed (`Clock`
invariant), so it never persists silently across sessions.

Step-hold capture (§21.4) and note-edit overlay (§20.3) are
unaffected — both already have explicit replace/append semantics
independent of the record arm state.

## 22. Sampling and Resampling

A single capture flow underlies both "sample audio coming into the
plugin" and "resample a track or master output."

### 22.1 Capture sources

The Sampling overlay (chord TBD, consistent with §13 grammar)
opens a non-modal capture surface with a source picker:

| Source | Meaning |
|---|---|
| **Plugin audio input** | The plugin's sidechain / audio input bus. Whatever the host (or standalone audio device) routes in. Default. |
| **Track N** | The audio output of track `N`, post-machine and post-FILTER/AMP, pre-master. Selectable via `Track + Sampling` chord. |
| **Master** | The plugin's stereo main output, post-master gain. Selectable via `Song + Sampling` chord (Song scope reads as "the whole Set's output"). |

System / device input is explicitly **not** a source. The user
routes whatever they want through the host (or standalone audio
device) and into the plugin's audio input. This keeps the capture
path the same in every context.

### 22.2 Capture modes

Two modes share the source picker:

- **Free-form.** `Record` starts capture, `Stop` ends it. Capture
  duration unconstrained. Suited to one-off grabs.
- **Capture-N-bars.** Hold `Sampling + length-key` (1 / 2 / 4 / 8)
  to arm. Capture begins at the next bar boundary and ends
  precisely after N bars. Suited to performance use: "grab the
  next 4 bars exactly."

In both modes, the capture writes to a temporary buffer; on stop /
completion the naming flow opens.

### 22.3 Naming flow

On capture completion the UI offers five candidate names:

- **Four `adjective-noun` pairs** drawn (deterministically by
  capture time-of-day) from bundled wordlists. The wordlists ship
  in the repo as JSON; ~300 entries each, curated for evocative
  brevity ("brittle-loop", "loose-thump", "smear-bell"). Encoder
  1 scrolls adjectives alphabetically; encoder 2 scrolls nouns.
- **One hash-derived pseudo-word**, generated from the sample's
  `xxHash32` content hash via a small consonant-vowel grammar
  (e.g. "kovet", "bilum"). Deterministic per-content, so the
  same capture always proposes the same pseudo-word.

A third encoder cycles which of the five is highlighted; `Yes`
accepts the highlighted name; `No` cancels and discards the
capture. The user can also type a name directly via QWERTY at any
point — typing replaces the highlighted candidate.

### 22.4 Pool integration

Accepted captures land in the project's sample pool with the
chosen name. The on-disk path is project-relative
(`<project>/samples/recorded/<name>.wav` by convention; absolute
paths still supported for drag-and-drop samples). After write,
the sample is indistinguishable from any other pool entry —
P-lockable, slice-able, refed by `xxHash32`.

This keeps `PRINCIPLES.md` §12 intact: even captured audio leaves
the plugin state as a `{path, xxHash32}` ref, never embedded PCM.

**Freeze-to-disk.** The naming flow is also the promotion path for
the *volatile* buffers written by recorder trigs (§28). A volatile
buffer is a RAM-only pool entry with no file backing; running it
through this naming flow writes it to `samples/recorded/` and
promotes it to an ordinary persistent, serialisable entry. The
manual §22 capture and the sequenced recorder trig therefore share
one capture target (a volatile pool entry) and one promotion gesture
(this flow); the only difference is *when* capture is triggered.

## 23. Song/Scene Management UI

This section is derived against the `Set > Song > {Scene, per-Track
Phrase, per-Track Kit}` hierarchy (§4.7) and the §16 launch model. The
old Bank/Pattern/Part gestures (copy-across-banks, the Fork-Part chord)
are gone: Parts dissolved into Kits (§4.7.1–2, recalled live via
`Func + Track`), and there is no bank dimension — a Song *is* the bank-
sized unit.

The hierarchy (DESIGN §4.7) and the queue gesture (§16) underlie this:
the management UI sits on top, letting a performer navigate, label, and
re-arrange the hierarchy *during performance* without halting playback.

### 23.1 Names, colours, tags

Each Song, Scene, and Phrase carries:

- A short user-editable **name** (≤16 chars). Defaults are
  slot-derived (e.g. Song `A`, Scene `A03`, Phrase `A03·T2·p4`).
  Editable inline; no modal dialog.
- A **colour** from a small palette tied to the §24 state-colour
  taxonomy. Used in the queued-pattern chip, chain badges, and
  the browser. Tracks-by-eye colour grouping ("intro / chorus /
  drop") is the headline use case.
- An optional **tag** string (≤24 chars), free-form. Searchable
  in the browser; not surfaced in chrome.

### 23.2 Browser overlay

A non-modal Browser opens via a Func-layer chord (TBD, consistent
with §13). It shows **Song → Scene** as a focusable tree, with a
secondary per-track view (each track's **Phrase** pool and current
**Kit**), carrying names, colours, tags, and a **share-count badge
(SHR:N) on Phrases** — how many Scenes in the Song assign that phrase
(count of Scenes whose `globalPattern` targets that row), the share
relationship that survived the Part→Kit dissolve. Navigation is keyboard-driven (the existing arrow-row keys).

The browser is non-modal: playback continues, the sequencer
continues advancing, all existing chrome remains visible. Pressing
a step-row key on a highlighted **Scene** triggers the existing
queue gesture (DESIGN §16); pressing `Yes` cues it (queue without
immediately playing); pressing `No` cancels a pending cue.

### 23.3 Copy / move / duplicate

There is no bank dimension to copy across — the Set holds Songs, a Song
holds Scenes and per-track Phrase pools. Copy/paste/clear reuse the
§13.2 verbs with an optional destination prefix:

- **Scene.** `Func + Scene + Record` + a **source step_key** copies *that*
  Scene's effective layout to the typed clipboard (no step = the active
  Scene) — a lossless grab, source-selectable, and crucially **no move**
  (distinct from the create-and-move `Scene + step`, which seeds a new slot
  *and* relocates the active Scene). Holding a destination **step_key** under
  `Func + Scene + Play` pastes into that Scene slot (no prefix = the active
  Scene). The grab is full/effective; **baked-vs-floor is chosen at paste**
  (§23.3): plain paste lays down the baked layout, `Mute + …` strips it to the
  floor. Move = paste-then-clear-source, `Func + Scene + Yes`
  while a Scene clipboard is loaded.
- **Phrase.** `Phrase + Record` copies the focused track's phrase
  (§13.2); `Phrase + step_key` pastes into that phrase slot in the
  track's pool. Because a phrase can be shared by several Scenes (the
  SHR:N badge, §23.2), paste-into-a-shared-slot prompts to *fork* (paste
  as a fresh phrase, leaving the other Scenes' assignment intact) — this
  is the Kit-era replacement for the old Fork-Part chord.
- **Song.** Whole-Song duplication is a Browser-level action (no live
  chord): select a Song, `Record` to copy, `Play` onto a free Song slot.

Kit recall is **not** a Browser operation — it lives on the live surface
under `Func + Track` (§4.7.2).

**Scene create / copy payloads (shipped).** Lazy slots: Scenes (and
Phrases) don't exist until made; empty slots show capacity. Pressing an
empty Scene slot creates it. Only `Func` is a safe create qualifier —
`Mute + Scene + step` is **reserved for scene-mute** (under `Mute + Scene`
the step axis means *track*, not *scene*, so it cannot also address a scene
slot). So there are exactly **two direct create gestures**, and undeviated
duplication moves to the clipboard:

1. **Baked copy** — bare `Scene + empty-step`. The current Scene's
   **effective** layout (global + live deviations) is materialised onto the
   new Scene's home-row content (the same op as `Scene + Record`, §16). This
   is the clean **"save my experiment to another Scene"** gesture: bake a
   Scene, deviate to explore, then create a new Scene as a baked copy — the
   original is untouched and the exploration is preserved.
2. **Default / empty** — `Func + Scene + empty-step`. A fresh Scene whose
   global pattern defaults to its own index (§4.7); no content copied. This is
   the **baseline** reading of `Func + Scene + step` (§16): on an existing
   Scene it floor-launches; on an empty slot the baseline *is* a fresh
   default. (For an empty slot "baseline" and "default" coincide, so they
   share one gesture.)

**Undeviated duplication** (a Scene's floor only — global + mask + coreTime +
Morph, deviations stripped) is a *copy* act, not a create act, so it lives on
the Scene **clipboard** (§13.2). The clipboard is distinct from create: copy
does **not move** the active Scene, whereas the create gesture (`Scene + step` on
an empty slot) seeds *and* relocates. Both copy and paste carry a **verb**
(`Record`/`Play`), which is what cleanly separates them from the *verbless*
scene-mute chord — so a `Mute` strip-qualifier is unambiguous in a clipboard
chord even though `Mute + Scene + step` alone is scene-mute.

**Baked-vs-floor is decided at *paste* time.** The grab is one gesture and
**lossless** (always the source's full effective layout); the destination
chooses how much to lay down — plain `Func + Scene + Play` pastes baked,
`Mute + Func + Scene + Play` strips to floor. This keeps a single grab
reusable both ways and never traps a live experiment. The cost is a heavy
floor-paste chord (three modifiers + verb + step), accepted because it is a
deliberate, rare librarian action.

**Conflict.** When a copy/paste/bake target already holds phrase content at
a written index — especially a phrase shared by other Scenes (SHR:N, §23.2)
— a "Overwrite phrase slot N? SHR:M free:Pk P=Yes Func+P=No" confirm is
raised. `P=Yes` executes; `Func+P` cancels. Clean targets execute immediately.

## 24. State Colour Taxonomy

For hardware LED parity to work cheaply (DESIGN §10), every UI
state the LEDs need to mirror must be a *distinct, named state* in
the software model, not an ad-hoc rendering choice. This section
reserves the taxonomy; specific RGB values are a later visual pass.

The closed enum (extend by patch when a new state genuinely
emerges):

| State | Where it appears |
|---|---|
| `step.empty` | StepGrid cell with no trig. |
| `step.trig` | StepGrid cell with a base trig. |
| `step.trig.plocked` | StepGrid cell with a trig **and** ≥1 P-Lock. |
| `step.held` | StepGrid cell currently held (EditContext active). |
| `step.preview.certain` | Will fire this loop (§4.5). |
| `step.preview.skip` | Will not fire this loop. |
| `step.preview.probabilistic` | Stochastic — intermediate brightness. |
| `step.preview.fillOnly` | Fires only while Fill is held. |
| `step.microOffset.early` | Has a negative `microOffset` (§19). |
| `step.microOffset.late` | Has a positive `microOffset`. |
| `scope.held` | Any scope key currently held. |
| `scope.held.primary` | The primary scope in a multi-scope hold. |
| `track.focused` | Currently focused track key. |
| `track.muted.global` | Globally muted (DESIGN §13.4). |
| `track.muted.scene` | Scene-muted (active-mask off). |
| `track.muted.both` | Both. |
| `track.armed` | Record-arm on this track (Per-Track-MIDI mode). |
| `pattern.queued` | Pending pattern switch (DESIGN §16). |
| `pattern.chained` | Member of the active chain. |
| `mode.alt` | Trig-grid in an alt mode (Keyboard / Retrig / SoundPool / 16-levels). |
| `checkpoint.depth` | Chrome chip — depth `0..8`. |
| `scope.colour.step` | Default-grey (no scope held). |
| `scope.colour.track` | Track-scope held — applied to keys + reskinned step cells. |
| `scope.colour.phrase` | Phrase-scope held. |
| `scope.colour.scene` | Scene-scope held. |
| `scope.colour.machine` | `Func + Track` machine picker — distinct from Track so the picker is visibly its own mode. |
| `scope.colour.morph` | Morph-scope held. |
| `scope.colour.song` | Song-scope held. |

The seven `scope.colour.*` entries (3.3 ✓) are the **scope colour
grammar**: the visible side of "which scope is on the surface right
now". They drive key tints when a modifier is held, the step-grid
re-skin cells (§6.7), the held-context preview band (§6.8), and any
badge that needs to signal scope identity. Same deferred-palette
policy as the rest of §24 — the *set* is canonical, specific RGB
values land in the later visual-design pass.

**Implementation:** seven `kScope*` constants in `UITheme.h`;
`scopeColour(PrimaryScope, bool machinePicker)` helper in `KeyLabel.h`
is the single consumer-facing API — no renderer should hard-code scope
RGB directly. New chrome that needs to distinguish a state must first
add it to the taxonomy.

This taxonomy is realised as the **add-only `CellState` enum** carried by
the surface model (§35.8): the entries above become tokens, the screen
maps token→RGB via `UITheme`, and an external controller maps token→device
colour/brightness. That is the mechanism by which "hardware LEDs mirror
software for free" stops being a hope and becomes structural — both render
the same model. Per §35.8.6 the enum only grows (deprecate, never remove),
and every token always carries a resolved colour fallback.

Specific colours are deferred. The first cut ships with
placeholder colours that are *distinguishable* (no two states
collide) but not yet "designed." A later visual pass picks the
final palette without touching any of the renderers.

## 25. Coarse-Adjust Modifier

Holding `Func` while turning an encoder snaps writes for that
block to the target slot's **coarse step**. The same applies to
relative-CC deltas and to UI mouse-drag where applicable. The
modifier is global to all parameter writes and does not interact
with the EditContext (it still routes through the standard rule:
base vs P-Lock by held-step state).

The coarse step value is resolved per-slot via two mechanisms in
order:

1. **`ParamSpec::coarseStep` override** (optional). A machine
   author may set an explicit coarse step on a `ParamSpec`. This
   wins when present.
2. **Unit-hint default.** Otherwise, the coarse step is derived
   from `ParamSpec::unit`:

   | Unit hint | Default coarse step |
   |---|---|
   | time (samples, ms, seconds) | snap to musical division relative to current tempo (1/4, 1/8, 1/16, 1/32) |
   | frequency (Hz, cutoff) | one octave |
   | pitch (semitones) | one octave (12 semitones) |
   | percent / unipolar `[0,1]` | `0.1` |
   | bipolar `[-1, 1]` (pan) | `0.25` |
   | integer (sample id, slice id, channel) | `1` (no change — integers already step coarsely) |
   | stepped (closed enum) | `1` step |
   | unitless float | `0.1` of range |

The coarse step is rendered in chrome while `Func` is held so the
user knows the step size before they turn the encoder.

This is the principled answer to "I want to move this filter
cutoff in octaves" or "I want to jump this delay time to clean
divisions": there is one modifier, one rule, no per-slot UI for
coarse / fine toggles.

## 26. UI Shape: Vertical Growth and MZ Flexibility

Two structural UI commitments shape the editor's long-term
layout.

### 26.1 Square cells, vertical growth

The eventual hardware controller's key caps are square mechanical
switches under translucent frosted keycaps. To preserve the
"hardware = fewer-key QWERTY" mapping (`PRINCIPLES.md` §4), the
software StepGrid + SectionBar cells are *also* square — which
means as the editor adds rows (chrome badges, banners, the MZ,
the on-screen MIDI keyboard, future hardware-state mirrors), the
window grows **vertically**, not horizontally. Width is anchored
to the 9-column QWERTY layout; height is whatever the chrome
needs.

The ManipulationZone sits *above* the StepGrid in the final
layout (closer to the top-row encoders on the hardware); the
on-screen MIDI keyboard moves into a collapsible drawer below
the StepGrid. The exact pixel layout is the subject of 5.8.1;
the constraint is "every cell that maps to a hardware key is
square, and the window grows vertically."

**Encoder band (3.1, §33).** The 8 MZ encoders render as a band above
the key grid, **two staggered rows of four**, narrower than the
10-column grid below them — the stagger gives each knob breathing
room (the 10-key width is wide) and visually decouples the encoder
area from the grid. The **crossfader** is a vertical slider on the
right of this band, sharing its row-space, with **Scene A at the top
and Scene B at the bottom** (§17.5). The software UI mirrors the
intended hardware layout 1:1.

**Cell typography (3.1, §33).** Every functional cell carries up to
four registers in fixed positions, for a consistent read across the
whole grid:

- *Corner (dim, small):* the QWERTY-key legend — toggled by
  `show-key-legend` (§6.2).
- *Centre (largest, high-contrast):* the primary label.
- *Bottom strip (smaller, secondary hue):* the `Func`-layer label.
- *Held-chord overlay (accent colour):* when a modifier or compound is
  active, the cell's contextual meaning takes the centre in an accent
  colour — this is the per-cell form of "chrome announces state"
  (PRINCIPLES §10).

Abbreviations may run to ~5 characters now that cells are larger;
canonical section names stay ≤4 so they never reflow.

### 26.2 Manipulation Zone size

The MZ shows **8 slots** as of 3.1 (§33), matching **8 endless
encoders** on the hardware, laid out **4×2** (two staggered rows of
four — see §26.1). This halves the pagination count for any section
larger than 8 slots and maps the encoders cleanly onto the section
taxonomy.

The size remains a single named constant (`lockstep::kMZSlots`, now
`8`); no code outside the MZ may hard-code the slot count, so the
value stays the one place a future re-size is made.

### 26.3 Slot layout streamline (3.4)

Pre-Phase 3 each MZ slot rendered a parameter name label, a rotary
encoder, a separately-rendered numeric value label, and a "clear
P-Lock" `x` button — four widgets per slot, with the label and the
value duplicating information. 3.4 collapses each slot to **rotary
+ one value display**, where:

- The value display reads textual when the slot's `ParamSpec` carries
  a `valueLabels` table (§2) — `LP24 / LP12 / HP / BP`, `MONO / PARA`,
  sine / saw / pulse / tri — and numeric otherwise.
- The parameter's name moves to a slim header (or piggybacks on the
  section-key label since context already names the page).
- The standalone `x` clear button is removed; clearing a P-Lock is a
  grammar gesture under `Trig + (slot) + Stop` / `Trig + Func + Stop`
  / step-driven edit mode (§13.2), keyboard-first.
- The reclaimed space grows the rotary itself, so it's actually
  legible at performing distance.

Double-click on a rotary resets its slot to the `ParamSpec` default,
routed through one helper so the eventual hardware push-encoder-twice
gesture lands on the same code path (consistent with §17.5
single-axis push-encoder discipline).

## 27. Audio Routing and Track Input Sources

The Octatrack's defining trick is that a track can take another
track's (or an external) output as its *input*, turning tracks into
processing, sampling, and resampling chains. Lockstep adopts a
deliberately bounded form of this.

**The model is "explicit source-select", not free patching.** A
machine that consumes audio declares an `input_source` slot:

| Source | Meaning |
|---|---|
| **None** | Default. The machine synthesises into an empty buffer (every synth/sampler). |
| **External bus** | The plugin's audio input bus (sidechain / standalone device input). |
| **Track N** | Track `N`'s output, post-machine and post-FILTER/AMP, pre-track-sum. |
| **Master** | The plugin's master sum (prior block — see below). |

There is no implicit neighbour chaining and no patch matrix: a track
reads from exactly one declared source. This is the smallest model
that supports Thru, Recorder, Looper, and realtime resampling without
turning the sequencer into a modular host.

**Ordering: topological sort per block, cycles refused.** Each block,
the engine orders track processing so that every `Track N` source is
computed before its consumer. This is a topological sort over the
"track A sources track B" edges. A routing assignment that would
create a cycle is **refused at assignment time**, with a chrome
message — audio feedback loops are a deliberate non-feature (an
opinion of the instrument), and refusing them up front keeps the
block deterministic (`PRINCIPLES.md` §11). Because routing is by
*source selection* rather than track position, the user never has to
reorder tracks to re-route — the sort handles ordering for them.

**The Master exception.** A track that sources `Master` usually also
contributes *to* Master, which is an unavoidable cycle. The single
sanctioned escape: `input_source = Master` reads the **prior block's**
master sum. This is the one place a one-block tap is allowed, and it
exists precisely because master-feedback can never be cycle-free.
Realtime whole-mix resampling tolerates the ~one-block latency
without audible consequence.

**Buffer read/write is not a routing edge.** A Recorder or Looper
that writes a buffer while another track's Flex machine reads that
buffer is *not* a cycle — the buffer (§28) is a decoupled resource,
not a live audio edge. This is what lets loopers work under the
"no feedback loops" rule.

**MIDI-out tracks declare no input source** — they have no audio to
consume — so inter-track routing is simply a capability some machines
have, not a sequencer-wide rule that needs a MIDI-out special case
(`PRINCIPLES.md` §6).

## 28. Recorder Buffers and the Unified Audio-Source Pool

Live sampling needs a place to put captured audio. Rather than a
second, parallel resource type, recorder buffers are **volatile
entries in the existing sample pool**:

- A **volatile** pool entry is RAM-only, has no file backing, is
  badged (`REC`) in the UI, and is **not serialised** with the
  project (consistent with `PRINCIPLES.md` §12 — and matching the
  Octatrack, whose recordings are lost on power-down unless saved).
- A **persistent** pool entry is file-backed (`{path, xxHash32}`),
  exactly as today. Samples dragged in from disk are born persistent.

Both kinds share one namespace and one address space, so any
machine's audio-source slot (`sample_id`, `target_buffer`, …) can
point at either kind identically. A loop dragged in from disk is
usable as a "buffer" with no special handling; a freshly captured
buffer is usable as a sample with no special handling. The only
user-visible difference is the `REC` badge and a **save** affordance.

**Promotion (freeze-to-disk)** runs the §22.3 naming flow on a
volatile entry, writing it to `samples/recorded/` and converting it
to a persistent entry. This is the bridge between performance
(transient buffers) and the studio (saved, serialisable samples).

The default project provides a small fixed set of volatile buffer
slots (target count TBD, ~8) so recorder trigs and the §22 capture
overlay always have somewhere to write.

## 29. The Machine Catalogue

The stock catalogue is an "Elektron's greatest hits" set: one machine
per iconic engine, each authored against the same `IMachine` boundary
(§2) and snapping to the canonical sections (§6.1). The reference
lineage drives which machines ship stock:

| Reference box        | Lockstep machine(s)                         | Kind        |
|----------------------|---------------------------------------------|-------------|
| Digitakt             | `SamplerMachine` + `SlicerMachine`          | generator   |
| Digitone             | `FMMachine` (4-op)                           | generator   |
| Analog Four          | `VAMachine` (virtual-analog)                 | generator   |
| Analog Rytm          | `DrumSynthMachine` (analog/FM drum)          | generator   |
| **Monomachine**      | **`DigitalMachine`** (digital multi-model)   | generator   |
| Machinedrum          | `DrumSynthMachine` + `PercussionMachine`     | generator   |
| (modal / Volca Drum) | `PercussionMachine` (physical model)         | generator   |
| Octatrack — Flex     | `SamplerMachine`                             | generator   |
| Octatrack — Static   | `StaticMachine` (disk-stream)                | generator   |
| Octatrack — Thru / Neighbour | `ThruMachine` (`input_source`)       | router      |
| Octatrack — track Recorder   | `RecorderMachine` (overwrite)        | capture     |
| Octatrack — Pickup           | `LooperMachine` (overdub)            | capture     |

Two lineage entries are deliberately **recipes, not machines**, because
their character is already reachable by composing what the catalogue and
the foundation provide:

- **Neighbour → Thru.** The Octatrack's separate Neighbour machine is
  folded into Thru: source is chosen by `input_source`
  (`External bus` = classic Thru, `Track N` = neighbour-style
  inter-track passthrough), and gated-vs-open is the general AMP gate
  source (§14). One machine, one knob.
- **Syntakt → existing voices + master drive.** The Syntakt is its drum
  and digital voices (covered by `DrumSynthMachine` / `FMMachine` /
  `VAMachine`) plus a *master analog overdrive/filter*. The drive and
  filter are an `IEffect` (§32) and the canonical FILTER (§14), not a new
  machine. There is no `SyntaktMachine`; the box is a Sound-Pool +
  master-FX preset.

**Granular is a module, not stock.** Granular synthesis (Roland Aira P-6, and
the Tonverk lineage) is a genuinely useful but *specialised* engine: per
PRINCIPLES *"Machines generate; effects process"*, a specialised engine ships
as a third-party machine module (§36), not as a reason to grow the stock
catalogue. The stock set stays the iconic-and-foundational lineage above;
granular — and physical-model specialities beyond `PercussionMachine` — are
first examples of what the module ABI is *for*.

### 29.1 `DigitalMachine` — the Monomachine archetype

A **model-based digital monosynth** (built the way `DrumSynthMachine` is:
a stepped `model` slot reshapes the engine, with `valueLabels`). It
covers the digital timbres that `VAMachine` (analog) and `FMMachine`
(4-op FM) cannot reach. Stock models:

- **SWAVE** — SuperWave: stacked detuned saw/pulse with width / detune /
  spread (the supersaw character).
- **SID** — C64-SID-flavoured digital: pulse-width + ring-mod + hard-sync
  grit.
- **WAVE** — single-cycle wavetable / PWM scan (the Digipro/DPRO digital
  waveform engine).
- **VO** — formant / vowel synthesis (genuinely uncovered elsewhere in
  the catalogue).

`currentVoices() = V1` (mono) with a Mono/Poly voice-mode slot, pulled
live like `FMMachine` (§2). It does **not** opt out of the canonical
FILTER/AMP (§14): its character is in the oscillator/model, so the
multimode SVF and the AMP envelope sit downstream as usual; the
Monomachine's "+Drive" is a `drive`-role `IEffect` (§32), not internal.

Three of the original Monomachine engines are **subsumed**, mirroring the
Neighbour and Syntakt hygiene above — they are not re-implemented inside
`DigitalMachine`:

- **GND** (ground / utility) → **Thru** (`input_source`, §27).
- **FM** → **`FMMachine`** (the dedicated 4-op engine).
- **Drum / FMdrum models** → **`DrumSynthMachine`** / **`PercussionMachine`**.

### 29.2 Static, Thru, Recorder, Looper

These extend the catalogue beyond the baseline Flex sampler (§3). The
first is an ordinary playback engine; the latter three consume audio via
`input_source` (§27).

- **Static.** A disk-streaming sampler for long-form material
  (full songs, long field recordings) that should not be decoded
  into RAM. Same slot vocabulary as Flex where it overlaps
  (start/end, level), minus the RAM-only manipulations that streaming
  cannot cheaply support. Reinforces `PRINCIPLES.md` §12: the audio
  never enters RAM wholesale, let alone the project state.
- **Thru.** Turns a track into a processing block: `input_source`
  feeds audio into the track's signal path, the machine passes it
  through at unity, and the canonical post-machine FILTER/AMP/FX (§14)
  do the work. The machine itself is nearly empty — its value is
  routing audio *into* the uniform per-track processing the sequencer
  already provides. Thru subsumes the Octatrack's separate Thru vs.
  Neighbour split: the source is chosen by `input_source`
  (`External bus` = classic Thru, `Track N` = neighbour-style
  inter-track passthrough), and trig-gated vs. always-open is the
  general AMP gate source (§14), not a machine type. Thru is the only
  machine declaring `input_source` for now.
- **Recorder.** Captures `input_source` audio into a volatile buffer
  (§28). Slots: `input_source` (what to record), `target_buffer`
  (where to write), `rec_length` (how long — see §30). Capture is
  triggered by **recorder trigs** (§30); the machine itself holds no
  loop state — it overwrites the target buffer each time it captures.
- **Looper.** The overdub counterpart of Recorder, and Lockstep's
  equivalent of the Octatrack **pickup machine**. Overdub looping is
  a *state machine* (record → play → overdub → undo → clear), so it is
  encapsulated in a machine rather than smeared across trig flags. It
  is driven by the existing verbs while the track is focused
  (`Record` cycles record → overdub, `Play` plays, `Stop` stops,
  and a clear gesture empties the loop) — no new grammar, one new
  machine. Plain overwrite resampling stays with Recorder; sound-on-
  sound layering lives here.

This split — overwrite in Recorder, overdub in Looper — mirrors the
Octatrack (track recorders vs. pickup machine) and keeps the recorder
trig path simple and stateless.

## 30. Special Trig Types

Three trig types extend the trig grid beyond plain note-trigs. All
three live within the existing trig model and the OEB resolver.

- **Trigless (lock-only) trig.** A per-step state separating "apply
  this step's P-Locks/overrides" from "emit a note-on". The step is
  tri-state: `off → note → lock-only`, cycled by `Func + step`.
  A lock-only trig applies its parameter overrides to the
  already-sounding voice without retriggering it — the canonical use
  is a parameter sweep that rides a long note. Cleanly OEB
  (`PRINCIPLES.md` §7); a new colour in the §24 state taxonomy marks
  lock-only cells.
- **One-shot trig.** Fires once, then is **spent** until re-armed —
  a performance accent that does not repeat every loop. Modelled as a
  `TrigCondition` variant; the armed/spent flag is RAM-only runtime
  state, so the grammar stays deterministic *given* arm state
  (`PRINCIPLES.md` §11). Re-arm follows the Octatrack: **automatic** on
  pattern (re)entry and on transport stop→start, plus a per-track
  **arm-all / disarm-all** command on the Func layer for re-arming a
  continuously looping pattern without restarting it. Armed vs. spent
  is announced in chrome (`PRINCIPLES.md` §10).
- **Recorder trig.** Only meaningful on a Recorder track (§29):
  "capture `input_source` into `target_buffer` for `rec_length`,
  starting now." `rec_length` (the Octatrack RLEN) is a P-lockable
  slot defaulting to the track's loop length. A *plain* recorder trig
  re-captures (overwrites) every loop it fires — continuous live
  resampling; a *one-shot* recorder trig (the two types compose)
  captures once and is then spent until re-armed. Trigless is
  meaningless here and is disallowed on recorder trigs.

## 31. Cue Bus and Monitoring

A dedicated **cue (monitor) bus** lets the performer pre-listen
without disturbing the main mix — the Octatrack's "use it as a
performance mixer" workflow, scoped to a sequencer's needs. It enters
the grammar as a single new scope, `Cue`, with no bespoke buttons.

- **Cue + track (audio track).** Adds that track to the cue bus as an
  **additive monitor send**: the track stays in the main mix; the cue
  send is tapped **post-FILTER/AMP/Level** (you hear it as it sits in
  the mix). Cue never alters the main output — it is "let me hear
  this", not solo.
- **Cue + Scene.** Auditions the resolved scene on the cue bus
  *without moving the live fader* — "check the scene before I commit".
  Cued tracks resolve twice for that block (once live to main, once at
  the previewed scene endpoint to the cue bus); the cost is borne only
  by tracks being cued.
- **Cue + track (MIDI-out track).** Sends a *copy* of the track's MIDI
  events to a configured **cue MIDI destination**, leaving the main
  destination untouched — exact parity with the audio additive send
  (`PRINCIPLES.md` §6). If no cue MIDI destination is configured, the
  gesture is a no-op with a chrome note. (Cue is deliberately *not*
  reinterpreted as solo when no cue output exists: that would make one
  scope mean two things and let cue alter the main output — a silent
  mode. A true solo, if wanted, is a separate future gesture.)

**Outputs.** Standalone routes the cue bus to audio device channels
3–4 and the cue MIDI to a chosen output port; as a plugin it exposes
a second stereo output bus and a second MIDI output the host routes.
Chrome shows "cue unavailable" when the host has not wired the cue
output.

## 32. Insert and Master Effects

Effects are owned and managed by the sequencer foundation, not left
entirely to machines. This is what makes the canonical **FX section
(key 8)** mean the same thing on every track — exactly as FILTER (key 5)
and AMP (key 6) do — and it is what lets Lockstep double as a small
performance mixer (Thru tracks in, effects, cue out) without becoming
a mixer that happens to sequence (`PRINCIPLES.md` §3).

### 32.1 The `IEffect` unit

An effect is a schema-bearing DSP processor — `audio in → audio out`
— declared by a built-in or contributed `IEffect`. It **reuses the
`ParamSpec` / `role` infrastructure** that machines use, so:

- effect parameters render in the FX section exactly as machine
  parameters render in the MZ (the section is a *generic* renderer of
  whatever effect is loaded);
- effect parameters are **P-lockable per step**, **scene-assignable**
  (and morphable, §17.6), **Control-All-able** by `id`/`role`, and
  **copy/paste/clear**-able under the FX section scope;
- effect identity persists on disk as a stable string id (e.g.
  `"lockstep.fx.delay.v1"`); an unknown id on load falls back to a
  bypassed stub that preserves its stored params.

Effects are a contributor surface like machines: the catalogue
(delay, reverb, chorus, bitcrush, EQ, compressor, …) grows over time
and can ship via the same "pack" format as machines (ROADMAP 6.7).
A machine may *also* carry internal effects; those surface as
extension sections, leaving the canonical FX section for the
foundation inserts.

### 32.2 Per-track inserts

Two **insert** slots per track, **fixed**, positioned **post-AMP**:
`… → AMP → FX1 → FX2 → track sum` (§14). Each slot is empty or hosts
one `IEffect`. Insert state — effect identity, base params, and the
per-step P-Locks (which live with the Phrase) — belongs to the
**Kit** (per-track), consistent with where FILTER/AMP state lives
(§4.7). The FX section paginates across the two slots' pages via the
canonical extension-section mechanism (repeated key-8 press cycles
slot-1 pages, then slot-2 pages). Assigning an effect to a slot from
the catalogue is a load gesture analogous to assigning a machine
(exact gesture TBD at the FX milestone).

A **FX-section copy** (FX scope + Record) copies effect *identity*
plus params, so "copy the whole effect chain to another track" works;
this is a deliberate extension of the §13.2 section-copy rule, which
otherwise copies slot values only.

### 32.3 Master effects (insert or send)

Two **master** effect slots sit on the master bus, post track-sum,
pre master-gain: `track sum → master FX 1 → master FX 2 → master
gain`. Each slot carries a mode:

- **Insert mode** — the effect processes the full master signal
  in-line (master compressor, master EQ, …).
- **Send mode** — the slot becomes a return bus. Tracks feed it via
  their per-track **Send A / Send B** levels (in the AMP output mix,
  §14; P-lockable like any slot), and the processed return is summed
  back into the master. This is what gives one shared reverb/delay
  across many tracks — the capability the Octatrack's insert-only FX
  famously lacks.

Master FX parameters are edited under the existing `Master` focus
state (`{Master, Track1..16}`), so no new focus concept is needed: with
Master focused, the FX section shows the master effects.

**Scope (provisional).** Master FX state is **Set-scope** — one
master chain for the whole Set, read as global "front-of-house"
infrastructure distinct from the per-track kit. This is the one
scoping choice in this section flagged for revisiting: the
alternative (Kit-scope, so a kit swap can change master processing)
is coherent too, and consistent with per-track inserts being
Kit-scope. Defaulting to Set keeps "the master bus is the master
bus" simple until performance testing argues otherwise.

### 32.4 MIDI-out and parity

MIDI-out tracks have no audio, so they carry no inserts and no sends;
their FX section is repurposed to a MIDI CC bank exactly as 2.4.7
already specifies for FILTER/AMP. No performance feature special-cases
audio vs. MIDI-out here — the FX section simply renders whatever that
track type exposes (`PRINCIPLES.md` §6).

### 32.5 Animate — momentary insert toggle (performance punch-in)

Grooveboxes lean hard on momentary "performance FX" — glitch / stutter /
filter macros punched in on a held key (Polyend Play, OP-Z, MC-707 Scatter).
Lockstep **refuses a dedicated performance-FX mode** (NON-GOALS: it is dead
weight and a design/perform split). Instead it offers the thin, Novation-Peak-
"Animate" form — a **momentary toggle of effects that already exist**.

A bound gesture (exact key at the FX milestone) **enables an insert slot only
while held**; the effect is otherwise bypassed. The dual binding *bypasses* an
otherwise-active insert while held (the "drop the reverb" move). Releasing
returns to the resting state. Because inserts are already P-lockable, scene-
assignable, and Control-All-able (§32.1), Animate adds **no new effect surface
and no new mode** — it is one momentary *verb over existing inserts*, exactly
the weight a punch-in should cost. The toggled state is performance-only (never
written to the pattern); the effect's parameters are tweaked the normal way
(encoders / P-Locks). Master inserts (§32.3) carry the same momentary toggle,
giving a one-key master-FX punch-in without a master-FX mode.

## 33. 3.1 — The 10×4 Surface Revamp (amended by 3.2)

The intended *final* control surface and UX grammar. Earlier
milestones built on a 9×4 layout (one left modifier column + an 8-wide
functional block); 3.1 widens that to **10×4** to give the performance
grammar room to breathe before the machine catalogue (Phase 4) and scenes
(5.2) pile on more scopes. This section is the consolidated decision
record; the authoritative key map lives in §5.5, the grammar in §13,
the scene/fader surface in §17.5, and the MZ/encoder/typography in §26.

3.1 was intended as the last large UI/UX revamp. In practice the
cluster identities it shipped were assigned by intuition rather than
measured chord-value, and the canonical six elided the distinction
between LFOs and modulation more generally. **3.2** (ROADMAP §3.2)
amends the cluster identities, the right-utility row, and the
canonical-six naming — without moving any key. The 3.1 *geometry*
(10×4 footprint, cluster region in cols 0–1, section bar on row 0,
16-step grid on rows 2–3, nav inverted-T at `4/E/R/T`) remains frozen.

In short: 3.1 = the shape; 3.2 = the labels. Sections §5.5, §6, §13
have already been rewritten to the 3.2 layout; what follows is the
historical 3.1-as-shipped record.

**33.1 Geometry.** Ten columns, four rows. The **left two columns** are
an eight-key modifier cluster, all reachable by one (left) hand; the
**right eight columns** are the functional block — function/section
keys on the top two rows, the 16-step grid on the bottom two. Widening
left rather than widening the step grid keeps the Digitakt-lineage
16 steps and the six canonical sections untouched. The window grows
*vertically* (PRINCIPLES §4; §26.1), width anchored to the 10 columns.

**33.2 The eight modifiers.** Column 1 (structural): `Func` `Track`
`Phrase` `Mute`. Column 2 (performance): `Fill` `Cue` `Morph`
`Song`. Rationale for the slate (§5.5 has the key map):

> **Superseded — historical record.** This is the 3.1-era slate: it
> predates the launch-row `Scene` key and still lists `Cue` in column 2.
> The canonical 8-key cluster is `Func/Track | Phrase/Scene | Morph/Song
> | Mute/Fill` (§13 / §5.5); read this subsection only as the 3.1 shape
> it shipped as, not as current truth.

- `Phrase` is promoted from `Func+2` to its own key.
- `Mute` keeps a dedicated key specifically to preserve its
  hold-and-tap-many multi-mute gesture, which a verb-chord can't match.
- The old **Scene A / Scene B** pair collapses to a single `Morph`
  modifier; endpoint A/B is chosen by `Morph + ^/v` (§17.5). The freed
  slot goes to `Song` (song select; `Func+Song` = master-bus / FX focus,
  §32.3), which previously had a focus state but no key.

**33.3 Compound chords.** Modifiers compose under the hard rule in §13:
cross-column only, a modifier+modifier never fires on its own (it only
sets a compound scope awaiting a verb), and `Func` is the universal
qualifier *and the cheapest modifier*. A compound *qualifies* the scope;
it never changes what a verb means. Cost is graduated (PRINCIPLES §15):
the ceiling is four simultaneous keys (`Func + mod + mod + key`),
admitted only when earned; five is forbidden. The §13.0 inventory rungs
every gesture; the §13 exceptions table starts empty.

**33.4 Encoders, fader, typography.** 8 encoders (`kMZSlots = 8`) in a
4×2 staggered band above the grid; the crossfader is a vertical slider
to the right of that band (Morph A top, Morph B bottom). Functional
cells follow a fixed four-register typography (corner key-legend /
centre primary / bottom Func-label / accent held-chord overlay). See
§26.

**33.5 Open within 3.1.** The canonical sections (`3–8`) are fixed, but
the function-strip *nav arrangement* and the use of keys `9`/`0` are
provisional and may move as 3.1 implementation firms up.

**33.6 Implementation note.** Landing 3.1 touches the
`QwertyOverlay` tables (modifier set, step keys moving to `D–;` /
`C–/`, the relocated function strip), `isEdgeKey` (the decorative-edge
set changes for a 10-wide grid), `kMZSlots` (4 → 8) and the editor
re-layout (encoder band + vertical fader), plus the chrome that
announces the new modifiers. Until it ships, the running build remains
on the 9×4 layout; docs that describe the 10×4 surface are describing
the 3.1 *target*.

## 34. Per-Track Input Modes (3.9)

The scope+verb grammar (§13) governs what the user is operating *on*.
Per-track **input modes** govern what raw input *means* on the focused
track. The two are orthogonal: a held scope still reinterprets the
step grid the same way regardless of mode, and a mode only changes
what bare step-cell presses and incoming MIDI do.

`TrackInputMode` is a per-track enum:

| Mode | Step cells (bare press) | Incoming MIDI |
|---|---|---|
| `PLAY` (default) | Toggle `step.trig` | QWERTY-MIDI / external MIDI feed monitoring / record-arm path as today |
| `EDIT` | (placeholder for a future dedicated step editor; behaves like `PLAY` for now) | Same as `PLAY` |
| `CHROMATIC` | Become a 1-octave chromatic keyboard for live play; play into the focused track's machine | Same as `PLAY` |
| `LEVELS` | Become quantised velocity buckets (`1/16 … 16/16` of 127); write `velocityOverride` per §34.2 | Same as `PLAY` |

Default is `PLAY`. Mode applies on the **focused** track only;
non-focused tracks are unaffected by the mode setting. The top-bar
right zone (the held-context preview, §6.8) shows the current mode
when it is not `PLAY`.

### 34.1 Mode selector

`Track + NavUp / NavDown` cycles the focused track's input mode
(PLAY ↔ CHROMATIC ↔ LEVELS). NavLeft/NavRight is already the octave
shift in CHROMATIC (§34.2), so Up/Down is free and conflict-less.
Compound qualifier: `Track + track-key + NavUp/Down` sets the mode
on a specific track without changing focus (per the standard §13
compound-chord rule).

> **Supersedes (3.10).** 3.9 shipped an interim selector that
> repurposed the verb row as a radio (`Track + Y/U/I/O` =
> PLAY/EDIT/CHROMATIC/LEVELS). That mapping was grammatically
> arbitrary (a verb meant a mode) and the exit was undiscoverable, so
> it is replaced by `Track + Nav`. This also frees the verb row and
> the double-tap gesture for their real meanings (§13.7).

Modes are RAM-only initially; if play-testing shows that a track
"wants" to stay in CHROMATIC across project reloads, the field
will be promoted to per-Track serialised state.

### 34.2 CHROMATIC

While the focused track is in CHROMATIC, the 16 step cells render
as a 1-octave chromatic keyboard (cells 0–11 = C through B,
cells 12–15 unused), reusing the note-edit overlay's view-octave
state (`UiState::noteEditOctave`) and NavUp/NavDown shift gestures.
Cell presses are injected into the focused track's machine via a
per-track input-MIDI seam in `PluginProcessor::processBlock()`, so
the machine hears them identically to a QWERTY MIDI overlay key.

With record-arm on and transport running, captured notes route
through the §21 realtime record path (including the §21.4.1 per-note
velocity and §19.4 musical gate capture from 3.8) and land on
steps. CHROMATIC composes with all scope modifiers — holding a scope
key during CHROMATIC reinterprets the step grid per the scope re-skin
(§6.7) and suspends CHROMATIC playback for the duration of the hold.

**Scale-aware layout (not auto-correct).** A phrase carries an optional
*scale lock* (§6.1.2 Phrase row): a root + scale. When set, CHROMATIC remaps
the step cells from raw semitones to the *degrees* of that scale (cell 0 =
root, ascending through the scale, NavUp/NavDown shifting octaves), so the
in-key notes sit under the fingers and melodic play is faster. This is a
**playable layout — frets, not a net.** Per PRINCIPLES *"Reward mastery"*,
the scale never *corrects* input: notes entered any other way (held-step
pitch capture, external MIDI) are written verbatim, out-of-scale notes stay
reachable, and nothing is silently snapped. The scale changes which notes are
*easy to reach*, never which are *possible*. With no scale lock set, CHROMATIC
is the raw 1-octave chromatic layout described above.

### 34.3 LEVELS

While the focused track is in LEVELS, the 16 step cells become
quantised velocity buckets: cell `i ∈ [0..15]` represents velocity
`floor((i + 1) × 127 / 16)`. Cell-press semantics:

| Context | Cell-press effect |
|---|---|
| Step held | Write `velocityOverride` to all notes on the held step at the chosen level. Multi-step hold: all held steps receive the same velocity. |
| No step held, transport stopped | Set the focused track's base velocity (`track.defaultVelocity`) at the chosen level. |
| No step held, record-arm + transport running | Fire the focused track's machine at the chosen velocity (using the last-played pitch or the track's default note); write the trig + velocity to the next quantised step. |

3.9 ships **velocity-only** LEVELS. The role-tagged generic
(`cutoff`, `pitch.coarse`, `attack`, `cutoff`, …) per §20.1's
eligibility set lands as a follow-up sub-mode on top of 3.9 — the
binding selector (encoder cycles eligible roles, §20.2) sits on the
same surface as today's mode chord.

### 34.4 Phrase-length authoring (Phase 7 / 3.11)

In the §4.7 model, *pattern length* is **Phrase length** — each track's
`Phrase.length` (1–64 steps). The per-track polymeter model (§4.2) is
fully preserved: the focused musician's phrase may be 16 steps while
another's is 7. Phrase length is independent of the Scene's core time
(`§4.8`); core time only seeds the *default* length of newly-created
phrases.

Length authoring sits in the §13 grammar under the `Phrase` scope:

| Gesture | Effect |
|---|---|
| `Phrase + Func + step` | Set the focused track's active phrase length to that absolute (page-aware) step index. Grid re-skins in scope colour showing run / boundary / out-of-range. |
| `Morph + Func + step` | Broadcast: set **all** tracks' phrase length = N. `Morph` (the column-3 primary; the pre-7.9 "Scene" key) is the all-tracks qualifier. Grid re-skins in Morph colour to distinguish from the focused-track skin. |
| `Func + Up` | Double current phrase length, duplicating all step data (trigs, notes, P-Locks, overrides) into the new tail. |
| `Func + Down` | Halve current phrase length, truncating the tail. One automatic checkpoint push fires before truncation so the data is recoverable via the §13.6 checkpoint stack. |

Page navigation past the current phrase length is unlocked by a
**double-tap `NavRight`** (reusing `DoubleTapDetector`): the first tap
does nothing past the last in-length page; the double-tap steps onto the
empty page. The unlock clears once the currently-visible page again
contains the final step (either because a longer length was set on the
empty page, or because the performer navigated back). Out-of-range pages
render very dim; the nav row shows the full page count plus the live
`Length: N` while unlocked.

The `LEN` encoder in the TRACK meta-section provides the same write path
without leaving the MZ; it and the chord gestures write the same
underlying per-track length parameter — no divergence.

**The re-skin rule.** While `Phrase + Func` or `Morph + Func` is held,
the step grid re-skins (momentary; "the hold is the mode"):
- Cells within the run → `LengthInRun` token (scope colour body).
- The boundary cell → `LengthBoundary` token (brighter edge).
- Cells outside the run → `LengthOutRun` token (dim near-black).
All appearances route through `buildSurfaceModel()` as `CellState`
tokens (add-only); no ad-hoc paint.

## 35. External Controller Surfaces

Lockstep's grammar is designed for one canonical surface — the 10×4
QWERTY (and its denser hardware twin, §33, §26, `PRINCIPLES.md` §4).
But performers already own generic MIDI controllers (encoder boxes,
pad grids, fader banks). This section defines how such a device
*augments* the canonical surface without becoming a second, divergent
input language. This is ROADMAP **6.6**, *in progress*: the load-bearing
**surface model** (§35.8) has landed — `SurfaceModel.{h,cpp}`, the
`CellState` taxonomy + decoration channels, `tests/SurfaceModelTest.cpp`,
and the screen renderers re-pointed at it (6.6.5a) — while the controller
I/O ports, input router, and feedback emitter (6.6.1–6.6.4, 6.6.5b, 6.6.6)
are still to come. The design exists so the remaining build stays inside
the grammar. The worked example throughout is the Behringer X-Touch Mini;
its MCU/Mackie control map is captured in `XTOUCHMINI_MCU.md`.

### 35.1 Two classes of surface

| Class | Examples | Governed by |
|---|---|---|
| **Dedicated controller** | The eventual Lockstep hardware | §33 / §26 / `PRINCIPLES.md` §4 |
| **Augmentation surface** | Behringer X-Touch Mini, Launchkey, pad grids | this section |

The **dedicated controller** is *literally a fewer-key QWERTY* — same
topology class (10×4 cluster + functional block, 8 encoders, 1 fader),
same key→action map, full parity. `PRINCIPLES.md` §4 binds it. Nothing
in §35 changes it.

An **augmentation surface** is any generic third-party MIDI controller.
It is *not* a Lockstep keyboard and makes no claim to parity. The
reconciling rule with `PRINCIPLES.md` §4 (stated so the principle stays
honest):

> An augmentation surface may have a **different physical topology**
> from the QWERTY surface, but only a **strict-subset action
> vocabulary**. Every control it carries maps to an action already
> reachable from QWERTY; it may omit actions freely but can invent
> none. Topology differs; vocabulary only shrinks.

So §4's full-parity clause binds the dedicated controller alone, while
"one grammar, no exceptions" (`PRINCIPLES.md` §2) binds both: a generic
controller adds zero new actions, it only re-expresses existing ones
ergonomically, and the QWERTY + on-screen surface remain complete and
authoritative at all times.

**Coverage** is a property each profile declares — a monotonic ladder,
each level including the prior:

1. `encoders` — N relative encoders → MZ slots (live tweak only).
2. `encoders+grid` — adds a button grid → `Step` / `ToggleMute` / scope
   modifiers.
3. `encoders+grid+transport` — adds verbs / transport.

Plus a derived `covering` boolean: true only when coverage is
`encoders+grid+transport` **and** the grid is ≥16 cells — i.e. the
surface can stand in for the on-screen step grid. Only a `covering`
profile may trigger the adaptive layout (§35.6). Coverage is *declared*
by the profile, never inferred by the loader.

### 35.2 Controller I/O ports — a side-channel disjoint from the bus

The controller link is **not** the plugin's MIDI in/out bus and never
appears in `processBlock`'s `MidiBuffer`. (It is also distinct from the
§31 cue MIDI output, which is a *host-routed* second plugin bus — the
controller port is a device the plugin opens *itself*.)

A `ControllerPortManager` (in `src/io/`) owns its own `juce::MidiInput`
and `juce::MidiOutput`, opened **by device identifier** matched from the
loaded profile — the same mechanism the MIDI-out machine already uses
(`MidiOutMachine::openDevice` → `juce::MidiOutput::openDevice(info.identifier)`).

- **Standalone and DAW-hosted behave identically.** JUCE device-open is
  OS-level MIDI, independent of the plugin's audio/MIDI buses — which is
  exactly why the MIDI-out machine can open its own device even when
  hosted. The host's musical MIDI never carries controller traffic, and
  controller traffic never lands on a host track: "disjoint" holds by
  construction, not by host configuration.

- **Threading — three threads, marshalled.**
  `MidiInputCallback::handleIncomingMidiMessage` fires on JUCE's
  high-priority **MIDI thread**, not the message or audio thread. The
  manager fans a single incoming stream out by destination:
  - **Encoder / fader CC** → pushed into a lock-free FIFO drained at the
    top of `processBlock`, then run through the existing
    `CCMappingTable::dispatch` so soft-takeover (`AbsoluteCCRouter`),
    relative decoding (`RelativeCCRouter`) and the actual parameter
    writes all stay on the audio thread. Because this CC arrives on the
    dedicated device rather than the host buffer, it must be *marshalled
    in* — it does not ride the host CC path that §5 describes.
  - **Buttons** → marshalled to the **message thread** (the same thread
    QWERTY input uses) and dispatched as `ControllerEvent`s into the
    editor.
  - **Feedback out** (§35.4) → written to the dedicated `MidiOutput`
    from the UI timer. **No MIDI is ever written from the audio thread.**

- **Lifecycle.** Open on profile match; periodically rescan the device
  list so a controller plugged in *after* launch is picked up; release
  on profile change and shutdown. Persistence stores the device
  **identifier**, not its display name (matching the MIDI-out machine's
  `destinationId` discipline — display names are unstable across hosts
  and OSes).

- **Platform caveat (graceful).** Windows (WinMM) MIDI input is
  **exclusive**: if the DAW has already claimed the controller as a
  control surface, the plugin's open fails. This degrades to a chrome
  warning ("controller in use by host"), never a crash — and the manual
  guidance is "don't also map the controller inside the host". macOS
  CoreMIDI and Linux ALSA are multi-client and unaffected.

### 35.3 Input bindings

With ports owned by §35.2, input still terminates where it does for
every other source: `ControllerEvent` on the message thread
(`LockstepEditor::dispatch…`), parameter CC on the audio thread. Input is
the `onInput()` half of an `IControllerSurface` (§35.8); the default
`JsonControllerSurface` owns the loaded profile and compiles each binding
to one of the existing primitives — it produces **no new event types**.
Every binding targets a cell or slot by its `(ControllerButton, index)`
identity (`io/ControllerEvent.h`) — the *same* identity the surface model
(§35.8) exposes for feedback, so press and light are two halves of one
key:

| Physical control | Compiles to | Notes |
|---|---|---|
| Encoder | `CCMapping{ isRelative, scope = Contextual, mzPosition = k }` | Rides `CCMappingTable` via the §35.2 FIFO; follows focus track + MZ exactly as §5.3 / §26.2. 8 encoders ↔ `kMZSlots = 8`. |
| Button | `ControllerButton` + `index` → `ControllerEvent` | Note-on (vel > 0) = `ButtonDown`, note-off / vel 0 = `ButtonUp`. The 2.1.1 stream, no parallel path. A button may instead bind to a CC where its natural target is a parameter. |
| Fader | Scene crossfader (§17.5) | The fader axis has no QWERTY mapping by design (§17.5); the augmentation surface *honours* that omission rather than inventing a button for it. **Depends on 5.2** for the final scene-fader parameter; until then it drives the software crossfader slider directly. |

This is the concrete realisation of 2.1.1's promise: hardware-controller
integration "wires its own producer onto the same stream — no parallel
code path."

### 35.4 Feedback — the bidirectional seam

There is no controller-feedback path today (the MIDI-out machine is
sequencer→synth only). §35.4 adds feedback as **a second renderer of the
surface model defined in §35.8** — *not* a parallel re-derivation of UI
state. The screen and the controller both render from the same
`SurfaceModel` produced by the same pure `buildSurfaceModel()`; "it can
never disagree with the screen" is therefore *structural*, not a property
maintained by discipline. (Read §35.8 first — it defines `SurfaceModel`,
`SurfaceCell`, `SurfaceSlot`, and `CellState`.)

Concretely, `ControllerFeedbackEmitter` is the `render()` half of the
default `JsonControllerSurface` (§35.8). It runs on a **timer at the
ManipulationZone cadence (30 Hz)** — reuse that proven clock, do not add a
second. Each tick it calls `buildSurfaceModel()`, **diffs against a
per-indicator shadow cache**, and emits MIDI only for cells/slots that
changed (bounded by a profile `maxMessagesPerTick` cap). No MIDI is ever
written from the audio thread.

Each indicator is driven by a model field, not by a private read of
domain state:

| Indicator | Model field (§35.8) | Signal |
|---|---|---|
| Encoder LED ring | `SurfaceSlot::norm` for the bound `mzPosition` | CC out, value = `round(norm · ringMax)` |
| Button LED (on/brightness) | `SurfaceCell::base` token + `level` | note-on velocity = brightness curve |
| Button LED (colour) | `SurfaceCell::base` token (smart device maps it) **or** `baseColour` (dumb device); decoration channels (`border`/`dot`/`strip`/`pip`) overlaid where the device can show them | device colour index, else brightness-only (X-Touch is single-colour) |

A controller maps the **`CellState` token** where it understands it and
falls back to the carried `baseColour` otherwise (§35.8 versioning), so a
single-colour device, an RGB pad, and a future colour-ring device each
render the *same model* to the limit of their hardware with no per-state
code in the engine.

The playhead is the one always-moving indicator and is handled at the
render site as a single moving cursor (clear previous cell, light new)
rather than a full-grid repaint, so a running transport does not saturate
the link. This is a property of the emitter's diff/render step, not of the
model.

### 35.5 Profile files

Profiles are **user-editable JSON**, parsed at runtime. A profile is the
data payload of the default `JsonControllerSurface` (§35.8) — the
data-driven path most contributors use, no C++ required. Built-ins ship
embedded as defaults; user files in
`<userAppData>/Lockstep/controllers/*.json` override a built-in of the
same `id`. The plugin matches an opened device against `device.match`
(by identifier; see §35.2). The schema covers device identity, declared
coverage, input bindings, and feedback bindings:

```jsonc
{
  "schemaVersion": 1,
  "id": "behringer.xtouch-mini",
  "name": "Behringer X-Touch Mini",
  "device": { "match": "X-TOUCH MINI", "midiChannel": 1 },
  "coverage": "encoders+grid+transport",
  "covering": false,                      // only 16 grid cells — not ≥16 + spare
  "input": {
    "encoders": [
      { "index": 0, "cc": 1, "relative": "twosComplement",
        "target": { "kind": "cc", "scope": "Contextual", "mzPosition": 0 } }
      // …encoders 1-7 → mzPosition 1-7
    ],
    "fader":   { "cc": 9, "target": { "kind": "sceneFader" } },
    "buttons": [
      { "note": 8,  "target": { "kind": "event", "button": "Step", "index": 0 } },
      // top row → steps 0-7, bottom row → steps 8-15
      { "note": 16, "target": { "kind": "event", "button": "Step", "index": 8 } },
      { "note": 24, "target": { "kind": "event", "button": "VerbRecord" } },
      { "note": 25, "target": { "kind": "event", "button": "VerbPlay"  } }
    ]
  },
  "feedback": {
    "encoderRings": [ { "mzPosition": 0, "cc": 1, "ringMax": 11, "ringMode": "fan" } ],
    "buttonLeds":   [ { "step": 0, "note": 8, "colourModel": "brightness",
                        "brightness": { "off": 0, "trig": 127, "playhead": 90, "fillAdd": 64 } } ],
    "maxMessagesPerTick": 48
  }
}
```

A colour-capable device (e.g. a Launchpad) sets
`"colourModel": "velocityPalette"` and a `paletteMap` keyed by
**`CellState` token** (§35.8) to its colour-index space; the X-Touch uses
`brightness` and ignores hue. A `paletteMap` entry naming a token the
build does not know (or has deprecated) is **non-fatal**: that cell
degrades to the model's carried `baseColour` and the load raises a chrome
warning (`Controller profile X: unknown state <token>, using colour
fallback`). This is the contributor-facing edge of §35.8's add-only
versioning — a profile written against an older token set keeps working.

**Loading is otherwise graceful**: an unknown device installs no
bindings (silent — it is just a MIDI port); a malformed or
schema-invalid profile is skipped whole with a chrome warning
(`Controller profile X failed: <reason>`), never partially applied,
never throwing into the audio path. Validation checks coverage enum,
that every binding target resolves to a real `ControllerButton` /
`CCScope`, CC/note in 0–127, and `covering` only with full coverage +
≥16 grid cells.

### 35.6 Adaptive layout (specified, opt-in, deferred)

`LockstepEditor::resized()` is a single hardcoded layout today. §35.6
specifies an **opt-in `layoutMode`**, gated on *both* `profile.covering
== true` **and** an explicit user toggle (never automatic), that:

1. relocates the MZ to a horizontal row **below** the QWERTY block
   (knob-row ergonomics under the screen), and
2. optionally collapses / hides the on-screen step grid when a covering
   controller owns the grid.

**Reconciliation with §26.1.** §26.1 commits to "MZ *above* the grid;
the window grows vertically" — that remains the **default and
authoritative** software layout and the spatial mirror of the dedicated
hardware (encoders above keys). §35.6's below-QWERTY relocation is a
**guarded, opt-in alternate**, active only when a covering augmentation
surface is attached and the user enables it. It does not revoke §26.1;
the square-cell / vertical-growth invariants still hold in both modes,
and the default experience is unchanged.

It is sequenced last (ROADMAP 6.6.8) because it touches the one
hardcoded layout method, needs a covering device to test against, and
adds ergonomics, not capability (`PRINCIPLES.md` §1). Note that the
X-Touch Mini is *not* `covering` (only 16 grid buttons, no spare
modifiers), so the first adaptive-UI-eligible device is a larger pad
grid — another reason the work genuinely defers.

### 35.7 Principle and grammar compliance

- **§2 / §4 (one grammar; nothing unreachable).** Every binding
  terminates in an existing `ControllerButton`, a `CCScope` mapping, or
  the §17.5 fader axis — all reachable from QWERTY, the fader excepted by
  §17.5's *own* QWERTY-omission rule, which the augmentation surface
  honours rather than violates. No binding can name an action that does
  not already exist.
- **No bespoke single-purpose control.** A button only relabels a
  physical control onto an existing scope / verb / step primitive; it is
  never a one-off gesture.
- **§5 (equal citizens).** Encoders drive `CCScope::Contextual` and
  feedback reads the same per-track value accessor the MZ uses — both
  machine-agnostic, so a MIDI-out track's CC slots ring and tweak
  identically to a sampler's.
- **§8 (chrome announces state).** "Controller attached: <profile>",
  profile-load failures, the Windows-exclusivity warning, and any active
  `layoutMode` are all surfaced in chrome. A profile that does not
  announce itself is not finished.
- **§10 (refs, not contents).** Project state stores the profile *id*,
  not the profile body — mirroring the MIDI-out machine's preset
  reference. The profile file is content referenced by id/path.

### 35.8 The surface model and the controller authoring seam

§35.3 (input) and §35.4 (feedback) both lean on one structure. This
section defines it. It is the load-bearing decision of 6.6: the on-screen
renderer and every controller render from **one** description of the
surface, so they cannot diverge, and a new sequencer mode lights up on
hardware with **zero per-mode controller code**.

#### 35.8.1 One model, two renderers

Today the editor has two inline paint paths and no shared cell-state
object: `paintKeyButton` (`ui/KeyButton.cpp`, modifier/section/function
keys via `KeyButtonState` + `KeyGroup`) and `KeyboardArea::paintStepRows`
(step cells, computing fill / probability-brightness / playhead+held
borders / P-Lock dots / fill states / scope tint *inline*). Feedback that
re-derived that logic would be a second source of truth — the exact
divergence 6.6 exists to avoid.

Instead, a **pure** `buildSurfaceModel()` is the single computation.
Concrete signature (6.6.5(a) expansion of the `Focus` placeholder):

```cpp
SurfaceModel buildSurfaceModel(const UiState&,
                               const EditContext&,
                               const PressTracker*,
                               LockstepProcessor& /*read-only*/,
                               int activeTrack,
                               int stepPage,
                               GridDisplayMode);
```

- The **screen** renders from it each paint (`paintStepRows` /
  `paintSectionRow` / `paintFunctionRow` / `ManipulationZone` become
  renderers, not deriver+renderers; the look is preserved).
- Each **controller** (`IControllerSurface::render`, §35.8.4) renders from
  it on the 30 Hz emitter tick (§35.4).

Sharing the *function* — not a cached instance — is what guarantees
agreement: both call sites see identical output for identical state.

#### 35.8.2 Logical zones, keyed by input identity

A controller subscribes to **logical zones**, never pixel regions. A zone
is a set of cells addressed by the existing `(ControllerButton, index)`
identity from `io/ControllerEvent.h`:

| Zone | Cells |
|---|---|
| `StepGrid` | 16 `Step` cells (index 0–15) |
| `ModifierCluster` | the 8 modifier buttons |
| `SectionRow` | the 6 canonical `Section` cells |
| `FunctionRow` | nav + verb + transport keys |
| `ManipulationZone` | 8 `SurfaceSlot`s (continuous controls, §35.8.5) |

Because a cell's model identity **is** its input identity, press and light
are two halves of one key: to actuate a cell, a controller emits the same
`ControllerEvent{ButtonDown/Up, button, index}` onto the 2.1.1 stream that
the QWERTY surface emits (§35.3) — the editor reacts identically, by
construction. **Generality follows directly:** a new trig-grid mode (a
future "mode that uses the sequencer steps") changes the *state tokens*
the `StepGrid` cells carry, not the zone's shape or identities, so every
controller tracks it for free — no per-step, per-mode controller code.

#### 35.8.3 Cell schema and the `CellState` taxonomy

A cell carries a **semantic token and a resolved colour** (we send both),
plus a closed, named set of decoration channels:

```cpp
struct CellDecoration { CellState token; uint32_t colour; bool present; };

struct SurfaceCell {
  // Frozen §35.8.3 contract — do not reorder (controllers bind by field offset)
  ControllerButton button;   // identity — matches the press path (§35.8.2)
  int              index;    // step / section / track index, else -1

  CellState  base;           // semantic token (add-only — §35.8.6)
  uint32_t   baseColour;     // resolved ARGB — fallback for unknown/deprecated tokens
  float      level;          // 0..1 brightness (probability dim, etc.)

  CellDecoration border;     // playhead / held / mode-active outline
  CellDecoration dot;        // P-Lock presence
  CellDecoration strip;      // compound-chord / fill marker
  CellDecoration pip;        // latch / virtual-hold (3.10)

  // Screen-text extension (6.6.5(a)) — appended after frozen block; controllers ignore.
  // Adding these fields is NOT a contract break: the frozen prefix is unaffected and
  // controllers never read past it. Do not insert fields before `pip`.
  juce::String primary;    // ALWAYS the live function (decision 1)
  juce::String funcHint;   // dim secondary (Func-variant or always-on hint); "" = none
  juce::String keyHint;    // physical QWERTY legend ("D", "5", "Q" etc.)
  bool pressed  = false;   // physical OR mouse press, every modality
  bool disabled = false;   // dead key — base label dimmed (decision 3)
};
```

`CellState` makes the §24 taxonomy concrete — one enum unifying the
step-grid family (`Inactive`, `OutOfRange`, `TrigCertain`,
`TrigProbable`, `TrigSuppressed`, `FillAdd`, `FillSuppress`, `Held`, …)
and the key family (`Resting`, `Pressed`, `ModeActive`, `FuncHeld`,
`Disabled`, …). The decoration channels intentionally mirror how the
screen *already* layers: base fill, then border, then corner dot, then
edge strip, then pip.

**Why named channels, not a flat colour and not an opacity stack.** A
single flattened colour per state-combination is combinatorial and
destroys the orthogonality that keeps the paint code maintainable. An
arbitrary ordered opacity stack keeps the screen orthogonal but hands
controllers an open-ended thing to interpret — hostile to contributors.
The **closed, named channel set** is the middle path: the screen draws
each channel independently (adding an overlay = adding a channel — still
orthogonal), and a controller renders only the channels its hardware can
express (single-colour LED → `base` + `level`; RGB pad → `baseColour`; a
ring+centre device → `border` and `base` separately; unknown channels
ignored). New overlays append a channel; old controllers ignore it.

#### 35.8.4 The authoring seam — `IControllerSurface`

How a contributor adds a controller mirrors how they add a machine: a C++
base class, registered in a registry — **not** an embedded interpreter
(consistent with the locked "`IMachine` is C++, no scripting/IPC"
decision). The machine registry (§36) now carries both statically
registered first-party machines and dynamically discovered modules
behind one factory; the `ControllerRegistry` here is its parallel and
may adopt the same static/dynamic dual path if controllers ever ship
as loadable modules.

```
class IControllerSurface {
public:
  virtual ~IControllerSurface() = default;
  // device MIDI in -> ControllerEvent (2.1.1) / CC FIFO (§35.2)
  virtual void onInput(const juce::MidiMessage&, ControllerEventSink&) = 0;
  // diff against shadow cache + emit feedback for this device (§35.4)
  virtual void render(const SurfaceModel&, juce::MidiOutput&) = 0;
  // tokens this surface consumes — drives deprecation warnings (§35.8.6)
  virtual std::span<const CellState> statesOfInterest() const { return {}; }
};
```

`JsonControllerSurface : IControllerSurface` is the **default
data-driven implementation** — it interprets a §35.5 profile and covers
the great majority of devices with no code. A device needing imperative
logic (sysex LED framing, dynamic remap, bespoke ring encodings) ships a
C++ subclass instead. Both are registered in a `ControllerRegistry`
(parallel to the machine registry) and matched by device identifier.

This keeps the door open without committing to it: a future
`ScriptControllerSurface` hosting an embedded VM (the Bitwig-style path)
would be *just another implementation* behind this seam, addable later
without disturbing the model, the screen, or existing profiles. It is
explicitly **not** built now.

#### 35.8.5 The ManipulationZone zone — slots, not tokens

The MZ zone's cells are continuous controls, described for feedback +
optional device-screen text:

```
struct SurfaceSlot {
  const char*  id;        // ParamSpec id on the focused track
  const char*  label;
  juce::String valueText; // stepped/enum text or formatted value
  float        norm;      // 0..1 — the LED ring
  bool         stepped;
  bool         mapped;     // has a CC mapping / is learnable
};
```

Built from the focused track's `ParamSpec` (existing) plus the current
value. The slot descriptor adds only the *feedback + name* half: encoder
**writes** continue to ride the existing `CCScope::Contextual` path, with
`AbsoluteCCRouter` / `RelativeCCRouter` already handling absolute vs
relative (§35.3, §5.3). A value change from *any* source — pagination,
host CC, mouse — moves `norm`, so the next emitter diff re-rings the
encoder automatically.

#### 35.8.6 Versioning and compatibility

`SurfaceModel` carries a `schemaVersion`; `CellState` is **add-only** —
tokens may be deprecated but never renumbered or removed. `compatColour()`
guarantees *every* token (including deprecated ones) resolves to a colour.
A controller therefore degrades safely along two axes: a token it doesn't
recognise → render via the cell's `baseColour`; a decoration channel it
can't display → ignore it. `statesOfInterest()` (and the JSON
`paletteMap`, §35.5) let the loader warn in chrome when a profile names a
token that is unknown or deprecated, while still rendering it. The model
grows; old controllers keep working.

## 36. The Machine Module ABI (6.7)

§2 introduced the machine boundary as "one authoring model, two link
paths." This section specifies that machinery: the C ABI a loadable
module exports, the SDK base a contributor writes against, the
host-services it calls back into, the registry that unifies static and
dynamic machines, and the discovery / versioning / missing-module
rules. The motivation is decoupling: machines become independently
buildable units so the core does not have to carry every engine, and
third parties can author machines without forking Lockstep — against a
**stable, documented, JUCE-free contract**.

This is deliberately **not** CLAP/VST3 sub-hosting. The contract is
bespoke and may place unusual constraints on a module precisely because
every module is purpose-built for Lockstep. Hosting arbitrary foreign
plugins is out of scope (§9).

### 36.1 Three layers

```
   authoring source (identical everywhere):
        sdk::MachineBase : public IMachine
        ├── STATIC PATH  — first-party machine linked into lockstep_core,
        │                  registered via MachineRegistry's static factory.
        │                  Presents straight as IMachine; the C ABI is
        │                  never crossed at runtime.
        └── DYNAMIC PATH — third-party .so/.dll/.dylib, one exported C
                           entry point, loaded via juce::DynamicLibrary
                           and fronted by WrapperMachine : IMachine, which
                           translates C-ABI calls <-> the IMachine vtable.
   Both present plain IMachine to the sequencer — PluginProcessor's hot
   loop never learns which path a machine took.
```

The invariant: **the sequencer keeps talking to `IMachine` exactly as
today.** A statically-linked machine *is* an `IMachine` (via the SDK
base). A dynamic machine is reached through one extra `IMachine`
subclass, `WrapperMachine`, handed back by the factory like any other.
The static path is what first-party machines ship as; the dynamic path
is exercised end-to-end by the CI template module (§36.8).

### 36.2 The C ABI (`lockstep_machine_abi.h`)

A single C header, `extern "C"`, **POD only** — no C++, STL, or JUCE
types in the linkage-visible surface — so a module needs neither our
exact toolchain nor JUCE. Fixed-width types, explicit field order.

**One exported symbol.** The host passes its services and ABI version;
the module returns a static descriptor (or `NULL` to reject the host):

```c
#define LSM_ABI_VERSION 1

const LsmModuleDesc* lockstep_module_entry(uint32_t hostAbiVersion,
                                           const LsmHostVTable* host);
```

**Module descriptor** — manifest + factory + per-instance vtable:

```c
struct LsmModuleDesc {
    uint32_t    abiVersion;     /* LSM_ABI_VERSION the module was built against */
    uint32_t    structSize;     /* sizeof — add-only growth guard (§36.6) */
    const char* machineId;      /* "vendor.engine.vN" — static lifetime, UTF-8 */
    const char* displayName;
    const char* badge;          /* default badge; per-instance vtable may override */
    uint64_t    capabilityFlags;/* LSM_CAP_* bitset (§36.6) */
    void*       (*create)(const LsmHostVTable* host, uint32_t trackHint);
    void        (*destroy)(void* inst);
    const LsmMachineVTable* vtable;
};
```

**Per-instance vtable** — every function takes `void* inst` first (no
C++ `this`); mirrors the `IMachine` surface:

```c
struct LsmMachineVTable {
    uint32_t structSize;
    void (*prepare)(void* inst, double sampleRate, int32_t maxBlockSize);
    void (*reset)(void* inst);
    void (*process)(void* inst,
                    const LsmMidiEvent* events, int32_t numEvents,
                    const float* params, int32_t numParams,
                    float* const* channels, int32_t numChannels, int32_t numFrames);
    int32_t (*numParams)(void* inst);
    void    (*paramSpec)(void* inst, int32_t index, LsmParamSpec* out);
    int32_t (*numSections)(void* inst);
    void    (*section)(void* inst, int32_t index, LsmSectionInfo* out);
    const char* (*machineId)(void* inst);
    const char* (*badge)(void* inst);
    int32_t (*currentVoices)(void* inst, const float* baseParams, int32_t n); /* 0..4 */
    uint32_t (*flags)(void* inst);     /* hasInternalFilter/Amp, isMidiOut (cacheable) */
    int32_t (*isVoiceActive)(void* inst);  /* dynamic per-block — not cached */
    void (*processMidi)(void* inst,
                        const LsmMidiEvent* events, int32_t numEvents,
                        const float* params, int32_t numParams,
                        LsmMidiEvent* midiOut, int32_t* midiOutCount, int32_t midiOutCap);
};
```

**POD payload mirrors.** Audio crosses as a channel-pointer array
(`float* const*` + channel/frame counts), MIDI as a flat event array,
and `ParamSpec` as a POD struct whose strings are `const char*` of
**static lifetime** (the host copies into `juce::String` on return and
never retains the raw pointer — the existing `valueLabels` lifetime
rule, §2):

```c
typedef struct { int32_t sampleOffset; uint8_t bytes[4]; uint8_t numBytes; } LsmMidiEvent;

typedef struct {
    const char* id;            /* static lifetime */
    const char* label;
    float   minValue, maxValue, defaultValue, skew;
    uint8_t isStepped, unit, role, variant;   /* enums as uint8 — closed, add-only */
    int32_t sectionIndex;
    uint8_t zeroCrossingSnap;
    const char* const* valueLabels;           /* NULL-terminated, static lifetime */
} LsmParamSpec;

typedef struct { const char* label; int32_t parentCanonical; } LsmSectionInfo;
```

`LsmSectionInfo` carries only `label` + `parentCanonical`; the host
fills `firstSlot` / `pageCount` itself, exactly as it already augments
`IMachine::section()` today.

### 36.3 The SDK base — one source, two link paths

`sdk::MachineBase : public IMachine`. A contributor overrides the same
ergonomic virtuals that `IMachine` exposes today (`process`,
`paramSpec`, `currentVoices`, …). Below the line the SDK provides:

- **Static half.** Because `MachineBase` *is* an `IMachine`, a
  statically-linked machine needs nothing more — it registers straight
  into `MachineRegistry` as an `IMachine` factory; the C ABI is never
  crossed.
- **Dynamic half.** A one-line `LOCKSTEP_EXPORT_MACHINE(MyMachine)`
  macro emits `lockstep_module_entry`, the static `LsmModuleDesc`, and
  the C trampolines that wrap `float* const* channels` in a zero-copy
  `juce::AudioBuffer`, reconstruct a transient `MidiBuffer` from
  `LsmMidiEvent[]`, present `params` as `std::span<const float>` (the
  boundary type already documented in §2), and convert
  `ParamSpec ↔ LsmParamSpec`.

The two paths differ only in the build target — `STATIC` linked into
the core, or `MODULE` plus the export macro. The SDK convenience layer
may use JUCE, but compiles with hidden symbol visibility so the only
exported symbol is `lockstep_module_entry`; authors who want a tiny
module can write against the raw C ABI with no JUCE at all.

### 36.4 Host-services (`LsmHostVTable`)

Passed at entry and retained by the module; every callback takes an
opaque `hostCtx` first. POD C, JUCE-free. This is how a module reaches
the **single shared sample pool** every machine draws from — upholding
"state refs, not state contents" (PRINCIPLES §12): the module resolves
a `{path, xxHash32}` ref to a handle and reads *borrowed* PCM; it never
owns or serialises bytes.

```c
struct LsmHostVTable {
    uint32_t structSize;
    void*    hostCtx;
    /* shared sample pool — borrowed PCM, resolve-by-ref (never by index) */
    int32_t      (*resolveSample)(void* ctx, const char* path, uint32_t xxHash32);
    int32_t      (*sampleInfo)(void* ctx, int32_t handle,
                               int32_t* numFrames, int32_t* numChannels,
                               double* sampleRate, int32_t* missing);
    const float* (*sampleChannel)(void* ctx, int32_t handle, int32_t channel);
    /* transport / rate */
    double (*hostSampleRate)(void* ctx);
    void   (*transport)(void* ctx, double* ppqPosition, double* bpm, int32_t* isPlaying);
    /* sanctioned RNG (PRINCIPLES §11 — the only RNG a module may use) */
    uint32_t (*rngNext)(void* ctx);
    /* lock-free logging */
    void (*log)(void* ctx, int32_t level, const char* msg);
};
```

**Thread affinity is part of the contract.** `resolveSample` follows
`SamplePool::load` semantics (message thread); `sampleInfo` /
`sampleChannel` follow `get` (audio-thread-safe for already-resolved
handles). A borrowed `const float*` is valid for the duration of one
`process()` call; the pool is not mutated concurrently. Handles are
resolved by ref, never raw pool index, so pool `remove`/`swap`
index-shifts can't dangle a module. `transport` / `hostSampleRate` /
`rngNext` / `log` must be audio-thread-safe (logging via a lock-free
FIFO, not direct I/O).

### 36.5 Registry and catalogue merge

A `MachineRegistry` replaces the hand-written `makeMachineForId` switch
and the `kAvailableMachines[]` table. Its factory takes a
`HostServices&` aggregate — exposing the raw `SamplePool&` to static
first-party machines and the `LsmHostVTable` to `WrapperMachine` for
forwarding — so `SamplePool&` is **not** baked into the public factory
type. Resolution order for a `machineId`: static registry → loaded
modules → `StubMachine` fallback (§36.7).

`numAvailableMachines` / `availableMachineInfo` iterate static entries
plus discovered-module manifests merged into one list; `MachineInfo`
gains a `badge`, an `origin {Static, Module}`, and an `abiOk` flag so
the picker can show third-party badges and grey out version-mismatched
modules. The post-machine FILTER/AMP virtual-slot append and the
`hasInternalFilter()` / `hasInternalAmp()` opt-outs stay entirely
host-side and ABI-agnostic — only the two booleans cross (via `flags`);
the module never sees the FILTER/AMP slots that live past its
`numParams()` range. This registry is the static/dynamic sibling of the
planned `ControllerRegistry` (§35.8.4).

### 36.6 Versioning and capabilities — add-only

`hostAbiVersion` is passed into the entry; the module returns `NULL` if
it cannot satisfy it, and the host checks `desc->abiVersion` and
`structSize`. **Growth is add-only**, identical in spirit to the
`CellState` rule (§35.8.6): new vtable / descriptor fields append after
`structSize`; the host reads only `min(known, module structSize)`;
fields are never renumbered, reordered, or repurposed. Optional surface
is negotiated through `capabilityFlags`
(`LSM_CAP_MIDI_OUT | INTERNAL_FILTER | INTERNAL_AMP | AUDIO_INPUT`; the
last is forward-compat for the §2 `input_source` Thru/Recorder idea).
CI carries a frozen golden-header test: a module built against ABI v1
must still load under a vN host.

MIDI-out modules emit into the host-provided `midiOut[]` buffer only —
a third-party module must **not** open its own `juce::MidiOutput`;
device routing stays host-side (the standalone vs plugin distinction is
the host's concern, §5).

### 36.7 Missing-module round-trip

A patch may reference a machine module the user has not installed.
This reuses the missing-sample discipline (PRINCIPLES §12): on load,
an unknown `machineId` resolves to a `StubMachine` carrying the
unresolved id, and the UI offers it as a relink/replace target, exactly
as a missing sample offers relink. Crucially the stub **retains and
re-emits verbatim** the opaque `{id → value}` base-param and P-Lock map
it loaded, so re-saving a project that references an uninstalled module
does not silently drop that module's state. The stub can't *interpret*
the slots, but it preserves them.

### 36.8 Discovery, install, and the template module

The host scans a per-platform machines directory and, for each module,
opens it, calls `lockstep_module_entry`, and reads the descriptor —
**no instance is created at scan time**, only the manifest. Modules
with a rejected `abiVersion` / `structSize` are logged and skipped.
Installing a third-party machine is copying its module file into that
directory and rescanning. Install is an OS file operation, **not** a
scope+verb gesture — it is an out-of-grammar administrative action, on
the same footing as managing sample files (a drag-and-drop install flow
in the UI is an affordance over that file op, not a grammar addition).

A forkable **template module** is the third-party on-ramp and the CI
fixture in one: it is built on every cycle but **not installed by
default**; the test cycle installs it and asserts the full dynamic path
— discover → load → instantiate → `process` → unload. Because the
static path never crosses the C ABI, this template is the one artifact
that genuinely exercises it, and keeping it green is what prevents ABI
bit-rot.

The SDK (C ABI header + `MachineBase`) lives as a single self-contained
copy inside the main repository; first-party machines are
subdirectories of the main repo, each a buildable unit linking that
SDK; the template lives in one separate forkable repository that
vendors the same SDK. No per-machine repositories, no separate SDK
repository.
