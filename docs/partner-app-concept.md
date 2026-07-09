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

- **Modes:** reel (linear, position-addressed) and tape-loop (circular, the
  Echoplex/Frippertronics idiom) — the same two topologies the deck already has,
  presented as the two things a tape *is*.
- **Colour:** a tape model on the medium — wow/flutter (rate + depth),
  saturation, head bump, hiss, and the transport artefacts (spin-up, brake).
  These are medium properties, not an insert effect; they apply on write and on
  read, and they are why the app exists.
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
on its own merits:

`lockstep_core` already serves three plugin formats from one library. The partner
app asks it to serve a **second product**, which means the deck engine, the
sample pool, the transport, and the surface model must be reachable without the
sequencer — the same way they must already be reachable without JUCE's plugin
wrappers. Concretely:

1. The deck engine (§40.3) lands in Lockstep as a machine, with its medium,
   layers, punch, and jog behind a plain interface.
2. Pool + take-groups (§40.7) are already product-neutral.
3. Transport-with-position (PRINCIPLES §25) is already a single authority; a
   second product consumes it rather than reimplementing it.
4. `SurfaceModel` / `CellState` (§35.8) gives the app a console for free.

Nothing above is speculative work done *for* the app. It is the deck arc, done
cleanly. The app becomes possible as a consequence — which is the only honest
reason to write this brief now and build nothing.

## Open questions (deliberately unanswered)

- Does it share a project file with Lockstep, or only take-groups?
- Is the medium fixed-RAM (the §40.3 discipline) or disk-backed, given that a
  dedicated tape app has no sequencer competing for memory?
- Is there a hardware surface story, or is it screen-and-controller only?
