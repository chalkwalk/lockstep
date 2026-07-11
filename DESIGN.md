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

Standalone and plugin are **co-equal hosts**, not a primary mode and a
fallback. The same surface, grammar, and project run in both; the only
difference is who owns the transport, the clock, and the save file — the
standalone owns its own (project files, file bar, quit guard; ROADMAP
Phase 9), the DAW host owns them when embedded (`PRINCIPLES.md` §3).

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
   parameter page and never launch a picker or popup. The graded target
   (`PRINCIPLES.md` §4): a fluent performer does **many things without
   looking at the screen, and almost anything without the mouse.** The
   surface used *today* is the computer keyboard plus a generic MIDI
   controller — an X-Touch-class box is the sweet spot, supported now as a
   first-class augmentation surface (§35). The eventual dedicated hardware
   is the ergonomic distillation of that pairing once playtesting has
   proven it: literally a fewer-key QWERTY in a grid layout — same
   key→action mapping, denser package, **no extra features**. If a workflow
   can't be done from the software's QWERTY today, the hardware won't add
   it.
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
  (`{None | External | Master}`, the outside-world tap); when set, the
  sequencer fills `buffer` with the chosen upstream signal before
  calling `process()`, and the machine reads-then-overwrites (Route) or
  reads-and-captures (Record). Inter-track routing is the *other*
  half: a track's CHANNEL "Out" slot (`{Master | Track N | Off}`)
  directs its finished signal, and a bus track reads the sum of tracks
  routed into it. Together these are the capability behind Route,
  sub-mix buses, Record, and Loop machines and the realtime
  resampling chain — see §27. Ordering is forward-only by topological
  sort with cycles refused; `input_source = Master` is the one
  sanctioned prior-block tap (§27).
- **Per-machine voice topology, pulled live.** A machine returns
  `currentVoices(baseParams) -> Polyphony { V0..V4 }`. The sequencer
  calls this for each fired trig (so a parameter-driven mode flip such
  as Analog Mono↔Para or FM Mono↔Poly takes effect on the next trig). `V1`
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
  events: `SampleMachine`, `FMMachine`, `AnalogMachine`, `DrumMachine`,
  `SliceMachine`, `StreamMachine`, `StretchMachine`, `PercussionMachine`,
  `DigitalMachine`. The `SampleMachine` is rate-based (pitch = speed, the
  turntable); `StretchMachine` is its **Flex** counterpart with *independent
  pitch and tempo* — driven (9.23) by the **Bungee** stretch engine behind the
  JUCE-only `IStretchEngine` pull-model seam (`src/dsp/IStretchEngine.h`), with
  `timestretch=Tempo` stretching a buffer to the project tempo via its stamped
  bar-length. Both PCM players (Stretch = RAM, Stream = disk) share one param
  family — `player_pitch` / `player_tune` (±50 c) / `player_timestretch` /
  `player_loop` (+ `player_reverse`, Stretch only) — and one tempo-ratio helper
  (`StretchMath::stretchTimeRatio`) so they cannot drift. The engine is a
  pull-based grain loop: the machine hands it an `IStretchSource` (RAM buffer or
  `BufferingAudioReader`) and the engine fetches arbitrary source positions, so
  **reverse / scrub / zero-speed are free, looping is seamless by source-position
  wrapping** (no restart or crossfade — the overlap-add makes the seam continuous),
  and **input↔output rate conversion is native** (Stream now resamples an off-rate
  file correctly). Bungee is MPL-2.0 (GPL-compatible, see `THIRDPARTY.md`); the
  WSOLA `dsp/TimeStretch` voice it replaced was deleted. **A440**: a `tune_mode`
  (Auto/Raw) cancels a sample's *effective* (user-else-detected) tuning deviation,
  so it plays in tune by default; Raw plays it as recorded.
- **Routers** — a machine that *carries* audio from an `input_source`
  into the track's own signal path. There is exactly one: `RouteMachine`
  (§29). It is near-empty by design — the actual shaping is done by the
  canonical post-machine FILTER / AMP / FX (§14), not by the machine.
- **Capture engines** — machines whose value is *stateful audio
  capture*: `RecordMachine` (overwrite) and `LoopMachine` (overdub).
  **Capture stamps, playback stretches:** a capture machine writes a
  volatile pool slot and stamps its musical bar-length (`sourceBars`); a
  tempo-tracking `StretchMachine` reads that stamp to stretch to tempo. The
  looper additionally *self-plays* its slot **varispeed** (tape: time-locked,
  pitch glides on tempo change — chosen over stretch because varispeed's
  linear time-map composes with in-place overdub, where WSOLA's grain map
  does not). The same captured loop is therefore playable two ways: the
  looper's varispeed self-play, or the Stretch machine's pitch-locked playback.
  **Loop monitoring** is a `monitor {Auto | On | Off}` switch governing
  whether the live input passes through to the output (separate from
  recording, which always captures) — so an insert looper is audible *before*
  you record. **Auto** is **state-aware**: for an `None`/`External` insert it
  monitors in every state **except while the take is Playing back** — the
  capture has replaced the live source, so live-thru drops on the record→play
  transition and is restored when Idle/Stopped; for a `Track`/`Master` tap it
  is always loop-only (the tapped source is already audible on its own path,
  so passing it through would double-monitor). On/Off are absolute. This makes
  the looper "work as expected" whether you feed it by routing a track's `Out`
  into it (it passes through + layers) or by tapping a source on its SRC panel
  (it adds the loop layer without re-monitoring the live source).

  **`loop_sync {Free | Free Len | Sync}` selects the loop's *length*** (the
  Octatrack QREC/QPL analog): **Free** records at native length (ignores tempo);
  **Free Len** varispeeds to the recorded musical duration; **Sync** grid-locks
  the loop length to the track's own length × divider and phase-locks it — the
  loop *is* the track grid by construction (no looper-only length params).
  **Edge *timing* is not a looper concept** — record-start, punch-out, play,
  stop, and overdub enter/exit all arm to the one shared `launchQuant` grid
  (§4.8, PRINCIPLES §25), exactly like a Scene launch. A quantized stop is a
  **punch-out** that lands on the boundary and hands straight to Play; a
  Free-length take simply records with no armed edge (equivalent to
  `launchQuant = Instant` for that gesture). **Double-tapping** `REC` (or
  `PLAY`) fires instantly, the universal instant override (a held-armed record
  shows **ARM** until the boundary). **Decay** (`loop_decay` 0…1,
  `loop_decay_mode {Overdub | Always}`) makes the loop quieter over iterations
  — Overdub fades the old layer only at the overdub write (feedback knob),
  Always fades the whole loop each pass (tape echo). With a looper focused the
  16-button step grid is repurposed as an **always-on console** (transport +
  length + beat-repeat + tape FX, §29.2); the mini-seq strip shows the loop
  position (a continuous playhead + a landing pip for a pending edge).

Pure timbre *processing* is **not** a machine. A filter, EQ, distortion,
bitcrusher, reverb, delay, compressor, or any other "audio in → audio
out" colourist is an **`IEffect`** (§32) — inserted post-AMP or on the
master bus, reusing the same `ParamSpec` / `role` / P-Lock
infrastructure. Building such a processor as a machine would waste a
generator slot, duplicate the FX system, and break the §6.1 promise that
FILTER / AMP / FX mean the same thing on every track (`PRINCIPLES.md` §8).
The litmus test: *does it originate or capture sound, or merely colour an
existing signal?* Originate/capture → machine; colour → `IEffect`. Route
is the single deliberate exception, and it earns it by doing no colouring
of its own.

This is also where catalogue scope is drawn. A machine ships **stock**
only when it is iconic and foundational to the reference lineage (§29);
anything more specialised is a **third-party module** authored against
`sdk::MachineBase` and loaded via the 6.7 ABI (§36) — including engines
the author maintains in their own separate repositories. "Would this be
better as a separate download?" is a real question for every proposed
machine, not a formality.

## 3. The Baseline Sample Machine

The first engine to inherit `IMachine` is a monophonic sampler
designed for trip-hop / drum-machine workflows. It is the **Flex**
archetype in the Octatrack lineage — samples are decoded into RAM and
fully manipulable (pitch, slice, trim, loop). Its disk-streaming
sibling (**Stream**) and the input-consuming machines (**Route**,
**Record**, **Loop**) are described in §29:

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
- a musical subdivision (1/64 … 4/1, straight / dotted / triplet) against a shared PPQ grid;
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
  (§13.5), and `launchQuant` (the single launch-quantize grid governing every
  deferrable action; §4.8, default `Bar`). Matches Ableton's "Live Set" and the
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
  - **Diagonal routing** — Scene `N` always plays phrase row `N` (the
    diagonal is pinned; it is not a stored field). All non-deviated tracks
    play their phrase from row `N`. Per-track phrase deviations are
    session-RAM-only and are never serialized.
  - `activeMask[track]` — which musicians play in this scene (replaces
    the old per-Pattern mute mask; runtime silence =
    `globalMute[t] || !activeMask[t]`).
  - `coreTime` — the Scene's time signature; see §4.8.
  - Morph A/B snapshot fields; full crossfader implementation: §17/5.2.

**Scene = saved floor + live overlay.** The fields above (activeMask,
coreTime, Morph snapshot) are the Scene's **saved floor** — its identity,
serialized, and the §13.6 Scene-scope checkpoint floor. On top sits a
**live overlay**: per-track phrase deviations. Deviations are **always**
part of the overlay and are **never** promoted to the floor *as routing*
(commit instead bakes them into phrase content — see below). The effective
live phrase for a track is `liveDeviation(t) ?? sceneIdx`. Each Scene
**remembers its own** overlay while the set is running. **`Scene + Record` =
commit-and-bake** (confirm-gated, §16): it writes live mask changes into
the floor *and* **bakes** the per-track deviations down into content — for
each deviated track it copies that track's effective live phrase into its
diagonal row (`sceneIdx`) slot, then clears the deviation. A hand-curated
heterogeneous arrangement persists by **materialising content onto the
diagonal**, not by storing a routing vector. Because the bake **overwrites
phrase content and severs sharing** — the baked slot becomes a snapshot,
disconnected from the phrase it copied (the Phrase/Scene split, below) — it
is destructive and prompts a **confirmation** (CONFIRM/CANCEL) before writing.
Discarding the overlay: re-launching the active Scene (`Scene + active-step`
single-tap, or `Scene + Clear`) returns to the floor; `Func + Scene + step`
launches any Scene at its clean floor (§16). This split is the same machinery
as the scope-respecting Checkpoint (§13.6): the floor is the saved state, the
overlay is working state above it.

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

**Live phrase grammar.** `Phrase` (or `Track + Phrase`) selects breadth =
*the focused musician*; `Scene + Phrase` selects breadth = *the whole band*.
Both then pick the phrase row from the step press. So:

- `Phrase + step` (or `Track + Phrase + step`) — **deviate** the focused
  musician (the track highlighted in the VU bar) to the picked phrase. A live
  overlay change.
- `Scene + Phrase + step` — **deviate the whole band** to the picked phrase.
  Landing on the Scene's own diagonal row (`sceneIdx`) clears all deviations.

Deviation management: "rejoin one musician" = pick the **diagonal** phrase
(row N for Scene N), which the selector marks with a border (below); "clear
all deviations" = `Scene + Phrase + step` on the diagonal, or re-launch the
active Scene (`Scene + active-step`), or `Func + Scene + step` (baseline
launch, §16). See §16 for the launch model and §13 for all gesture bindings.

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
  `Track`-scope floor of the Checkpoint stack (§13.6) — hold `Func+Y`
  (RESTORE) with `Track` held to revert the live Kit to its on-disk saved state.
  This is the generalised Octatrack "Part-reload" (§4.7.1).

> ⚑ **Provisional gesture detail.** The machine-vs-library paging of the
> picker and the exact save/load verbs are a UI-design detail to settle
> when the picker is built (it ships as part of the §23 management pass);
> the *model* commitment here is only that the Kit is a named, library-
> recallable unit reached through `Func+Track`, not a selectable per-Scene
> object.

### 4.7a State-ownership invariants (working copies vs authoritative stores)

Several values exist in two places by necessity — a real-time/working copy and an
authoritative store — and must be kept consistent. Per PRINCIPLES §20 each such
pair has **one named sync point**; nothing else may write across the boundary.

- **Track length.** Working `Track.length` (read by UI/generators) vs the APVTS
  `trackLength` param (read by the audio thread). All edits go through
  `LockstepProcessor::setTrackLength` (writes both); scene/song switches project
  via `projectPhraseToTrack` then mirror to the param via
  `syncTrackParamsFromActiveKit` (called from `reinstallMachinesFromActiveKit`).
  Bypassing the setter is what once desynced the Euclid generator from playback.

- **Density.** `Arrangement::liveDensity[]` / `liveMasterDensity` (message-thread
  working copy, serialised) mirror the processor's `trackDensity_[]` /
  `masterDensity_` atomics (audio-thread live). The working copy is the
  authority; it is re-projected on song/scene switch (`syncWorkingFromActive`),
  and floor/song boundaries reset both sides together (`setActiveSong` /
  `setActiveSceneToFloor` wipe the atomics; `Arrangement` resets `liveDensity`).

- **Working sequence vs active phrase/scene.** `arrangement_.working` is a
  projection of the active phrases + kit. `syncWorkingFromActive()` builds it;
  `applyTrackEditsToPhrase` writes live edits back before any scene/phrase switch.
  Switch order is always write-back → switch → re-project, so live edits survive.

- **Master-FX unit focus.** `UiState::masterFxInsertSlot` (0–3) may point at an
  *unloaded* unit; readers must check loadedness — use `firstLoadedMasterUnit` /
  `nextLoadedMasterUnit` to land on a real unit rather than trusting the slot.

### 4.8 TIME page — tempo and time signature

Tempo and time-sig share an identical scope ladder (Set → Song → Scene) and
are edited together on a single **TIME** page.

**Grammar entry** — **`Song+TRIG`** or **`Scene+TRIG`** both open the TIME
sticky band (the TRIG key relabels to "TIME" while the band is open). The
held modifier at entry sets the **entry scope** — the scope that edits target
when no modifier is held. Inside the band: `Func+Song` → Set/global scope;
bare `Song` → Song scope; bare `Scene` → Scene scope; no modifier → entry
scope (see §13 momentary-hold convention).

**TIME band — two controls:**

- **Field 0 — Tempo** (continuous). Shows the resolved absolute BPM at the
  current scope; a scope-coloured arc tick marks the parent floor. Editing
  writes the back-solved ratio. At Song/Scene scope, dialing to the floor
  displays `INHERIT (<parent bpm>)` and clears `hasTempo`. At Set scope there
  is no INHERIT floor — the global owns its value. In DAW mode the global
  scope is read-only (host BPM; Lockstep never writes to the host clock —
  PRINCIPLES §3).

- **Field 1 — Time Sig** (stepped). Curated list ordered by ascending bar
  length: `3/8 → 2/4 → 5/8 → 3/4 → 6/8 → 7/8 → 4/4 → 9/8 → 5/4 → 11/8
  → 12/8 → 7/4`; Set default is 4/4. At Song/Scene scope index 0 =
  `INHERIT (<parent sig>)`, which clears the override. The scope-coloured
  reference tick shows the inherited parent.

**Revert to parent (INHERIT floor).** For both controls the revert mechanism
is built into the control itself: dial tempo to the range floor, or dial
time-sig to index 0. Each reverts independently. (The hold-scope + Clear
chord is **not** used on this band — it remains active for Swing only,
§19.2.)

**Effective values:**

```
effectiveTimeSig = Scene.timeSig           (if Scene.hasTimeSig)
               ?? Song.timeSig             (if Song.hasTimeSig)
               ?? Project.defaultTimeSig   (always present; default 4/4)

effectiveTempo =
    globalRoot
    × (Song.hasTempo  ? Song.tempoRatio  : 1.0)
    × (Scene.hasTempo ? Scene.tempoRatio : 1.0)
```

All consumers (`PluginProcessor` launch-quantize, metronome, velocity-weight,
density-weight, phrase-length seeding) call `effectiveTimeSig` rather than
reading `Scene.coreTime` directly. The top-bar readout shows both values in
scope colour. The Clock is fed `effectiveTempo` at every Song/Scene boundary
and when tempo is edited in real time; rate changes apply at the next
sub-block boundary for phase continuity.

Note: 8/8 and 4/4 are *different* time signatures for Lockstep's purposes —
numerator drives the `MetricGrid::metricWeight` pulse tree, so 8/8 produces
eight weight-graded ticks per bar while 4/4 produces four. Both have the same
`barPpq`; they differ in accent density.

**Swing suppression.** Holding Song or Scene alone (no TRIG) opens the Swing
band as normal. Once the TIME page is open, the `swingDismissed` flag prevents
the bare modifier from accidentally re-entering Swing for the duration of the
session; it clears on a fresh unmediated modifier press.

**Merge rule.** Controls are merged onto one page only when they share a scope
ladder. Tempo and time-sig both live on Set → Song → Scene. Swing's ladder is
Song → Scene → Track (per-track, no Set); it stays on its own page.

**Core time roles:**

1. **Launch-quantize grid.** `launchQuant` (Set-level; default `Bar`) is the
   **single quantize authority** for *every* launch-like action (PRINCIPLES
   §25): Scene launch, Song switch, Phrase deviation, mute/unmute, per-track
   phase-reset, and the looper's record / play / overdub edges. There is no
   second grid. The Set-level grid selects one of
   `{Instant, Beat, Bar, Bars2, Bars4, Bars8}`. A bar =
   `numerator × (4.0 / denominator)` quarter-note PPQ; a beat =
   `4.0 / denominator` PPQ. The boundary is the next multiple of the selected
   grid at or after the block start:
   - `Instant` — fire now (no deferral).
   - `Beat / Bar / Bars2 / Bars4 / Bars8` — fire at the next multiple of
     `grid × (beatPpq | barPpq)`. This is the generalisation of the old
     `launchQuantizeBars × barPpq` maths (which only ever expressed
     `Bar/Bars2/Bars4/Bars8`).

   The `LaunchQuant` enum carries a seventh value, `PhraseEnd`, that is
   **only reachable as a per-track override** (below), never as the Set-level
   grid: whole-band actions (Scene, Song) stay atomic on one shared boundary,
   so a band-wide `PhraseEnd` — where coprime-length tracks would each fire at a
   different time — is deliberately not offered. `PhraseEnd` fires at the end of
   *that track's* current phrase cycle (`trackLen × divPpq`; a zero-length cycle
   degrades to `Instant`), letting an individual track re-align to its own
   material rather than the shared bar.

   **Double-tap = instant override.** Any launch-like gesture, double-tapped,
   fires *now* regardless of `launchQuant` (PRINCIPLES §17 verb family). Instant
   fires are **phase-preserving**; a phase-reset is a separate explicit rider
   (§13.4), never a side-effect of firing now.
2. **Metronome downbeat.** The "1" fires at `barPpq` intervals.
3. **Default phrase length.** New Phrase seeded from `numerator × (4 /
   denominator)` steps (a default only — freely editable afterward).

**Per-track override.** Each track carries a `launchQuant` field defaulting to
`FollowGlobal`; set to any concrete `LaunchQuant` value — including `PhraseEnd`,
which lives here only — it overrides the Set grid for that track alone. The old
per-track `launchMode {GlobalBar | PhraseEnd}` flag folds into this: `PhraseEnd`
is now just `launchQuant = PhraseEnd`, and `GlobalBar` is `FollowGlobal` with a
`Bar`-family Set grid. The per-track value is edited on the track params page
(cyan Track scope, TRIG key); the Set-level grid value is edited on the
transport-globals page (`Func + 7`).

**Serializer v25** fields: `Project.launchQuant` (enum, replaces the pre-v25
`launchQuantizeBars` int — legacy int `n` maps to the nearest bar-family value:
1→`Bar`, 2→`Bars2`, 4→`Bars4`, 8→`Bars8`), per-track `launchQuant`
(default `FollowGlobal`).

**Serializer v21** fields: `Project.defaultTimeSig` (numerator/denominator),
`Song.hasTimeSig`/`Song.timeSig`, `Scene.hasTimeSig`, `Song.hasTempo`/
`Song.tempoRatio`, `Scene.hasTempo`/`Scene.tempoRatio`. `Clock.localBpm_`
continues to serialize for standalone. Legacy v20 projects load with Set-default
4/4 and all ratios = 1.0 (no deviation from root).

### 4.10 KEY — key signature and the brightness model

(§4.9 is reserved for the anticipated tempo split; tempo currently lives in §4.8.)

Lockstep's tonal model is **opinionated but flexible** (PRINCIPLES §23): the
primary way to think about key is the **circle of fifths as a single bright→dark
line**, not classical mode names. Classical names remain as labels; the
chromatic path is always available. Key signature shares the TIME page's scope
ladder (Set → Song → Scene) and is edited with the same grammar.

**The brightness line.** Lay the fifths on a line with the root at 0:

```
   …  Db  Ab  Eb  Bb   F  | C |  G   D   A   E   B   F#  …
 (flat / dark side)      root      (sharp / bright side)
```

A scale is a contiguous **window** on that line:

- **Brightness** = window *position*. For a 7-note window there are exactly 7
  placements that contain the root; they are the modes in brightness order:
  Lydian (brightest, `[0…+6]`) → Ionian → Mixolydian → Dorian → Aeolian →
  Phrygian → Locrian (darkest, `[-6…0]`). One integer selects the mode; sliding
  it sharp brightens, flat darkens.
- **Richness / core** = window *size*: 7 notes, the central 5 (pentatonic core),
  the central 3 (triad core). The cores are the inner fifths of the same window
  — which is why dropping major's two outer fifths yields the major pentatonic.
  Cores are derived; they belong to the generators (§39.11), not to the key.
- **Color / exotic = functional modifiers** (below).

**Representation (`src/core/Scale.h`).**

```cpp
struct KeySig {
  uint8_t  root = 0;          // 0–11 pitch class of tonic
  int8_t   brightness = -1;   // 7-note window offset: -1 Ionian, 0 Lydian, -4 Aeolian, -6 Locrian
  std::vector<Modifier> modifiers;
  uint8_t  symmetric = 0;     // 0 diatonic system; 1 whole-tone; 2 diminished (overrides the above)
};
struct Modifier {             // the invariant authoring atom
  enum class Op : uint8_t { Add, Raise, Lower };
  uint8_t homeMode;           // mode the op is defined in (Aeolian for harmonic, …)
  uint8_t homeDegree;         // degree within that mode the op targets (7 for harmonic, …)
  Op      op;
};
```

Everything else is **derived, never stored**: `pcMask()` (12-bit pitch-class
set), `degrees()`, `coreTier(degree)`, `quantize(note)`, `classicalName()`,
`degreeNameOf(modifier)`. This keeps the serialized field tiny and
transposition/mode-portable.

**Functional modifiers — Add vs Alter, anchored to the collection.** A modifier
is either an **Add** (augment, +1 note — e.g. the blues ♭5) or an **Alter**
(Raise/Lower an existing pool note, same count — e.g. harmonic minor's ♭7→7).
Each is *defined in a natural home mode* (harmonic/melodic/blues on minor,
Neapolitan on major, double-harmonic on Phrygian) but resolves to an **absolute
note change anchored to the shared note-pool (the fifths window)**. Because
relative modes share that pool, the same change applies across them and only its
*degree-name* changes: A-minor's ♭5-add and C-major's ♭3-add are the *same*
operation (both add E♭). A modifier is **offered only where compatible** — its
target note must be present and not already in the target state, so blues ♭5
won't apply to Phrygian/Locrian, which already hold it (`isCompatible`).

v1 named modifiers: **Harmonic** (raise ♭7→7) · **Melodic** (raise ♭6→6 & ♭7→7)
· **Double-harmonic** (raise ♭3→3 & ♭7→7) · **Harmonic-major** (lower 6→♭6) ·
**Blues** (add the blue note — one fifths-anchored Add that reads ♭7 Lydian / ♭3
Ionian / ♭6 Mixolydian / ♭2 Dorian / ♭5 Aeolian / ♭1 Phrygian / ♭4 Locrian;
valid in **all seven** modes of the full scale) · **Neapolitan** (lower 2→♭2).
Whole-tone and diminished are
the two **symmetric** scales — deliberately outside the brightness/modifier
system, selected via `symmetric` for completeness.

**Cascade and resolution.** `KeySig` lives at three levels exactly like
`TimeSig`: `Project.defaultKeySig` → `Song.keySig`/`hasKeySig` →
`Scene.coreKeySig`/`hasKeySig`. A new `effectiveKeySig()` resolves
Scene → Song → Set, mirroring `effectiveTimeSig()`; all consumers (quantize,
generators, scale-highlight) call it.

```
effectiveKeySig = Scene.coreKeySig          (if Scene.hasKeySig)
              ?? Song.keySig                (if Song.hasKeySig)
              ?? Project.defaultKeySig      (always present; default D Dorian)
```

**Editor grammar.** Key shares the TIME page's scope ladder and entry: the
signatures band opens via `Song+TRIG` / `Scene+TRIG`, and **re-pressing TRIG
cycles TIME ↔ KEY** as a family on the same key. The TRIG section key shows the
**destination** of the next press (reads "KEY" on the TIME page, "TIME" on the
KEY page); the MZ header shows where you are. Scope works as on TIME: `Func+Song`
→ Set, bare `Song` → Song, bare `Scene` → Scene, no modifier → entry scope.

The KEY surface splits across the MZ encoders and the step grid (the "grid is the
menu" pattern, §6):

- **MZ encoders — the scalars:** **Root** (fifths-ordered, D-centred — each
  detent moves by a fifth), **Tonality** (brightness, Lydian…Locrian), and
  **Note-count** (the functional `ScaleType`: Triad 3 / Pentatonic 5 /
  Diatonic 7). At Song/Scene scope, Root index-0 is **INHERIT** (clears the
  override), mirroring TIME.
- **Step grid — the toggles:** row 1 holds the **functional modifiers** as
  checkbox cells (4 states: enabled/disabled × active/inactive — bright = applies
  in this tonality, grey = dormant, mark = on; always toggleable, the mark
  persists when a modifier goes dormant). Row 2, right-aligned, holds the two
  **symmetric scales** (whole-tone, diminished) as a radio pair. Selecting a
  symmetric cell overrides into symmetric mode and **greys** the Tonality and
  Note-count encoders and the modifier cells — they have no meaning there.

Default key is **D Dorian** — the symmetric centre (D centres the circle of
fifths, Dorian centres the bright/dark axis; its interval pattern is a
palindrome), so the default leans neither sharp nor flat.

- **Brightness** (stepped, primary) — Lydian…Locrian; the resolved classical
  name shows as a label, "" when a modifier set has no common name.
- **Root** (stepped, 12 pitch classes).
- **Modifiers** — compatibility-gated Add/Alter toggles, each shown with its
  degree-name relative to the current root.

INHERIT floor at Song/Scene scope clears the override, exactly like TIME.

**Scale-aware authoring (guides, never constrains — PRINCIPLES §23).**

- **In-scale highlighting** — the NoteEdit grid and play-in keyboard highlight
  in-scale notes with the root emphasized (display only). An optional
  **root-anchored** keyboard layout shifts so the root sits left with balanced
  sides.
- **Diatonic navigation** — holding a note in NoteEdit, `Nav` moves the pitch
  **diatonically**, `Func+Nav` moves **chromatically**, move-mode `Up/Down`
  shifts by octave. Transpose defaults to by-scale-degree, chromatic under
  `Func`.
- **Scale-quantize as a per-track MIDI-effect transform** — an opt-in,
  **default-off** per-track stage at the note-emit boundary that snaps emitted
  *and* live-played notes to `effectiveKeySig().quantize()`. Non-destructive:
  authored step pitches are untouched; only the live stream is snapped (the
  Squarp-Pyramid "quantize as a MIDI effect" model). It is a deliberate tool the
  performer *chooses*, not an always-on "no wrong notes" safety net — which is
  precisely why it does not cross NON-GOALS §13 (PRINCIPLES §14, §23).

**Serializer.** New version one past the current head: `Project.defaultKeySig`,
`Song.hasKeySig`/`keySig`, `Scene.hasKeySig`/`coreKeySig`, plus the per-track
quantize flag. Stored as the semantic axes (root/brightness/modifiers), not a
baked mask. Legacy projects load with default D Dorian, no overrides, quantize
off.

## 5. Input Layer

All parameter writes travel through the **EditContext** before
landing. The target depends on both the active section type and
whether a step is currently held:

| Active section | No step held | Step held |
|---|---|---|
| Machine section | Track Base (`baseParams`) | Step PLock (`paramOverrides`) |
| Track meta — COND | Track Base Condition (`baseCond`) | Step Condition (`step.condition`) |
| Track meta — TRIG | Track defaults (`defaultNote`, `defaultVelocity`, `gateLength`) | Step trig overrides (`noteOverride`, `velocityOverride`, `gateOverride`) |
| Track meta — DIV (Track+TRIG) | Kit divider (shared across track's phrases) | (no step-level override) |
| Track meta — PHRASELEN (Phrase+TRIG) | Active phrase length | (no step-level override) |
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

**Discrete-encoder mapping rule.** For stepped (integer-valued) parameters,
encoder deltas are translated into integer steps, not normalized fractions.
The device sends acceleration magnitude 1–7; the mapping scales this so
one turn never moves more than ¼ of the total step count, and `ceil` ensures
the minimum never rounds to 0 (one slow detent = exactly 1 step). This applies
uniformly to machine params and meta-band fields (Euclidean pulse/offset/accent,
phrase length, Musicality, etc.) whenever the param or field carries a stepped flag.

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
| `W` | Scene   | section scope | scene launch (`Scene+step` = overlay; `Func+Scene+step` = floor) / commit / revert (§16). The Kit / machine picker is on `Func+Track`. |
| `A` | Morph   | section scope | morph-assign per scene |
| `S` | Song    | section scope | song select; `Func+Song` = Global / master FX / gain cells |
| `Z` | Mute    | performance specialist | hold-and-tap-many multi-mute |
| `X` | Fill    | performance specialist | `Fill+step` marks fill-only |

Column 1 (`1 Q A Z`) and column 2 (`2 W S X`) compound only **cross-
column** per the §13 compound-chord rule. The two specialists on row 3
signal the semantic split: they don't take a section. `Cue` is the
audition scope (DESIGN §31), entered as the compound `Func+3` rather
than a dedicated cluster key (hardware parity); the cue *bus* /
pre-listen feature is still 6.4, and cue-scene functionality folds
under `Func+Scene` and master-scope cells in the interim.

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
- `Y U I O P` = verbs `Snapshot / Record / Play / Clear / Confirm` (colour-rethink).
  `Func+Y` = Restore (pop checkpoint); `Func+U` = Omni copy; `Func+I` = Unqualified paste; `Func+O` = Delete (+ confirm);
  `Func+P` = Cancel. `Record-Arm` / `Play-Stop` chords on `9 / 0` are deferred —
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
  | 7 | 2 | **FILTER** | Track filter block (§14) — always present with an OFF mode; machines with internal filter expose their own pages first, track filter accessible as a virtual section. |
  | 8 | 3 | **AMP**  | Output mix (Level, Pan, Sends) + optional amplitude envelope (§14); CHANNEL is always present, ENVELOPE block present only for machines without `hasInternalAmp()`. |
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
  | *(none)*  | machine trig | machine SRC | machine FILTER (if present) | machine AMP (if `hasInternalAmp()`) | machine MOD | machine FX (drive/bit) |
  | `Func`    | COND (conditions) | NOTE (step entry) | (dim — reserved) | (dim — reserved) | (dim — reserved) | (dim — reserved) |
  | `Track`   | kit divider (DIV) | input_source / Route | track FILTER (always; OFF mode available) | CHANNEL (level/pan/sends) + ENVELOPE (if !hasInternalAmp) | per-track LFO (if any) | IEffect insert 1+2 |
  | `Phrase`  | phrase length (LEN) | (dim) | (dim) | (dim) | (dim) | (dim) |
  | `Scene`   | launch / commit · coreTime | global + deviations | (dim) | active-mask | Morph snapshot | (dim) |
  | `Morph`   | (renamed `CXFD`) | morph-assign SRC | morph-assign FILTER | morph-assign AMP | morph-assign MOD | morph-assign FX |
  | `Song`    | (dim) | (dim) | master FILTER (if any) | master gain + sends | (dim) | master FX 1+2 |

The `Phrase` and `Scene` rows follow directly from the §4.7 ownership
split (this is the resolved content pass — the rows are no longer
provisional):

- A **Phrase** is pure per-track *content* — steps, P-Locks, length, a
  per-phrase scale lock (§34.2 — a *playable layout*, not auto-correct) — and
  carries **no sound**. So only the timing-domain `TRIG` cell is live
  (`LEN` — phrase length, per active phrase); every sound cell (`SRC`–`FX`)
  dims, because the kit-base params a Pattern used to share with its Part now
  live in the **no-scope / Track-base** row (machine params write to the
  track's **Kit** base, §4.7.2). Divider is **Kit**-owned (shared across all
  of a track's phrases) and lives under `Track+TRIG` (`DIV`).
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

   This reactivity is **colour, not just text.** Holding `Func` (like
   holding any scope) must repaint the whole section row: every cell
   that has a wired secondary glows in the `Func`/secondary hue, and
   every cell that has none dims to the disabled state. Text alone
   (swapping the primary label) is not sufficient — a row where only
   the labels change, with no colour transition, hides which
   secondaries are actually reachable and is the failure §10 forbids.

#### 6.1.1 Uniform scope overlay + origin colour (Part 4)

The scope×section matrix above is resolved by one **ordered layer
stack**: `Machine > Track > Phrase > Scene > Song > Global` (highest
precedence first). A section key resolves **top-down** — the
highest-precedence layer that owns params at that section wins; a
section with no owner there falls through to the next layer down.
Holding a scope **peels** every layer of higher precedence than it, so
you see that scope's layer *and everything below it*, never the layers
above. This is **one rule for every scope** — unqualified is simply the
`Machine` floor (nothing above to peel), `Track` is the `Track` floor,
and so on. The stack deliberately contains only the *hierarchy* scopes;
**`Morph` is excluded** (it is a parameter-assignment mechanic, not a
param-owning layer — it keeps its bespoke morph-assign row), and `Mute`/
`Fill` are **not** members either (there is no principled position for
them in the hierarchy, so adding them would be dogmatic).

- **Unqualified** (no scope held): fill top-down. `Machine` wins each
  section; a section the machine owns nothing at falls through to the
  `Track` layer (e.g. `FILTER` on a bare sampler shows the track FLTR).
  The machine owning a section **suppresses** the track page as an extra
  page — no double-append (the historical "buried track AMP" bug).
- **Hold `Track`**: the `Machine` layer is peeled. You see **track
  sections only, not combined with machine params**; a section with no
  track block (`SRC`/`MOD` on a synth) is **dim** and inert — strict,
  not a fall-back to the machine page.

Each resolved page carries the **origin scope** it came from, and that
drives colour on both surfaces (`ScopeSectionSelect.h::SecOrigin`,
`selectScopeSections`):

- **Section keys** are tinted by origin: machine-owned reads neutral
  steel; track-owned reads **cyan** (`scope.colour.track`) — even at
  rest, and even for a section that only *fell through* to the track
  layer. Under `Track` hold, machine-only sections dim.
- **The ManipulationZone reflects the scope of the page it is
  *showing*** (`scope.colour.<origin>`) — the scope the page was reached
  through, **not** whatever modifier is momentarily held. A normal
  section page washes/labels by origin and the **banner names the scope
  in text** (`MACHINE` / `TRACK` prefixes the page title); colour is a
  learned shorthand, the word is the durable signal, so a new user is
  never reliant on the palette alone. **Scoped section-secondary meta
  bands carry their origin scope too** — `COND` (Func+TRIG) reads Func
  and wraps the MZ in a Func border, `DIVIDER` (Track+TRIG) reads Track,
  `PHRASE LEN` (Phrase+TRIG) reads Phrase, `MASTER FX`/`GLOBAL` (Song+FX)
  read Song. `SWING`, `TIME`, and `KEY` target a scope chosen at
  *runtime* rather than a fixed origin, so they tint by that active
  **target** scope (`SWING` → Song/Scene/Track; `TIME`/`KEY` →
  Set/Song/Scene, with Set/global reading neutral slate). The remaining
  generator/overlay pages (DENSITY, VEL, EUCLID, MELODY, CHORD, …) and
  the step inspector (`MOVE`, `P-LOCK`, `FILL`) are a **different axis**
  — they keep their own identity (violet / amber) and name themselves in
  the banner. The Func border marks a page whose
  *origin* is Func, mirroring the section-key marker — it is not tied to
  the transient Func hold (that governs the section-key *preview* row,
  which is a distinct surface).

**Reality note.** Only `Machine` and `Track` own per-section parameters
today; `Phrase`/`Scene`/`Song`/`Global` are wired into the stack but
produce no section candidates yet, so holding them yields dim sections
until those layers grow params (master FX keeps its own `Song+FX`
access, §32.3 — it is *not* folded into this six-section overlay).

**`Func` is a parallel stack, not a rung.** Holding `Func` does not
insert a layer into the primary stack — it switches you to a **second
copy of the whole stack**, entered by the `Func` modifier, with the
*identical* layer order and the *identical* resolution rule. The
scope modifier still selects the floor within it: `Func` alone = the
Func stack at the `Machine` floor; `Func+Track` = the Func stack at the
`Track` floor; and so on. Each Func layer owns that scope+section's
*secondary* content — today `Func`-`Machine` owns `COND` (on `TRIG`),
`NOTE` (on `SRC`), and the pickers/reskins where bound; the other Func
layers are mostly empty and fall through / dim, exactly like the sparse
primary stack. Pickers are terminal (an owned action, not a page, so
they do not fall through). The two stacks are **independent**: a section
that no Func layer owns is **dim** (strict) — it does *not* borrow the
primary page. This keeps "`Func` = universal qualifier" intact (Rule 1
below) while making section-button handling read the same in both
stacks.

**Func-stack colour.** A Func-qualified cell keeps its **origin fill**
(machine-secondary reads neutral, `Func+scope` reads that scope's
colour) and is marked by a **Func-coloured border** (`scope.colour.func`)
— not an all-orange fill. So the only visual difference between the
primary and Func stacks is the border; everything else about section-
button handling is identical. Empty Func cells stay dim with no border.

### 6.2 Track-meta content: `COND`/`NOTE` on `Func`, the rest relocated (3.2 / 5.3)

Pre-3.2, the section bar had a separate "track meta" layer reached
by Shift (`COND` / `TRIG` / `TRACK` / `GLOBAL`). The matrix folds that
layer in and distributes it to the scope each meta belongs to — so a
meta is reached through the modifier that *owns* its domain, not a
catch-all Shift/`Func` overlay:

- **`COND`** (probability, m:n, prev-dep) — `Func+TRIG`. A trig-
  condition layer over the trig defaults, so it sits on `Func` as the
  secondary of `TRIG`. Trig defaults themselves (default note,
  velocity, gate length) live on `TRIG` with no modifier.
- **`NOTE`** (explicit note / velocity / gate step entry) — `Func+SRC`.
  The numeric-entry counterpart to the live pitch-record gesture
  (§5.4), so it sits on `Func` as the secondary of `SRC`.
- **`TRACK`** (track length / divider) — `Track+TRIG`. Length and
  divider are per-*track* properties, so they belong under the `Track`
  scope, not `Func`. (`Func+FILTER` no longer carries them.)
- **`GLOBAL`/`TRSP`** (output gain, sync mode, channel mode) — the content
  of the **Global** scope (`Func+Song`, §32.3), sitting on its `FILTER`
  key (`MetaBand::Transport`); **`Func+7`** is a shortcut to the same page
  without holding `Song`. *(Amended at 6.5: originally on `Song+FX`, moved
  to `Func+7` when the master FX bus shipped. At 9.22 the transport globals
  became the content of the Global scope proper — `Func+Song` — with
  `Func+7` retained as the shortcut. Global is **floor-only**: a shallower
  scope pressing `FILTER` reaches the filter, not `TRSP`.)*

The result: the bare-`Func` section secondaries are `COND` (`Func+TRIG`)
and `NOTE` (`Func+SRC`); the transport globals are Global-scope content
(`Func+Song` / `Func+7`). The remaining `Func` section cells (`AMP`/`MOD`)
dim until a machine declares a `ParamSpec.variant = Secondary` page for
them (§6.1 rule 1) — the slot is reserved, not occupied. (`Func+FX` is the
effect picker, §32.2.)

**Func colour model (9.22).** `Func` never recolours the row a flat
orange. Every non-dim section key keeps the **colour of its content's
origin** (`COND`/`NOTE` machine-neutral, `TRSP` the Global azure,
`Func+Song` fall-up keys their scope hue); the **func-colour border** is
layered on top purely as the "modifier held" signal (never latches). One
helper — `sectionResolveMode(ui)` — owns the promotion rule (`Func+Song`
→ Global; bare `Func` → the meta hierarchy) so the painter, the section
dispatch, and the MZ header cannot disagree.

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
4. **Universal secondary rule (8.24).** Every key's `hint` field
   carries the label of what adding `Func` would do from the same
   context. If there is no Func variant (or the Func key is not
   meaningful on that chord), the hint is empty and the key dims
   under `Func`. This rule is enforced by `testHintRule()` in
   `tests/KeyBindingTest.cpp`: for every row in `kKeyBindings`, the
   test resolves the row's mods + `kModFunc` and asserts `row.hint ==
   funcVariantRow.primary`. Exemptions are documented inline in the
   test. Label constraints: primary ≤ 8 code points (≤ 6 preferred
   for 15 pt primaries), hint ≤ 8 code points — enforced by
   `testLabelLengths()` in the same file.

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
always wears its own colour — no generic violet borrowed when unlit. This colour
grammar is the backbone of PRINCIPLES §19 (*visual grammar is token-first and
dual-target*): the same hue + brightness drives the on-screen cell and the
hardware RGB-LED alike, so the surface stays "read by colour" without a screen.

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

- **Left dashboard.** Persistent performance state: effective **BPM** and
  **time signature** (both scope-coloured — Song-gold when a Song override
  is active, Scene-green when a Scene override applies, neutral for the Set
  default), Song / Scene identity, transport position, checkpoint depth
  (`CK:N`).
- **Right held-context preview.** Derived from currently-held
  modifiers — e.g. `TRACK 3 + …`, `FUNC + PART → machine picker`.
  Acts as a live cheat sheet without being authoritative: the
  *behaviour* is set by the cluster keys, the preview just *shows*
  what those held keys mean.

Both zones read the same view-model so what the bar says and what
the next verb does cannot drift.

The standalone-only `StandaloneTempoBar` mouse-drag widget is **retired**.
Tempo is set entirely through the grammar (§4.8) and reflected in the left
dashboard readout. The freed vertical space is reclaimed by the header.

### 6.9 Naming-clarity policy — param labels and value labels

Labels in the Manipulation Zone must be readable under performance
conditions. Two common failure modes: (a) param labels so abbreviated they
require memorisation ("F Atk" — filter attack? flanger attack?); (b) value
labels abbreviated to the point of opacity ("STR" — straight? stretch?
string?). This section sets the policy.

#### Param labels (the name strip above the rotary)

- Labels are **terse but unambiguous**: short enough to fit the ~5-character
  cell without reflow, clear enough to identify the parameter without
  cross-referencing the section page.
- When a label would be ambiguous in isolation, **prefix with the domain**:
  `F.Atk` (filter attack) over `Atk` (could be amp or filter); `Osc2 Dtun`
  over `Dtune`.
- Meta band labels (density, swing) must identify their target fully enough
  to stand alone. With the §26.4.1 header providing the band name, per-track
  slot labels can be short: `Trk 1` through `Trk 8` (not `T1`, which reads
  as a trig condition at a glance).

#### Value labels (`valueLabels` / `valueText`)

Stepped params with a `valueLabels` array should **spell out the value where
space allows**, rather than abbreviating to a cryptic short form:

| Avoid | Prefer | Reason |
|---|---|---|
| `STR` | `STRAIGHT` | "str" has multiple expansions |
| `EXMT` | `EXEMPT` | abbreviation of an uncommon word |
| `SCRB` | `SCRUB` | the full word fits |
| `RROL` | `RE-ROLL` | unpronounceable abbreviation |
| `S+R` | `SUS+REL` | inconsistent with the adjacent `SUS` label |
| `UNIF` | `UNIFORM` | fits if the cell is wide enough; otherwise `UNIFM` |

Exceptions — short forms that are genuinely idiomatic and self-evident:

- Waveform names: `SIN`, `TRI`, `SAW`, `SQR`, `S&H`, `RND` — these are
  industry-standard abbreviations musicians already carry.
- Filter type names: `LP24`, `LP12`, `HP`, `BP` — standard.
- On/Off: `OFF`, `ON` — universal.
- Note-name / octave fields where the cell is too narrow for full text.

The test: **a new user encountering the label for the first time should be
able to infer its meaning without a manual**. If they can't, spell it out.

### 6.10 Contextual parameter-name aliasing

Some parameters take on a different *meaning* — and warrant a different label —
depending on the value of another parameter on the same machine. The two acute
cases:

- **DrumSynth TYPE.** Eight synthesis slots (Tone, Body, Snap, Punch, Sweep,
  SwpDec, NoiseDec, Tune) are *repurposed* by the TYPE param: a KICK maps them
  to drive/body/click/punch/sweep/decay/noise decay/tune; a SNARE maps them to
  band-pass frequency, resonance, etc. The raw slot name ("Tone") is actively
  misleading when TYPE = SNARE.
- **Sample/Slice loop-mode-dependent slots.** LpStart and LpLen mean
  different things in auto vs free vs active loop mode; a contextual label
  annotating the mode (`LpStart (auto)` vs `LpStart`) aids recall.

**Mechanism.** `ParamSpec` gains an optional per-slot hook:
```cpp
juce::String (*contextLabel)(const ParamFrame&) = nullptr;
```
`nullptr` means "use the static `label` field." When set, `ManipulationZone`
calls it with the track's current base `ParamFrame` to derive the displayed
label. The function pointer keeps the field POD-compatible for the future
Machine ABI (§36); when that ABI lands the hook maps behind a capability flag
(the ABI is add-only — CLAUDE.md gotcha). Context labels are **first-party
C++ only** for now; third-party modules declare their static label and opt into
dynamic labels via the capability bit once the ABI supports it.

The contextual label renders where the static label renders today (the name
strip above the MZ rotary, §26). Value labels (`valueLabels`) are not aliased
by this mechanism — they are chosen per `ParamSpec` at machine-declaration
time. The `valueLabels` array covers the mode-dependent *value text*; `contextLabel`
covers the mode-dependent *name*.

### 6.11 Gesture-affordance visual language and context inspector (9.11/9.12) ✓

#### 6.11.1 In-cell gesture affordances

Each key cell uses a **fixed uniform band layout** (vertical, top→bottom). The
five grammar pieces (letter hint, double-tap, the non-primary gesture, PRIMARY,
func variant) pack into **four bands** so the layout fits the shortest cell while
the PRIMARY stays at one vertical position on every key:

```
[ hint  ........  double-tap ]   ← letter (left) + dbl chip in the dead space
[ PRIMARY                    ]   ← large, bright, locked band (access glyph beside)
[ secondary rail             ]   ← the non-primary of tap/hold; blank for most keys
[ func-variant               ]   ← amber chip + text; blank when absent
```

**Why four, not five.** A key's primary is either its tap or its hold, so only
*one* of {tap, hold} can be a non-primary secondary — they collapse into a single
secondary rail beneath the PRIMARY. The double-tap chip moves onto the letter-hint
row (the otherwise-dead space beside the QWERTY hint), reclaiming a whole band.
This is what fixed the 9.11 overflow, where a reserved five-row stack pushed the
func rail (e.g. Song's `GLOBAL`) off the bottom of the shorter cells (~48 px).

**Fixed-uniform layout.** Every key reserves the same bands so the PRIMARY locks
to one position; blank space is held when a band has no content (cells never jump).

**Strongest-wins primary (promotion).** The PRIMARY shows the `promotedGesture()`
winner: (1) explicit `promoted = true` row; (2) any Hold row; (3) else Tap. The
primary's own rail is suppressed (never shown twice), as is any secondary whose
action *duplicates* the primary — so a scope modifier (one `HoldXScope` action for
both press and hold) has **no phantom tap rail**: it reads as a bare name (`TRACK`,
`MORPH`, `MUTE`) + hold ring + `LATCH` double-tap.

**Painted vector glyphs.** Each populated slot renders a small glyph inline with
its label text: tap = one dot, double-tap = two dots, hold = hollow ring, func =
filled amber chip (`theme::kFuncAccent`). The **access glyph** beside the PRIMARY
(hold ring vs tap dot) is the **tap / no-tap signal** — modifiers ring, tap keys
dot.

**Single grammar SSOT (9.12).** Display derives entirely from
`resolveBinding(button, idx, heldMods, layer, Gesture)` — the same function that
dispatches behaviour. `buildSurfaceModel` fills `tapLabel`, `holdLabel`,
`doubleTapLabel`, `primaryGesture`, and `funcHint` from grammar queries on every
cell (QWERTY + grid, at rest and with modifiers held).
`KeyAffordances.{h,cpp}` has been deleted.

Adding a gesture to a key: add a row to `kKeyBindingsData` with the appropriate
`Gesture` field; `SurfaceModel` derives all slot labels automatically via
`deriveSlots`. No paint-path change required.

#### 6.11.2 Top-chrome consolidation (9.11)

The two separate chrome rows (tempoReadout 28px + fileBar 24px) were merged into
**one 28px row** — tempo/time-sig on the left, fileBar controls on the right
(standalone only). This freed 26px for the inspector strip placed directly below.

#### 6.11.3 Context inspector — 4-region strip

A slim always-on full-width strip placed between the info row and the MZ:

```
[ KEY: 3 — tap TEMPO / hold GEN HUB ][ HELD: SCENE — verbs→scene, dbl=LATCH ]
[ OVERLAY: GENERATOR HUB — pick cell ][ EDIT: step 5  vel 110               ]
```

**Four fixed regions**, each always populated (idle fallbacks are never blank):

| Region | Active content | Idle fallback |
|---|---|---|
| KEY | last-touched key: name + gesture list from grammar (`resolveBinding`) | `--` |
| HELD | held modifier scope name + grammar note (LATCH, verbs, etc.) | track N + machineId |
| OVERLAY | active picker/overlay purpose + cancel gesture | scene N |
| EDIT | held-step overrides (vel, gate, P-lock slot value) | `--` |

**Pure builder.** `buildInspectorModel(UiState, EditContext, proc, focusedButton,
focusedIndex) → InspectorModel` is a side-effect-free function, unit-testable,
reusable for controller displays (dual-target per PRINCIPLES §19).

**KEY uses grammar SSOT.** The KEY region description is derived from the same
`resolveBinding(..., Gesture)` calls the in-cell affordance renderer uses — gesture
descriptions are never duplicated.

**Not a crutch.** States whose only expression is the inspector text are hardware
bugs (see PRINCIPLES §19). The inspector enriches; `CellState` colours and glyphs
must still carry the performable subset on their own.

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

**Current version: v16** (added in Phase 9.1). v16 adds `Project::soundPool`
(SoundPool/SE nodes); missing node on load = empty pool (trivial v15→v16
upgrade). The serializer also exposes `buildStateTree` / `applyStateTree`
tree-level helpers so both the DAW binary path and standalone `.lockstep` file
path share one serializer.

**Standalone project files** (Phase 9.1): `.lockstep` files are plain UTF-8 XML
written by `PluginState::writeToFile` and read by `readFromFile`. The full upgrade
chain applies on load; `readFromFile` returns `false` (fail-safe) before touching
processor state on parse failure. `writeToFile` is a pure serializer operation;
callers must flush edits (`writeBackWorkingToActive`) before calling.
`LockstepProcessor` exposes `newProject`, `saveProjectFile`, `loadProjectFile`,
`stateHash`, `savedStateHash`, and `currentProjectFile` for standalone chrome use.
Loading a project file is a `[SUSPEND]`-class mutation wrapped by `withQuiescedEngine`.

**Sound-pool delete** is also a `[SUSPEND]`-class mutation: `removeSoundEntry` calls
`withQuiescedEngine { remapSoundIdsAfterRemoval(...); project_.soundPool.remove(i); }`,
traversing all songs × tracks × phrases × steps plus the working sequence to remap
both `trigOverride.soundId` and `fillTrigOverride.soundId`.

## 8. Voice Lifecycle and Choke

Each machine owns its own voice lifecycle. The sequencer's
responsibility is bounded: it asks the machine for its live
`currentVoices()`, runs the `NoteSelection` picker to choose which
notes from a chord step survive, then emits those note-ons (and
scheduled note-offs) as plain MIDI.

- **Mono (`V1`).** The machine fades its active voice over a 1–2 ms
  choke before starting a new voice for the next note-on. `SampleMachine`,
  `FMMachine` (in Mono mode), and `AnalogMachine` (in Mono mode) all do this
  with a per-voice `VoiceChoke` helper.
- **Poly (`V2..V4`).** The machine manages its own voice pool and steals
  the oldest voice when note-ons exceed the live voice count (with a
  brief fade on the stolen voice). `FMMachine` (Poly) and `AnalogMachine`
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

## 10. The Eventual Dedicated Hardware

The dedicated controller is a **later** distillation, not a near-term
deliverable: it is designed only after playtesting on the keyboard +
generic-controller surface (§35, `PRINCIPLES.md` §4) has proven the
ergonomics worth casting in hardware. It adds **no new features** — it is
a consolidated, gig-ready package of the exact same key→action map, a
fewer-key QWERTY mirroring the software surface 1:1. The sketch below is
forward design, not committed scope (and not a roadmap phase — current
status lives in `ROADMAP.md`):

- A 256×64 SPI OLED flanked by 4 endless push-encoders for the
  Manipulation Zone.
- A 32-key transparent mechanical matrix (8×4): top row pages, second
  row navigation/transport, bottom 2×8 the trig grid.
- An RP2040-class MCU enumerates as a USB HID keyboard *and* a USB
  MIDI device simultaneously: scancodes drive the deterministic
  QWERTY mapping; MIDI carries CC and SysEx.
- Bidirectional SysEx for LED state, P-Lock indicators, and OLED
  rendering data, so the hardware is a self-sufficient face for the
  instrument during live performance.

## 11. v0.1 Scope *(historical — shipped)*

> This was the original "first usable" milestone and shipped long ago;
> it is kept for context. Live status and forward work live in
> `ROADMAP.md`, not here.

v0.1 included:

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

The dedicated hardware (§10) and arbitrary-plugin wrapping (§9) were
out of v0.1 scope; the architecture was built to absorb them without
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
| `Scene` | `W` (col 2) | Scene launch + commit/revert (§16): `Scene + step` on a *different* Scene = carry overlay; on the *active* Scene = revert to floor; on an *empty* slot = create a **baked copy** (current effective layout, deviations included). `Func + Scene + step` = **baseline** (existing Scene → floor-only launch; *empty* slot → create a fresh default/empty Scene). `Scene + Clear` = revert active Scene; `Scene + Record` = **commit-and-bake** (confirm-gated): folds global-pattern + mask into the floor and bakes per-track deviations into home-row phrase content (§16, §23.3). *(Undeviated duplication lives on the clipboard, not a create chord — `Mute + Scene + step` is reserved for scene-mute; §23.3.)* | Verb, step key, `Track`. |
| `Song` | `S` (col 2) | Song select: `Song + step` queues a Song change (§16). `Func+Song` = Global/project params (incl. master-bus / FX focus, §32.3). | Verb, step key, section key, `Morph`. |
| `Fill` | `X` (col 2) | "While I'm holding this, fill conditions evaluate true." | Step keys; (no verb needed — it's the state itself). |
| `Trig` (hold a step) | `D–;` / `C–/` | The held step(s); multi-step hold is allowed. | Verb, encoder, or note key. |
| Section key | `5–0` | The held section's slots. | Verb, scope modifier (scope-section matrix, §6.1.2). |

The cluster identities above are the 3.2 layout (frequency-of-use
ordered, with the two performance specialists on row 3). `Cue` is the
audition scope (§31), entered as the compound `Func+3` rather than a
dedicated cluster key (hardware parity). The cue *bus* / pre-listen
feature is still 6.4; its routing functionality currently lives under
`Func+Morph` (cue morph) and `Song`-scope cells — see §31.

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
- **Scope+Func+O = deletion picker.** Holding a scoped Delete chord
  (`Track/Phrase/Scene + Func + O`) enters the **deletion picker** modality:
  the step grid repaints as a slot-selector for that scope; status reads
  "Delete which PHRASE?" (or TRACK / SCENE). The user taps a slot to proceed.
  The picker is **sticky** — releasing the arming chord does not cancel; only
  an explicit non-Func key press does (shows "Cancelled"). `Func` itself never
  cancels (it is needed to navigate CONFIRM/CANCEL). After tapping a slot, a named
  **pending-confirm** replaces the picker: "Delete PHRASE 3?  P=CONFIRM  Func+P=CANCEL".
  The `P` key shows **CONFIRM (green)** when Func is up and **CANCEL (red)** when Func
  is held; the live colour is the confirmation surface — no modal popup
  (PRINCIPLES §5, §16). The pending-confirm is also sticky; any non-Func key
  other than `P` cancels (status "Cancelled"; press swallowed). `Song+Func+O`
  has no picker and remains inert; the key dims honestly under Song+Func.
  Morph+Func+O = morph **erase** (no picker; Morph does not host deletable
  entities). See PRINCIPLES §16.
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

**Standing grammar conventions.** Two recurring patterns apply to *any* feature
that adds an incremental action or a scoped verb, so they are documented here
once rather than re-derived per feature:

- **Hold = "all the way."** When a verb performs an incremental/stepwise action,
  *holding* it means "carry the action to its terminal extent" — floor, not just
  one level; all, not just one. Canonical instance: a brief `Func+Y` (RESTORE)
  = pop one checkpoint entry; hold `Func+Y` then release = jump straight to the
  floor. Any new incremental verb inherits this intensifier automatically;
  controller surfaces and hardware both reach the extremity by holding.
- **Bare verb = universal-scope synonym.** A verb pressed with no scope modifier
  is identical to the verb qualified with the top / universal scope. Canonical
  instance: bare `Y` (Snapshot) ≡ `Song + Y`. This makes the bare form a
  *shorthand*, not an exception — `Song + Y` is always the admissible explicit
  form of the bare press, and the grammar is total (no scope is ever "missing").

- **Hierarchical-band single-knob pattern.** When a meta-band edits a
  *derived* (hierarchical) value — one composed from a parent and a local
  override — the band shows exactly **one** editable knob at the resolved
  value *at the held scope*. Editing that knob stores the back-solved delta
  (ratio, delta, or flag) so the displayed value matches what you set. The
  inherited parent value is shown as a **scope-coloured reference tick** on
  the knob arc (`ReferenceMark`). The header readout is the always-on global
  effective. A second, read-only "effective" knob is never added — that would
  be an unintuitive dead control. Canonical instances: Swing (§19.2), TIME
  page — tempo + time-sig (§4.8).
- **INHERIT floor = revert (TIME page).** Tempo and time-sig each carry their
  own revert mechanism built into the control. Dial tempo to the range floor
  → label shows `INHERIT (<parent bpm>)`, clears `hasTempo`. Dial time-sig to
  index 0 → clears `hasTimeSig`. Each reverts independently; no chord is needed.
  Reverts at Set scope are no-ops (no parent above them).
- **Hold-scope + Clear = revert (Swing only).** For the Swing band, pressing
  **Clear** (`O`) while a scope is held zeroes that scope's delta. If no
  modifier is held, the revert targets the entry scope. This chord is *not*
  active on the TIME page (see above).
- **Momentary-hold + entry-scope fallback.** When the TIME sticky mode is
  entered (`Song+TRIG` or `Scene+TRIG`), the scope remains determined by
  whichever modifier the performer holds — Song, Func+Song (=Set/global),
  Scene. Releasing all modifiers while the band stays open does not silently
  retarget to a wrong scope: edits fall to the **entry scope** — the scope
  that was held at the moment of entry. This is recorded on the `UiState` at
  the moment the sticky mode activates so that subsequent writes are
  unambiguous even with no modifier held.

These conventions compose with §13.7 Latch and with PRINCIPLES §17: the
"hold = all the way" behaviour is an instance of the verb hold-intensification
family, and "bare = universal scope" ensures no bare verb carries a second
meaning that differs from its Song-scoped form.

**Time-based gestures (two axes; see PRINCIPLES §17).**

| Axis | Legal on | Key rule |
|---|---|---|
| **Double-tap / double-press** | Modifiers (→ latch); `Func` (→ escape); steps (→ edit context, scope-local to trig grid); verbs (→ amplified action); nav at boundary (→ reveal/unlock); HW operand encoder push (→ reset to default) | Every double-tap must belong to a named family; step-double-tap is trig-grid-scoped only |
| **Press duration (hold)** | Verbs and operands **only** | Modifiers and `Func` are forbidden — they are already held for chords; no free "long" variant exists |

Band-pinning (sticky mode) for meta-bands is a **`Func + section` chord**, not
a double-tap. Velocity sticky → `Func + AMP` (§13.7 / §39.10); density sticky
→ `Func + MOD` (§39.5). This is consistent with §17: `Func` never latches or
pins via double-tap.

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
top / `O` clear active P-Lock / `P` (CONFIRM) confirm a pending prompt);
navigation; tap-tempo.

**Rung 2 — `Func + key` (2 keys, Func cheapest).** `Func+Y` (RESTORE)
checkpoint pop (tap) / floor (hold+release); `Func+P` cancel a pending
prompt; `Func+U` omni copy; `Func+I` unqualified paste; `Func+O` delete
(+confirm); `Func+step` P-Lock clear mode; `Func+section` secondary
section layer; `Func+Song` global/project params. (Checkpoint **push** is
the bare `Y` — rung 1.) **Panic** (kill voices): `Song + Clear (O)` —
rung 3.

**Rung 3 — `mod + key` (2 keys).** `Mute+step` global mute (hold-tap-
many); `Scene+step` launch (carry overlay); `Func+Scene+step` = floor launch;
`Scene+verb` bake (Rec) / revert (Clear) / re-sync-all (Snap);
`Song+step` queue song; `Song+Clear` Panic; `Phrase+step`
unison/set-global; `Fill+step` mark fill; `section+verb` copy/paste/clear
a section; `Trig+verb` step copy/paste/clear; `Track+Nav` cycle input
mode (PLAY/CHROM/LEVELS); `Morph+^`/`Morph+v` pick endpoint A/B (5.2,
planned).

**Rung 4 — `Func + mod + key` (3 keys).** `Func+Track+step` machine/Kit
picker assign; `Func+Scene+step` baseline launch (floor, discard
overlay); `Func+Scene+Rec`/`Func+Scene+Play` Scene copy / paste;
`Func+Section+step` note-edit entry; `Trig+Func+Clear` clear
all P-Locks on the held step(s); **`Func+Mute+step` solo** (the
secondary layer of mute).

**Rung 5 — `mod + mod + key`, cross-column (3 keys).** `Scene+Mute+step`
per-scene mute (active-mask); `Track+Phrase+step` per-track phrase
deviation; `Trig+section+Clear` domain-scoped step clear (`Trig+SRC+Clear`
= clear notes); `Track+section+verb` track-scoped section copy/paste/clear;
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
heterogeneous machines (Sample + FM synth + MIDI-out): only the
`role`-tagged slots participate. A machine author opts in by tagging.

### 13.2 Copy / Paste / Clear / Delete

A single uniform grammar: **hold scope, press verb**. **Copy is always a
deep clone** — all content is duplicated independently. There is no
shallow (structure-only) copy path; machine-only track copies are
done via the Kit picker (`Func+Track`) instead.

**Clear vs Delete (the orchestra paradigm, PRINCIPLES §21).** A **phrase** is
the sheet music, a **track** is the musician, its **machine** the instrument,
its **FX** the musician's own signal chain. **Clear** *blanks what a thing is
doing while it stays present*; **Delete** *removes the entity and its
belongings*. A bare destructive verb acts at the **finest live context** — so
`Track + Clear` empties the musician's part **on the current phrase only**
(the actor is the scope; the stage is the current phrase). Widening is explicit:
**`Track + Song + Clear`** clears that track across **all** phrases — an
ordinary cross-column compound (rung 5), earned by the wider reach.

**Confirmation scales by blast radius.** Single-step / single-lock / single-note
clears and `Section + Clear` (one track, current phrase) commit **immediately**
but auto-push a checkpoint (so `Func + Y` Restore is the undo). `Track` /
`Phrase` / `Scene` clears, anything widened with `+ Song`, and **every Delete**
arm a **sticky confirm prompt** that names the target and its reach
(§16) — "CLEAR track 3 · this phrase" / "CLEAR track 3 · ALL phrases" /
"DELETE track 3 + machine + FX" — before committing. **Delete** (`scope +
Func + Clear`) routes through the deletion picker (§16); it removes the slot
*and its belongings* (a track deletes with its machine + FX).

| Gesture | Effect |
|---|---|
| `Trig` (hold 1+ steps) + Record | Copy those steps (trigs + condition + P-Locks). |
| `Trig` + Play | Paste clipboard onto the held steps. |
| `Trig` + Clear | Clear those steps' overrides (trig + P-Locks). |
| `Trig` + `Func + Clear` | Clear **all** P-Locks on the held step(s), leaving the trig itself intact. The `Func` qualifier narrows `Clear`'s scope from "clear the step" to "clear locks only". |
| `Trig` + `(MZ slot)` + Clear | Clear **only that slot's** P-Lock on the held step. Targeted by the held slot (the same slot the MZ would write). |
| `Trig` + `(section key)` + Clear | **Domain-scoped clear** on the held step(s): wipe every override owned by the held section, leaving other sections, the trig, and the condition intact. **SRC** owns the note payload, so `Trig + SRC + Clear` clears the **note / velocity / gate** overrides (and keeps P-Locks). Same "hold the thing, press Clear" idiom as the MZ-slot row, one level coarser. (This is the home of "clear notes" — it replaced the old `Func + P` overload, which is now purely Cancel everywhere.) |
| long-press one held step (held-step inspector) | Long-pressing a single held step reveals its **inspector** (§13.8): its P-Locks show as bright (locked) / dim (empty) cells — tap a locked cell to clear that slot's P-Lock; the MZ shows the section's params with lock badges (twist still writes a lock). This is the discoverable home of single-lock surgery; it absorbs the former `Func + step` P-Lock-clear mode. (A bare hold — one or many steps — is the multi-step edit context, §19.1; it does not open the inspector.) |
| `Section` key + Record | Copy all of that section's params (base + P-Locks across all steps). |
| `Section` key + Play | Paste section onto current track. |
| `Section` key + Clear | Reset section to default (clears that section's P-Locks across the track). **SRC** additionally clears the note / velocity / gate overrides track-wide — the whole-track mirror of `Trig + SRC + Clear`. *Immediate tier* (auto-snapshots). |
| `Track` (specific track) + Record | Copy the whole track within the Song. |
| `Track` + Play | Paste track. |
| `Track` + Clear | Clear the track's part **on the current phrase only** (steps, conditions, overrides). Machine / FX / length stay. *Confirm tier* — names "track N · this phrase". |
| `Track` + `Song` + Clear | Clear the track across **all** phrases in the song. *Confirm tier* — names "track N · ALL phrases". |
| `Phrase` + Record | Copy the focused track's phrase. |
| `Phrase` + Play | Paste phrase. |
| `Phrase` + Clear | Clear phrase (all tracks' steps in it) back to empty. *Confirm tier.* |
| `Func` + `Scene` + Record | Copy the active Scene to the typed clipboard. The grab is the full **effective** layout (floor + live deviations as content), losslessly. **No move** (cf. the create-and-move `Scene + step`). Baked-vs-floor is chosen at *paste* (§23.3). Type tag = `Scene`; badge = `CPY:SCN`. |
| `Func` + `Scene` + Play | Paste the Scene clipboard onto the active Scene — lays down the **baked** layout. Conflict-gated (§23.3). |
| `Mute` + `Func` + `Scene` + Play | Paste **floor only** (deviations stripped). `Mute` = the "floor-only / strip the overlay" qualifier; distinct from verbless scene-mute by the `Play` verb (§23.3). |
| `Func` + `Scene` + Clear | Clear the Scene to empty / default. |
| `Func + U` (no scope) | **Omni copy** — captures the full live stack: scene + active track + pattern. Type tag = `All`; badge = `CPY:ALL`. |
| `Func + I` (no scope) | **Unqualified paste** — stamps the single captured layer by reading the clipboard's type tag. Rejects with "Paste: pick a scope" if type is `All` (omni grab has no single default). `None` = "Nothing copied". |

> **Why `Scene` copy is `Func`-qualified.** Every other scope's bare
> copy/paste/clear triad is `Record`/`Play`/`Clear`. `Scene` is the one
> exception: its *bare* triad is reserved for live launch-unit verbs —
> `Scene + Record` = **author** (commit-and-bake live state into the Scene —
> folds global/mask into the floor and bakes deviations into home-row content,
> confirm-gated; §16),
> `Scene + Play` = **launch now**, `Scene + Clear` = **revert to stored**
> (§16). `Func` therefore *lifts* the clipboard triad up one level
> (`Func + Scene + Record/Play/Clear` = copy/paste/clear of a Scene as
> data), exactly as `Func` narrows `Trig + Clear` (clear step) to
> `Trig + Func + Clear` (clear locks only). The clipboard is typed: a
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
  drums" performance gestures. Entered by `Mute + step` (MuteScope +
  step key), quantized to `launchQuant` (§4.8); double-tap the step to
  toggle instantly.
- **Scene mute (per-track).** The inverse of the Scene's `activeMask`
  (§4.7), serialised as a bitfield. Saved with the Scene and recalled on
  Scene launch — useful for arrangement-style scene variants without
  duplicating notes. Entered by `Scene + Mute + step`.

Mute mask resolution: a track is muted at runtime iff
`global.muted[i] || !scene.activeMask[i]`. Muting is non-destructive (no
note-offs are emitted); a muted track simply has its emitted trigs
suppressed at the sequencer→machine MIDI boundary, after condition
evaluation but before machine dispatch. External MIDI input bypasses
the mute (the user can still play a muted track manually).

**Quantized mute is the per-track "clip" transport.** Mute and unmute now arm
to the shared `launchQuant` grid (§4.8), which turns the per-track mute into a
Session-View clip stop/start: mute on the grid = "stop this track on the bar,"
unmute on the grid = "bring it back on the bar." A pending mute/unmute shows
the same landing chrome as a pending Scene. Because a muted track's **cursor
still advances** (only emission is suppressed; the audio path is skipped after
the ~100 ms declick, so there is no ongoing DSP cost), the clock *is* the
track's phase and un-muting rejoins the band already in phase — no held-phase
bookkeeping. This is deliberately *not* a separate `Stopped` state
(PRINCIPLES §25): "stopped" = muted, and nothing new stores a paused phase.

**Phase-reset (relaunch / retrigger).** Bare unmute resumes **in phase**. To
restart a track from the top of its pattern instead, use the phase-reset rider:

- `Mute + Play + step` — **relaunch**: unmute *and* reset phase if the track is
  muted; a pure **retrigger** (phase-reset only) if it is already playing. Reads
  as "Play/relaunch this track." Quantized to `launchQuant` (double-tap =
  instant relaunch). At the boundary the track's cursor is snapped to its
  step-0 grid position and its one-shots are re-armed, so "from the top" means
  from the top — useful to re-anchor a coprime-length track (e.g. length 7) back
  onto the bar without waiting for its natural cycle. `Play` (a verb) is the
  qualifier, so this never collides with the existing mute chords
  (`Scene + Mute` = scene mute, `Func + Mute` = solo).
- The phase-reset is a **decoupled primitive**: it can ride an unmute
  (relaunch) or stand alone on an unmuted track (retrigger). It is RAM-only
  live state, not serialised.

**Atomic multi-mute** *(planned; gesture TBD — it cannot reuse `Func + Mute`,
which is solo, below).* Flag several tracks under a held qualifier and commit
them together on release; because the batch is deferred it **arms and fires
together** at the next `launchQuant` boundary — a whole group drops in or out on
the same beat. Today the immediate, one-track-at-a-time equivalent is plain
`Mute + step` hold-and-tap-many.

**Solo** (`Func + Mute + step`) is the secondary layer — "mute everything
else." It is **routing-aware** (§27): soloing a bus keeps its feeders audible;
soloing a feeder keeps its downstream bus chain audible. It composes with the
quantize grid like any mute.

### 13.5 Alternate Trig Modes

The trig grid (the 2×8 step row) is the densest physical surface on
the controller; reusing it for non-step roles is a major workflow
multiplier. Modes are entered by a dedicated mode chord and exit on
release of that chord — the trig grid becomes a *modal* surface, not a
permanently-reassigned one.

> **Status (5.7 / 5.9 shipped).** The chords are now bound: keyboard
> mode shipped as the per-track **CHROMATIC** input mode (3.9,
> `Track+Nav`); **Retrig/Slice** is `Fill+TRIG` (the shipped rate set is
> eight musical rates `/4 … /32T`, not the 1/16–1/96 list drafted
> below; ISliceable tracks show the slice picker instead); **Sound
> Pool** is `Fill+SRC`; the **Euclidean generator** is `Phrase+Fill`.
> README §5.18 documents the shipped behaviour; this section keeps the
> design rationale.

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

While armed, the generated rhythm is **audible and visible** in real time —
the step grid shows live trigs (not the phrase-select banner) and the
Euclidean MetaBand persists whether the chord keys are held or latched.
The status bar reads **"EUCLID  pulses / offset / accent  |  COMMIT / CANCEL"**;
Y and P relabel accordingly.

**COMMIT (Y):** the original phrase is saved as an undo checkpoint
(always, even onto an empty phrase — §13.6), then the live Euclidean
pattern is baked in place. **CANCEL (P or Func-escape):** the original
phrase is restored. The printed output is **ordinary trig data**,
indistinguishable from hand entry and fully hand-editable afterward.

*Implementation note:* `euclidHeld` (not `phraseScopeHeld`) is the
authority for the Euclidean MetaBand and the `resolveActiveLayer()`
bypass. The mode survives releasing the arm chord because `euclidHeld`
remains true until commit/cancel/Func-escape clear it.

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
The bare **`Y` (SNAP)** verb snapshots **whatever scope is currently
held**, onto *that scope's own* LIFO stack; **`Func+Y` (RESTORE)** walks
that same scope's stack back down. With **no scope held, the scope is the
Song** — the default working unit. (The `Y` key owns both halves —
snapshot and restore; `P` is the confirm/cancel key and plays no part in
checkpoints.)

**Intent (9.x ruling): both safety-net _and_ scratchpad.** The dual purpose is
deliberate, not a tension to collapse. Snapshot is a live-undo **safety net**
(capture a known-good state; restore if a live edit goes wrong) *and* a
performance **scratchpad** (stash the current state, try a variation live, walk
back to the floor). The same scope-respecting LIFO serves both; restore semantics
for the "both" model are specced in ROADMAP 9.4. Neither reading is privileged.

| Held scope | `Y` (SNAP) snapshots | `Func+Y` (RESTORE) restores |
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
are one behaviour reached several ways — `Scene + active-step`, `Scene + Clear`,
or `Func+Y` (RESTORE) walked to the Scene's floor.

**The floor is the saved state.** Each scope's stack is seeded, on Song
load / Song switch, with a single **floor** entry = that scope's
on-disk saved state. `Y` (SNAP) pushes working snapshots above the
floor; the floor itself can never be popped away. This folds the
Octatrack "Part-reload" into the stack:

> **"Reload saved" = walk a scope's stack down to its floor.** `Func+Y`
> (RESTORE) resolves **on key release**, and the hold duration picks the action
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
`Func+Y` (RESTORE) if it didn't land — at the grain you were working at. The
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
  **`Func` also never pins a meta-band via double-tap.** Meta-band sticky modes
  (velocity, density) are entered by a `Func + section` chord: `Func + AMP` =
  velocity sticky (§39.10); `Func + MOD` = density sticky (§39.5). This is
  consistent with PRINCIPLES §17 — `Func`'s double-tap is escape only.
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
two double-tap families cleanly separated. Both families are fenced by
PRINCIPLES §17 — adding a new meaning to either requires a revision to that
principle, not a silent exception here.

| Gesture | Effect |
|---|---|
| Double-tap a modifier | Latch (virtual-hold) that scope |
| Double-tap the same modifier | Release that latch |
| Double-tap a step | Virtual-hold it into the edit context (add operand) |
| Single-tap a step (latched edit active) | Toggle its trig (unchanged) |
| Double-tap a latched step | Remove that operand |
| Double-tap `Func` | Universal escape — clear all latches |
| Double-press a verb | Amplified action (e.g. Play = stop + reset) |
| `Mute + step` | Quantized per-track mute/unmute (double-tap step = instant); unmute resumes **in phase** |
| `Mute + Play + step` | Quantized **relaunch** (unmute + phase-reset) / **retrigger** (phase-reset only) — restart the track from step 0 |
| Double-tap any launch gesture | Fire now, **phase-preserving** (the §4.8 instant override) |

### 13.8 Held-step inspector

Holding a step enters that step's **edit context** (PRINCIPLES §5, §21) — and a
**long-press of a single held step** promotes it to the step's **inspector**, the
visible face of that context. It is the single discoverable home for per-step
surgery that used to be buried behind deep modal chords (the old `Func + Src +
step` note edit and `Func + step` P-Lock-clear mode are **absorbed** into it).

A bare hold does **not** open the inspector: holding several steps at once is a
first-class flow (multi-step edit context, §19.1), so the inspector — which is
inherently single-step — is reserved for the deliberate long-press, and only when
exactly one step is held. `Func + Src + step` note editing and encoder writes
still work directly from a bare (multi-)hold without the inspector. The reveal
lasts as long as the hold.

While the inspector is open (single held step):

- **MZ = the step's params, with lock state.** The Manipulation Zone shows the
  current section's params as usual, but each slot carries a **lock badge** for
  this step. Twisting a slot still *writes* a P-Lock (the cheap-creation path is
  preserved); a held-step inspector adds the *read* and the *delete* that were
  missing.
- **Step grid = lock overview / clear.** The step grid re-skins to show the
  step's P-locked slots (bright = locked, dim = empty); **tap a locked cell to
  clear that one lock** — single-lock surgery in 2–3 gestures, replacing the
  old 4-gesture `Func + step` mode.
- **Tap SRC → chromatic note editor** for the held step (the former
  `Func + Src + step` surface), edits landing on that step.
- **Move / micro-time → the Step-Position panel.** Pressing `←/→` (move) or
  `Func + ←/→` (micro-time) flips the MZ to a **Step-Position panel** and acts
  on the step; see §19.1.

Everything an inspector edits is per-step override data resolved by
Override-ELSE-Base (§7); nothing here is a new resolution layer.

## 14. Signal Path and Post-Machine FILTER / CHANNEL / ENVELOPE

The sequencer-side signal path for an internal-audio track is:

```
[input fill] → machine.process() → [FILTER] → [ENVELOPE] → [CHANNEL] → [FX1] → [FX2] → track sum
track sum → [master FX 1] → [master FX 2] → master gain → out (+ cue split)
```

The optional `[input fill]` stage (§27) is present only for machines
that declare an `input_source`; for an ordinary synth or sampler the
machine synthesises into an empty buffer and the stage is a no-op.
`[FX1]`/`[FX2]` are the per-track insert effects and `[master FX 1/2]`
the global effects — both described in §32.

`[FILTER]`, `[ENVELOPE]`, and `[CHANNEL]` are sequencer-side DSP blocks
owned by the track (not the machine):

- **FILTER.** A multi-mode state-variable filter (LP / HP / BP / Notch
  / **OFF**). Present on every audio track. When mode is **OFF** the
  filter is a bit-exact passthrough (early-exit before the SVF
  computation) — no processing overhead, no DC offset. Drive,
  Resonance, Cutoff, and envelope-to-cutoff are only active while mode
  ≠ OFF. New tracks default to OFF.
- **ENVELOPE.** The track's AHDSR amplitude envelope, driven by the
  sequencer-emitted note-on/off pair. Present only for machines where
  `IMachine::hasInternalAmp()` returns `false` (the default for all
  machines that do not embed their own amplitude envelope — Sample,
  Slice; absent for Analog, FM, DrumSynth which shape their own). When
  absent the envelope stage is skipped; the CHANNEL block still applies.
  - **Gate source** (`{Envelope | Held-open}`). Default `Envelope`:
    the amplitude stage follows the note-on/off envelope, so the track
    needs trigs to sound. `Held-open` keeps the amplitude stage open
    continuously — the basis of a continuous **Route** and of **drones**
    on synth tracks. Composes with trigless/lock-only trigs (§30).
- **CHANNEL.** Level, Pan, Send A, and Send B — the track's output mix.
  Present on **every** audio track regardless of machine type (including
  machines with internal amp). This is the mixer-channel view: level
  sets post-machine/post-envelope gain (0..2, default 1), pan is
  constant-power stereo position, and the two send levels are
  post-fader taps into the master send buses (§32.3). Because CHANNEL
  is always present, P-Locks and Morph snapshots on `lockstep.amp.level`
  / `lockstep.amp.pan` / `lockstep.amp.sendA` / `lockstep.amp.sendB`
  work uniformly across all machine types.

**Slot layout (audio tracks, per-track slot space):**

```
[0, mnp)            machine params
[mnp, +6)           FILTER     — always present (ids: lockstep.fltr.*)
[mnp+6, +4)         CHANNEL    — always present (level, pan, sendA, sendB)
[mnp+10, +6)        ENVELOPE   — iff !hasInternalAmp() (gate, att, hld, dec, sus, rel)
[then]              insert FX params
```

Channel slots use the existing `lockstep.amp.level/pan/sendA/sendB` ids on
disk for compatibility with v17 saves. Envelope slots use
`lockstep.amp.gateSrc/attack/hold/decay/sustain/release`.

This is the Elektron model: a machine focuses on producing raw audio;
the surrounding FILTER + CHANNEL (+ optional ENVELOPE) + FX is uniform.
The canonical FILTER (key 7) and AMP (key 8) section buttons drive these
blocks regardless of which machine the track hosts. For machines with
internal amp (Analog, FM, DrumSynth) the AMP section button exposes the
machine's own amp pages first, followed by the CHANNEL page as a virtual
section (parentCanonical = kAmpSecIdx) so level/pan/sends remain
accessible.

`IMachine::hasInternalAmp()` returning `true` suppresses only the
ENVELOPE block; the CHANNEL block is always present. The
`IMachine::hasInternalFilter()` virtual is **deleted** — the track filter
is now universal; machines that previously returned `true` (Analog) simply
present their internal filter slots before the track FILTER page, which
the user can engage or leave at OFF.

For a MIDI-out track (§15), all three blocks — FILTER, ENVELOPE, and
CHANNEL — are bypassed implicitly (no audio to process) and the
FILTER/AMP section buttons are repurposed to expose MIDI CC banks
for that track instead.

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
- **`Scene + step` on the active Scene** (or `Scene + Clear`) — revert:
  discard the active Scene's overlay and return to its saved floor. This is
  the "reset to stock" move.
- **`Func + Scene + step`** — **baseline launch**: switch to any Scene at
  its clean saved floor, discarding all overlay state regardless of which
  Scene you pick.

At the launch boundary the band takes the Scene's effective phrases
(floor, or floor+overlay for a single-tap), the active mask
(`activeMask[t]`) takes effect, and core time + Morph snapshots update. A
pending Scene shows as a badge in the top bar; a second `Scene + step`
before the boundary replaces the queue ("change your mind").

**Song switch.** `Song + step` queues a Song change, quantized by the same
`launchQuant` grid as every other launch (§4.8; the *currently playing* Scene's
core time supplies the bar/beat length). At the boundary, the incoming Song's
first Scene establishes the new meter and phrase assignments, and all tracks'
kits update. **Deviations are cleared on a Song switch** — a song change is a
full performer reset; the musician steps up with fresh assignments.

**One authority, one instant override.** Scene launch, Song switch, and Phrase
deviation (below) all defer to `launchQuant` (PRINCIPLES §25); none has a
private timer. **Double-tapping** any of these gestures fires *now*, and an
instant fire is **phase-preserving** — the change lands where the music already
is, cursors untouched. `Scene + Play` (below) is exactly the instant case of
Scene launch. Restarting a track's pattern from the top is never a byproduct of
firing instantly; it is the separate phase-reset rider (§13.4).

**Phrase swaps and deviation** (the live overlay; full grammar in §4.7):

- `Phrase + step` (or `Track + Phrase + step`) — **deviate** the focused
  musician to the picked phrase, quantized to `launchQuant` (double-tap =
  instant, phase-preserving). *(Formerly immediate; deviation now arms to the
  shared grid so a live phrase swap lands musically, matching Scene launch.)*
- `Scene + Phrase + step` — **deviate the whole band** to the picked phrase.
  Landing on the diagonal row (`sceneIdx`) clears all deviations.

To rejoin one musician, pick the **diagonal** phrase (row N for Scene N) —
the selector borders it (§4.7). To clear all deviations: `Scene + Phrase +
step` on the diagonal, re-launch the active Scene (`Scene + active-step`), or
use `Func + Scene + step` (baseline launch, above).

**Scene commit / revert** (the bare `Scene` triad; the copy/paste/clear
meanings are `Func`-qualified, §13.2):

- `Scene + Record` — **commit-and-bake** (confirm-gated): write live mask
  changes into the Scene's saved floor, *and* **bake** the per-track deviations
  into content — each deviated track's effective phrase is copied into its
  diagonal row (`sceneIdx`) slot, then the deviation clears. This is the
  **only persistence path for a hand-curated heterogeneous arrangement**:
  the floor stores no routing vector, so curated phrases are materialised
  onto the diagonal *as content*. Because it overwrites phrase slots and severs
  phrase sharing (the baked slot is a snapshot, §4.7), it is destructive and
  **prompts a confirmation** (CONFIRM/CANCEL). *Scratch-pad workflow:* park on one Scene,
  audition ideas into high phrase slots via `Phrase + step` deviations, then
  `Scene + Record` → `CONFIRM` bakes the keepers onto the diagonal.
- `Scene + Play` — **launch now**: an instant re-fire of the active
  Scene immediately (apply its phrases + active mask now, not at the next
  `launchQuant` boundary). This is the instant case of the one launch
  authority (§4.8), and like every instant fire it is phase-preserving.
- `Scene + Clear` — **revert**: discard the live overlay, return to the
  Scene's saved floor. Same behaviour as `Scene + active-step` (re-tap the
  active Scene), and the same restore the §13.6 Checkpoint floor reaches via
  `Func+Y` (RESTORE) walked to a Scene scope's floor — one behaviour, several
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

### 16.1 Launch CUJs (the Session-View surface)

The one launch authority (§4.8) plus the mute + phase-reset "clip" model
(§13.4) cover the core Session-View critical user journeys without a per-track
transport state:

- **Drop the drums, bring them back on the 1.** `Mute + step` (drums) to stop
  on the bar; `Mute + step` again to unmute — the track rejoins **in phase**
  with the band, because its cursor never stopped advancing.
- **Relaunch a track from the top on the next bar.** `Mute + Play + step` on a
  coprime-length track (e.g. length 7) snaps it to step 0 at the boundary and
  re-arms its one-shots — re-anchoring it to the bar without waiting a full
  7-step cycle.
- **Retrigger a *playing* track.** The same `Mute + Play + step` on an unmuted
  track is a pure phase-reset — a live "stutter it back to the top."
- **Queue a Scene on a 4-bar phrase.** Set `launchQuant = Bars4`; `Scene +
  step` lands the new Scene on the four-bar boundary.
- **Change your mind instantly mid-fill.** Double-tap any launch gesture
  (Scene / Phrase / mute) to fire *now*, phase-preserving, without disturbing
  the groove.
- **Loop record armed to the band.** The looper's REC/PLAY arm to the *same*
  grid as Scene launches (§3); punch-out lands on the boundary and hands to
  Play/overdub. Double-tap REC to override.
- **A track that switches on its own phrase end.** Set that track's
  `launchQuant = PhraseEnd`; its launches wait for the end of its current
  phrase cycle rather than the shared bar.

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
- `Morph + ^` + `Clear` on a slot → removes from A's map
- `Morph + v` + `Clear` on a slot → removes from B's map
- `Morph + Clear` on a slot → removes from both maps (clears assignment)

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

**Move-step and the Step-Position panel.** While a step is held, two key
gestures act on it, and both flip the MZ to a **Step-Position panel** that
*follows the moved step(s)*:

- **hold step + `←/→`** = **move**. A *single* held step moves by a sticky
  bubble-swap: it swaps content with its neighbour and **keeps focus**, so a
  repeat swaps it onward — sequential swaps that read as a move/insert. *Multiple*
  held steps **block-move together** by one position, clamped at the track
  boundary (the whole block stops rather than colliding or wrapping). In both
  cases the whole `Step` travels (notes, P-Locks, condition, `microOffset`), and
  the held set follows the moved positions so a repeat keeps moving it.
- **hold step + `Func + ←/→`** = nudge the `microOffset` of **every** held step
  (the same field above). (`Func + ←/→` with **no** step held keeps its existing
  whole-track rotate role; `Func + ↑/↓` stays length ×2/÷2.)

The held set is a **multi-step edit context**: hold any number of steps on one
track and encoder turns, note keys, conditions, and clears all fan across them
(continuous parameters *relative*, keeping each step's offset; stepped/enum
*absolute*). A bare hold does **not** open the P-Lock inspector (§13.8) — that
opens on a **long-press of a single held step**; two-plus held steps never open
it. Hold-step + `Func` latches the held set (hands-free), unchanged.

Once flipped, the Step-Position panel's encoders do both jobs continuously —
encoder 0 = position (drives the swaps), encoder 1 = `microOffset` — and
**`QUANT`** (`Scope + P`, §19.3) remains the reset that zeroes the offset. The
panel is the §13.8 inspector's move/timing face; it does not introduce a new
data model — both axes are existing `Step` fields.

### 19.2 Swing (Song / Scene / per-track, additive)

Swing is a **signed** off-beat displacement, expressed as a fraction
of the step length. `0` (the default) is perfectly straight. `+0.5`
pushes every odd-indexed step a full half-step late (classic
maximum-shuffle). `−0.25` pulls odd steps a quarter-step early
(slightly rushed, ahead-of-the-beat feel). The range mirrors
`microOffset`: ±50% of the step length.

Three levels contribute, stored as **musical state** (not APVTS):

| Level | Storage | Semantics |
|---|---|---|
| Song-all | `Song::swing` | The piece's base groove — the conductor's downbeat |
| Song-track | `Song::SongTrack::swing` | Per-musician delta within the song |
| Scene-all | `Scene::swing` | Section-wide delta (e.g. chorus pushes vs. intro straight) |

A fourth level (Scene-per-track) is a **reserved, unbuilt addend**.
Phrase-level swing is deliberately not planned; it would be inaudible
at typical phrase lengths.

They are **composed additively** at resolve time and clamped:

```
effectiveSwing(t) = clamp(Song::swing
                        + Song::SongTrack[t]::swing
                        + Scene::swing,  −0.5, +0.5)
```

Swing is part of the saved musical hierarchy, not APVTS. It is **not**
host-automatable or MIDI-learnable; it is a feel property of the
composition, not the instance.

**Editing model — "edit the effective, store the delta"** (mirrors the
morph crossfader idiom, §17.3). Swing is surfaced **by held scope** —
no separate meta band is needed. While a scope key is held the
manipulation zone band shows that scope's swing level:

| Held scope | Band shows (cumulative) | Write stores |
|---|---|---|
| **Song** (S key) | `Song::swing` | `Song::swing` (absolute — the root) |
| **Scene** (W key) | `Song::swing + Scene::swing` | `Scene::swing` delta |
| **Track** (T key) | `Song::swing + Scene::swing + SongTrack[t]::swing` (= effective) | `SongTrack[t]::swing` delta |
| *(nothing)* | *(normal machine params)* | — |

A single rotary (slot 0) is shown; label is always `"Swing"`. The
value displayed is the **cumulative groove at the held scope** — what
you hear for that layer — rather than a raw stored delta. The rotary
edits that scope's contribution; the write path back-solves the stored
delta from the cumulative value you set.

**Reference ticks.** The rotary shows scope-coloured radial ticks
marking the inherited floor from higher layers:

- **Song scope:** no ticks (it is the root).
- **Scene scope:** one **gold (Song colour)** tick at the song floor
  (`Song::swing`). You set on top of this floor.
- **Track scope:** two ticks — a **faint gold** tick at the song floor
  (drawn first, underneath), then a **green (Scene colour)** tick at
  the scene floor (`Song::swing + Scene::swing`). When `Scene::swing`
  is zero the scene tick sits on the song tick and covers it, so only
  one tick shows. You can read whether the track is pushing above or
  pulling below the scene floor at a glance.

The tick model (`ReferenceMark{present, position, colour, alpha}` in
`SurfaceModel.h`) is **reusable for any layered/delta parameter** where
a value is composed additively across scope levels — morph is the
obvious next consumer.

The swing band is a **transient default**: it disappears the moment any
non-swing-scope interaction occurs (section press, verb, nav, step, other modifier).
Releasing and re-holding the scope key restores it. DIV / PHRASELEN / GLOBAL bands
(opened via Track+TRIG, Phrase+TRIG, Song+FX) are sticky — they persist until
another section is selected or the track changes.

**Combined cap.** At emit time the total sub-step shift on a step is:

```
totalShift = clamp(effectiveSwing(t) × {1 if odd step, 0 if even}
             + step.microOffset, −0.5, +0.5)
```

This preserves §19.1's "every step fires within its own cell"
guarantee and bounds the look-ahead scan to ≤ half a step.

The Song-all level anchors the project groove (the conductor). The
Song-track level lets individual musicians deviate within the song
(snare consistently a little late, hi-hat a little early). The
Scene-all level adjusts the whole section's push/pull relative to the
song base (the bridge rushes slightly, the outro lays back).

### 19.3 The Quantize verb

Quantize zeros microtiming offsets in scope. It composes onto the
existing verb set via `<scope> + No`:

| Gesture | Effect |
|---|---|
| `Trig` (1+ steps held) + `P` (QUANT) | Zero those steps' `microOffset`. |
| `Track` (single track held) + `P` (QUANT) | Zero all `microOffset` values on that track. |
| `Phrase` + `P` (QUANT) | Zero `microOffset` across every step of the active phrase. |

(The `P` key bare = CONFIRM; under a `Trig` / `Track` / `Phrase` scope it
relabels to **QUANT** and zeroes timing offsets. Checkpoint pop is
unrelated — it lives on `Func + Y` (Restore), §13.6 — so the two readings
of `P` do not collide.)

Quantize never touches trigs themselves, P-Locks, or trig
overrides — only the timing offset. To remove a trig entirely, use
`Clear`.

Swing is not affected by Quantize. To reset swing, hold the relevant
scope key (Song / Scene / Track) and zero the swing encoder.

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

Two preview gestures. Audition is **monitoring** — "let me hear this" —
so it lives on the reserved **`Cue`** scope (§31, the monitor/preview
column), *not* on a verb key. (It was originally drafted on `Confirm`,
but `Trig/Track + Confirm` is the live **QUANT** gesture — zero
`microOffset` on the held steps / track, §19.3 — so `Confirm` is taken
under exactly the scopes audition wanted. `Cue` is the semantically
correct home and collision-free; it awaits its keybinding at MU/§31.)

| Gesture | Effect |
|---|---|
| `Cue` + step (one step held) | Fire that step's *resolved* trig once, off the sequencer's schedule. P-Locks, trig overrides, and the OEB-resolved note/velocity/gate all apply, so the user hears exactly what that step will produce when it next fires. |
| `Cue` (held, no step) | Fire the focused track's *base* trig once: `Track::defaultNote`, `Track::defaultVelocity`, `Track::gateLength` at the current `baseParams`. Useful for "what does this track sound like right now?". |

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
  `Trig + SRC + Clear` clears notes + velocity + gateValue;
  `Trig + Func + Clear` clears P-Locks.

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
(SHR:N) on Phrases** — how many Scenes in the Song play that phrase row
(counting diagonal rows + any baked content that landed there), the share
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

**Scene create / copy payloads (shipped).** Lazy slots: Scenes don't exist
until made; empty slots show capacity. Pressing an empty Scene slot creates it.
There are **three direct create gestures**:

1. **Baked copy** — bare `Scene + empty-step`. The current Scene's **effective**
   layout (diagonal row + any live deviations baked down) is materialised as the
   new Scene's diagonal content. Same op as `Scene + Record` but targets a new
   slot — the **"save my experiment"** gesture.
2. **Baseline copy** — `Func + Scene + empty-step`. Copies the current Scene's
   **floor diagonal row only** (deviations ignored). On an *occupied* slot `Func`
   is a floor-launch (§16); on an *empty* slot it stamps the clean saved content.
3. **Blank** — `Mute + Scene + empty-step`. A fresh, empty Scene; no content
   copied. (`Mute + Scene + occupied-step` is **reserved for scene-mute**, below.)

**Undeviated duplication** lives on the **clipboard** (§13.2): copy does not
move the active Scene; only the create gesture seeds-and-relocates. Both copy
and paste carry a **verb** (`Record`/`Play`), cleanly separating them from the
verbless scene-mute chord.

**Baked-vs-floor is decided at *paste* time.** The grab is one gesture and
**lossless** (always the source's full effective layout); the destination
chooses how much to lay down — plain `Func + Scene + Play` pastes baked,
`Mute + Func + Scene + Play` strips to floor. This keeps a single grab
reusable both ways and never traps a live experiment. The cost is a heavy
floor-paste chord (three modifiers + verb + step), accepted because it is a
deliberate, rare librarian action.

**Conflict / no-op skip.** When a create/paste target already holds phrase
content, a "Overwrite phrase row N? free:Sk P=CONFIRM Func+P=CANCEL" confirm is
raised. A free-slot hint (`free:Sk`) suggests an empty Scene. Re-stamping
**identical** content skips the prompt and executes silently. `P` (CONFIRM) confirms;
`Func+P` cancels. Clean rows execute immediately.

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
  grammar gesture under `Trig + (slot) + Clear` / `Trig + Func + Clear`
  / step-driven edit mode (§13.2), keyboard-first.
- The reclaimed space grows the rotary itself, so it's actually
  legible at performing distance.

Double-click on a rotary resets its slot to the `ParamSpec` default,
routed through one helper so the eventual hardware push-encoder-twice
gesture lands on the same code path (consistent with §17.5
single-axis push-encoder discipline).

### 26.4 Mode identity — self-announcing MZ design

Every param mode must **self-identify at a glance**. Two failure modes
the current surface has exhibited: (a) there is no persistent title showing
*which* band is active, so the user must infer from slot labels; (b) every
meta-modal and P-Lock override share the same amber tint, making them
visually identical.

#### 26.4.1 MZ header strip

A **persistent header strip** appears at the top of the ManipulationZone,
always rendered, naming the active band:

| Active band | Header text | Page indicator |
|---|---|---|
| Machine section params (no meta band) | Section name, e.g. `FILTER`, `AMP` | `1/2` when the section has multiple pages |
| P-Lock / step-override edit (held step) | `P-LOCK · T3 S5` (track and step number) | slot page if paginated |
| Density band | `DENSITY` | `1/2` for tracks 1-8 / 9-16 |
| Swing band | `SWING` | none (single page) |
| Euclidean band | `EUCLID` | none |
| Velocity overlay band | `VEL` | `1/2` for track banks |
| Transport / global band | `GLOBAL` | sub-page index |

The header title is the **unambiguous discriminator**: even when colour
families look similar on a dim screen, the title leaves no doubt.

#### 26.4.2 Modal colour families

Replace the single shared amber tint with two named families so
P-Lock is visually distinct from meta-modals at a glance:

- **Step-override / P-Lock** — **amber** (warm, ties to the existing
  P-Lock dot chrome throughout the surface). All held-step edits stay amber.
- **Meta-modals** (Density, Swing, Euclidean, Velocity, Transport/Global)
  — a **cool / violet** tint, visually distinct from amber, indicating
  "system-level overlay, not a per-step edit." The header title remains the
  primary discriminator; the tint is a fast pre-attentive cue.

Machine section params (band = None, normal mode) use the section's
canonical scope colour from §6.6, not amber or violet — machine params
are never highlighted as "modal" because they are the default state.

#### 26.4.3 Section-key fill highlight

When a machine section's params are showing in the MZ, that section key
receives a **fill highlight** (the key's background is painted with the
section's scope colour at reduced alpha), not just the existing page-dot
cue. This closes the feedback loop: the user sees *both* which section is
active (filled key) and how many pages remain (dot count). The fill is
removed when any meta-band takes over the MZ.

This is a chrome update — it ships in the same commit as the MZ header
(PRINCIPLES §10: "every new modifier ships with its chrome update").

## 27. Audio Routing: Output Destinations and Track Buses

The Octatrack's defining trick is that a track can take another
track's (or an external) output as its *input*, turning tracks into
processing, sampling, and resampling chains. Lockstep adopts a
deliberately bounded form of this.

**The model is output-directed, not input-select.** Two orthogonal
controls together describe the routing graph:

1. **`input_source`** — a machine slot declaring an audio tap. Closed
   enum `{None | External | Master | Track N}`:

   | Source | Meaning |
   |---|---|
   | **None** | Default. The machine synthesises into an empty buffer (every synth/sampler). |
   | **External** | The plugin's audio input bus (sidechain / standalone device input). |
   | **Master** | The plugin's master sum (prior block — see below). |
   | **Track N** | A read-only **post-chain tap** of track `N` (the *fork*, below). |

   **Tap-fork.** `Track N` is a separate edge class from the output
   destination (below): it reads a *copy* of track `N`'s finished output,
   so `N` keeps flowing to its own destination — no out-degree > 1, no
   duplication. The tap only constrains processing order (`N` before the
   tapper), so the topological sort runs over the **union** of mix edges
   (CHANNEL "Out") and tap edges, and cycles across either are refused at
   assignment. Because the tap reads the *current* block (the source ran
   earlier in topo order), it is **same-block / zero latency** — strictly
   better than the `Master` tap, which is necessarily one block late. This
   is the foundation for aux sends (a Route tapping a track → parallel FX →
   Master) and resample-a-single-track (a Record tapping one track
   post-FX), available to every input-consuming machine (Route / Record /
   Loop).

2. **Output destination** — a per-track **"Out" slot in the CHANNEL
   block**, closed enum `{Master | Track N | Off}`, default **Master**.
   This is where the track's *finished* signal (post machine →
   FILTER → ENV → CHANNEL level/pan → FX) goes.

   | Destination | Meaning |
   |---|---|
   | **Master** | Default. The track contributes to the master sum, as today. |
   | **Track N** | The track is removed from the master sum and added into track `N`'s input buffer; `N` (a Route/bus) reads the **sum of all tracks routed into it** and processes them as one signal. |
   | **Off** | The track's output goes nowhere (silent at master; useful for a track whose only product is a recorder/send tap). |

Routing track A → track B is expressed on A's "Out" slot, not on B's
input. There is no patch matrix and no neighbour chaining: a track has
exactly one output destination, and a bus track reads the sum of its
inbound tracks plus its own outside-world `input_source` (if any).
This is the smallest model that supports Route, sub-mix buses, Record,
Loop, and realtime resampling without turning the sequencer into a
modular host — and, unlike an input-select model, it can *remove* a
track from the master mix (mute can't: muting zeroes the buffer before
the sum, which would also starve any bus the track feeds).

**Ordering: topological sort per block, cycles refused.** Each block,
the engine orders track processing so that every track feeding a bus is
computed before that bus. This is a topological sort over the "track A
outputs to track B" edges. A routing assignment that would create a
cycle is **refused at assignment time** (at the CHANNEL "Out" write),
with a chrome message — audio feedback loops are a deliberate
non-feature (an opinion of the instrument), and refusing them up front
keeps the block deterministic (`PRINCIPLES.md` §11). Because ordering
is derived from the edges rather than track position, the user never
has to reorder tracks to re-route — the sort handles ordering for them.

**The Master exception.** A track whose machine taps `input_source =
Master` reads the **prior block's** master sum. This is the one place a
one-block tap is allowed, and it exists precisely because
master-feedback can never be cycle-free. (`Out = Master` is *not* an
edge — it is the default contribution to the sum, not a tap.) Realtime
whole-mix resampling tolerates the ~one-block latency without audible
consequence.

**Master tap feedback guard (symmetric with `Out`).** A Master tap can
never legitimately reach the master sum — any path back to master is an
echo of master, so a track that taps Master *and* whose own output
reaches master is a runaway loop. The input/tap rotary therefore omits a
feedback-forming source exactly as the `Out` rotary omits cyclic
destinations: `Master` is offered only when the track's output does **not**
reach master (`outputReachesMaster`), and a `Track` tap only when it does
not close a mix+tap cycle. A stale/loaded selection that slips through is
muted at run time (the second layer, mirroring `Out` dormancy). Master is
a node in this feedback check even though it is not a node in the mix
graph's topological sort.

A §31 **cue send is deliberately *not* a mix edge** and does not count
toward `outputReachesMaster()` — it is a monitor tap, not a route to the
sum. This is what lets cue and this guard compose: a capture track can tap
`Master` with `Out = Off` (silent at master, so the guard permits it) and
still be *heard* via `Cue + track`. See §31 ("monitored master-resampling").

**Only input-aware machines can be a bus.** A routing target must be a
machine that *consumes* audio (declares `input_source` — Route today,
Record/Loop later). Routing to a synth, sampler, or MIDI-out track
is a type error, refused at the edit with a chrome reason
(`NoAudioInput`); self-routing and cycles are likewise refused (`Self`,
`Cycle`). A target's own `input_source` and its inbound bus sum do not
conflict — they **mix**: the bus reads its outside-world tap (if any)
*plus* the sum of tracks routed in. A pure sub-bus therefore uses
`input_source = None` (the Route default) so it hears only its feeders.

**Edges validate at read time, not just at edit time.** Because the
"Out" slot lives in the CHANNEL block (sequencer-owned foundation, like
FILTER/ENV), it survives a machine swap — but the target's
bus-*capability* does not. If a target's machine is later changed to a
non-bus, inbound edges go **dormant**: each falls back to `Master`
(audio-safe — never a black hole) and **revives** automatically if the
target becomes a bus again. The stored "Out" value is never mutated, so
the routing is lossless and reversible across machine swaps; the engine
just announces the dormancy so the user is not surprised.

**Buffer read/write is not a routing edge.** A Record or Loop
that writes a buffer while another track's Flex machine reads that
buffer is *not* a cycle — the buffer (§28) is a decoupled resource,
not a live audio edge. This is what lets loopers work under the
"no feedback loops" rule.

**MIDI-out tracks declare no input source and route nowhere audible** —
they have no audio to consume or contribute — so inter-track routing is
simply a capability audio tracks have, not a sequencer-wide rule that
needs a MIDI-out special case (`PRINCIPLES.md` §6).

**Solo and mute are routing-aware.** Silencing a track must respect the
graph, or buses become unsoloable. Under solo a track is audible iff it
is *connected to a soloed track through the routing graph*: either
**upstream** — its single-out chain reaches a soloed track (so soloing a
bus keeps its feeders running, and the performer hears what flows in) — or
**downstream** — it lies on the chain *from* a soloed track to master (so
soloing a feeder keeps its bus chain alive and the feeder still reaches
the output). An explicit mute still wins over a solo-pulled-in feeder.
The rule is a pure function of the edge array (`soloAudibleMask`), so it
stays deterministic and per-block.

**Routing is legible on the surface.** The "Out" rotary steps only
through *currently valid* destinations — Off, Master, and live buses —
with the target shown live as it turns; invalid targets (synth, MIDI-out,
self, cycle) are never reachable, so the reject banner is a backstop, not
the primary feedback. On the track/VU row a bus and the tracks routed
into it share a colour (the bus full, its feeders dimmed) on an always-on
underline channel, plus a background tint in the plain audible state, so
routing groups read at a glance without competing with the mute/solo
state colours.

## 28. Record Buffers and the Unified Audio-Source Pool

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

The default project provides a fixed set of **16** volatile buffer
slots so recorder trigs and the §22 capture overlay always have
somewhere to write. Two rules keep sixteen slots from costing sixteen
buffers' worth of RAM:

- **Lazy commit.** A slot's capacity is allocated *without zero-fill*,
  so an untouched slot costs address space, not resident pages. The
  maximum capture length is a project setting (default 60 s) rather
  than a compile-time 12 s.
- **Used length.** Every volatile slot carries its recorded length, and
  nothing — playback, promote, waveform display — ever reads past it.
  (Past it, the memory is uninitialised, not silent.)

Slot exhaustion is a **deliberate choice, never a silent reuse**: when
every slot is occupied the capture surfaces the collision and asks which
slot to overwrite. The same lazy-commit / used-length discipline governs
the deck medium (§40.3).

### 28.1 Sample analysis metadata (4.9)

Every **persistent** pool entry is analysed once, at load, on the
message thread. The analysis is a pure function of the decoded PCM plus
priors read from the file, and it produces three musical facts that ride
along with the sample:

- **Tempo** (`dsp/TempoEstimate.h`, shipped earlier) — autocorrelation of
  the RMS onset novelty; `0` = one-shot / non-rhythmic.
- **Key + tuning** (`dsp/KeyEstimate.h`, 4.9) — a full-sample FFT →
  band-limited (60–2000 Hz) chroma → tuning reference (deviation from
  A440, via parabolic peak interpolation) → key scored over 12 roots × 7
  brightnesses with the same circle-of-fifths `noteStrengthRank` the
  melodic generator uses (§4.10). Depth is **root + brightness only**; the
  result is `-1` (unknown) unless a chroma-concentration and score-margin
  confidence gate is cleared. This deliberately reuses the tonal core so
  a detected key is expressed in exactly the vocabulary the rest of the
  system already speaks.

Both run under one length gate (`kMaxAnalysisSeconds`, 30 s): longer
material is `StreamMachine`'s domain (§29.2) — it never enters the pool as
PCM, so there is nothing to analyse. Volatile REC/Loop buffers are never
key-analysed (they carry their own captured bar-length instead).

**Hints and fusion.** Before detection wins, the loader gathers priors
from the **filename** (`120bpm`, `F#maj`) and embedded **ACID WAV** tags
(`acidTempo`/`acidRootSet`/`acidRootNote`/`acidOneShot`), metadata beating
filename. Fusion (`dsp/SampleHints.h`) is **detection-first**: a hint only
overrides the detected tempo to resolve the estimator's octave fold
(detected ≈ 2× or 0.5× the hint), or fills a field detection left unknown;
a one-shot flag suppresses any tempo hint. Detected key always beats a
hint key.

**Caching.** The fused bpm/key/tuning is serialised per pool entry (v26),
keyed by the entry's existing sample hash. On load a hash match adopts the
cache and skips re-analysis; a mismatch (the file changed on disk) or a
legacy entry re-analyses. A missing file keeps its cached values so they
survive an offline session. This is the same hash-keyed pattern the
`StreamMachine` uses for its own on-disk references — analysis is treated
as derived data that is cheap to recompute but wasteful to recompute
needlessly.

**Consumers.** The pool browser hint reads `128 bpm  Amin` (or `one-shot`);
`SliceMachine`'s SYNC source (§29) slices on a beat grid at the detected
tempo; `StretchMachine` already consumes `detectedBpm` for tempo sync.
**Follow-up:** key-synced `StretchMachine` playback — offsetting a musical
sample's pitch to the project key — is the natural next consumer of the
fused key metadata, not built in 4.9.

## 29. The Machine Catalogue

The stock catalogue is an "Elektron's greatest hits" set: one machine
per iconic engine, each authored against the same `IMachine` boundary
(§2) and snapping to the canonical sections (§6.1). The reference
lineage drives which machines ship stock:

| Reference box        | Lockstep machine(s)                         | Kind        |
|----------------------|---------------------------------------------|-------------|
| Digitakt             | `SampleMachine` + `SliceMachine`          | generator   |
| Digitone             | `FMMachine` (4-op)                           | generator   |
| Analog Four          | `AnalogMachine` (virtual-analog)                 | generator   |
| Analog Rytm          | `DrumMachine` (analog/FM drum)          | generator   |
| **Monomachine**      | **`DigitalMachine`** (digital multi-model)   | generator   |
| Machinedrum          | `DrumMachine` + `PercussionMachine`     | generator   |
| (modal / Volca Drum) | `PercussionMachine` (physical model)         | generator   |
| Octatrack — Flex     | `SampleMachine`                             | generator   |
| Octatrack — Static   | `StreamMachine` (disk-stream)                | generator   |
| Octatrack — Thru / Neighbour | `RouteMachine` (`input_source`)       | router      |
| Octatrack — track Recorder   | `RecordMachine` (overwrite)        | capture     |
| Octatrack — Pickup           | `LoopMachine` (overdub)            | capture     |

Two lineage entries are deliberately **recipes, not machines**, because
their character is already reachable by composing what the catalogue and
the foundation provide:

- **Neighbour → Route.** The Octatrack's separate Neighbour machine is
  folded into Route: source is chosen by `input_source`
  (`External bus` = classic pass-through, `Track N` = neighbour-style
  inter-track passthrough), and gated-vs-open is the general AMP gate
  source (§14). One machine, one knob.
- **Syntakt → existing voices + master drive.** The Syntakt is its drum
  and digital voices (covered by `DrumMachine` / `FMMachine` /
  `AnalogMachine`) plus a *master analog overdrive/filter*. The drive and
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

A **model-based digital monosynth** (built the way `DrumMachine` is:
a stepped `model` slot reshapes the engine, with `valueLabels`). It
covers the digital timbres that `AnalogMachine` (analog) and `FMMachine`
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

- **GND** (ground / utility) → **Route** (`input_source`, §27).
- **FM** → **`FMMachine`** (the dedicated 4-op engine).
- **Drum / FMdrum models** → **`DrumMachine`** / **`PercussionMachine`**.

### 29.2 Stream, Route, Record, Loop

These extend the catalogue beyond the baseline Flex sampler (§3). The
first is an ordinary playback engine; the latter three consume audio via
`input_source` (§27).

- **Stream.** A disk-streaming sampler for long-form material
  (full songs, long field recordings) that should not be decoded
  into RAM. Same slot vocabulary as Flex where it overlaps
  (start/end, level), minus the RAM-only manipulations that streaming
  cannot cheaply support. Reinforces `PRINCIPLES.md` §12: the audio
  never enters RAM wholesale, let alone the project state. Its source
  is a **Stream-origin `SamplePool` entry** — a path plus a light hash
  of the file's first bytes and **no decoded PCM** — selected through
  the ordinary `sample_id` picker on SRC, so a streamed source is a
  first-class pool citizen (browsable, serialised as a reference) rather
  than a per-Kit side-channel. The serializer marks the origin (`org`)
  so a reload rebuilds it via `addStreamRef` and never decodes it; a
  Flex sampler that later picks a Stream entry decodes it on demand
  (`ensurePcm`, under the same length gate as analysis). Projects saved
  before this (≤ serializer v27) carried the path in a per-Kit
  `streamPath` property; v28 migrates that at load into a Stream pool
  entry + the track's `sample_id`.
- **Route.** Turns a track into a processing block: `input_source`
  feeds audio into the track's signal path, the machine passes it
  through at unity, and the canonical post-machine FILTER/AMP/FX (§14)
  do the work. The machine itself is nearly empty — its value is
  routing audio *into* the uniform per-track processing the sequencer
  already provides. Route subsumes the Octatrack's separate Route vs.
  Neighbour split: `input_source = External` = classic pass-through, and
  neighbour-style inter-track passthrough is achieved by routing other
  tracks' CHANNEL "Out" at this track (the bus reads their sum, §27).
  Trig-gated vs. always-open is the general AMP gate source (§14), not a
  machine type. Route is the only machine declaring `input_source` for
  now.
- **Record.** Captures `input_source` audio into a volatile buffer
  (§28). Slots: `input_source` (what to record), `target_buffer`
  (where to write), `rec_length` (how long — see §30). Capture is
  triggered by **recorder trigs** (§30); the machine itself holds no
  loop state — it overwrites the target buffer each time it captures.
- **Loop.** The overdub counterpart of Record, and Lockstep's
  equivalent of the Octatrack **pickup machine**. Overdub looping is
  a *state machine* (record → play → overdub → undo → clear), so it is
  encapsulated in a machine rather than smeared across trig flags.
  Plain overwrite resampling stays with Record; sound-on-sound
  layering lives here. Because a looper doesn't sequence, a focused
  looper turns its 16-button step grid into an **always-on console**
  (no mode to enter): top row = transport + length
  (`REC · PLAY · STOP · ERASE · UNDO · HALF · DBL · DUB`), bottom row =
  momentary performance (beat-repeat `1/16..1/2`, tape FX
  `TSTOP · DIP · x1/2 · REV`). Transport verbs cross the message→audio
  boundary on a lock-free command FIFO (discrete press edges + momentary
  press/release with a rate value); the boundary
  `(MidiBuffer, ParamFrame, AudioBuffer)` is unchanged. **HALF/DBL**
  resize the loop window with no resample. **Beat-repeat** captures the
  grid cell under the playhead and loops it while held (silent at the
  press instant), resyncing on release. **Tape FX** drive a playback-rate
  envelope while held — tape-stop brakes to a graceful Stopped, dip/
  half-speed/reverse resync to the grid on release. The mini-seq strip is
  the loop-position display (playhead + pending-edge landing pip).

- **Tape** *(designed, §40; not built)*. The linear, position-addressed
  face of the same deck engine Record and Loop are faces of: four
  sub-tracks recorded against the **project timeline**, with overdub
  layers, quantized punch, markers, jog/scrub, and varispeed. Its output
  is an ordinary track output. See §40 for the whole model — including
  why all three names stay, and why each deck defaults to a single
  stereo sub-track.

This split — overwrite in Record, overdub in Loop, position in Tape —
mirrors the Octatrack (track recorders vs. pickup machine) and keeps the
recorder trig path simple and stateless. The three are one engine wearing
three faces (§40.1); the catalogue entries above describe the faces a
performer picks, not three implementations.

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
- **Record trig.** Only meaningful on a Record track (§29):
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

**The killer use — monitored master-resampling.** The cue bus earns its
keep on a workflow the main mix cannot express. A Record or Loop that
taps `input_source = Master` (§27) to resample the whole mix is, by the
feedback guard, only *legal* when its own output does **not** reach master
(`Out = Off`) — which leaves the resample **inaudible while it is being
made**. A `Cue + track` send restores monitoring of that capture track
*without* putting it back into the master sum, so the performer hears what
they are resampling as they resample it. This is the concrete answer to
"what is the cue *for*" on a live instrument (as opposed to a DAW, where
the host monitors): **no cue = capture-blind; cue = monitor the master
resample and still capture master.** The same shape covers auditioning a
send-effect track or a bus without committing it to the front-of-house
mix.

**Composition invariant — a cue send is not a mix edge.** Because the cue
tap is additive and post-FILTER/AMP/Level, it is deliberately **excluded
from `outputReachesMaster()`** (§27): cueing a track never adds a path to
the master sum, so it can never invalidate a `Master` tap or form a
feedback loop. Cue and the §27 feedback guard therefore compose with no
special case — the whole reason the workflow above is safe.

**Outputs.** Standalone routes the cue bus to audio device channels
3–4 and the cue MIDI to a chosen output port; as a plugin it exposes
a second stereo output bus and a second MIDI output the host routes.
Chrome shows "cue unavailable" when the host has not wired the cue
output.

### 31.1 Host integration — the static output complement

Lockstep is a **co-equal standalone and plugin** instrument, not a
standalone-first app that happens to load in a DAW. The output layout is fixed
at build time so both hosts see the same thing:

- **Master + Cue + 6 Aux stereo output buses (8 total).** The main **Master**
  bus is always enabled; **Cue** and **Aux 1–6** are declared **disabled by
  default** — a host enables the ones it wants to patch. This is deliberate:
  JUCE builds the CLAP / VST3 port list from the *statically declared* buses, so
  a fixed complement is the only reliable way to expose extra outputs. **Dynamic
  port rescan is rejected** — it is a host lottery (many hosts ignore or
  mishandle a mid-session port-count change), so we never resize the bus list at
  runtime.
- **Cue = additive monitor send** (unchanged from §31): it never affects the
  Master sum and is excluded from `outputReachesMaster()`. Standalone maps Cue to
  device channels 3–4.
- **Aux = a mix routing destination.** The CHANNEL "Out" slot's destination set
  grows from `Off | Master | Bus(track)` to also include **Aux 1–6**. A route
  (bus) track can itself target an Aux — that is how an internal bus reaches a
  physical/host output. **Aux is exclusive, not additive.** Out is a single
  destination (out-degree ≤ 1): picking `Aux N` *replaces* Master, it does not
  tap alongside it — the track leaves the master sum entirely and the DAW mixes
  the aux return however it likes. This is deliberate: the host, not Lockstep,
  owns what happens to an aux output; there is no internal Aux return into the
  master sum (that is what the additive per-track **Send A/B** taps are for,
  §32). **Fallback rule: an Aux route whose host bus is disabled
  folds to Master**, so choosing an output the host hasn't enabled is never
  silent data loss.

**DAW-alongside-standalone workflows** worth naming (all first-class, none
standalone-only):

- **Direct controller → plugin.** A generic MIDI controller drives Lockstep in
  the DAW without being registered as a DAW control surface — the same surface as
  standalone. This already works and is kept un-registered on purpose.
- **Host transport / tempo follow** — existing; the sequencer locks to the host
  clock in Locked sync mode.
- **Live stem capture via Aux outs.** Route buses/tracks to Aux outputs and
  record them onto DAW tracks in real time — the blessed, host-native answer to
  "stem export" (it supersedes a bespoke offline stem-export path).
- **APVTS parameter automation** from the host.

## 32. Insert and Master Effects

Effects are owned and managed by the sequencer foundation, not left
entirely to machines. This is what makes the canonical **FX section
(key 8)** mean the same thing on every track — exactly as FILTER (key 5)
and AMP (key 6) do — and it is what lets Lockstep double as a small
performance mixer (Route tracks in, effects, cue out) without becoming
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

#### 32.1a Placement-aware quality tiers

Some effects warrant a heavier, oversampled treatment on the master bus
than is worth spending on a single track insert. Rather than clutter the
catalogue with separate "Reverb" and "HQ Reverb" entries, a single
catalogue id presents **two faces** chosen by *placement*:

- **`EffectTier::Track`** — a lean LQ face (≈4 params, no oversampling)
  built for per-track inserts where CPU is multiplied across many tracks.
- **`EffectTier::Master`** — a rich HQ face (≈8 params, internally 2×
  oversampled) for the master inserts and send returns, where one
  instance can afford to be lush.

The tier is **structural, never serialised**: it is derived from where the
slot lives, so `makeEffectForId(id, tier)` is called with `Master` at every
master save/load/placement site and `Track` at every track site. Because an
effect is never moved across track↔master, a given slot's tier — and thus
its param schema — is fixed, so the two faces are free to expose *different*
param sets without any cross-placement migration. `Reverb`, `Delay` and
`Saturation` are the tiered entries; the former standalone `HQ Reverb` /
`HQ Delay` ids fold into them and `canonicalEffectId()` migrates old
projects on load. Genuinely master-only units with no LQ counterpart
(Bus Comp, Utility) stay `masterOnly`.

#### 32.1b Gain staging is master-only

Tracks and buses run **linear** in 32-bit float — there is no per-stage
clip. The only structural ceiling is at the master output: a *transparent
soft-knee clipper* (`dsp::SoftClip.h`) that is exactly unity below ≈−3 dBFS
and only engages above it, smoothly bounding peaks just under 0 dBFS. This
keeps a clean machine (e.g. the oversampled FM) uncoloured all the way to
the master and makes overall level predictable. Character saturation that
*belongs* to a sound — Analog glue/drive, drum-kick drive, the Saturation /
Distortion inserts — is a deliberate machine/effect choice and is separate
from this structural clip.

### 32.2 Per-track inserts

Two **insert** slots per track, **fixed**, positioned **post-AMP**:
`… → AMP → FX1 → FX2 → track sum` (§14). Each slot is empty or hosts
one `IEffect`. Insert state — effect identity, base params, and the
per-step P-Locks (which live with the Phrase) — belongs to the
**Kit** (per-track), consistent with where FILTER/AMP state lives
(§4.7).

**Tap navigates params; hold opens the picker (planned, 9.x).** *Tapping* the
FX section key paginates across the two slots' pages via the canonical
extension-section mechanism (repeated tap cycles slot-1 pages, then slot-2
pages) — a tap never pops a menu. **Holding** the FX section key opens the
**track-FX picker** as a step-grid re-skin (choose insert slot + effect), the
long-press affordance sanctioned by PRINCIPLES §5/§17. This replaces the
former scope-less `Func + FX` picker (which is freed). Assigning an effect is
then a step-key press in the picker, analogous to the Kit machine picker.

A **FX-section copy** (FX scope + Record) copies effect *identity*
plus params, so "copy the whole effect chain to another track" works;
this is a deliberate extension of the §13.2 section-copy rule, which
otherwise copies slot values only.

### 32.3 Master effects — 2 inserts + 2 send returns

The master bus carries **four fixed slots** in two roles:

**Master inserts (FX1, FX2)** — in-line processing of the full master
signal, post track-sum:

```
track sum → master FX1 → master FX2 → metronome → master gain → out
```

**Send returns (Send A, Send B)** — each track carries **Send A** and
**Send B** levels (CHANNEL block, slots `lockstep.amp.sendA` /
`lockstep.amp.sendB`; P-lockable, morph-able). The levels are
post-fader/post-insert taps, summed into dedicated send buses that pass
through the send-return effects and are mixed back into the master bus
before the master inserts:

```
each track: machine → FILTER → [ENVELOPE] → CHANNEL → FX1 → FX2 ─┬─ (×sendA) → send bus A
                                                                   ├─ (×sendB) → send bus B
                                                                   └──────────→ track sum

send bus A → send FX A (return) ─┐
send bus B → send FX B (return) ──┴→ master sum → FX1 → FX2 → out
```

This gives one shared reverb/delay across many tracks — the capability
the Octatrack's insert-only FX chain famously lacks — while keeping the
master insert slots available for in-line processing (bus compressor, EQ,
limiter). Any effect may be loaded in any slot; the **masterOnly** flag in
the effect catalogue restricts certain HQ effects to master/send pickers only
(they are too expensive for 32-instance track budgets).

**External send (`lockstep.send.external.v1`).** A send slot may instead hold
the **External** sentinel (catalogue flag `sendOnly` — offered *only* in the two
send slots, never a track or master insert). It has no DSP instance: rather than
processing send bus A/B and summing the return into master, the processor copies
that bus straight to a dedicated static host output bus — **"Send A" / "Send B"**,
appended after the Aux buses at physical bus index `kSendBusBase + slot`
(`OutputDest.h`), stereo, disabled by default. The DAW opts the bus in and patches
it into outboard gear or a separate track. When the host bus is disabled
(standalone, or the DAW never enabled it) the tap is **dropped — silent, never
folded to master** (deliberately unlike a disabled `Aux N` *out* route, which
folds): an external send is a hard hand-off, not a fallback mix. The UI shows a
loaded-but-unavailable External send in the amber `EffectLoadedBypassed` state.

*Host-side routing is manual, by necessity.* The Send buses are declared
disabled-by-default, but that flag is not honoured consistently across formats: the
JUCE VST3 wrapper tags them as inactive aux buses (host opts in), while the CLAP
wrapper (`clap-juce-wrapper.cpp` `audioPortsInfo`) reports **every** JUCE bus as an
always-present non-main port and ignores `isActivatedByDefault` — there is no
per-port active/mute signal without implementing the CLAP `audio-ports-activation`
extension in the vendored submodule (invasive, spotty host support). So the plugin
cannot force the outputs muted; hosts that auto-sum all plugin outputs will double
the send (own bus + Master fold). The workflow is therefore a documented manual
step — route the Send output to its destination (PRE-fader on a send/aux track) and
mute it on the main output — surfaced by a one-time dismissible explainer
(`maybeWarnExternalSend`, persisted opt-out) when an External send is first loaded.

**CHANNEL block (Send A/B):** `lockstep.amp.sendA` / `lockstep.amp.sendB`,
both 0..1, default 0 (dry). The CHANNEL block is present on every audio
track — including machines with internal amp (Analog/FM/DrumSynth) — so sends
are always patchable via P-Lock or Morph. When both sends are zero the send
buses are not processed. The AMP section key (key 8) cycles pages to expose
all four CHANNEL params (level, pan, sendA, sendB).

**Song+FX focus cycles loaded units:** `Song+FX` *tap*-re-press walks master
FX1 → FX2 → Send A return → Send B return, skipping empty units; the FX
section key shows `"FX"` (not "GLBL") under Song scope — the Song modifier
already disambiguates it as the master copy. A dim **"PICK FX"** secondary
hint (the hold affordance, §32.2) appears on the FX key in both track and Song
scope. **`Song` + hold FX** opens the master effect picker for the focused
unit (cycles all four slots so you can load into empties; re-pick the active
effect to toggle bypass; other units' loaded effects show a dim cross-slot
hint), replacing the former `Func+Song+FX`. The picker shows the full
catalogue (including masterOnly effects) for master slots, and hides
masterOnly effects for track slots.

**Smoothing policy (§32.1 addendum):** Machines read params block-rate
(acceptable for ≤512-sample blocks). Effects must per-sample-smooth any param
in the direct signal path (gain, mix, feedback, delay time) with a one-pole
smoother (~5–15 ms). This is implemented in every effect in the catalogue.

**Scope:** Master FX state is **Set-scope** — one master chain for the whole
Set, shared "front-of-house" infrastructure distinct from per-track kits.

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

**Master-unit Animate under Song+FX focus (8.26).** When the Song+FX MetaSection
is focused (`uiState_.masterSection == 5`) and the FX picker is closed, the same
FX-held+step gesture targets the four master units instead of the active track's
inserts. The step grid is divided into four quadrants: steps 0-3 momentarily bypass
master FX1 (insert slot 0), steps 4-7 bypass master FX2 (insert slot 1), steps 8-11
bypass master Send A (send slot 0), steps 12-15 bypass master Send B (send slot 1).
The step is silently ignored if the targeted unit has no effect loaded, or if the FX
picker is open (steps mean "choose effect" there). Release restores the bypass state.
Track Animate remains available when Song+FX is not focused.

### 32.6 Performance capture — the tape deck (8.26, UX 9.16)

`Func+Song+Record` is the **CAPTURE** cell: a *separate recording device*
for the master output (a tape deck), not a DAW timeline export. The tap
point is the very end of the audio thread's `processBlock`, after master
gain, DC blocker, and soft-clip — exactly what appears at the main outputs.

**State machine (9.16, `src/io/CaptureController.h`, pure/tested).** Phases
Idle → Armed → Recording → JustSaved, driven by one cell's gesture timeline
(`tap` = gentle, `double-tap` = decisive, `long-press` = deliberate):

| State | tap | double-tap | long-press |
|---|---|---|---|
| Idle | arm (record on Play; or now if already playing) | roll now (tape) | reveal folder |
| Armed | disarm | roll now | — |
| Recording | stop (let tails ring) | hard cut (close now) | — |
| JustSaved (~6 s) | arm next | roll now | discard the take |

**Emergent-tail finalize rule — there is no mode.** The only automatic stop
is the silence tail: while *winding down* (armed by a transport stop *edge*
or a tap-stop, cancelled by a play edge), the file finalises after the
master stays below `kSilenceThreshold` for `kTailMs`. Silence **never**
finalises during active playback, so a long musical rest cannot chop a take;
stopping the transport ends a take for free (it goes quiet). A drone that
never falls silent is ended with the `double-tap` hard cut. This deliberately
replaces the earlier auto-finalize "mode" idea — the gesture set carries no
TAKE/TAPE toggle.

**Feedback strip** (`paintCaptureStrip`, under the master meter, present from
arm onward): `ARMED ▸ starts on Play` → `● REC m:ss` → `◐ STOPPING — waiting
for silence` → `✓ saved → …/Captures/…wav (hold REC to discard)`. The
destination is visible the whole time.

**Exit while recording** (`captureExitGuard`) chains a dialog — *Stop & exit*
(finalise) / *Discard & exit* (drop partial) / *Cancel* — ahead of the 9.2
dirty-project save guard. Armed-but-not-rolling disarms silently.

Capture runs across transport stop and start, so a performance file is a
single uninterrupted stream; the file is written **directly** to its
destination, so a crash leaves a real partial file.

**File location:** next to the current project file in a `Captures/`
subdirectory when a project is open; otherwise in
`~/Music/Lockstep/Captures/`. The directory is created on arm if absent.

**Implementation note:** a `CaptureRecorder` owns a `juce::TimeSliceThread`
and a `juce::AudioFormatWriter::ThreadedWriter` (lock-free ring buffer).
`arm()` and `disarm()` run on the message thread; the audio thread only
touches an `std::atomic<bool> capturing_` gate and the ring-buffer write.
The recorder is shaped to hold N streams so that per-track stem export can
be added later by arming additional streams — no redesign needed. Per-track
taps are not added now; today exactly one stream (the master bus) is active.

**Status chrome:** arming shows `REC <filename>` in the status band;
disarming shows `Captured m:ss -> <filename>`. Failure (unwritable
directory) shows a single error status and leaves capture unarmed — the
audio thread never panics.

**Non-goal (explicit):** multi-track stem export (one file per track +
master). The recorder shape accommodates it, but no per-track taps are
wired and no UI for stem selection is planned at this milestone.

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

The `LEN` encoder in the `Phrase+TRIG` meta band provides the same write path
without leaving the MZ; it and the chord gestures write the same
underlying per-phrase length parameter — no divergence.

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
Pairing that keyboard with a generic MIDI controller the performer
already owns (encoder boxes, pad grids, fader banks) is the **current,
first-class** input story, not a someday nicety — it is the surface
Lockstep is played on today while the dedicated hardware is still being
proven (`PRINCIPLES.md` §4). This section defines how such a device
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

The playhead is the one always-moving indicator. `SurfaceModel.playheadPhase`
carries a normalised sub-step phase (0 = step start, 1 = step end; -1 when
stopped) derived from `clock().cumulativePpq()`. A dumb single-colour
controller (e.g. X-Touch Mini) uses this to synthesise a phase-accurate
**1/8-off / 3/4-on / 1/8-off** envelope rather than the slow firmware flash
(`vel=1`). The on-screen playhead remains integer-step as before.

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

Built from the focused track's `ParamSpec` (normal machine-param path) *or*
from `buildMetaBand` (when a meta band is active). The single `resolveMetaBand`
cascade (`MetaBand.h`) determines which — both `buildSurfaceModel` (controller)
and `ManipulationZone::refreshSliders` (screen) call the same function with the
same `UiState`, so they cannot diverge. Controller encoder writes route through
`writeMetaField` (in `applyParamDelta`) when a meta band is active, mirroring
the screen's `onValueChange` path.

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

#### 35.8.7 Cell appearance table (8.6)

Every `CellState` token maps to a row in a single compile-time appearance
table. This dual-target table is exactly what PRINCIPLES §19 requires: one token
resolves to *both* a screen appearance (`screenFill` / `screenAccent`) and a
hardware LED appearance (`pushPad` / `xtouchVel`), so a meaningful state is never
expressible on one target but not the other. It closes the three-way divergence
where `KeyButton.cpp`,
`Push1Surface.cpp`, and `XTouchMiniSurface.cpp` each maintained a
separate switch over `CellState`. The table lives in two files:

- **`src/ui/CellStates.def`** — one X-macro row per token:
  `LS_CELLSTATE(token, value, screenFill, screenAccent, pushPad, xtouchVel)`
  Values are *literal integers* — the add-only rule is preserved because
  `value` must equal the enum integer pinned by every existing controller
  profile. The `pushPad` column holds the hand-tuned Push-1 firmware palette
  indices from `Push1Palette.h` (never recomputed from colour math).
- **`src/ui/CellAppearance.h`** — generates `enum class CellState :
  uint16_t` from the `.def` and builds a `constexpr` row array with
  `appearanceOf(CellState)` for O(n) lookup. `static_assert`s pin a
  handful of token values against accidental renumber.

Rendering rule: *table for state colour, `baseColour` for
model-computed overrides (e.g. scope tint), button-scheme as fallback.*
Decoration overrides (border flash, pip) remain coded above the table
lookup — the table gives the base, code adds layer-specific chrome.

The page-dot residual noted in §35.8.1 is closed by 8.7b: `gridBanner`
and `pageDots` fields are added to `SurfaceModel` and populated by
`buildSurfaceModel()`, making banners and page indicators
controller-visible for free (see §37.4).

### 35.9 The invalidation channel — *when* the surface redraws

§35.8 made *what* is drawn a single source of truth. This section does the
same for *when* it redraws, satisfying PRINCIPLES §22. The model build
(§35.8.1) is the shared computation; this is the shared **trigger** for it.

**Problem.** Three uncoordinated mechanisms decide when to redraw: ~50
synchronous `refreshSurface()` call sites (each easy to forget → stale
surface); polling timers that diff atomics to *notice* a change
(`KeyboardArea::timerCallback` on `cumulativePpq()` + track length; the editor
on meters/transport/morph); and per-mode "repaint every tick" workarounds (the
ManipulationZone `StepPosition` case). The controllers sidestep the lot by
re-rendering every 30 Hz tick — the emitter comment itself records that "dirty
is not a reliable signal," which is exactly the defect: there is **no single
'the surface may have changed' signal.**

**Two redraw kinds, one each.** Redraws split cleanly, and conflating them is
what produced the timer hacks:

- **Discrete** — a step toggles, a scope is held, a CC moves a parameter, the
  playhead crosses into a new *step index*. These are events.
- **Continuous** — a VU meter's ballistic decay, a blink fade, a smoothing
  crossfader. These have no discrete event per frame; they are animations.

#### 35.9.1 `SurfaceDispatcher` — the one discrete channel

A message-thread `SurfaceDispatcher` (a `juce::AsyncUpdater`) owned by
`LockstepEditor`:

- `invalidate()` — marks dirty and `triggerAsyncUpdate()`. JUCE coalesces:
  any number of `invalidate()` calls within one message-loop cycle collapse to
  a single `handleAsyncUpdate()`.
- `handleAsyncUpdate()` — the **one** place a frame is produced. It calls
  `buildSurfaceModel()` once and renders **every** sink from that single
  instance: the screen (`KeyboardArea`) and each open controller
  (`controllerPorts_`, `push1Ports_`). This replaces both `refreshSurface()`'s
  `repaint(); keyboardArea_.repaint();` body and the separate per-tick
  controller rebuild in the editor `timerCallback`.

`refreshSurface()` survives as a thin alias for `invalidate()`, so the existing
call sites keep compiling and now feed the one channel. No component schedules
its own `repaint()` for shared state.

#### 35.9.2 Audio → UI bridge (discrete)

Two distinct things happen on the audio thread, and they are *not* the same
kind of redraw:

- **The playhead is driven by the loop that fires the notes.** The step
  highlight is a step function of the PPQ clock; sampling it on a 30 Hz UI timer
  quantises the advance to the tick grid (a 16th-note ~125 ms at 120 BPM spans 4
  or 3 ticks → visible "fast, fast, slow"). So the source of truth is the audio
  loop itself: `processBlock` computes the **focused track's current step** and
  publishes it (`focusStepUi_`, atomic). The on-screen grid is repainted when
  that step advances, consumed on the **display vblank** (`VBlankAttachment`) so
  the repaint is display-synced and the grid rebuilds *once per step*, not once
  per vblank. The step is computed with the same musical step grid
  `buildSurfaceModel` renders, so the published step and the drawn step agree.
  The **controllers** are a separate consumer: they additionally render a
  *sub-step phase envelope* (`SurfaceModel::playheadPhase`, e.g. the X-Touch
  playhead LED fades off→on→off within a step), which needs sub-step updates, and
  they must keep moving when the **vblank is paused** (display asleep, window
  hidden) — the performer reads the hardware, not the screen (§19). So the
  controller playhead rides the always-on ~30 Hz timer, not the vblank. Two
  consumers, one published source.
- **Param mutations are discrete events.** Machine-param writes (CC, encoder,
  P-Lock) are *asynchronous*: `writeParam` enqueues an `EngineCmd`, and the
  audio thread applies it in `drainEngineCmds`. The UI value is only correct
  once it lands there, so that is the event. `drainEngineCmds` sets the
  `surfaceDirtyFromAudio_` atomic flag whenever it applies any command; the
  editor reads-and-clears it (`takeSurfaceDirty()`) on its tick and
  `refreshSurface()`s. This is what *settles* a CC/encoder write — including the
  final value after the user stops turning, which an immediate post-input frame
  would miss because the write had not yet been applied.

APVTS params (track length / divider / mute / solo) are not on this path; they
change through their own value tree and are caught by the editor's APVTS
`parameterChanged` listener → `refreshSurface()`. That listener is what replaced
`KeyboardArea`'s length poll. The atomic-flag-plus-tick pattern (rather than the
audio thread calling `triggerAsyncUpdate()` directly) is deliberate: posting a
message from the audio thread is not real-time-safe.

#### 35.9.3 The single animation clock

Animation has two natural cadences. The **screen playhead** moves fast and must
look smooth, so it rides the **display vblank** (§35.9.2): a `VBlankAttachment`
repaints the grid when the audio-published focused-track step
(`focusStepUi_`) advances — display-synced, once per step, nothing when stopped.
(The controller playhead is a separate, always-on-timer consumer of the same
source, since the vblank pauses when the display sleeps.) The **slow decays** —
meter / activity
blinks, crossfader smoothing — plus timed gesture promotions (generator-hub /
FX-section long-press, timed *events* that fire once then `invalidate()`) ride a
~30 Hz timer; a VU envelope does not need display-rate updates. That timer
advances the decays and repaints while any is live, and self-suspends once they
settle (the meter floor-to-zero rule) so an idle surface issues zero repaints; a
fresh audio-thread pulse re-arms it. (Stage 4 consolidates the remaining
component timers onto these two; whether the slow-decay timer also folds onto
the vblank is an open simplification.) Controller **input** drain stays on a small tick (input ≠ render).
The redundant render pollers are retired onto the channel: `KeyboardArea`'s
PPQ/length poll is **gone** (Stage 3 — playback frames come from the editor
tick, length/divider from the APVTS listener), and `InPluginTransport` /
`SamplePoolOverlay` follow in Stage 4. `ControllerPortManager`'s 1 Hz
device-presence poll is hardware hotplug detection, not a render path, and is
unaffected.

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
    uint32_t (*flags)(void* inst);     /* hasInternalAmp, isMidiOut (cacheable); hasInternalFilter removed in 8.28 */
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
    /* track loop grid — the ABI equivalent of ILoopGridAware; useful to any
       tempo-synced generator (LFOs, arps), not just loopers. Add-only (§36.6). */
    void   (*loopGrid)(void* ctx, int32_t* lengthSteps, double* stepPpq);
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
`loopGrid` / `rngNext` / `log` must be audio-thread-safe (logging via a
lock-free FIFO, not direct I/O). `transport` already subsumes what a
static machine gets from `ITempoAware`; `loopGrid` is the pull-based
equivalent of `ILoopGridAware`. Note that these two seams are all a
*generator* needs from the loop context — the deeper looper machinery
(console command dispatch, live state readback) is deliberately **not**
exposed here; see §36.9.

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
modules. The post-machine FILTER/CHANNEL/ENVELOPE virtual-slot append and
the `hasInternalAmp()` opt-out stay entirely host-side and ABI-agnostic —
only `hasInternalAmp` crosses (via `flags`); the module never sees the
FILTER/CHANNEL/ENVELOPE slots that live past its `numParams()` range.
This registry is the static/dynamic sibling of the planned
`ControllerRegistry` (§35.8.4).

### 36.6 Versioning and capabilities — add-only

`hostAbiVersion` is passed into the entry; the module returns `NULL` if
it cannot satisfy it, and the host checks `desc->abiVersion` and
`structSize`. **Growth is add-only**, identical in spirit to the
`CellState` rule (§35.8.6): new vtable / descriptor fields append after
`structSize`; the host reads only `min(known, module structSize)`;
fields are never renumbered, reordered, or repurposed. Optional surface
is negotiated through `capabilityFlags`
(`LSM_CAP_MIDI_OUT | INTERNAL_FILTER | INTERNAL_AMP | AUDIO_INPUT`; the
last is forward-compat for the §2 `input_source` Route/Record idea).
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

### 36.9 Host-privileged machines — the ABI's scope line

The ABI's job is **generators and effects** (§29: "granular, physical-model
specialities are the first examples of what the module ABI is *for*").
**Capture and console machines — `RecordMachine`, `LoopMachine`, the `Tape`
face of the same deck engine (§40), and any machine that owns an always-on
console — are first-party, statically linked, and out of ABI scope.** This is a deliberate boundary, not a missing feature,
and it is drawn where it is for two structural reasons:

- **The console is host-owned UI, and generalising it means generalising the
  surface.** A looper's console is not DSP: it is a command-in FIFO
  (`postCommand` / `postPerf` — discrete transport verbs plus momentary
  press/release perf actions with a rate value), six live state mirrors
  (loop state, playhead phase, pending-edge, loop length, beat-repeat rate,
  tape-FX cell), and a 16-cell always-on grid painted from those mirrors. To
  let a *module* provide this, the C ABI would need a generic control-in ring
  **and** a generic state-token readback — which is exactly the
  `SurfaceModel` / `CellState` system (§35.8), *itself unbuilt*. Baking a
  half-designed surface into an **add-only-forever** ABI (§36.6) is precisely
  the commitment we must not make. When the surface model lands, revisiting
  this is a clean, additive decision; pre-committing is not.
- **Capture machines are infrastructure that reaches into host-owned graph
  and pool.** A Record/Loop is defined by the routing graph it drives —
  `input_source` resolution, the §27 feedback guard, the union-of-mix+tap
  topological sort — and by *writing* RAM-only volatile pool slots (§28). The
  host owns all of it. `LsmHostVTable` exposes only *read-only, resolve-by-ref*
  PCM (`{path, xxHash32}`), never a writable transient buffer, precisely
  because handing a module a mutable pool slot would also hand it a slice of
  the graph it cannot safely own.

What a module *does* get from the loop context is the two pull-based seams in
§36.4 — `transport` and `loopGrid` — which are all a generator needs. The net
effect: the "the ABI can't express the Loop" observation is true and
**intended**. A third party authors engines and effects; loopers, recorders,
and bespoke consoles stay in the core. If a genuinely module-shaped capture
need appears later, it is an add-only extension negotiated behind a new
`capabilityFlags` bit, taken up deliberately rather than by default.

## 37. Command Core (8.3–8.5)

The goal of Phase 8's structural work is that **`PluginEditor` performs
no grammar decisions.** It resolves inputs via `resolveLayer`, forwards
to the command core, and executes the returned effects. All three input
paths — QWERTY, mouse, controller — converge at a single seam.

### 37.1 Button-layer resolution (`src/command/ButtonLayers.h`)

Raw physical events arrive with a `ControllerButton` encoding the button
identity. A held modifier (Track/Mute/Func) can redirect that button to a
different logical button — Step → SelectTrack under Track, Section →
MetaSection under Func, etc. Previously this was implemented three times
and had diverged.

```cpp
struct LayerContext {
  bool funcHeld  = false;
  bool trackHeld = false;
  bool muteHeld  = false;
};

[[nodiscard]] ControllerEvent resolveLayer(ControllerEvent raw,
                                           const LayerContext& ctx) noexcept;
```

`resolveLayer` consults `kLayerRemaps[]` — a `constexpr` table of
`{raw, layer, effective}` triples — and returns a copy of the event with
only the `button` field changed. The `index` and `velocity` fields survive
unchanged so Push pad velocity and step indices are preserved through the
remap. Priority order (Track > Mute > Func) is encoded as table position
and pinned by the golden test in `tests/LayerResolveTest.cpp`.

`LayerContext LockstepEditor::layerContext() const` reads the effective
(physical OR latched) modifier flags from `UiState`. Every input path
calls `resolveLayer` *before* forwarding to the command core; no
unresolved raw event reaches `handleDown/handleUp`.

### 37.2 Command context and effects seam

```cpp
// src/command/CommandContext.h
struct CommandContext {
  Arrangement&        arrangement;
  Sequence&           sequence;
  EditContext&         editContext;
  EditMode&           editMode;
  UiState&            uiState;
  Clipboard&          clipboard;
  SoundPool&          soundPool;
  CheckpointStore&    checkpoints;
  const IMachineCatalog& catalog;   // narrow pure-virtual; grows only as needed
};

// src/command/CommandEffects.h
struct CommandEffects {
  virtual void status(juce::String) = 0;
  virtual void requestRepaint()     = 0;
  virtual void transport(TransportAction) = 0;
  virtual void machineAssign(int track, const char* machineId) = 0;
  virtual void openOverlay(OverlayId, int track) = 0;
  virtual void crossfader(float) = 0;
};
```

`IMachineCatalog` is a narrow pure-virtual seam: `numParams(track)`,
`paramSpec(track, slot)`, `section(track, idx)`, `machineId(track)`.
It is implemented by the processor in editor wiring and by a lightweight
fixture in tests — isolating the command core from JUCE and the plugin
processor without requiring a full mock.

`EditorEffects final : CommandEffects` is a private inner struct in
`LockstepEditor`; it holds a reference to the editor and dispatches each
effect to the appropriate JUCE component call. The test target supplies
`RecordingEffects` which records calls for assertion.

### 37.3 Command core (`src/command/CommandCore.{h,cpp}`)

```cpp
bool handleDown(const ControllerEvent&, CommandContext&, CommandEffects&);
bool handleUp  (const ControllerEvent&, CommandContext&, CommandEffects&);
bool handleVerb(EditMode::PrimaryScope, ControllerButton,
                CommandContext&, CommandEffects&);
```

All return `bool handled`. The editor calls core *first*, then falls
through to legacy switch cases during migration. Each stage of 8.4
moves one scope/category *and deletes* the corresponding legacy case in
the same commit. The core is JUCE-Component/Timer-free (`juce::String`
is permitted); `PluginProcessor.cpp` is never pulled into the test target.

Verb handlers live in `src/command/VerbCommands.{h,cpp}` as free
functions, one per scope. No scope×verb function-pointer table — the
bodies are heterogeneous and a table would be cosmetic not structural.

`activeTrack` moves from `KeyboardArea` into `UiState` as an `int`
member so it is shared state visible to both the command core and the
surface model.

### 37.4 Status and contextual text SSOT

All user-visible status strings are built in `src/command/StatusText.h`
as typed builders in `namespace lockstep::status` (e.g.
`copiedSteps(int)`, `sceneCommitted(int)`). **No string literal may be
passed to `status()` outside this file.** This enforces that every
user-visible message is findable, testable, and correctable in one place.

Contextual banners (`"SELECT MACHINE"`, `"SELECT TRACK/PHRASE/SCENE"`)
and per-track page-dot state are part of the surface model, not painted
ad-hoc from `UiState`. `SurfaceModel` gains:

```cpp
const char* gridBanner = nullptr;          // null = no banner
struct PageDots { uint8_t count; uint8_t active; };
std::array<PageDots, kNumTracks> pageDots{};
```

Both are populated by `buildSurfaceModel()`, making them
controller-visible for free and eliminating the `KeyboardArea.cpp`
direct `uiState_.trackPage` paint path.

`ScopedSectionMatrix.h` section name cells that duplicate canonical
names reference `IMachine::kCanonicalSectionNames[i]` directly; only
genuine overrides (`"DIV"`, `"LEN"`, `"FX"`, `nullptr`) stay literal.

### 37.5 ParamRow — LsmParamSpec precursor (`src/machine/MachineParamTable.h`)

`struct ParamRow` is field-order-identical to `LsmParamSpec` (§36.2):

```cpp
struct ParamRow {
  const char* id;             // static lifetime — serialization key
  const char* label;
  float minValue, maxValue, defaultValue, skew;   // skew 1.0 = linear
  std::uint8_t isStepped, unit, role, variant;    // enums as uint8 (ABI shape)
  std::int32_t sectionIndex;
  std::uint8_t zeroCrossingSnap;
  const char* const* valueLabels;   // NULL-terminated array, static; nullptr = none
};
[[nodiscard]] ParamSpec toParamSpec(const ParamRow& row);
```

Each machine defines `static constexpr ParamRow kParams[]` and replaces
its `paramSpec(i)` switch with a bounds-check + `toParamSpec(kParams[i])`.
This is the in-tree shape that 6.7 will expose as the C ABI; converting
now means the 6.7 migration is mechanical. `ParamRow` is **JUCE-free** —
no JUCE includes in `MachineParamTable.h`. The conversion `toParamSpec`
is the single place `juce::String` is created from the `const char*`
fields. The golden id test in `tests/ParamSpecTest.cpp` ensures no
accidental id change silently breaks existing project files.

### 37.6 The A-series: key-cell label/action SSOT (8.11)

Phase 8 closed the dispatch and appearance divergences; the A-series
closes the last one — *what a key cell says* and *what pressing it
does* were still resolved in separate, per-renderer code. Three new
SSOTs, in dependency order:

- **`src/command/ScopePriority.h` — `kScopePriority`.** The one
  encoding of "which scope wins", highest first.
  `EditMode::recomputePrimary`, every label/colour resolver, and
  binding-row tiebreaks all derive from this array; no other file may
  hard-code a scope ordering.
- **`src/command/SurfaceLayer.{h,cpp}` — `resolveActiveLayer()`.** A
  single priority-ordered enum of every step-grid overlay (pickers,
  note-edit, P-Lock clear, chromatic/levels, mute views, …). The one
  place all layer conditions are encoded; the step-grid renderer and
  dispatch both consult it. Caller-supplied facts it cannot derive from
  `UiState` arrive in a small `LayerFacts` struct.
- **`src/command/KeyBindings.{h,cpp}` — the binding table.** One row
  per reachable action: `(button, index, layer, requiredMods)` →
  `(ActionId, label, CellState)`. Resolution is **most-specific wins**
  (highest `popcount(requiredMods)` among matching rows; ties broken by
  `kScopePriority`), replacing per-key forbidden-modifier checks.
  Today the table drives key-cell *rendering*; **A4** wires `ActionId`
  to the dispatch handlers so render and dispatch cannot disagree.

Golden tests pin each: `tests/SurfaceLayerTest.cpp`,
`tests/KeyBindingTest.cpp`. Status: A0–A3 shipped; A4 (dispatch wiring)
and Task B (confirm-prompt + master-FX-picker layers through
`SurfaceLayer`) pending. ROADMAP 8.11 tracks the series.

**8.24 additions to the binding table:**

- **Explicit scope×Func combined rows.** A `{kModScope|kModFunc}` row
  now exists for every scope where `Func+Scope+Clear` = Delete. Because
  resolution is most-specific-wins, these outrank the bare-scope rows at
  popcount 2; they carry the correct label and `ActionId::VerbDelete`.
  Song has no delete-picker row; the key dims under `Song+Func`.
- **Universal secondary rule (§6.5 policy 4).** Every row's `hint`
  field must equal the `primary` of the resolved `Func`-variant row.
  `testHintRule()` in `KeyBindingTest.cpp` enforces this; exemptions are
  documented inline.
- **Deletion picker + confirm layer.** `SurfaceLayer::DeletePicker` and
  `SurfaceLayer::PendingConfirm` are now live layers (in that priority
  order; confirm outranks picker). `resolveActiveLayer()` checks
  `ui.deletePicker.active()` and `ui.confirm.pending()`. The confirm
  layer exposes two binding rows for `VerbNo` (the `P` key): bare →
  `VerbConfirm / "YES" / ConfirmYes`; with `kModFunc` →
  `VerbCancel / "NO" / ConfirmNo`. Sticky lifecycle is in
  `CommandCore::handleDown` — Func never cancels; any other foreign
  press does. Execution routes through `CommandEffects::executeConfirm`
  so confirm bodies are testable without `PluginEditor`.

### §37.7 UI mode layer (`src/ui/mode/`) — correct-by-construction overlay management

The `src/ui/mode/` package makes illegal overlay coexistence structurally
unrepresentable and "forgot to wire an exit" a compile-time error, not a
silent gap. All modules are pure (no `juce::Component`) and unit-tested.

**Overlay enum (`state/UiState.h`).**
`UiState::overlay` is a single closed-enum field of type `Overlay`
`{None, Euclid, Time, Density, Vel}`. At most one sticky mode is active;
the second assignment atomically exits the first. `Overlay.h` is a shim
that includes `UiState.h`. See PRINCIPLES §18.

**`OverlayDescriptor` + `kOverlays` table (`ModeReducer.cpp`).**
Each overlay has one declarative record stating its full exit policy:
which section index is "own" (cycles sub-page vs. exits), which scope
keys are foreign (exit on press), whether `Func` double-tap exits, etc.
`ExitPolicy` has **no default** — every field must be stated. Adding a
new overlay: add one `Overlay` enum value + one row to `kOverlays`; the
compiler rejects any omission. The TIME "too-sticky" bug (three missing
exits) cannot recur under this design.

**`ModeReducer` public API (`ModeReducer.h`):**
- `activeOverlay(ui)` — returns `Overlay::Euclid` when `euclidHeld`
  (transient chord), otherwise `ui.overlay`.
- `escapeOverlay(ui, ov)` — clears `overlay` and resets per-overlay
  params; guards with `if (ui.overlay == ov)` so defensive calls are no-ops.
- `handleOverlayEvent(ui, ev, ctx)` — iterates `kOverlays` and applies
  consume/exit policy. Returns `Consumed`, `Exited`, or `NotConsumed`.
- `overlayInternalSectionLabel(ui, idx)` — dynamic relabel for the active
  overlay's "own" section key (replaces scattered per-mode `if` branches).

**Supporting modules:**
- `GestureRecognizer.h` — single timing home for double-tap + long-press.
  Non-colliding tokens: modifiers `1000 + int(ControllerButton)`, steps
  0–63, `kNavRightUnlock = 4000`, `kRestoreLongPressToken = 5000`.
- `FuncReskin.{h,cpp}` — `activeFuncReskin(ui)` / `exitFuncReskin(ui)`:
  unified enter/exit for the five Func-layer picker+editor modes
  (MachinePicker / TrackFxPicker / MasterFxPicker / NoteEdit / PLockClear).
- `LatchOps.{h,cpp}` — `latchColumn(cb)` / `latchBoolFor(state, cb)` /
  `clearLatchColumnExcept(state, cb)`: pure column-exclusivity helpers for
  modifier latch; tested independently to catch drift from `setModifierLatch`.

Tests: `tests/ModeReducerTest.cpp`, `tests/GestureTest.cpp`,
`tests/FuncReskinTest.cpp`, `tests/LatchOpsTest.cpp`.

---

## §38 Threading Contract

`LockstepProcessor` runs on two threads simultaneously once the host has
called `prepareToPlay` and started delivering `processBlock` calls:

- **Audio thread** — calls `processBlock`; must never allocate, block, or
  reach the message/UI thread's data structures without a lock-free gate.
- **Message/UI thread** — the JUCE message loop; responds to UI events,
  APVTS change callbacks, timer ticks, and calls all public non-audio
  methods (writeParam, setGlobalMute, queueScene, …).

### §38.1 Three ownership classes

Every member of `LockstepProcessor` belongs to exactly one class.
The class is marked `[AUDIO]`, `[ATOMIC]`, `[QUEUE]`, or `[SUSPEND]`
in `PluginProcessor.h` comments; unmarked members are message-thread-only.

#### Class 1 — Audio-owned `[AUDIO]`

The audio thread reads and writes these members freely. The message
thread must not touch them while processing is active. This includes:

- Sequencer running state (`nextTriggerPpq_`, `lastStepFired_`,
  `anchorPpq_`, `pendingNoteOffs_`, `pendingTrigs_`, `lastScheduledStepNum_`)
- Live voice tracking (`liveVoices_`, retrig audio state, chord capture,
  realtime-record tables, preview playback state)
- Per-block DSP buffers and state (`trackBuffers_`, `trackChokes_`,
  `trackFltrs_`, `trackAmps_`, morphFader smoothed value, DC state)

The working sequence/kit data inside `arrangement_` is logically
audio-owned once processing starts; the message thread may read aligned
32-bit scalars relaxed but must never mutate containers (`baseParams`
vectors, `steps` arrays, P-Lock maps) mid-block.

#### Class 2 — Atomic `[ATOMIC]`

Shared between threads via `std::atomic` (or APVTS-managed atomics,
denoted `[ATOMIC]*`). Any thread may read or write at any time using
the default (sequentially consistent) or relaxed ordering as documented
per member. This includes all APVTS parameter pointers, metering
outputs, fill state, mz slots, retrig request fields, morph fader
target, scene queue, panic request, and the keyboard FIFO indices.

#### Class 3 — Queue-mediated `[QUEUE]`

Post-8.16: the message thread enqueues `EngineCmd` records via a
`juce::AbstractFifo`-backed SPSC FIFO (`engineCmdFifo_`); the audio
thread drains the queue at the top of each `processBlock` call, before
the sequencer advances. Mutations covered: base param writes (currently
`writeParam`), P-Lock writes, trig-override edits, mute/solo changes,
track-length changes, and control-all fan-outs. Until 8.16 these land
directly on the message thread — a known data race on `baseParams`
containers, documented in ROADMAP 8.16.

Stopped-audio fallback: an atomic `lastBlockTimeMs_` heartbeat lets the
message thread detect that no block has fired for >100 ms and drain the
queue itself (bounded, safe because the audio thread is not running).

#### Class 4 — Suspension-mediated `[SUSPEND]`

Structural changes that cannot be expressed as bounded atomic stores:
machine/effect swap (`reinstallMachinesFromActiveKit`, `setTrackInsert`,
`setTrackMachine`, `copyKit`), full state load, checkpoint restore,
sample-pool rebuild, and MIDI-learn table writes. These are routed
through `withQuiescedEngine(fn)` (shipped 8.18) which suspends processing,
drains the EngineCmd queue on the message thread so pending param writes
land before structural edits, calls `fn()`, then resumes.

Serializer snapshot rule: `getStateInformation` / checkpoint snapshot
run inside `withQuiescedEngine` — suspend, drain queue, read, resume
— so the snapshot sees a consistent state that includes any queued but not
yet applied edits.

### §38.2 preparedSampleRate_ / preparedBlockSize_

`juce::AudioProcessor::getSampleRate()` and `getBlockSize()` return 0
until a plugin host calls `setRateAndBufferSizeDetails()`, which only
happens in the hosted plugin context, not in headless tests. All
install/swap helpers (`reinstallMachinesFromActiveKit`, `setTrackInsert`,
`setMasterInsert`, `setTrackMachine`, `copyKit`, and the state-load path)
use the cached `preparedSampleRate_` / `preparedBlockSize_` members
(set at the top of `prepareToPlay`) instead of `getSampleRate()` /
`getBlockSize()`. Defaults: 44100 Hz / 512 samples — valid until the
first `prepareToPlay` call.

### §38.3 currentSampleIndex_ in SamplePlayingMachineBase

`currentSampleIndex_` is written from `process()` (const audio-thread
path, via `SampleMachine` and `SliceMachine`) and read from the
message-thread `detectTransientSlices()`. It is `mutable std::atomic<int>`
with relaxed semantics — we only need the most-recently-set index, not
strict ordering.

### §38.4 Pre-staged scene switch (8.17)

`queueScene` (message thread) calls `Arrangement::prepareSceneLaunch`,
which flushes live edits (`writeBackWorkingToActive`), stashes the
departing scene's overlay, and projects the new scene into a pre-allocated
`StagedSceneSwap` struct. `stagedSwapReady_` is raised after.

At the bar boundary, the audio thread sets `pendingSceneApply_`. At the
top of the next block (after queue drain), the audio thread calls
`Arrangement::applySceneLaunch`, which swaps the staged `Sequence` into
`arrangement_.working` using `std::swap` (O(N) bounded, no allocation)
and updates `sceneIdx` and deviation state.

THREADING-DEBT: `arrangement_.sceneIdx` and `arrangement_.deviated` are
written by the audio thread and read by the message thread without a lock.
Both fields are ≤ int-sized (aligned); the read is benign in practice but
not formally safe — will be addressed in a future mutation sweep.

Machine reinstall after the switch: `callAsync` fires `reinstallMachines
FromActiveKit()` under `withQuiescedEngine`. Kit data is already correct
after the writeback in `prepareSceneLaunch`, so the reinstall is safe to
run any time after staging completes.

## 39. Density — Live Trig-Thinning Overlay

Density replaces the old Chance macro. It is a **live, subtractive performance
overlay** that thins the trigs that *would* fire, strictly downstream of the full
trig-evaluation pipeline (fill → base-trig → iteration → prev-dep → probability).
It can only *silence* would-fire trigs; it never re-enables a step and never
modifies the probability/condition system.

### 39.1 Keep-score math

After `TrigEvaluator::shouldFire` returns true, one final gate runs:

```
effective = clamp(per_track_density + master_density, 0.01, 1.00)
w         = 1 - metricDrop(ppqInBar, barPpq)   // importance: 1 = downbeat, 0 = finest offbeat
m         = musicalityM(track.musicality)       // 0, 0.5, or 1
p         = clamp(effective + kBias * m * (2w - 1) * (1 - effective), 0.01, 1)
R         = see §39.3 (Scrub hash or cadence-aware Reroll hash)
survive iff R < p
```

`kBias ≈ 0.9` (tunable constexpr in `Density.h`). This model:
- Preserves the full pattern at `effective = 1` (p = 1 for all steps).
- Produces pure random thinning at `Uniform` (m = 0, p = effective).
- At `Metric` (m = 1) drops least-important beats first, gradually —
  downbeats approach `p ≈ kBias` at low density while the finest offbeats
  approach `p ≈ 0.01`. Average `p ≈ effective`, so the knob stays honest.
- R always matters (Scrub ≠ Reroll in every musicality).

**Floor at 1%** — Density is never a second Mute; a fully-closed Density still
lets the rarest step through. Use `Mute` to silence a track.

**Additive master** — master density is a **signed offset** (`−1.0 … +1.0`,
default 0) applied additively before clamping. This lets the master sweep the
whole kit up or down without proportional interaction. The offset has no mode of
its own; each track thins according to its own Musicality setting.

### 39.2 Metric weighting (Dmetric)

There is no global bar at trig time (Clock is PPQ/BPM only). Anchor off the
active scene's `coreTime` (`Scene.h`, `TimeSig::barPpq()`):

```
ppqInBar = fmod(nextTriggerPpq_[i], barPpq)   // use the grid PPQ, not swing-shifted firePpq
```

**Metric weight** is computed by `MetricGrid::metricWeight(ppqInBar, barPpq, numerator, denominator)`
(`src/core/MetricGrid.h`) using the **Lerdahl–Jackendoff dot-count** algorithm:

1. Partition the bar into `numerator` equal pulses. Recursively split each span
   (prefer binary halving; then ternary; then greedy front-loaded 3s for primes):
   `w[i]` = number of recursive levels at which pulse `i` is the group head
   (its "dot count"). Downbeat heads every level → maximal.
2. Extend below the pulse level with a fixed sub-grid (`kSubGridSize=4`,
   `kSubLevels=2`): a position exactly on pulse `i` adds `+kSubLevels` to its
   count; a position `f` fractions of a pulse interval away contributes
   `kSubLevels − trailing_zeros(round(f * kSubGridSize))`.
3. Normalise: `weight = (count−1) / (downbeatCount−1)` → [0,1].
   Downbeat = 1.0, finest sub-pulse offbeat = 0.0.

Drop propensity: `Dmetric = 1 − weight` (the density `metricDrop` wrapper).

Worked pulse-weight vectors (pinned by `MetricGridTest`):

| Meter | pulse weights `w[]` | strong beats |
|---|---|---|
| 4/4 (4 pulses) | `[3,1,2,1]` | 1 > 3 > 2,4 |
| 3/4 (3 pulses) | `[2,1,1]` | 1 > 2,3 |
| 6/8 (6 pulses) | `[3,1,1,2,1,1]` | 1 > 4 (two dotted beats) |
| 9/8 (9 pulses) | `[3,1,1,2,1,1,2,1,1]` | 1 > 4 > 7 |
| 7/8 (7 pulses) | `[3,1,1,2,1,2,1]` | 1 > 4,6 (= 3+2+2) |

In 4/4 with a 16th-note grid this reproduces the old trailing-zero ranking
exactly (counts 5/4/3/2/1 at positions 0/8/4·12/2·6·10·14/odd) — regression-safe.
6/8 vs 3/4 differ purely from numerator (6 vs 3); no special compound-meter case.

This works for polymetric tracks because weighting is keyed off the step's
absolute PPQ position in the bar, not the track's own length.

### 39.3 Selection modes

| Selection | Intent | Behaviour |
|---|---|---|
| **Scrub** | Deterministic | Count-based tier+Euclid (see §39.3a). Same knob level always selects the same subset; turning reshuffles. Recallable. |
| **Re-roll** | Stochastic | `r < p` comparison (§39.1 formula); `r` is a cadence-aware hash (see §39.3b). Evolves; not recallable. |
| **Exempt** | Bypass | Track is invisible to both master dial and per-track amount; always fires regardless of global density. Amount + Musicality cells greyed in MZ. |

**The two modes are fundamentally different algorithms, not just different hash sources.**
Scrub produces a clean, evenly-spread subset at every density level; Reroll produces a
continuously-evolving random subset shaped by the metric probability curve.

#### 39.3a Deterministic Scrub model (MetricSelect)

Scrub ignores the `r < p` formula entirely. It is **fully deterministic and loop-stable**
— no per-step hash, no stochastic element. A fixed per-track rotation seed
(`off = densityScrubHash(track, 0, 0)`) de-correlates same-density tracks without
any loop-to-loop reshuffling.

Each musicality mode is independent:

```
effective = clamp(per_track + master, 0.01, 1)
off       = densityScrubHash(track, 0, 0)        // fixed per-track rotation

Uniform  (whole loop, period L = track length in steps):
    Tl = round(effective * L)
    survive(loopPos) = euclidHit(loopPos, L, Tl, off)   // even, rotated

Metric   (global bar, period N = stepsPerBar):
    T = round(effective * N)
    survive(barStep) = metric[T] bit barStep             // tier+Euclid, no rotation

Mixed    (global bar, period N):
    T = round(effective * N);  P = round(0.5 * T)
    survive(barStep) = mixed[T] bit barStep
      metric-protected core: mixed[T] ⊇ metric[P]       // top-P always kept
      even fill: bjorklund(N−P, T−P, off) on unprotected positions
```

**Loop scope:** Metric and Mixed lock to the **global bar** (mask indexed by
bar-local step → identical across bars for the same density). Uniform spans the
**whole track** (period = track length) so it may vary bar-to-bar within a long
loop but repeats exactly every loop.

**Per-musicality behaviour:**

| Musicality | Period | De-correlation | Result |
|---|---|---|---|
| Uniform | track length | per-track `off` rotation | Even spread over whole loop; same pattern every loop. |
| Metric | bar | none (downbeat anchored at step 0) | Clean tier-by-tier Euclid thinning; downbeats outlast backbeats outlast offbeats. |
| Mixed | bar | per-track `off` rotation on fill | Metric core always kept; remainder filled by even Euclid, rotated per track. |

**Metric mask construction (`MetricSelect::build`, `src/core/MetricSelect.h`):**

For each target count `T` from 0 to N, `Table::metric[T]` is computed:
1. Sort bar positions by descending `MetricGrid::metricWeight`. Equal-weight positions
   form a **tier**; positions within a tier retain their index order.
2. Walk tiers strongest-first, accumulating the survivor set.
3. When a tier would overflow the target (boundary tier, M positions, k more needed):
   select k positions using `bjorklund(M, k)` — evenly distributed over the tier.
4. Store as a `uint64_t` bitmask `metric[T]`.

**Mixed mask construction:** For each T, `Table::mixed[T]` = `metric[P]` (P=round(0.5T))
ORed with `bjorklund(N−P, T−P, off)` mapped onto the ascending list of positions absent
from `metric[P]`. Count-honest: exactly T survivors.

**Per-count Euclid, not drop-point Euclid.** `bjorklund(M, k)` is recomputed at each
density level independently. A position may be present at T, absent at T+1, present at
T+2 — this is by design. Even spread at each count matters more than monotonic add/remove.

The table is a pure function of (numerator, denominator, stepsPerBar, trackOffset). It is
precomputed into a per-track `DensityTableCache` in `PluginProcessor` and rebuilt only
when the meter or track divider changes (O(N²) over N≤64).

#### 39.3b Stochastic Reroll model (unchanged)

The §39.1 `r < p` formula with `kBias = 0.9` is **frozen**. Reroll hashes are:

- **Uniform** → `rerollPerStep(trackIdx, stepNum)`: fresh each time the step plays;
  selection evolves every loop iteration.
- **Mixed / Metric** → `rerollPerBar(trackIdx, barIndex, stepInBar)`: one value per
  (track, bar, step-in-bar) triple; fixed for a full bar, fresh each new bar. Creates
  a per-bar filter set that gently shifts bar-to-bar.
  `barIndex = floor(firePpq / barPpq)`, `stepInBar = stepNum % stepsPerBar`.

Mixed-stochastic is the exact mean of Uniform-prob and Metric-prob because `p` is
linear in `m` — no special-casing needed.

All Reroll hashes are **pure functions** (no mutable RNG state). Main and lookahead
scans agree automatically since they hash the same inputs.

Scrub and Reroll hashes must use **different salt constants** than `TrigEvaluator`'s
`deterministicPercent` (salts `2654435761u / 2246822519ull / 0x45d9f3bu`) to
avoid correlating density selection with which steps barely passed probability.

### 39.4 Two lifetimes

| Data | Lifetime | Location |
|---|---|---|
| **Musicality** (Uniform / Mixed / Metric), **Selection** (Scrub / Re-roll / Exempt) | Durable, per-song, per-track | `TrackKit` — serialized (v19) |
| Per-track density amounts | Ephemeral | RAM — reset on song/project change; rides scene overlay |
| Master density offset | Ephemeral | RAM — same |

**Ephemerality** is implemented by carrying density + master offset inside the
scene overlay (`SceneOverlay`), mirroring the existing `deviated` / `deviationPhraseIdx`
mechanism. Overlay-carry scene launch keeps live density deviations (sticky); floor
launch (`Func + Scene + step`) wipes them. `setActiveSong` and `loadPosition` always reset to defaults.

### 39.5 Grammar and surface

| Gesture | Effect |
|---|---|
| `Func` (held) | MZ → transient Density band (8 per-track rotaries, bank follows focused track) |
| `Song`-held + encoder or drag (within Density band) | Adjust master density offset; arc + effective tick sweep on all rotaries; thumbs stay per-track |
| `Func + MOD` (chord) | Enter sticky DENSITY mode (pinned). `Func + Song + MOD` enters with the master page engaged. Exit via `Func` double-tap (universal escape) or a foreign cluster scope key. Entry blocked if a foreign cluster scope (other than `Song`) is physically held — see §39.8. |
| nav keys (↑↓←→) while sticky | Page between bank 1-8 and bank 9-16 |
| MOD section key while sticky | Toggle Amount sub-page (per-track rotaries) ↔ Mode sub-page (Musicality/Selection) |
| Track / Phrase / Scene / Morph / Mute / Fill while sticky | Discharges sticky mode before running the scope's normal handler — see §39.8 |

**Song disambiguation:** `Song`-alone opens song-level swing (unchanged). `Song`-held
within the Density band adjusts master offset — both the hardware encoder *and* the
on-screen MZ drag. The switch is mode-scoped, not chord-order-dependent: holding Func
then Song, or Song then Func, both reach master density while the Density band is
active. This replaces the old `densityModeArmed` flag; `Song` no longer has a separate
DensityMode band — Musicality/Selection is reached via the MOD sub-page key within
sticky DENSITY mode.

**Why MOD, not FX:** the `FX` section key is the effect-picker key (`Func+FX` =
track insert picker, `Func+Song+FX` = master FX picker). Because `ButtonLayers`
remaps `Section`→`MetaSection` whenever `Func` is held, a `Func+FX` press is
consumed by the picker and can never reach a density-entry handler — so the
original `Func+FX` density gesture was unreachable. `MOD` (canonical index 4) is
unused under both `Func` and `Song`, so it hosts density entry + the sub-page
toggle cleanly. The symmetric velocity sticky lives on `AMP` (index 3) for the
same reason. Both entries are dispatched from the `MetaSection` case in
`PluginEditor.cpp`; the bare re-press toggle and exit are dispatched from the
`Section` case (the toggle key is pressed without `Func`).

**Routing SSOT:** `densityEditsMaster(UiState)` (returns `ui.songHeld`) is the single
predicate consulted by every write path. `densityWriteTarget(ui, field, focusedTrack)`
resolves the per-track index and absorbs the bank-paging formula. Both live in
`MetaBand.{h,cpp}` — all three decision sites (mouse in ManipulationZone,
encoder in PluginEditor, writeMetaField guard) call these rather than inlining
their own check.

**Per-device delta extraction:** The on-screen rotary uses JUCE `RotaryHorizontalVerticalDrag`,
which accumulates an absolute value from the drag origin and ignores mid-drag `setValue`
calls. Master writes therefore use an incremental-delta approach: `ManipulationZone`
tracks `lastSlotValue_[slot]` per drag and applies `(v - last) / 100.0f` additively
to `masterDensity`. The hardware encoder provides a raw relative delta (`rawDelta / 128`).
Both clamp to `[-1, 1]`. Per-track writes continue to use the absolute slot value directly.

### 39.6 Density cell visual

The agreed single-visual readout per rotary cell:

- **Rotary pointer** = per-track density amount (your hand; always editable).
- **Arc** = master offset applied additively (length and direction = how much/
  which way the global pushes the track value).
- **Tick** = effective (audible) value. Normally sits at the arc's far end.
  When `track + master` exceeds the clamped range, the tick **sticks at the
  rail (1% or 100%)** and the arc continues into a **dimmed overshoot zone** —
  showing "turning but pinned" with no silent deception.

Reuses the `ReferenceMark` / `RingMode` visual vocabulary from the swing band
(§19.2), extended with the arc segment and the dimmed overshoot path.

### 39.8 Sticky-mode mutual exclusion invariant

**Invariant:** `densityStickyMode` and any *foreign cluster scope* being held
(`trackHeld / phraseScopeHeld / sceneHeld / morphHeld / muteHeld / fillHeld`)
are mutually exclusive at all times.

This is what lets `resolveMetaBand` (MZ axis) and `resolveActiveLayer` (grid
axis) remain consistent without merging into one resolver: because the conflicting
state is never allowed to exist, they can never disagree about which is active.

**Entry guard** — `Func + MOD` (chord) only enters sticky mode when no foreign
cluster scope *other than `Song`* is physically held. `Song` is exempt because
`Func + Song + MOD` is the master-page entry; any other held scope ignores the chord.

**Discharge guard** — in `dispatchDown`, pressing a foreign cluster scope key
while density is already sticky calls `escapeDensitySticky()` + `refreshMetaBand()`
*before* the scope's normal handler runs. On release, the user lands at Base, not
back in density mode.

**Density's own keys** (exempt from the discharge rule):
- `Song` — master-density offset in the Density band
- `Func` (double-tap) — universal escape / exit sticky mode
- `Nav` (↑↓←→) — bank flip
- MOD section key (index 4) — Amount/Musicality/Selection sub-page cycle

**Section-select exit** — pressing any section key *other than MOD (index 4)* while
density sticky is active exits the mode (same result as `Func` double-tap escape) and
selects the pressed section, routing encoders back to that section's machine params.
`sectionSelectClearsDensitySticky()` in `MetaBand.cpp` is the canonical predicate
for this rule; `PluginEditor.cpp` Section handler calls it right after
`consumeDensityStickyKey()`.

These are centralised in `consumeDensityStickyKey()`, `escapeDensitySticky()`,
and `sectionSelectClearsDensitySticky()` in `PluginEditor.cpp` / `MetaBand.cpp`;
do not duplicate inline.

### 39.9 Prev-dep interaction

A density-killed trig must take the **not-fired branch** of the existing evaluator
(so `lastStepFired_[i] = false`). This means density thinning is visible to
prev-dep trig conditions on subsequent steps — thinning a step can cascade to
silence a step that was conditioned on it firing. This is intentional and is part
of what makes metric-weighted thinning feel musical (gutting offbeats also removes
the syncopated deps that hang off them).

### 39.10 Live velocity overlay (AMP sticky mode)

A **durable, per-track** overlay that modulates note velocity at emit time using
the same `MetricGrid::metricWeight` primitive as Dmetric. Unlike the removed
bake/print generator, it never writes into phrase steps — it is computed live on
every trig fire, so phrase-length co-primes with the bar never produce drift.

**State (TrackKit, serialized v21):**

| Field | Type | Default | Meaning |
|---|---|---|---|
| `velMode` | `VelMode` (Off / Bar / Phrase) | Off | Metric frame |
| `velBlend` | `VelBlend` (Replace / Mix) | Replace | Replace overwrites authored vel; Mix swings around baseline |
| `velDepth` | float [0,1] | 0.6 | Depth of velocity swing |
| `velCenter` | int [1,127] | 90 | Velocity at the metric peak (weight = 1) |

**Engine formula (emitTrig lambda):**

*Bar mode:*
```
ppqInBar = fmod(nextTriggerPpq, barPpq)          // position within the coreTime bar
w        = MetricGrid::metricWeight(ppqInBar, barPpq, num, den)   // [0,1]
```

*Phrase mode:*
```
ppqInBar = fmod(stepIdx * divPpq, barPpq)         // bar grid anchored to phrase start
w        = MetricGrid::metricWeight(ppqInBar, barPpq, num, den)   // same weight function
```
Phrase mode avoids drift when the phrase length is co-prime with the bar: the bar
grid is measured from phrase step 0 rather than from the global playhead. The
accent cycles within the phrase regardless of where in the song the phrase sits.

*Blend:*
```
maxSwing = min(velCenter−1, 127−velCenter)
delta    = round(velDepth * maxSwing * (2w − 1))

// Replace: vel = clamp(velCenter + delta, 1, 127)
// Mix:     baseline = (step has authored velocity) ? authoredVel : velCenter
//          vel = clamp(baseline + delta, 1, 127)
```
**Replace** supersedes all authored velocity. **Mix** swings around `velCenter`
when the step has no authored velocity, so Mix ≡ Replace on flat material; it
diverges only where steps carry explicitly authored velocities — the delta is
added on top of the authored value in that case. The Euclidean generator's
baked accent is authored content and is treated accordingly.

**Entry:** `Func + AMP` section key (index 3), dispatched from the `MetaSection`
case (ButtonLayers remaps `Section`→`MetaSection` under `Func`, so the entry
cannot live in the `Section` case). (Section keys never take a double-tap
gesture — PRINCIPLES §17; new section-key gestures use `Func + section` or
long-press.) AMP re-press while vel-sticky is active cycles sub-pages;
disabled sub-pages are **skipped** (see below). Nav keys page between bank 1-8 /
9-16. Foreign cluster scope keys (Track / Phrase / Scene / Morph / Mute / Fill)
exit vel sticky, parallel to density sticky's §39.8 invariant. Density sticky
and vel sticky are mutually exclusive; entering one exits the other.

**MetaBand sub-pages (8 per-track rotaries, paginated like density):**

| Sub-page | Controls | Range | Enabled when |
|---|---|---|---|
| Depth | velDepth per track | 0–100 % | any track in scope has velMode ≠ Off |
| Center | velCenter per track | 1–127 | any track in scope has velMode ≠ Off |
| Mode | velMode per track | Off / Bar / Phrase (stepped) | always |
| Blend | velBlend per track | Replace / Mix (stepped) | any track in scope has velMode ≠ Off |

**Skip-disabled sub-pages (general rule).** Sub-pages whose content is not
applicable are skipped during the AMP re-press cycle and on entry. When all
tracks are Off, entry lands directly on the Mode page. When at least one track
is enabled, entry lands on Depth. This rule is general — future modal bands
with conditional pages follow the same skip behaviour. Pages are never greyed
and still visible; they simply do not appear in the navigation cycle while
inapplicable.

**"Available-but-inert" modal-entry affordance.** The `Func+AMP` key uses a
distinct third visual state (between normal and active) when the band exists
but every track in scope has velMode == Off. This signals "the overlay is
configurable here but has nothing active yet" — a reserved `CellState` token
(`ModalEntryInert`) differentiates it from the normal idle state. The same
affordance applies to `Func+MOD` (density) and future scope-qualified
modal-entry keys.

**Func+Fill is freed** — the old bake/print accent generator gesture is removed.

### 39.11 Melodic generator (Generator Hub)

The melodic generator is a **deterministic print** tool — the Euclid model
(§5.5): turning encoders previews live, commit (`Y`) bakes ordinary
hand-editable steps, cancel (`P`) restores the stash. It is **not** a stochastic
engine (NON-GOALS §2); a `seed` parameter makes variation *reproducible*, so the
same seed+params always yield the same notes (PRINCIPLES §11, §14).

Entry: the Generator Hub (`3` held ≥350 ms, ROADMAP 9.10) gains a **Melodic**
cell. It consumes `effectiveKeySig()` (§4.10) and writes a monophonic line
(`step.trig` + `trigOverride.notes[0]`). Encoder parameters:

- **Range / octaves** — span of the line.
- **Core bias** — triad core ↔ pentatonic core ↔ full scale, weighting note
  choice by `coreTier` (brightness-window centrality). Low = arpeggio-like on the
  triad core; high = uses passing tones. The blue note follows the chosen core
  (`blueNoteFitsCore`): available across all seven modes at full scale, the five
  pentatonic-rooted modes at the 5-note core, and only Mixolydian/Dorian/Aeolian
  at the 3-note core.
- **Contour** — ascending / descending / arch / random-walk *shape*
  (deterministic from the seed).
- **Density** — which steps fire (may reuse the Euclidean generator, §5.5).
- **Step-vs-leap** — stepwise-within-scale vs leaps to chord tones.
- **Seed** — deterministic variation.

Output is indistinguishable from hand entry and fully editable afterward.
`src/core/MelodyGen.h` is pure and unit-tested (determinism, in-`pcMask`,
core-bias histogram).

### 39.12 Harmonic voice-mover (Generator Hub, sticky)

A hands-on **authoring convenience**, not a chord/progression picker. There is
no chord-quality menu, no progression template, no library, no auto-voicing
engine: you **move voices and listen**, with the scale keeping you in key and an
audition letting the ear lead. (A preset-style "simpler" chord generator, if ever
built, is a *separate* thing.) This stays clear of NON-GOALS §2/§13 — it is
manual and deterministic, and the scale constraint is a chosen tool, not
auto-correction.

Entry: a **Harmonic** Generator Hub cell → sticky `Overlay::Harmony`. The model
is a **progression of K chord slots**, each ≤4 voices (the `kMaxNotesPerStep`
ceiling — triads and 7ths). The 8 encoders:

- **Left 4 = the voices.** The four voice slots are re-laid as **one display
  spanning four columns**, each column a voice (V1 bass … V4 top). The three rows
  are the **previous / current / next chord** of the progression, so a column
  reads that voice's motion across the progression and `CUR` scrolls the chords
  so the current one stays in the bright middle row. Each voice's **knob is
  centred on the current row** (the arc reads above and below the chord view); a
  bare turn steps that voice **diatonically** through the in-key ladder, an
  off-detent removes the voice and the first empty column adds one, and
  **`Func`+turn = a chromatic borrowed tone** (a per-voice semitone offset that
  slides with the chord under the collective moves; the current row tints). A
  voice that lands back in-scale snaps to a rung (offset cleared).
- **Right 4 = structure + collective moves.** Length (K slots) · Cursor (select
  active chord) · Transpose the selected chord by scale-degree (all voices,
  in-key) · Octave-shift the selected chord.

The progression **starts at length 1** and grows from there. Growing **clones
the previous chord** into a genuinely-new slot (so motion starts at zero), and
shrinking is **lossless** — a slot re-grown within the high-water reach is
restored as authored rather than re-cloned. **Audition:** any cursor-chord change
(a voice edit, a collective move, or a cursor move) **immediately re-strikes**
the chord through the live-note engine, so slow-turning an encoder previews each
landing. (There is no separate idle context loop — the live preview printed onto
the steps carries the musical context when the transport runs.) **Placement:**
commit prints the K chords **one per bar** of the in-scope time signature (chord
*k* → bar *k* downbeat), falling back to even spacing across the steps when the
phrase has fewer bars than chords; the result is ordinary ≤4-note steps you
reposition with step-move and hand-edit freely. `src/core/HarmonyGen.h` carries
no chord theory — it is a scale-constrained multi-voice step buffer (ladder rung
+ semitone offset per voice).

*Planned follow-on (existing-rhythm placement):* a mode that keeps the track's
existing trigs where they are and assigns each the chord of the bar it falls in
(harmony follows the bars, rhythm preserved) — the harmonic twin of the melodic
SRC "Keep" transform.

## 40. The Deck Engine — Record, Loop, and Tape

> **Status: design only.** This section is the contract; the implementation is
> its own later milestone arc (ROADMAP). Nothing here ships with the Part-A
> bug/feature round that accompanied its writing.

### 40.1 One engine, three faces

Lockstep already has two capture machines (§29): **Record** overwrites a
volatile buffer on a recorder trig; **Loop** is a circular sound-on-sound
looper with an always-on console. A play-testing round asked for the third
thing both of them gesture at and neither is — a **tape**: a linear, multitrack
medium you punch into against the song's own timeline.

The answer is **not** three machines with three code paths. Record, Loop, and
Tape are **three faces of one deck engine**:

| Face | Medium topology | Driven by | Product |
|---|---|---|---|
| **Record** | linear, overwrite | recorder trigs (§30) | a volatile pool buffer to *process* |
| **Loop** | circular, layered | console verbs, quantized edges (§25) | a looping performance part |
| **Tape** | linear, layered, position-addressed | console verbs against the project timeline | a take on the song's timeline |

Every deck has **four sub-tracks** and **defaults to one stereo sub-track**.
This default is a hard requirement, not a nicety: a freshly loaded Record or
Loop must behave *exactly* as it does today — same console, same slots, same
memory, same CUJ. The 4-track depth is opt-in, reached through the deck's
console, and Record may well never present it.

That default is what makes the unification honest rather than a mega-machine
(§24: a machine is named by its role, one bare word). The three names remain
three roles the performer reasons about; they simply stop being three
implementations of overdub, undo, punch, varispeed, and capture-close.

**What this replaces.** An earlier draft coordinated several Loop *machines*
into a "loop group" so four tracks could arm and punch together. That is
rejected: cross-machine grouping puts the coordination state in a place no
single machine owns, invents a second launch-timing conversation (§25), and
makes the surface answer "which tracks are in the group?" before it can answer
"what am I recording?". A 4-track deck keeps the coordination **local** — one
machine, one arm state, one punch. The §15 "batch selection is free" gesture
stays available if a genuine cross-machine need ever appears.

### 40.2 One project timeline

A tape is made of **position**, and Lockstep's transport has deliberately had
none: tracks advance phase, patterns loop, scenes are launched, and the
corollary of PRINCIPLES §25 is that there is no per-track `Stopped` state and no
song ruler to locate on. Fence #1 refuses a stored arrangement. So where does a
tape's position come from?

**From the transport, which gains it.** Split what the transport authority
carries into two facts:

- **Rate and grid** — BPM, the launch quantum, phase. This is what §25 has
  always owned.
- **Absolute position** — how far into the performance we are, in samples/ppq
  from a zero.

The second is not a rival authority; it is the same authority carrying one more
field. Lockstep already computes it (the internal clock counts samples from
transport start). The amendment (PRINCIPLES §25) is to make it **first-class,
single-sourced, and shared**:

- **Hosted in a DAW, the host timeline *is* the tape timeline.** The host is
  the root of transport (PRINCIPLES §3): its play head is our position, its
  locate is our locate. Winding the tape *is* dragging the host playhead. This
  falls out of §3 rather than fighting it, and it is the single strongest
  argument that the tape belongs inside Lockstep rather than beside it.
- **Standalone, Lockstep exposes its own absolute clock** as that same field.
  The standalone gains a locate; it does not gain a second timeline.

**Locate is a transport act, and patterns absorb it deterministically.**
Position is not pattern state. On a locate, each track re-derives its phase as
`position mod trackLength` — the same arithmetic the clock already does every
block, evaluated once at the new position. Nothing is "recalled": scenes,
mutes, kits, and P-locks are *state*, and state does not travel with position
(that is the fence-#1 line, and it is why a locate never reproduces "what the
song sounded like there" — only what the tape sounded like there). Pending
launch edges re-derive against the new position's grid.

**The Tape is chase-locked to musical time.** The organizing lens is a studio
**master reel-to-reel at the centre of the split-desk console** — Looper is
performative, the 4-track is a multitrack recording flow, and the Tape is the
master reel. A studio deck chase-locks to a timecode master; here the master is
the sequencer, and the reel's head position is a pure function of **musical
time**:

    headPos = ppq × K

`K` (the **calibration**, samples-per-ppq) latches from the transport tempo at
the **first record onto an empty reel**, and `CLEAR` resets it (a fresh reel is
uncalibrated and chases at unity, so an empty deck behaves exactly as a 1×
timeline). Because `ppq` is continuous and `K` is constant per reel, position is
bar-aligned **by construction** under any tempo history: locate to a bar, punch
"re-record bars 33–41", cue a marker — they land on the musical position they
name, with no offset to manage, no tempo map, no sync flow, and no per-marker
bar stamps (musical position *is* reel position, so markers are bar-true for
free, §40.4).

**Tempo ≠ calibration is varispeed, openly.** The chase rate is `r = K /
samplesPerPpq(current)`; `r == 1` exactly at the calibration tempo. Away from
it the reel reads *and writes* at `r` through the §40.10 head law — pitch
follows tempo, as tape does when the capstan changes speed. This is accepted,
not hidden: at the calibration tempo the whole path is unity and bit-exact (the
no-tempo-change majority pays nothing), and away from it the console/strip shows
the chase ratio (e.g. "×0.5"). Recording below calibration writes at `r < 1` and
downsamples into the reel — highs above the shifted Nyquist are permanently lost
(honest chase-locked physics). That is allowed (expert-first) and surfaced; a
2× oversampled reel (§40.10 medium rate) is the documented remedy if it ever
bites, not built pre-emptively.

*Why chase-lock and not a real-time reel* (the position integrating in real
time, tempo landing only in the audio, tape not caring how fast the band plays):
because **this is a sequencer**, and most of what a deck records is *sequenced*
material driven by the same clock. A real-time reel desyncs the sequencer
timeline from the reel timeline the instant tempo changes, and then the deck's
bread-and-butter CUJ — "re-record bars 33–41" — needs the two re-aligned by
hand (a manual offset the user must track). Chase-lock makes that CUJ correct
with zero ceremony. The alternative was considered twice and rejected on exactly
this ground; the reasoning is recorded here so it is not re-litigated. The
"fun" tempo tricks (detune rides, conform, free varispeed as a *performance*
control) are the looper's domain and the partner app's — the master deck has no
performed varispeed UX.

**Winding is a standalone-only affordance.** Scrubbing and FF/RW move the
**transport** with the reel (reel-is-truth: a wind commits `Clock::locate` so
play and punch start where your ear found the point). Hosted, a plugin cannot
move the DAW's playhead, so the jog and wind affordances are simply **not
offered** (cells absent, jog inert) rather than made to half-work — one rule,
"transport winding exists standalone only." While winding, no writes occur; the
read runs through the same bandlimited head as playback, so a wind is audible
(chipmunk one way, growl the other) and stops honestly at the reel's leader.

**Multiple decks share the one timeline.** Two Tape machines are two media
addressed by one position, like two tracks of one reel. There is no per-deck
timeline and no timeline negotiation.

### 40.3 The deck engine

**Sub-tracks.** Four, each stereo (§40.8), each with `level` / `pan` / `mute`
as ordinary machine params (so they are OEB-resolved, P-lockable, scene-able,
Morph-able like every other param — §7). Sub-track count is a deck param,
default 1.

**The medium.** For Tape, a **fixed-length RAM medium**, settable, allocated
lazily. An honest, visible limit is the OP-1 lesson: a tape you can fill is a
tape you make decisions on. At 48 kHz:

| Medium length | 1 sub-track (f32) | 4 sub-tracks (f32) | 4 sub-tracks (i16) |
|---|---|---|---|
| 1 min | 23 MB | 92 MB | 46 MB |
| 5 min | 115 MB | 461 MB | 230 MB |
| 10 min | 230 MB | 922 MB | 461 MB |

Default 5 minutes; a 16-bit medium option halves it, and both depths ship. The
length and the depth are **per-deck params**, not a project-wide budget: a deck
is the thing you fill, so the deck is where the limit is legible, and two decks
that need different lengths should not have to negotiate. (A project budget can
be added later over the top; it cannot be subtracted from.) **Lazy commit**: the
buffer is allocated without zero-fill and never read past each sub-track's
recorded high-water mark, so an unused sub-track and an unrecorded tail cost
address space, not resident pages. (This is the same discipline A5 brings to
the volatile slots, and the two share the used-length rule.)

Loop keeps the circular medium it has; Record keeps the volatile pool slot it
writes. The engine differs in *addressing*, not in machinery.

**A Loop's four sub-tracks live in one wide pool slot.** A single-sub-track Loop
records into a 2-channel volatile slot, exactly as today. A four-sub-track Loop
records into **one 8-channel slot** — sub-track *k* is channel-pair *k* — rather
than four separate slots. This keeps a deck's sub-tracks together as one thing
that arms, punches and promotes together (the §40.1 argument against loop groups,
restated at the storage layer), and it matches the channel arithmetic of §40.7's
stereo-sub-track medium literally. The cost is a **member sub-index**: a reference
to "sub-track 2 of this slot" is `(pool entry, channel-pair)`, so `SampleId`, the
pickers, and the read path each carry a channel-pair alongside the entry. That
sub-index is a *volatile-slot* concern only — **promotion splits the slot into
ordinary 2-channel files** (§40.7), after which every member is a plain pool
citizen with no sub-index at all.

**Loading a sample onto a sub-track copies it in.** A deck sub-track is audio you
overdub, punch, erase and undo — it is tape, not a reference to a file. So loading
a File (or another pool entry) onto a sub-track **decodes its PCM into the
medium**; the disk file is untouched, and the deck can be performed on
immediately. This is the same "the medium is content, not a pointer" line that
makes promotion the only way audio leaves a deck (§40.8): a sub-track that merely
*referenced* a file could not be overdubbed without either a hidden
copy-on-write state on every write path or a lie about what a deck is. The copy is
the honest cost of the tape paradigm, and it is bounded by the deck's own window.

**Fitting to tempo is an explicit verb, not a load-time surprise.** A deck's
medium is dumb audio played by a varispeed head — it does not tempo-track the way
a Player does (§29). So when a loaded sample's musical length is known (carried as
`sourceBars`, or detected), the deck does **not** silently resample it to the
window. Loading is always native. A separate console **`FIT`** verb renders the
sub-track to the deck's window through the Bungee stretch engine (§29's
`IStretchEngine`), once, on the message thread — a destructive act you chose,
like every other edit to a deck's content. Native-in, fit-on-request keeps the
one surprising operation (a stretch that changes the audio) an explicit gesture.

**Sub-tracks share the deck's window.** A four-track Loop is four sub-tracks of
*one* loop length, and punch/overdub target the armed sub-tracks within it.
Polymetric looping — sub-tracks of differing lengths inside one deck — is
designed out rather than deferred: the tape-loop paradigm the face is named for
is a single reel of a single circumference, and a performer who wants polymeter
already has the honest expression of it, which is several loopers. That keeps
one window, one phase, one seam per deck, and it keeps the single-sub-track face
bit-for-bit what it is today.

**Input is a tap-only pull matrix.** Each sub-track selects its own source from
the existing `input_source` enum (§27): `None | External | Master | Track N`.
The deck's console shows this as a Route-style grid — sub-tracks down, sources
across — and it reuses the tap-fork edge class wholesale: read-only, same-block,
already in the topological sort, already cycle-checked at assignment.

Concretely, this is one param slot per sub-track. Sub-track 0 keeps the
canonical `input_source` slot (id `input_source`); sub-tracks 1–3 carry appended
slots `input_source_2/3/4` (`inputSourceSlotId(sub)`), default `None`. The
append is deliberate: nothing renumbers, params serialize by string id, and a
single-sub-track Loop declares only slot 0 — so today's Record, Loop and Route
are byte-identical on disk and in the graph. Before these slots existed, only
sub-track 0's SRC cell did anything (the per-sub selector was declared with no
backing slot); declaring the slots is the whole fix — the fill path
(`fillDeckSubTrackInputs`) and the console already resolve each sub by id.

There is deliberately **no push side**. A `Trk6-1`-style output destination
(source track pushes into deck sub-track 1) was considered and rejected: the
CHANNEL "Out" enum would grow from `{Master | Track N | Off}` to
`{Master | Track N | Track N sub-track M | Off}` — 4× the entries, on the one
selector every track has — and it would create a second edge class into the
topo-sort with its own cycle rules. The pull model expresses every routing the
push model does, from the place that already knows how to express it.

**Overdub and undo** are the looper's, extended across the sub-tracks: a take is
a stack of layers, `UNDO` is one level deep, and a new layer begins at each
record edge. On the 4-track Loop, overdub fans out to the **armed** sub-tracks —
sub-track 0 is armed by default (a single-track looper session never thinks
about arming and behaves exactly as before), and the TRACKS console arms the
rest. Undo is **whole-deck**: it restores every sub-track to the pre-overdub
snapshot in one act, not per-track.

**The seam is spliced, never crossfaded on playback.** A loop recorded from a
performance is discontinuous where its end meets its start, and that jump is a
click. It cannot be fixed at the read: the read is circular, so just after the
wrap the kernel's taps reach backwards across the seam, and the discontinuity
lands *inside* the read window no matter what gain the player applies on its way
out of the loop's end. It is fixed in the **content**, once, at close — which is
also why every other reader of that content (a Player on the same pool slot, a
promoted WAV) gets a clean take rather than a private fix.

The requirement is exactly one thing: *whatever preceded the take's first sample
in the recording must also precede it in the loop*. So the deck keeps a rolling
few-millisecond **pre-roll** of its input, freezes it when a take begins, and
fades it into the loop's **end** at close (`dc::spliceLoopEnd`). Post-roll — the
take's continuation, faded into the loop's *head* — buys the same continuity and
costs the downbeat, which is the one sample a loop cannot spare; it also defers
the close until the tail arrives. The pre-roll splice alters only the ring-out
and lands atomically. This is what samplers do, and why they crossfade the loop
end.

**Punch** is a span-replace expressed as a layer: the punched span is recorded
into a fresh layer whose contribution is zero outside the span. Edges are
quantized by the §25 authority (double-tap = instant, the universal override).
Punch-out is therefore never destructive — undo restores the take, which is what
keeps fence #8 satisfied while still *feeling* like tape.

**Bounce** renders sub-tracks 1..N down to one, in place or to a new deck; it is
the deck's `Confirm`-gated commit. **Promote** hands the take to the pool via the
9.18 machinery (`promoteVolatileToFile`), producing a take-group (§40.8).

### 40.4 Markers are dumb

Markers are **navigation points on the timeline**: a position, an ordinal, an
optional auto-label. They are dropped

- **automatically**, at every Scene or Song switch that happens *while a deck is
  recording* — so a take you performed by launching scenes comes back with the
  launches marked; and
- **manually**, by a console marker cell.

They are metadata, survive every audio operation (overdub, punch, bounce), and
are removed only by an explicit delete. A marker stores **position only** — no
tempo or bar stamp. Under chase-lock (§40.2) musical position *is* reel
position, so a marker is bar-true for free; stamping the bar into the marker
would be complexity for a case chase-lock already handles.

**Markers never fire anything.** A marker does not recall a scene, and a deck
never emits a scene/song change during playback. This is precisely where fence
#1 sits: recording *what you performed* is a recording; replaying position-keyed
launches would be a stored arrangement, and the whole instrument would tilt into
the session view we have twice refused. A marker tells you where the chorus was.
You still launch the chorus.

### 40.5 The console

The deck is a console machine (`consoleMode`, §29/§35): a focused deck turns its
16-button step grid into an always-on console. Because a deck now has more to
say than one loop did, the console **pages** (nav left/right), and a
single-sub-track deck simply has fewer pages:

- **`DECK`** (always) — the current looper layout: transport + length on the top
  row, momentary performance (beat-repeat, tape FX) on the bottom.
- **`TRACKS`** (when sub-tracks > 1) — four rows of four: `ARM · MUTE · SOLO ·
  SRC` per sub-track. `SRC` opens the tap matrix (§40.3), which is the Route
  machine's routing grid, reused.
- **`MARKS`** (Tape face) — eight marker cells. Tap = cue (quantized), double-tap
  = cue now. That is the §25 instant-override family, not a new gesture.

Elsewhere in the grammar: **nav = rewind / fast-forward**, held; **one MZ
encoder is the jog** (scrub per §40.2); the remaining MZ slots carry medium
length, varispeed, punch in/out, monitor mode, level. Every one of these is an
existing key doing an existing thing to a new scope — no bespoke button (§2).

### 40.6 The timeline strip

A tape you cannot see the position of is a tape you cannot punch into. The
answer is **not** a canvas (fence #5) and not a mode: it is a second
**inspector-class always-on chrome strip** (the 9.11 precedent, §6.11.3),
placed under the context inspector, carrying:

bars and beats · every deck's markers · the position cursor · the armed punch
region · each deck's recorded extent.

It is **display-only, forever.** You never click it, drag it, or edit in it —
interaction is the console and the jog encoder, both of which a hardware surface
has. Built from a pure `buildTimelineModel(...)` beside `buildInspectorModel`,
so a controller display can render it (PRINCIPLES §19, dual-target).

Its **hardware proxies** — the performable subset that must survive with no
screen (§19) — are console-cell chrome: marker-approach (marker cell brightens
as the cursor nears it), punch-armed (in/out cells pulse), region-active (punch
cells solid while inside the span), medium-full warning (transport cell reddens
in the last 10 %). Everything the strip adds beyond those is enrichment.

### 40.7 Take-groups and the channel policy

**Take-groups.** Promoting a 4-track take writes **N+1 files**: one WAV per
non-empty sub-track plus a **materialized downmix**. They are linked into a
**take-group** — a new, explicit group id stamped on each pool entry, distinct
from the existing origin groups (`FILE / STREAM / RECORD / LOOP`, which is what
`SamplePool::groupOrdinal` numbers within). Each member keeps an ordinary
hash-keyed persistent ref (§12): a take-group is a *link*, never a container of
PCM.

Pickers then behave by capability (9.18 `sampleClass`):

- **Deck-class pickers** (Loop, Tape — machines that support four sub-tracks) see
  the group as **one entity plus its members**: the whole take, plus each
  non-empty sub-track, plus the downmix — **up to six** for a full four-track
  take. Picking the group loads all sub-tracks; picking a member loads that one.
- **Sample-class pickers** (Sampler, Slicer, Player, Stream) see the **members
  only**: each non-empty sub-track plus the downmix — **up to five**, no group
  entry, because a non-deck machine has one place to put audio. Every member is an
  ordinary pool citizen you can slice, stretch, or stream.

This is the 6/5 rule: a deck offers the group and its parts; everything else
offers just the parts. Both counts are ceilings — an empty sub-track contributes
no member.

**Implementation.** The enumeration is a pure function (`buildTakePickerRows`,
`machine/TakePicker.h`) over the pool and the machine's `isDeckClass()`, so it is
unit-tested and shared by every picker surface. Both sides are live: a promoted take's members read as a labelled cluster
("Take N sub k / mix"), and a deck-class picker's group *entity* loads the whole
take onto the deck's sub-tracks on pick (`loadTakeGroupToDeck`). Loading copies
each member's PCM into its sub-track's channel-pair (native), and `FIT` (Func+SRC
on the TRACKS console) renders a loaded sub-track's source stretched to the window,
pitch-preserved, through the Bungee engine — native in, fit on request.

**Channel policy** (stated once, here, because it has been implicit and
accidental):

- **The engine boundary is a stereo invariant.** Deck sub-tracks, players, and
  volatile capture emit and consume two channels. Mono material reads as two
  identical channels. This makes the `min(2, …)` clamps scattered through
  `LoopMachine` and friends *policy* rather than defensive accident.
- **Storage keeps the native channel count.** A mono file stays 1-channel in the
  pool and on disk; the duplication happens at the engine boundary, not in RAM
  and not in `samples/`.
- **The deck medium is therefore 4 stereo sub-tracks** (8 channels), with empty
  sub-tracks effectively free under lazy commit. A live four-track Loop holds
  these as one 8-channel volatile slot (§40.3); promotion is where the eight
  channels become four ordinary 2-channel files. The wide slot is an
  implementation of the stereo invariant, not an exception to it — each
  channel-pair is a stereo sub-track, and nothing reads across the pair boundary.

### 40.8 Serialization

- **Tape audio is never PCM in project state** (§12). A take is saved by being
  promoted (files + hash refs) or it is lost with the session, exactly like
  today's volatile buffers. This is the fence-#8 answer restated: the medium is
  a reference-holder, not a bag of bytes.
- **The marker lane is NOT serialised** (implementation decision, 2026-07). An
  earlier draft persisted the markers independently of the audio, but a marker is a
  navigation point *into a take*, and the take's audio does not survive a session
  unless it is promoted (above). Markers pointing into a reel that reloaded blank
  are noise, so the lane lives and dies with the session's audio. If a promoted
  take ever carries its own timeline, markers can ride with it then — additively.
- **Deck config** — sub-track count, per-sub-track tap + level/pan/mute, medium
  length and bit depth — is ordinary machine param + kit state.
- **Take-groups** serialise as the group id on each member entry; missing files
  surface through the 9.18/9.19 relink banner unchanged.

### 40.9 Gate and fences

Against the 9-rung gate (PRINCIPLES "How to use this document"):

1. **Grammar** — no new keys. Console cells, the five verbs, nav, MZ encoders,
   the §25 double-tap instant override.
2. **Cost** — arm/punch are rung-1 (single press, very common in a take);
   sub-track routing is rung-3 (console page + grid, rare, set once).
3. **Mastery** — the practised skills are *punch timing* (hitting the edge with
   the quantum, or beating it with the instant override), *jog cueing* (finding
   the seam by ear), and *bounce economy* (four tracks is a budget, and a fixed
   medium makes committing a skill).
4. **Surface** — grid and scope changes only; the timeline is chrome, not a
   canvas; the jog is an encoder, which hardware has.
5. **Equality** — a deck's output is an ordinary track output (§6): FX, mute,
   scene, Morph, and `Out` all apply. A MIDI-out track can sit beside it and
   nothing special-cases either.
6. **Resolution** — every deck control is a machine param under OEB (§7).
7. **Sections** — the deck is a machine (it *captures*: §9's "generate or
   capture"); its processing is the ordinary post-machine chain (§14).
8. **Chrome** — the strip, the console cells, and the hardware proxies ship with
   the behaviour (§10).
9. **Fences** —
   - **#8 (destructive tape)** — cleared. Layers + undo + span-replace-as-layer;
     the medium holds refs on save. Nothing overwrites nothing.
   - **#1 (arrangement)** — cleared by the marker rule: a deck records audio and
     marks where you launched; it never launches. The moment a marker fires a
     scene, we have built the thing we refused.
   - **#5 (canvas)** — cleared: the strip is a fixed-height display, interaction
     lives on the 16 cells.
   - **#10 (dual project)** — untouched: one timeline, one set.

### 40.10 The heads — one signal law at every rate

The deck's audio machinery is three head types over a medium, and the design
point is that each obeys **one law across the whole signed rate axis** — above
and below native, through zero, and in reverse — instead of special-casing
"normal speed". Rate here is medium samples advanced per engine sample; scrub,
varispeed, tape-stop, and reverse are all just values of it.

- **Read head** — bandlimited fractional read: the 9.25 polyphase
  (windowed-sinc, rate-aware cutoff — reading faster than unity scales the
  source spectrum up, so the cutoff drops to Nyquist/rate before anything
  folds). Direction-agnostic by construction; rate 0 holds a sample. Circular
  media wrap the kernel taps, linear media clamp them.
- **Write head** — the transpose: a bandlimited **scatter-add** distributing
  each input sample across the same kernel at the head position. The cutoff is
  again rate-aware (above unity the deposit is sparse and the cutoff tracks it;
  at or below unity the full-band kernel is already correct — the medium's own
  sampling does the rest). The deposit is scaled by **|rate|**: kernel density
  on the medium is 1/rate, so this one factor makes the write
  amplitude-invariant (which is what tape does — flux does not depend on
  transport speed), makes a stalled head write *nothing* rather than pile
  unbounded energy onto one spot, and makes a scrub through zero behave — the
  write fades through the turnaround for free.
- **Erase head** — replace = erase + write, as on tape. Additive overdub needs
  no erase; *replace at varispeed* attenuates the medium under a rate-aware
  window running just ahead of the write deposit. (Integer-unity replace —
  today's initial record — is the trivial case: the window is one sample.)

**Heads are free-standing, and a medium may carry many.** A head is an object
*over* a medium, not a field *of* one: it owns a signed fractional position and
a rate, nothing else. Any number of read heads may sit on one medium at once,
and a read head may be **slaved to another head at a fixed offset** rather than
run its own position. That is the whole of what a tape delay is: one write head,
N read taps trailing it by N distances, each tap's output summed to the output
and (optionally) back into the write head's input. Lockstep's decks use one read
head and one write head, but the interface must not assume it, because the
partner app's delay/echo idiom is exactly this and it is a *configuration* of
these heads rather than a second engine.

Consequently the heads expose a **per-sample step API** alongside the block API.
A delay's regeneration path is a feedback loop through the caller — write, read
the taps, mix, write again — and the loop closes at whatever granularity the
caller steps at. Feedback is therefore **caller-side**, never a head property:
the core owns the signal law, the host owns the topology. A block-API caller
gets block-granular tap latency (fine for a deck, wrong for a short delay); a
per-sample caller gets none.

The audit of shipped code against this law (2026-07-09) found it partially
kept: reads above unity are law-abiding in the sample players but the looper's
Hermite reads alias whenever its rate exceeds 1 (sync-length mismatch, resync
catch-up); the scatter write's cutoff selection is correct in both directions
but the |rate| gain factor is missing (a half-speed overdub lands +6 dB, a
rate-2 overdub −6 dB, and a stalled write is unbounded); and the initial
record write is integer-unity only. **ROADMAP 9.28** closes the read and gain
gaps in place; the erase head is deck-arc work — landed by the Phase 11 tail's
**chase-locked Tape** (§40.2), whose `r ≠ 1` record is exactly *replace at
varispeed* (erase-ahead + |rate| scatter). The Tape reel additionally supports
an **i16 medium depth** (`medium_depth` param, Tape only — the Loop stays a
float volatile-pool slot); `dc::Store` is depth-erased, so the choice is an
allocation-edge concern, not a second signal path.

**Medium rate is a medium property.** The medium carries its own sample rate,
decoupled from the engine rate — the heads already read and write at arbitrary
ratio, so an oversampled (2×) medium is *configuration, not a mode*: no second
code path, no resampling stage bolted on. Lockstep instantiates media at 1× —
heads that keep the law make oversampling unnecessary for clean capture and
playback. The partner app opts into 2× where it actually pays: nonlinear tape
colour (saturation, head bump) generates harmonics that need the headroom to
avoid folding, and repeated read-modify-write generations accumulate less
kernel loss.

### 40.11 The deck core library

The deck engine is built as a **JUCE-free library from day one** — not because
Lockstep needs the separation, but because the partner app does, and because
the core needs nothing from JUCE anyway. Three layers:

- **`deck_core`** (JUCE-free: `std` + the vendored signalsmith headers only) —
  the medium, the heads (§40.10), overdub layers + undo, punch, the marker
  lane, and the take structure as pure state. Audio crosses the boundary as
  span/pointer views over caller-owned channels; transport arrives as a POD
  snapshot — the core is a *client* of the §25 authority, never an owner of
  position. No strings, no files, no threads, no allocation on the process
  path (media allocate on construction/resize, message-thread side). The
  medium is **non-owning** over caller storage: allocation policy (a pool slot,
  a lazily committed reel) is a host concern, and keeping it there is what lets
  Lockstep's volatile slots and the partner app's reels be the same medium.
  The library's acceptance test for "did we get the head interface right" is
  that a **working tape delay** — write head, offset taps, caller-side feedback
  — can be built from `deck_core` alone, in a pure test, with no deck at all.
- **`deck_juce`** — a thin adapter (AudioBuffer glue, snapshot packing) used by
  Lockstep's Record/Loop/Tape machines, and later by the partner app if it
  builds on JUCE.
- **The hosts** — Lockstep (console, pool, promote, serialization) and the
  partner app (its own buffers, its own fully disjoint UI, its own
  persistence). Pool, take-group *promotion*, WAV IO, and transport all stay
  host-side; the take-group file convention (§40.7) is the exchange format.

Why JUCE-free is worth the ceremony: the only JUCE facility the core would use
is `AudioBuffer`, which is ~50 lines of `std::vector` + channel pointers to
replace; in exchange the core is **licence-independent** (JUCE's dual
AGPL/commercial terms never attach to our own DSP), the dependency policy
**enforces the seam structurally** (a core that takes span views *cannot*
reach for `juce::File` or the message manager — the same argument as §36's
ABI header staying JUCE-free POD), and pure-core tests build and run without
linking JUCE. The `Resampler` already qualifies today.

What this is **not**: a separately versioned product. It is a CMake target in
this repo with in-repo consumers; ABI/semver ceremony waits until a second
shipping consumer exists. (The §36 machine ABI remains the only stable binary
boundary, and the deck is not a tenant of it.)

### 40.12 Deferred to the implementation arc

- Scrub DSP *fine* quality (windowing, the granular-vs-varispeed choice at very
  low rates). The Phase 11 tail ships scrub/wind on the shared bandlimited head
  (§40.2); a dedicated low-rate granular mode is the remaining refinement.
- Whether the metronome (A6) monitors *through* a recording deck or beside it.
- The practical limit on simultaneous decks (the medium length question is
  settled: per-deck param, §40.3).
- Whether Lockstep ever surfaces the multi-tap read heads (§40.10) as a UX —
  a tape-delay face of the deck. The *interface* supports it from day one; the
  grammar for placing taps does not exist and is not needed for the deck arc.
