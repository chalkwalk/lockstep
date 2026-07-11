# Partner app — concept brief

> **Status: concept only.** No dates, no roadmap milestone, no code. This exists
> so that the idea is written down once, with its boundaries drawn, instead of
> leaking into the Tape design (DESIGN §40) as scope. Read §40 first.

## The one-sentence version

A small **standalone tape/looper instrument** built from Lockstep's deck engine,
where the tape *character* lives — wow, flutter, saturation, head bump, noise —
because that character is exactly what does not belong inside a sequencer whose
first principle is pragmatic determinism.

## Why it is a separate app and not a Lockstep feature

Two pressures point outward:

- **Character is not determinism.** Wow and flutter are, by definition, an
  un-clocked timing behaviour (NON-GOALS fence #3's cousin). Inside Lockstep they
  would be a tension every time; inside a tape instrument they are the *point*.
  A saturating, drifting medium is a musical instrument's soul and a sequencer's
  bug.
- **The deck engine is bigger than its Lockstep face.** Four sub-tracks, a
  medium, layers, punch, jog, take-groups — that is already the whole of a
  standalone tape machine. What Lockstep needs from it is the subset that fits
  the 16-cell console. The surplus is an app.

And one pressure points inward, which is why the deck stays in Lockstep too: in
a DAW the host timeline *is* the tape timeline (§40.2). A performer recording
their set does not want to leave the instrument to do it.

## What it would be

- **Modes:** reel (linear, position-addressed), tape-loop (circular, the
  Echoplex/Frippertronics idiom), and **echo** — the same two topologies the deck
  already has, presented as the three things a tape *is*.
- **Echo is not a third engine.** A Space Echo is a circular medium with one
  write head and several read heads at fixed distances behind it, their outputs
  summed to the output and partly back into the write. That is a *configuration*
  of `deck_core`'s heads, not new machinery: heads are free-standing, a read head
  can follow the write head at an offset, and the per-sample step API lets the
  app close the feedback loop itself (DESIGN §40.10 — feedback is caller-side).
  Head spacing, feedback, and the medium's colour are then the whole instrument:
  the wow and saturation the app exists for are what make an echo a *tape* echo,
  and they land on the repeats for free because they are medium properties.
  Varispeed sweeps the head spacing, which is the sound everybody wants and no
  digital delay has.
- **Echo also ships as an insert effect.** Because the echo is a head
  configuration over `deck_core` and closes its feedback caller-side, the same
  build reduces cleanly to a **VST3/CLAP insert** — a tape delay on a DAW
  channel. The insert signal *is* the write-head input; there is no sequencer,
  no take surface, no transport ownership — just the medium, the heads, and the
  colour. It is the standalone instrument's echo mode with everything that
  isn't the delay removed, so it inherits the wow/flutter/saturation for free.
  This is the one form of the app that lives *on* another instrument's channel
  rather than beside it, and it is still not a Lockstep tenant (DESIGN §36.9) —
  it is the partner app in its smallest useful shape.
- **Colour:** a tape model on the medium — wow/flutter (rate + depth),
  saturation, head bump, hiss, and the transport artefacts (spin-up, brake).
  These are medium properties, not an insert effect; they apply on write and on
  read, and they are why the app exists. Because medium rate is a medium
  property (DESIGN §40.10), the app runs its media **oversampled (2×)** — the
  nonlinear colour generates harmonics that need the headroom — at zero
  architectural cost: the heads already read and write at arbitrary ratio.
- **Takes:** the same take-group format Lockstep promotes to (§40.7), so a take
  recorded in one opens in the other. This is the integration; there is no
  session sync, no link protocol, no shared transport.
- **Formats:** standalone-first, plus VST3/CLAP — the same three-format build
  Lockstep already produces.

## What it is explicitly not

- **Not an ABI tenant** (DESIGN §36.9). Capture and console machines are
  first-party by rule; the partner app is *another first-party host of the same
  core*, not a module loaded into Lockstep and not Lockstep loaded into it.
- **Not a companion editor** (NON-GOALS fence #9). It is a second instrument
  that shares code, not a second surface for the first instrument. Lockstep's
  screen stays the host's own window.
- **Not a sub-host** (fence #7), **not a second project playing alongside the
  first** (fence #10).

## Prerequisite: factoring the core

The app is downstream of one piece of engineering, and that piece is worth doing
on its own merits: the **deck core library** (DESIGN §40.11).

The app does **not** consume `lockstep_core`. It is deliberately lighter than
that: the app makes its own buffers and a completely disjoint UI, and shares
only the JUCE-free `deck_core` — the medium, the heads (§40.10), layers, punch,
markers, and take structure — plus the take-group *file convention* (§40.7) as
the exchange format. Concretely:

1. **`deck_core`** is the shared sound. JUCE-free (`std` + signalsmith), audio
   as span views, transport as a POD snapshot. Both products wrap it; neither
   can drift from the other's tape behaviour, because there is one. Its heads
   carry the echo mode's requirement (N taps, per-sample stepping) from day one,
   proven by a pure test that builds a tape delay out of the library alone —
   Lockstep never exercises that path, so only a test defends it.
2. **Transport is reimplemented, law-kept.** The app builds its own transport
   authority satisfying PRINCIPLES §25/§25.1 (one authority: rate, grid,
   position); the core consumes a snapshot from whichever host owns it. No
   shared transport code, one shared contract.
3. **Pool, promotion, persistence, and UI are per-host.** Lockstep keeps its
   pool and console; the app keeps its own file story and surface. Takes cross
   over as take-group files on disk, not as shared state.
4. The `deck_juce` adapter is available if the app is built on JUCE (likely),
   but the core does not require it — that is the point of §40.11.

Nothing above is speculative work done *for* the app. It is the deck arc, done
cleanly. The app becomes possible as a consequence — which is the only honest
reason to write this brief now and build nothing.

## Open questions (deliberately unanswered)

- Does it share a project file with Lockstep, or only take-groups?
- Is the medium fixed-RAM (the §40.3 discipline) or disk-backed, given that a
  dedicated tape app has no sequencer competing for memory?
- Is there a hardware surface story, or is it screen-and-controller only?
