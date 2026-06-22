# Lockstep — Principles

> The principles below are the touchstones for every feature decision.
> When a proposal conflicts with one of them, the proposal is the thing
> that has to bend.
>
> They are deliberately opinionated. Lockstep is not a neutral tool; it
> is a stance about how a step sequencer should feel. The principles
> exist so that stance survives contact with feature requests,
> contributors, and our own future temptations.
>
> Read this file before adding a milestone to `ROADMAP.md`, before
> opening a section in `DESIGN.md`, and before approving a feature idea.
> If an idea can't be expressed within these principles, it doesn't
> belong in Lockstep — or the principles need to be revised first,
> deliberately, not silently.
>
> **Citing principles.** Principles are referenced by number (`§N`) across
> `DESIGN.md`, `ROADMAP.md`, `README.md`, and source comments, and the
> `NON-GOALS.md` fences are referenced as `§N` / `fence #N`. The numbers are
> **stable anchors** — do not renumber a principle or a fence casually. The
> short titles (e.g. *"One grammar, no exceptions"*) are mnemonics, not the
> citation key. If a renumber is ever unavoidable, sweep every `PRINCIPLES §N`,
> `NON-GOALS §N`, and `fence #N` reference in the docs **and** `src/` in the
> same change.

## North Star

Lockstep is a performance-first step sequencer you *play*, not configure: one
small **scope + verb** grammar, learned by hand and read by colour, that turns
practice into live expression. Every key earns its place in that grammar —
nothing is a single-purpose button — and the gestures that edit are the gestures
that perform. It is efficient, opinionated, grammatically *and visually*
consistent, and it is equally at home as a standalone instrument and a DAW
plugin.

The principles below serve that sentence. Where two of them are in tension, the
North Star is the tie-breaker: the reading that lets a practised performer do
more, live, with less looking, wins.

## How to use this document

Run a feature proposal through this gate **before** it reaches `DESIGN.md`. Each
rung names what the proposal must answer and the principle that owns the test.
If any answer is "no" or "well, except…", the proposal bends, not the principle.

1. **Grammar** — Is it scope + verb, with no bespoke one-off chord? (§2)
2. **Cost** — Name its rung (§15 ladder) and its expected frequency. Does cheap
   track common? (§15)
3. **Mastery** — Name the practised skill it rewards and the live moment that
   practice pays back. Is it neither a crutch nor dead weight? (§14)
4. **Surface** — Is it a grid/scope-change, not a popup, on a hardware-honest
   axis? (§5, §4)
5. **Equality** — Does it work identically on a MIDI-out track? (§6)
6. **Resolution** — Does it extend Override-ELSE-Base rather than replace it? (§7)
7. **Sections** — Does timbre processing stay an `IEffect`, and any new control
   land under its canonical section? (§8, §9)
8. **Chrome** — Does its state announcement ship in the same commit? (§10)
9. **Fences** — Does it clear the reserved double-tap / press-duration gestures
   and the Non-Goals catalogue? (§17, Non-Goals)

## 1. Vim, not nano

Lockstep is a deep, opinionated instrument. The grammar rewards
practice; the UI does not apologise for itself. Beginners are welcomed
not by hiding capability behind training wheels but by *layering more
visual feedback* on top of the same grammar — a beginner sees what a
power user does, plus annotations. (How that layering works is the job
of *"Ergonomics first; chrome announces state"* below.)

**Consequence.** There is no "simple mode" that removes features. The
underlying gestures are identical at every level of skill; only the
density of on-screen annotation changes.

This is one half of a pair with *"Reward mastery — no crutches, no dead
weight"* (§14): this principle refuses to strip capability for beginners; §14
refuses to admit capability that doesn't reward practice. Together they fence
both sides — Lockstep is neither dumbed down nor padded out.

## 2. One grammar, no exceptions

Every action in Lockstep is some combination of **scope + verb**. Scopes
are the held modifiers — the eight-key cluster (`Func`, `Track`,
`Phrase`, `Scene`, `Morph`, `Song`, `Mute`, `Fill`), plus a **held
step** (`Trig`) and a **section** key — combined cross-column into
compound scopes. Verbs are the small fixed set (`Snapshot`, `Record`,
`Play`, `Clear`, `Confirm` — each with a `Func`-layer secondary
`Restore` / `Copy` / `Paste` / `Delete` / `Cancel` — plus encoder turns
for live tweaks). (`Stop` is retired as a standalone verb: transport stop
is now a `Play` double-press.) New features
must fit this grammar. If a feature needs a bespoke chord — a one-off
key combination that doesn't compose with anything else — the design is
wrong.

**Consequence.** Exceptions are the single biggest barrier to learning.
We refuse them. The cost of squeezing a new feature into the existing
grammar is paid once, by us; the cost of *not* doing so is paid every
time a user has to remember a one-off rule. `Func` is the universal
qualifier — it composes with any other scope to reach a "secondary
variant" — but it never invents a meaning that doesn't compose.

**Orthogonality — predictable composition.** The grammar is learnable because it
is *orthogonal*: knowing a scope and knowing a verb is enough to predict their
compound. A scope + verb with no defined meaning is **reserved** — it renders
dim/inert (§10) and does nothing; it is never quietly repurposed into a
surprising action. This is the half of "no exceptions" that faces the learner: a
user who has learned the pieces can derive the whole, and is never punished for a
reasonable guess. Surprise is the tax orthogonality refuses to charge.

## 3. Performance is the goal; standalone and DAW are equal homes

Lockstep is a live instrument, and performance ergonomics shape every
decision — that is the lens through which features are accepted or
rejected. It runs **equally** as a standalone application and as a plugin
inside a DAW; neither is the "real" Lockstep and the other a degraded
fallback. The same surface, the same grammar, the same project — the only
difference is who owns the transport, the clock, and the save file (the
standalone owns its own; the DAW host owns those when embedded). Studio
integration (host transport, MIDI-out, stem / WAV capture, resampling,
project state serialisation) exists so that a performer can carry their
stage flow into *either* host and produce stems for editing — not so that
the tool can pretend to be a DAW.

**DAW host as the global tempo root.** Per-Song and per-Scene tempo
deviations are stored as **ratios vs the parent** (`songRatio × sceneRatio`)
and are *set* in absolute terms — because when running in a DAW the host
clock is the absolute root (Lockstep never fights the host BPM). The ratios
are implicit, computed as `enteredBPM ÷ resolvedParentBPM`. Time-signature
overrides (per-Song / per-Scene) affect only Lockstep's internal bar sense
(launch-quantize grid, metronome accent, velocity weight) and do not
rewrite the host's time signature; they are a bounded, intentional deviation,
not a conflict with the host. DESIGN §4.8 / §4.9.

There is no "design mode" vs "performance mode": the gestures that let a
performer manipulate pre-authored material **are** the gestures that let
them improvise new material from a blank pool. The Chain, copy/paste,
conditional trigs, control-all, fills, mutes, and checkpoints all serve
both workflows because they are the same workflow.

**Consequence.** When a feature is great for studio composition but
doesn't survive on stage, we reject it — or accept it only if it costs
no new chord, no new mode, and no expansion of the grammar.

## 4. Hardware = fewer-key QWERTY

The surface a performer uses **today** is the computer keyboard plus a
generic MIDI controller — an X-Touch-class encoder/fader box is the sweet
spot, and such devices are first-class *augmentation surfaces* now, not a
someday nicety (DESIGN §35). The **eventual** dedicated hardware is the
ergonomic distillation of that pairing once it has been proven by
playtesting on exactly those: the same key→action map in a denser,
gig-ready package. It is not designed up front and it adds *no new
features* — it only makes the proven gestures faster.

That sets the discipline. Every action must be reachable from software
QWERTY today; the hardware adds nothing QWERTY can't already do.
Conversely, no software gesture may require a control axis a typing
keyboard can't honestly stand in for. The one continuous axis is the
crossfader (DESIGN §17); it has no QWERTY mapping because a typing
keyboard cannot satisfy it honestly, which is exactly why it is the
*single* sanctioned exception.

The usability target is graded, and it is the yardstick the whole surface
is measured against: a fluent performer should be able to do **many things
without looking at the screen, and almost anything without the mouse.**
Chrome (§10) exists for when you *do* look; the grammar exists so that you
mostly needn't.

**Consequence.** Single-purpose buttons are forbidden — both in software
(they violate the grammar) and in hardware (they bloat the surface).
Every key earns its placement by participating in the same scope+verb
system everywhere it appears. The only keys that sit outside a held-scope
chord are *ambient utilities*, and those earn their place by being
**multi-purpose and always-available** (the navigation keys page, shift octaves,
set length, and unlock out-of-range content depending on context) — never by
being a lone fixed function. A key that would otherwise be single-purpose is
given a real grammatical role instead: the `3` key is the **generator-hub**
modifier on hold (§5 — "the hold is the mode") and tap-tempo on tap, so even the
transport tap participates in the grammar rather than squatting a prime slot.

This also fences off whole input paradigms before they reach DESIGN. Any
feature that needs a control axis the planned hardware cannot honestly
provide is rejected here, not worked around: MPE / per-pad pressure /
tilt-and-motion sensing (Push 3, EP-133, Aira), or a wall of per-track
physical faders and channel strips (MC-707). The single continuous axis is
the crossfader; there is no second one, and there is no axis a typing
keyboard cannot stand in for.

## 5. The grid is the menu; the hold is the mode

When a workflow reaches a "pick one of N" decision — machine type, P-lock
target slot, track, pattern, part — the answer is a **step-key press**,
not a floating menu or a mouse click. Section keys navigate the
Manipulation Zone to a parameter page; they never launch a picker. Any
feature that requires a popup widget or mouse interaction as its primary
mechanism has the wrong design and must be re-expressed as a
scope-change that re-skins the step grid.

**The modifier hold is the mode.** There are no sticky modes. Two
patterns:

- *Scope-select*: hold a modifier → step cells re-skin to show the
  available options → press a step key to select → release modifier →
  done. The mode lasts exactly as long as the hold.
- *Step-driven edit* (e.g. P-lock slot edit, step note edit): hold
  non-step modifier(s) + press the target step → step cells re-skin to
  show editable items → interact with step keys → release the
  modifier(s) → mode exits automatically.

In both patterns "what you hold determines what mode you're in." Nothing
is ever left armed after you let go. (Latch — DESIGN §13.7 — is the one
sanctioned persistence of a hold, and it is explicit, reversible, and
loudly chromed; it adds no new meaning, only duration.)

**Consequence.** This is what keeps the surface portable to hardware
(*"Hardware = fewer-key QWERTY"*): a pad grid can be a picker, but a
hardware panel can't grow a popup menu.

By the same logic we refuse two adjacent paradigms. The tracker
command-column / hex-FX text grid (M8, Polyend Tracker) replaces the picker
with a typed command language — coherent, but the opposite of "the grid is
the menu." The unbounded scrolling canvas (Deluge) trades the fixed,
memorisable surface for an infinite one, so muscle memory never settles. Both
are good designs; neither is *this* design.

## 6. Internal-audio and MIDI-out tracks are equal citizens

A track driving a MIDI-out destination has the same trig grid, the same
P-Lock semantics, the same conditional trigs, the same
copy/paste/clear, the same control-all, the same scenes, the same mutes,
the same fills, and the same checkpoint behaviour as a track driving an
internal sampler or synth. If a feature has to introduce a special case
for one or the other, the design is wrong.

**Consequence.** This is what justifies the Pyramid lineage in DESIGN
§1. It is also a useful sanity check: when designing a performance
feature, ask "does this work identically on a MIDI-out track?" If the
answer involves "well, except…", redesign until it doesn't.

## 7. Override-ELSE-Base is the only resolution rule

Effective value = step override **if present** else track base. This
rule applies to machine ParamFrames, to sequencer-scope trig fields
(note / velocity / gate / condition), to Morph mixes (which extend OEB
with a continuous lerp layer between base and override), and to
control-all broadcasts. There is no reset sentinel; there are no
per-parameter precedence flags; there is no second model.

**Consequence.** The resolver is small, fast, and provable. New features
that introduce a layer (scenes, future expression macros, …) extend OEB
rather than replacing it.

## 8. Canonical sections are reserved; machines fill what applies

Section-bar keys 5–0 carry a fixed canonical taxonomy
(TRIG / SRC / FILTER / AMP / MOD / FX). The same key means the same
*concept* on every track. A machine that has nothing for a canonical
section leaves it empty. A machine that wants its own filter (e.g. a VA
emulation) re-implements *behind* the canonical FILTER section, so the
user-facing UI for "the filter" is identical across machines even when
the DSP under the hood is not. A machine may relabel a section under its
own pages, but canonical *placement* governs — a filter-like control
belongs under FILTER even when relabelled `MORPH`; a modulation matrix
under MOD.

**Consequence.** Cross-machine workflows like "hold FILTER + Copy" or
"Control-All sweep across kit cutoffs" are load-bearing. They only work
because FILTER means the same thing everywhere. Machine authors who
deviate from canonical taxonomy break those workflows; we don't let them.

**MOD is deliberately shallow.** The canonical MOD section carries only
*minimal, performable* modulation — a small set of live-tweakable,
P-lockable modulators with canonical targets. Rich modulation (deep matrices,
custom-drawn LFO shapes, per-op tables) lives *inside* a machine, reached
through its own pages, never hoisted into a canonical section every user must
learn. MOD keeps its canonical slot by being playable, not by being deep —
the test in *"Reward mastery — no crutches, no dead weight"* applied to a
section we already ship.

## 9. Machines generate or capture; effects process

A machine *originates* sound (a synth or sampler) or *routes/captures*
it (Thru, Recorder, Looper). Pure timbre processing — filter, EQ,
distortion, bitcrush, reverb, delay, compression — is an `IEffect`, not
a machine, so that the canonical FILTER / AMP / FX stay uniform across
every track. The litmus test: originate or capture → machine; merely
colour an existing signal → `IEffect`. Thru is the single exception, and
it earns it by doing no colouring of its own — the foundation
FILTER/AMP/FX do the work.

**Consequence.** The stock catalogue is the iconic-and-foundational set
keyed to the reference lineage (DESIGN §29); a specialised engine — a
granular voice, a physical-model resonator, any Tonverk-class speciality —
is a third-party module (DESIGN §36), not a reason to grow the in-box
catalogue. This keeps both the catalogue and the section taxonomy
(*"Canonical sections are reserved"*) from sprawling.

## 10. Ergonomics first; chrome must announce state

When live ergonomics conflict with explicit visibility, ergonomics wins
— a chord that does the right thing fast beats one that's
self-documenting. But every held modifier, every active scope, every
pending clipboard, every queued pattern, every checkpoint depth, every
latch, every input mode, and every fill / mute / record-arm state must
be **announced loudly** in chrome. No silent modes.

**A held scope recolours the keys it rebinds.** When a scope is held,
every key whose meaning it changes lights in that scope's colour; keys
it does not bind stay neutral. An unbound key is either an *ambient*
utility (the navigation keys — multi-purpose, always-available; they keep their context action) or
*reserved* (dimmed, inert). Verbs and operands never silently pass
through into a different scope's meaning: an op that would read as
scope-qualified but isn't — snapshot is the bare `Snapshot` (Y) verb,
restore is `Func+Y` — is suppressed under a held scope, not offered
ambiguously.

**Beginner mode is more chrome, never less grammar.** A beginner sees a
chrome densely annotated with current state and "next action will do X"
hints; a power user toggles individual hints off as fluency grows. Both
see the same grammar; the difference is only how much the UI shouts. This
granular, additive feedback layer is the mechanism by which *"Vim, not nano"*
(§1) welcomes newcomers without a stripped-down mode.

**Consequence.** Every new modifier ships with its chrome update in the
same commit. A modifier that doesn't render is not done.

## 11. Pragmatic determinism

Trig conditions (probability, m:n iteration) are the sanctioned RNG;
beyond those, the grammar is deterministic — the same state plus the
same trigger produces the same result, every time, so a performer can
build muscle memory and a preview can predict step firings ahead of the
playhead (DESIGN §4.5). Audio-side smoothing, voice stealing, and
similar DSP fuzz is fine when it sounds better; determinism is the rule
for the grammar and the resolver, not for every sample of audio.

**Consequence.** We will not add hidden randomness to the editing
surface — no random LFO start phase, no probabilistic voice steal, no
"humanize" toggles smeared across the sequence layer. If randomness is
desired, it expresses through trig conditions.

**Deterministic generators are not randomness.** A Euclidean fill, a
pendulum playback direction, a `Density` overlay that subtracts trigs
downstream of the probability/condition system — these are admissible
precisely because they are deterministic: same state, same result, and the
existing hand-editable trig data is unchanged (Density only silences
would-fire trigs; the pattern is unmodified). Euclidean shipped in this form
(5.9) as the `Phrase+Fill` held chord; Density (§39) replaces the old Chance
macro as the `Func`-held per-track band. The line is drawn at *stochastic authoring* —
engines that pick the notes or the pattern for you by rolling dice at edit
time (Oxi's stochastic modes, Polyend's smart genre fills, Torso's generative
voicing). Those we refuse; a clocked, repeatable generator we welcome, in the
performable form that *"Reward mastery"* demands.

## 12. State refs, not state contents

Samples live as `{path, xxHash32}` refs; the plugin state never carries
PCM bytes. The same discipline applies to anything content-heavy a
machine might want to point at (wavetables, IRs, external destination CC
tables): the project state holds a reference, not the payload. Project
files stay tiny; host auto-saves and standalone project files stay cheap;
performance recall stays instant.

**Consequence.** When a feature would tempt us to embed bulk data into
the project (e.g. "save the sampled audio inline so the user doesn't
have to manage files"), we instead build the reference flow properly —
explicit pool management, relink dialogs on load, hash verification — and
accept the slightly higher one-time UX cost in exchange for never
blocking the audio thread or bloating the host save / standalone project
file.

**Corollary (machine modules, 6.7).** This rule survives the machine
boundary. A loadable machine module never owns or serialises bulk
content: it asks the host to resolve a `{path, xxHash32}` ref and reads
*borrowed* PCM through the host-services interface (DESIGN §36). The host
owns the one shared sample pool; the module holds a handle, not the
bytes. The same trust posture is deliberate elsewhere: a third-party
machine module is **trusted, in-process native code** — loaded by the
user like any plugin they choose to install. Lockstep ships **no sandbox
and no IPC** (DESIGN §2, §36); the contract and the CI-tested template
module are the quality gate, not process isolation.

## 13. More specific scope wins

When a global gesture and a local gesture both target the same thing,
the more specific (local) one takes precedence and stays in force until
explicitly cleared. A live per-track phrase deviation (`Track + Phrase +
step`) is not overridden by the next Scene launch — the Scene
re-asserts only non-deviated tracks. A global unison phrase swap
(`Phrase + step`) likewise *skips* the tracks already deviating — except
the **focused** track, which the performer is explicitly looking at and so
deliberately pulls back into the unison (DESIGN §4.7/§16). The skip is the
default; the focused-track exception is a *targeted* act, not a wide gesture
trampling a narrow one.

**Consequence.** Features that "cast wide" — Scene launch, unison phrase
swap — must document what they *skip*, not what they *smash*. Returning a
deviated track to its home is never a blind broadcast: you pick its home
(global) phrase in the visible selector, or re-launch the Scene to its floor
(`Scene + active-step` revert, or `Func + Scene + step` baseline launch) to clear the
whole overlay — all observable, no bespoke re-sync or force-all gesture
(DESIGN §16). This keeps improvised
deviations safe from accidental overwrite during a live set.

**In the grammar (DESIGN §13).** Specificity is an *observable scope
level*, not a flag. The grammar is always "most recent explicit action at
the finest scope wins." A broad gesture that would trample a narrower
one that already has explicit state simply skips it — it does not trump
it. This applies across the whole surface: P-Locks win over Morph mixes
win over base params; local deviations win over Scene launch; per-step
overrides win over track base. The resolution stack is always traversed
finest-to-coarsest, stopping at the first explicitly-set layer.

## 14. Reward mastery — no crutches, no dead weight

Lockstep is an instrument you *learn*. Every gesture a user must carry in
their head is a tax paid on the way to mastery, and the surface has a finite
budget. A feature is admitted only when it **rewards practice** — a performer
measurably improves at it over time and it widens what they can express live.
Two failure modes get a feature rejected:

- **Crutch.** It does the musical work *for* the user — lowering the skill
  floor without raising the ceiling. Note auto-correct ("no wrong notes"),
  smart generators that pick the notes, genre-template fills, anything that
  makes *wrong* impossible. (The mirror half of *"Vim, not nano"* §1.)
- **Dead weight.** Its cognitive cost is never repaid in performance — a
  set-and-forget knob, a studio convenience, a mode you configure once and
  never touch on stage.

The test is **constructive**: it does not only say no, it says *"yes, but
only in the performable form."* A generator is welcome if you drive it live
and it prints to ordinary state (Euclidean — DESIGN §13.5); an arpeggiator is
welcome only as an engine you *perform* — live-driven, P-lockable — not a
noodler you arm and leave; a scale is welcome as a *playable layout* (frets
that let you move faster), never as note auto-correct.

**Consequence.** "It's a popular feature" is never sufficient justification.
Before a feature reaches `DESIGN.md`, name the practised skill it rewards and
the live moment it pays that practice back. If the honest answer is "it makes
music easier" or "you set it up once," it belongs in a DAW — not in Lockstep.
The *Non-Goals* below, and `NON-GOALS.md`, are this principle and its siblings
applied to the competitive landscape.

## 15. Gesture cost is graduated; cheap means common

The ergonomic complement to *"One grammar, no exceptions"*: the grammar
says every action is scope + verb; this says **how many keys an action
costs is a budget, and the budget must track frequency.** A gesture's
cost is the ladder below, cheapest first — and the earlier rungs must
carry the most common actions:

| Rung | Shape | Keys |
|---|---|---|
| 1 | `key` | 1 |
| 2 | `Func + key` | 2 |
| 3 | `mod + key` | 2 |
| 4 | `Func + mod + key` | 3 |
| 5 | `mod + mod + key` (cross-column) | 3 |
| 6 | `Func + mod + mod + key` | 4 |

`Func` is the **cheapest modifier** — it is key `1`, the universal
qualifier (§2), and mentally free — so `Func + key` is easier than a
column `mod + key`, and `Func + mod + key` is easier than `mod + mod`.
"`key`" is the single operand (a verb, a step, a section, a nav, or an
encoder turn).

**Ceiling: four.** Rung 6 (`Func + mod + mod + key`, four simultaneous
keys) is the maximum, and it is admitted only when the grammar and the
value clearly earn it. **Five keys is forbidden** — there is no
`Func + mod + mod + section + step`. Three is *not* a hard ceiling; four
is.

**Count scopes, not fingers.** The cost is the number of held
*modifiers/scopes*, plus the one operand. Repeated same-class targets do
**not** add cost: holding eight steps to copy them, or hold-tap-many on
the mute layer, is one logical operand, not eight keys. Batch selection
is free; *qualification* is what you pay for.

**Cost tracks frequency *and* stakes.** Frequency sets most of the budget, but
not all of it: a rare action with high *stakes* — one whose mistiming or absence
hurts a live set — earns a cheap rung anyway. **Panic** (kill all voices) and the
**`Func` double-tap escape** are rare yet sit low precisely because the moment you
need them, you need them instantly. The rule is *cost tracks frequency × stakes*,
with a **cheapness floor for safety-critical actions**: a recovery or escape
gesture may never be buried, however seldom it fires.

**Consequence.** Before a gesture reaches `DESIGN.md`, name its rung and
its expected frequency. A common live action on an expensive rung, or a
rare set-and-forget action on a cheap one, is a design smell — rebind
until cost tracks use. The cross-column rule (DESIGN §13) is *how* you
climb to the `mod + mod` rungs legibly; this principle is *why* you
should be reluctant to. See also §10 *"Ergonomics first"* — when speed
and self-documentation conflict, the cheap rung wins, but it must still
announce itself in chrome.

## 16. Destructive actions target selected entities, not the playing ones

Lockstep is a live performance tool: the transport is often running and
someone may be listening. A destructive action that silently deletes
"the currently playing phrase" is hostile to that context — the user
asked to *select* a target for deletion, which is a separate act.

**Consequence.** Any action that erases or overwrites a persistent slot
(phrase, scene, track) must first let the user **select the target**
(via the deletion picker modality — scope+Func+Clear → grid highlights
the scope's slots → tap to choose) and then **confirm by name** (the
confirm prompt names the entity and its slot number). Confirming must
not require re-holding the arming chord: the prompt is sticky until
answered or explicitly cancelled by a non-Func key press. The Confirm key
shows CONFIRM/CANCEL live (green when Func is up, red when Func is held) so
the choice is visible before the press completes.

Its mirror is *"Ergonomics first; chrome must announce state"* — that
principle says the surface announces what it is about to do; this one
says destructive actions must announce *which specific entity* is at
risk, so the user can verify before confirming.

## 17. Reserved gestures are fences, not conventions

The gesture vocabulary is **small and shared**: every user must carry the
complete grammar in their hands, which is both the instrument's strength
and a reason to guard the budget fiercely. Certain gesture patterns are
**reserved** because they carry cross-surface, cross-machine meanings;
grafting a new action onto them destroys the reliability that makes them
valuable.

The gesture vocabulary has **two time-based axes**: the *double-tap/double-press*
family (repeating the same key quickly) and the *press-duration* axis (how long
a key is held). Both are governed by this principle.

### Double-tap / double-press families

The reserved families, with their invariant meanings:

- **Modifier double-tap** = latch (virtual-hold): the same scope, hands-free.
  Double-tap the same modifier again = release the latch. No other meaning may
  be added to modifier double-tap. (DESIGN §13.7.)
- **`Func` double-tap** = universal escape: clear all latches and virtual-held
  steps in one gesture. `Func` never latches; its escape is *unconditional*,
  so the performer always knows the exit. (DESIGN §13.7.)
- **Step double-tap** = virtual-hold into the edit context (P-Lock / trig
  override operand). (DESIGN §13.7; *"The grid is the menu"* §5.) This
  reservation is scope-local to the trig grid; a re-skin of the step grid for
  a different purpose (e.g. scene slots) does **not** inherit this meaning — a
  borrow of step-double-tap for an unrelated action requires explicit approval
  and a matching family entry here.
- **Verb double-press** = amplified / intensified action (e.g. `Play`
  double-press = stop + reset phase). Verbs are instantaneous — their
  double-press is *intensification*, not latch. (DESIGN §13.7.)
- **Nav double-tap at a range boundary** = reveal / unlock out-of-range content
  (e.g. NavRight double-tap at the last page = unlock scroll past the visible
  end). Single nav = move; double nav at a boundary = reveal. This is the
  "light switch" pattern: the second tap unlocks a thing, not repeats a move.
- **Operand double-tap (hardware encoder push)** = reset the slot to its
  default value. Applies to any addressable slot (P-Lock slot, meta-band
  rotary). This is a *reset*, not a latch — the operand is the key, so it
  carries the double-tap meaning without touching any family above.

**Section keys are excluded from all double-tap gestures.** A section key
advances through its sub-pages on single tap; a repeated tap is already
semantically occupied by that paging cycle. New gestures targeting a section
key must use `Func + section` or long-press — never double-tap. (See also
*"One grammar, no exceptions"* §2; DESIGN §6.1.)

### Press-duration axis

The **hold duration of a key** is a meaning-bearing axis for **verbs and
operands only** — never for modifiers or `Func`. Modifiers and `Func` are
already held to form compound chords; a "long press" on them cannot be
distinguished from a chord-in-progress, so assigning a held meaning to them is
a category error and is **forbidden**.

The canonical legal instance is `Func + Y` (RESTORE): a brief tap pops one
checkpoint entry; holding `Y` then releasing jumps straight to the floor.
**`Y` (the operand) carries the hold; `Func` is merely the scope.** This is
the general form: any incremental verb or operand may carry a hold-intensifier
meaning, and the hold is measured on the *released* key, not on the qualifier.
(The "hold = all the way" grammar convention in DESIGN §13 is a direct corollary.)

**Consequence.** Before adding any double-tap, double-press, or held-key
meaning, confirm which family it belongs to and that the family has capacity
for the new entry. If it doesn't fit any existing family — and especially if it
would reach into the section-key layer, or attach a duration meaning to a
modifier or `Func` — the proposal requires a revision to this principle first,
not a silent exception. See *"Gesture cost is graduated"* §15; NON-GOALS §14.

## 18. One mode enum; no ad-hoc UI state booleans

The UI has a small closed family of **mutually-exclusive sticky overlays**
(Time, Density, Vel, Euclid). Exactly one may be active at any time.

**Consequence.** Mutually-exclusive modality is encoded in a **single closed
enum field** (`UiState::overlay`). Adding a new sticky overlay means adding
one enum value and one descriptor row in the reducer — not a new boolean.
Free-standing booleans (`timeStickyMode`, `densityStickyMode`, …) allow
illegal coexistence that the compiler cannot see; the single-field design
makes it structurally unrepresentable.

**Consequence.** The entry/exit/supersede policy for every overlay lives in
one declarative descriptor table (`kOverlays` in `ModeReducer.cpp`). A field
in `OverlayDescriptor` without a default value forces every existing entry to
state its policy explicitly — a missing exit wire becomes a compile-time
omission, not a silent gap. The TIME "too sticky" bug (missing three exit
wires) was the case study that motivated this design; it is now impossible.

**Consequence.** Do not add free-standing booleans for modal state elsewhere
either. The same logic applies to Func-layer pickers (`FuncReskin` enum),
modifier latch (`LatchState`), and confirm state (`ConfirmState`): each is a
small closed set; each uses a typed field so the compiler rejects combinations
that the grammar forbids. See `src/ui/mode/` for all current modal
sub-systems. See *"One grammar, no exceptions"* §2 for the general principle;
§17 for gesture-family enforcement. DESIGN §13 for the scope+verb grammar.

## 19. Visual grammar is token-first and dual-target

The surface is **read by colour**, not only by text. Lockstep runs on a screen
today and on dedicated hardware (mechanical keys + RGB LEDs) tomorrow, and the
visual language must serve **both** targets from one source of truth:

- **Screen** carries the *full* detail: text, hint-text, scope colour, and
  chrome (§10).
- **Hardware** carries colour + brightness + blink **only** — no text.

Every meaningful state is therefore a **`CellState` token** (the single-source
appearance table, DESIGN §35.8.7) that resolves to *both* a screen appearance and
a hardware LED appearance. The scope-colour grammar (DESIGN §6.6 — one hue per
modality at three brightness levels) is the backbone: a held scope recolours the
keys it rebinds, identically on screen and on LEDs.

**Graceful degradation, not parity.** Colour cannot carry every detail, and we do
not pretend it can. The commitment is narrower and honest: the *performable
subset* — what a fluent player needs to act **without looking** — must survive on
**colour + brightness + blink + muscle memory** alone. Exact values (a BPM, a
P-Lock number, an entity name in a confirm prompt) are legitimately screen-only;
but *which mode you are in*, *which scope is held*, *what is armed / latched /
muted*, and *which cell is the live target* must each have a colour or blink
proxy. A state whose only expression is text is a hardware bug, not a feature.

**Consequence.** A new state ships its `CellState` token — screen *and* LED
mapping — in the same change, exactly as a new modifier ships its chrome (§10).
And because the instrument is *learned by hand*, the home-row anchor keys carry a
persistent **orientation cue** (the F/J-style "bumps") on screen and as literal
nibs on hardware, so a player moving between a normal typing keyboard and
Lockstep keeps their hands placed. This is the visual half of the North Star's
"learned by hand and read by colour."

**In-cell gesture affordances (9.12).** Every key carries the same **fixed
uniform band layout**, so the PRIMARY locks to one vertical position on every key
and the eye learns it once. The five grammar pieces (letter hint, double-tap, the
non-primary gesture, PRIMARY, func variant) pack into four bands: the **double-tap
chip shares the letter-hint row** (using the dead space beside the QWERTY hint);
the **PRIMARY** sits in the locked centre band; a **single secondary rail** below
it shows whichever of tap/hold is *not* the primary (blank for most keys); the
**func variant** is the bottom strip. Each populated slot carries a **painted
vector glyph** (tap = dot, double-tap = two dots, hold = hollow ring, func =
filled amber chip). The PRIMARY is the *strongest* action (Hold promoted over Tap;
explicit `promoted` flag allows per-context overrides), and its **access glyph**
(hold ring vs tap dot) is the **tap / no-tap signal**: scope modifiers are
hold-to-engage keys with no distinct tap, so they show a bare name (`TRACK`,
`MORPH`, `MUTE`) + hold ring and a `LATCH` double-tap — never a phantom tap rail.
Key 3 (TapTempo) is the dual-gesture case: PRIMARY = GEN HUB (hold ring), secondary
rail = TAP TEMPO (tap dot).

**Single source of truth: the grammar.** Display derives entirely from
`resolveBinding(button, idx, heldMods, layer, Gesture)` — the same function that
dispatches behaviour. Slots are filled by querying Tap / Hold / DoubleTap /
Func-context rows; the primary is the `promotedGesture()` winner. Because display
and dispatch resolve from the same table, they cannot silently diverge.
`KeyAffordances.{h,cpp}` has been deleted. Adding a gesture: add a row to
`kKeyBindingsData` with the appropriate `Gesture` field; `SurfaceModel` derives
all slot labels automatically.

**Context inspector (9.11).** A slim always-on full-width strip (4 captioned
columns: KEY · HELD · OVERLAY · EDIT) narrates the current state in plain text —
the complement to the at-a-glance cell affordances. Each column always has
content (idle fallbacks supply "where am I" information when nothing is active).
Built by the pure function `buildInspectorModel(UiState, EditContext, proc,
focusedButton)`, making it unit-testable and reusable for controller displays
(dual-target). KEY region is also grammar-derived (calls `resolveBinding` per
gesture, same SSOT). A state whose only expression is the inspector is still
a hardware bug: the inspector is enrichment, not a crutch.

## 20. Invariants are the compiler's job, not the coder's memory

When correctness depends on a human *remembering* to do two things together, it
will eventually be done as one. Make the requirement structural.

**Single owner for shared state.** Any value with more than one reader or writer
has exactly **one owning setter**; nothing edits the underlying fields directly.
Two stores that are "meant to stay in sync" are a bug waiting to happen — track
length lived as both an APVTS param and a working `Track.length`, and every
writer that bypassed `setTrackLength` (which updates both) silently desynced the
Euclid generator from playback. The fix is never "remember to update both"; it is
"route all writes through the one setter, and the redundancy disappears." The
same rule retired the `repaint()` + `keyboardArea_.repaint()` hand-pairing
(one `refreshSurface()`), and is why modal state is one enum (§18), not a bag of
booleans. Prefer one source; where a mirror is genuinely required, give it a
single named sync point and document it (DESIGN state-ownership).

**Explicit switch control flow.** Every `case` that carries code terminates
explicitly — `break` / `return` / `throw` / `[[fallthrough]]`. Silent
fall-through is a defect (a `dispatchUp` fall-through once made releasing twelve
unrelated buttons fire tap-tempo, so changing tracks set the BPM); it is now a
compile error under `-Wimplicit-fallthrough -Werror`. **Intentional** fall-through
is allowed but must be marked with `[[fallthrough]];` *and* a comment naming where
it falls to. There is **no blanket "always add a `default:`" rule** — switches
that are exhaustive over a closed enum deliberately omit `default:` so `-Wswitch`
turns a new unhandled enum value into a compile error (`layerBanner`,
`Arrangement`'s `CheckpointScope`, `setModifierLatch`, `groupForCell`). Adding a
`default:` to those would *defeat* that check, so leave them as they are.

---

## Non-Goals — what Lockstep refuses to become

The standing refusals. `NON-GOALS.md` is the **authoritative catalogue** — it
carries the long form: which groovebox prompted each fence, the principle that
rejects it, and the performable alternative we offer instead. The numbering here
mirrors that catalogue exactly, so `NON-GOALS §N` resolves to the same fence in
both files. This index is a pointer, not a second copy — edit the fence text in
`NON-GOALS.md`.

| # | Refusal | Rejected by |
|---|---|---|
| 1 | Song / arrangement / linear chaining | *Performance is the goal* (§3); DESIGN §16 |
| 2 | Stochastic / generative note & pattern engines | *Pragmatic determinism* (§11); *Reward mastery* (§14) |
| 3 | Un-clocked / "organic" / quantize-off timing | *Pragmatic determinism* (§11) |
| 4 | Tracker command-column / hex-FX paradigm | *The grid is the menu* (§5) |
| 5 | Unbounded / scrolling canvas | *The grid is the menu* (§5); *Hardware = fewer-key QWERTY* (§4) |
| 6 | Control axis the hardware can't honestly provide (MPE, pressure, tilt, per-track faders) | *Hardware = fewer-key QWERTY* (§4) |
| 7 | Foreign-plugin / standalone-host ecosystem | DESIGN §2, §36 |
| 8 | Destructive tape workflow | *State refs, not contents* (§12) |
| 9 | Companion app as the primary surface | DESIGN §35.8 |
| 10 | Dual-project concurrent playback | *Performance is the goal* (§3) |
| 11 | Heavyweight performance-FX *mode* (shipped instead as the thin Animate toggle) | *Reward mastery* (§14); *Performance is the goal* (§3) |
| 12 | Custom-LFO designer / free automation lanes | *Reward mastery* (§14) |
| 13 | Note auto-correct ("no wrong notes") | *Reward mastery* (§14) |
| 14 | Reserved-gesture overload (double-tap / press-duration) | *Reserved gestures are fences* (§17); DESIGN §13.7 |
