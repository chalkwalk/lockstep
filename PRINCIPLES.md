# Lockstep — Principles

> The ten principles below are the touchstones for every feature
> decision. When a proposal conflicts with one of them, the proposal
> is the thing that has to bend.
>
> They are deliberately opinionated. Lockstep is not a neutral tool;
> it is a stance about how a step sequencer should feel. The
> principles exist so that stance survives contact with feature
> requests, contributors, and our own future temptations.
>
> Read this file before adding a milestone to `ROADMAP.md`, before
> opening a section in `DESIGN.md`, and before approving a feature
> idea. If an idea can't be expressed within these principles, it
> doesn't belong in Lockstep — or the principles need to be revised
> first, deliberately, not silently.

## 1. Vim, not nano

Lockstep is a deep, opinionated instrument. The grammar rewards
practice; the UI does not apologise for itself. Beginners are
welcomed not by hiding capability behind training wheels but by
*layering more visual feedback* on top of the same grammar — a
beginner sees what a power user does, plus annotations.

**Consequence.** There is no "simple mode" that removes features.
There are *feedback toggles* (see §8) that a new user has all on by
default and a power user turns off as their fluency grows. The
underlying gestures are identical at every level of skill.

## 2. One grammar, no exceptions

Every action in Lockstep is some combination of **scope + verb**.
Scopes are the held modifiers (`Func`, `Track`, `Pattern`, `Trig`,
section keys, `Mute`, `Fill`, `Scene A/B`); verbs are the small
fixed set (`Record`, `Play`, `Stop`, `Yes`, `No`, plus encoder
turns for live tweaks). New features must fit this grammar. If a
feature needs a bespoke chord — a one-off key combination that
doesn't compose with anything else — the design is wrong.

**Consequence.** Exceptions are the single biggest barrier to
learning. We refuse them. The cost of squeezing a new feature into
the existing grammar is paid once, by us; the cost of *not* doing
so is paid every time a user has to remember a one-off rule.

## 3. Performance is the goal, studio is the home

Lockstep is a live instrument that lives inside a DAW. Performance
ergonomics shape every decision — that is the lens through which
features are accepted or rejected. Studio integration (host
transport, MIDI-out, stem capture, resampling, project state
serialisation) exists so that the performer can carry their stage
flow into a studio session and produce stems for editing — not so
that the tool can pretend to be a DAW.

There is no "design mode" vs "performance mode": the gestures that
let a performer manipulate pre-authored material **are** the
gestures that let them improvise new material from a blank pool.
The Chain, copy/paste, conditional trigs, control-all, fills, mutes,
and checkpoints all serve both workflows because they are the same
workflow.

**Consequence.** When a feature is great for studio composition but
doesn't survive on stage, we reject it — or accept it only if it
costs no new chord, no new mode, and no expansion of the grammar.

## 4. Hardware = fewer-key QWERTY

The eventual hardware controller is the same key→action map in a
denser, gig-ready package. Every action must be reachable from
software QWERTY today; the hardware adds *no new features*, only
ergonomics. Conversely, no software gesture should require a
control axis the planned hardware can't provide. The one continuous
axis is the crossfader (§17 in DESIGN); it has no QWERTY mapping
because a typing keyboard cannot satisfy it honestly.

**Consequence.** Single-purpose buttons are forbidden — both in
software (they violate the grammar) and in hardware (they bloat
the surface). Every key earns its placement by participating in
the same scope+verb system everywhere it appears.

**The step grid is the in-context selection surface.** When a
workflow reaches a "pick one of N" decision — machine type, P-lock
target slot, track, pattern, part — the answer is a step-key press,
not a floating menu or mouse click. Section keys navigate the MZ to
a parameter page; they never launch a picker. Any feature that
requires a popup widget or mouse interaction as its primary
mechanism has the wrong design and must be re-expressed as a
scope-change that re-skins the step grid.

**The modifier hold is the mode.** There are no sticky modes.
Two patterns:

- *Scope-select*: hold a modifier → step cells re-skin to show
  the available options → press a step key to select → release
  modifier → done. The mode lasts exactly as long as the hold.
- *Step-driven edit* (e.g. P-lock slot edit, step note edit):
  hold non-step modifier(s) + press the target step → step cells
  re-skin to show editable items → interact with step keys →
  release the modifier(s) → mode exits automatically.

In both patterns "what you hold determines what mode you're in."
Nothing is ever left armed after you let go.

## 5. Internal-audio and MIDI-out tracks are equal citizens

A track driving a MIDI-out destination has the same trig grid, the
same P-Lock semantics, the same conditional trigs, the same
copy/paste/clear, the same control-all, the same scenes, the same
mutes, the same fills, and the same checkpoint behaviour as a
track driving an internal sampler or synth. If a feature has to
introduce a special case for one or the other, the design is wrong.

**Consequence.** This is what justifies the Pyramid lineage in §1
of DESIGN. It is also a useful sanity check: when designing a
performance feature, ask "does this work identically on a MIDI-out
track?" If the answer involves "well, except…", redesign until it
doesn't.

## 6. Override-ELSE-Base is the only resolution rule

Effective value = step override **if present** else track base.
This rule applies to machine ParamFrames, to sequencer-scope trig
fields (note / velocity / gate / condition), to scene mixes (which
extend OEB with a continuous lerp layer between base and override),
and to control-all broadcasts. There is no reset sentinel; there
are no per-parameter precedence flags; there is no second model.

**Consequence.** The resolver is small, fast, and provable. New
features that introduce a layer (scenes, future expression macros,
…) extend OEB rather than replacing it.

## 7. Canonical sections are reserved; machines fill what applies

Section bar keys 3–8 carry a fixed canonical taxonomy
(TRIG / SRC / FLTR / AMP / LFO / FX). The same key means the same
*concept* on every track. A machine that has nothing for a canonical
section leaves it empty. A machine that wants its own filter (e.g.
a VA emulation) re-implements *behind* the canonical FLTR section,
so the user-facing UI for "the filter" is identical across machines
even when the DSP under the hood is not.

**Consequence.** Cross-machine workflows like "hold FLTR + Copy" or
"Control-All sweep across kit cutoffs" are load-bearing. They only
work because FLTR means the same thing everywhere. Machine authors
who deviate from canonical taxonomy break those workflows; we don't
let them.

**Consequence (machines generate; effects process).** A machine
*originates* sound (a synth or sampler) or *routes/captures* it (Thru,
Recorder, Looper). Pure timbre processing — filter, EQ, distortion,
bitcrush, reverb, delay, compression — is an `IEffect`, not a machine,
so that the canonical FLTR / AMP / FX stay uniform across every track.
The litmus test: originate or capture → machine; merely colour an
existing signal → `IEffect`. Thru is the single exception, and it earns
it by doing no colouring of its own — the foundation FLTR/AMP/FX do the
work. The stock catalogue is the iconic-and-foundational set
(DESIGN §29); a specialised engine is a third-party module (DESIGN §36),
not a reason to grow the in-box catalogue.

## 8. Ergonomics first; chrome must announce state

When live ergonomics conflict with explicit visibility, ergonomics
wins — a chord that does the right thing fast beats one that's
self-documenting. But every held modifier, every active scope,
every pending clipboard, every queued pattern, every checkpoint
depth, every trig-grid mode, and every fill / mute / record-arm
state must be **announced loudly** in chrome. No silent modes.

A beginner sees a chrome densely annotated with current state and
"next action will do X" hints; a power user toggles individual hints
off as fluency grows. Both see the same grammar; the difference is
how much the UI shouts.

**Consequence.** Every new modifier ships with its chrome update in
the same commit. A modifier that doesn't render is not done.

## 9. Pragmatic determinism

Trig conditions (probability, m:n iteration) are the sanctioned RNG;
beyond those, the grammar is deterministic — the same state plus
the same trigger produces the same result, every time, so a
performer can build muscle memory and a preview can predict step
firings ahead of the playhead (DESIGN §4.5). Audio-side smoothing,
voice stealing, and similar DSP fuzz is fine when it sounds better;
determinism is the rule for the grammar and the resolver, not for
every sample of audio.

**Consequence.** We will not add hidden randomness to the editing
surface — no random LFO start phase, no probabilistic voice steal,
no "humanize" toggles smeared across the sequence layer. If
randomness is desired, it expresses through trig conditions.

## 10. State refs, not state contents

Samples live as `{path, xxHash32}` refs; the plugin state never
carries PCM bytes. The same discipline applies to anything
content-heavy a machine might want to point at (wavetables, IRs,
external destination CC tables): the project state holds a
reference, not the payload. Project files stay tiny; DAW auto-saves
stay cheap; performance recall stays instant.

**Consequence.** When a feature would tempt us to embed bulk data
into the project (e.g. "save the sampled audio inline so the user
doesn't have to manage files"), we instead build the reference flow
properly — explicit pool management, relink dialogs on load, hash
verification — and accept the slightly higher one-time UX cost in
exchange for never blocking the audio thread or bloating the host
save.

**Corollary (machine modules, M10).** This rule survives the machine
boundary. A loadable machine module never owns or serialises bulk
content: it asks the host to resolve a `{path, xxHash32}` ref and
reads *borrowed* PCM through the host-services interface (DESIGN §36).
The host owns the one shared sample pool; the module holds a handle,
not the bytes. The same trust posture is deliberate elsewhere: a
third-party machine module is **trusted, in-process native code** —
loaded by the user like any plugin they choose to install. Lockstep
ships **no sandbox and no IPC** (DESIGN §2, §36); the contract and the
CI-tested template module are the quality gate, not process isolation.
