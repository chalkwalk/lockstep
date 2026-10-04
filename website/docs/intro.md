---
sidebar_position: 1
title: "What Lockstep is"
slug: /
---

# Lockstep {#lockstep}

Lockstep is a performance-oriented step sequencer that runs as a plugin inside
your DAW or as a standalone application. It is a **VST3**, a **CLAP**, a
standalone app, and an **AU** on macOS.

It is a sequencer you *play*, not one you configure. Trigs, conditional logic
and per-step parameter locks replace the click-and-drag automation of a
traditional DAW, and the entire editing workflow is reachable from the computer
keyboard — no mouse required.

Three influences are explicit in the design:

- **Digitakt** — the feel of the core: the trig grid, P-Locks, trig conditions,
  fills, performance mutes, and the "control-all" gesture that broadcasts one
  parameter edit across every track at once.
- **Octatrack** — the project hierarchy, and the idea that **every track chooses
  its own sound engine**. A track can host a sampler, a synth, or a MIDI-out
  adapter, and the sequencer treats them identically.
- **Squarp Pyramid** — external gear as a first-class citizen rather than an
  afterthought.

One rule shapes more of the design than any other: every key earns its place in
a small **scope + verb** grammar, and adding a bespoke single-purpose button is
forbidden. If a feature would need its own key, it is not ready.

## Status: pre-beta, and honest about it {#status-pre-beta-and-honest-about-it}

Lockstep is not released yet. There are **no downloadable builds** — you build
it from source today. See [Installing](./installing.md).

Platform support, stated precisely, because "it compiles" and "it works" are
different claims:

| Platform | State |
|---|---|
| **Linux** | Built, tested and used. This is where every line was written. |
| **macOS** | Compiles and passes the full test suite in CI. **Never loaded in a host.** No window has been opened, no audio device opened, and the AU has never been through `auval`. |
| **Windows** | Compiles under MSVC and passes the vendored test suites. Its two GUI-dependent suites are excluded in CI because a Windows runner has no interactive desktop session, so they are **unrun rather than passing**. |

macOS and Windows were first compiled on 2026-10-04 and are marked experimental
in CI for exactly these reasons. If you run Lockstep on either, a report of what
happened is the single most useful contribution the project can receive — see
[CONTRIBUTING.md](https://github.com/chalkwalk/lockstep/blob/main/CONTRIBUTING.md).

## Where to go next {#where-to-go-next}

- **[Installing](./installing.md)** — building from source, and what each
  platform is actually known to do.
- **[Tutorial](./tutorial.md)** — your first piece of music.
- **[The paradigm](./paradigm.md)** — how to reason about the system. Worth
  reading before the reference; most confusion about Lockstep is a paradigm
  mismatch rather than a missing feature.
- **[Feature reference](./reference.md)** and
  **[Gesture tree](./gestures.md)** — the complete surface, every action by
  press order.
- **[Implemented vs. planned](./status.md)** — what exists today, stated
  honestly against what is designed.
- **[What is not done](./not-done.md)** — the gaps, the unverified claims and
  the internal debts, in one place rather than scattered.

## Licence {#licence}

Lockstep is free software under the **GPLv3**. Its dependencies and their
obligations are recorded in
[`THIRDPARTY.md`](https://github.com/chalkwalk/lockstep/blob/main/THIRDPARTY.md).
