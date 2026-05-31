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
> Other docs cite principles by their short title (e.g. *"One grammar,
> no exceptions"*) rather than by number, so the numbering can be revised
> without chasing cross-references.

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

## 2. One grammar, no exceptions

Every action in Lockstep is some combination of **scope + verb**. Scopes
are the held modifiers — the eight-key cluster (`Func`, `Track`,
`Pattern`, `Part`, `Scene`, `Master`, `Mute`, `Fill`), plus a **held
step** (`Trig`) and a **section** key — combined cross-column into
compound scopes. Verbs are the small fixed set (`Record`, `Play`,
`Stop`, `Yes`, `No`, plus encoder turns for live tweaks). New features
must fit this grammar. If a feature needs a bespoke chord — a one-off
key combination that doesn't compose with anything else — the design is
wrong.

**Consequence.** Exceptions are the single biggest barrier to learning.
We refuse them. The cost of squeezing a new feature into the existing
grammar is paid once, by us; the cost of *not* doing so is paid every
time a user has to remember a one-off rule. `Func` is the universal
qualifier — it composes with any other scope to reach a "secondary
variant" — but it never invents a meaning that doesn't compose.

## 3. Performance is the goal, studio is the home

Lockstep is a live instrument that lives inside a DAW. Performance
ergonomics shape every decision — that is the lens through which
features are accepted or rejected. Studio integration (host transport,
MIDI-out, stem capture, resampling, project state serialisation) exists
so that the performer can carry their stage flow into a studio session
and produce stems for editing — not so that the tool can pretend to be a
DAW.

There is no "design mode" vs "performance mode": the gestures that let a
performer manipulate pre-authored material **are** the gestures that let
them improvise new material from a blank pool. The Chain, copy/paste,
conditional trigs, control-all, fills, mutes, and checkpoints all serve
both workflows because they are the same workflow.

**Consequence.** When a feature is great for studio composition but
doesn't survive on stage, we reject it — or accept it only if it costs
no new chord, no new mode, and no expansion of the grammar.

## 4. Hardware = fewer-key QWERTY

The eventual hardware controller is the same key→action map in a denser,
gig-ready package. Every action must be reachable from software QWERTY
today; the hardware adds *no new features*, only ergonomics. Conversely,
no software gesture should require a control axis the planned hardware
can't provide. The one continuous axis is the crossfader (DESIGN §17);
it has no QWERTY mapping because a typing keyboard cannot satisfy it
honestly.

**Consequence.** Single-purpose buttons are forbidden — both in software
(they violate the grammar) and in hardware (they bloat the surface).
Every key earns its placement by participating in the same scope+verb
system everywhere it appears.

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
(note / velocity / gate / condition), to scene mixes (which extend OEB
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
keyed to the reference lineage (DESIGN §29); a specialised engine is a
third-party module (DESIGN §36), not a reason to grow the in-box
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
utility (navigation, tap / metronome — it keeps its normal action) or
*reserved* (dimmed, inert). Verbs and operands never silently pass
through into a different scope's meaning: an op that would read as
scope-qualified but isn't — snapshot / restore live on bare `Func` — is
suppressed under that scope, not offered ambiguously.

**Beginner mode is more chrome, never less grammar.** A beginner sees a
chrome densely annotated with current state and "next action will do X"
hints; a power user toggles individual hints off as fluency grows. Both
see the same grammar; the difference is only how much the UI shouts.
This is how *"Vim, not nano"* welcomes newcomers without a stripped-down
mode: the feedback layer is granular and additive, never a different set
of gestures.

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

## 12. State refs, not state contents

Samples live as `{path, xxHash32}` refs; the plugin state never carries
PCM bytes. The same discipline applies to anything content-heavy a
machine might want to point at (wavetables, IRs, external destination CC
tables): the project state holds a reference, not the payload. Project
files stay tiny; DAW auto-saves stay cheap; performance recall stays
instant.

**Consequence.** When a feature would tempt us to embed bulk data into
the project (e.g. "save the sampled audio inline so the user doesn't
have to manage files"), we instead build the reference flow properly —
explicit pool management, relink dialogs on load, hash verification — and
accept the slightly higher one-time UX cost in exchange for never
blocking the audio thread or bloating the host save.

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
explicitly cleared. A live per-track phrase deviation (`Track + Pattern +
step`) is not overridden by the next Section launch — the Section
re-asserts only non-deviated tracks. A per-track phrase chain explicitly
set on a track is not replaced when the performer issues a global
unison phrase swap — the deviation is already the more specific claim.

**Consequence.** Features that "cast wide" — Section launch, global
unison phrase swap — must document what they *skip*, not what they
*smash*. An explicit re-sync gesture (`Track + Part` for one musician,
`Part + Yes` for the whole band) is the only way to return a deviated
track to the Section's default. This keeps improvised deviations safe
from accidental overwrite during a live set.

**In the grammar (DESIGN §13).** Specificity is an *observable scope
level*, not a flag. The grammar is always "most recent explicit action at
the finest scope wins." A broad gesture that would trample a narrower
one that already has explicit state simply skips it — it does not trump
it. This applies across the whole surface: P-Locks win over scene mixes
win over base params; local deviations win over Section launch; per-step
overrides win over track base. The resolution stack is always traversed
finest-to-coarsest, stopping at the first explicitly-set layer.
