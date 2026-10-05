---
sidebar_position: 9
title: "What is not done"
---

# What is not done

Lockstep is pre-beta and this page is the list. It exists because "builds and
tests clean" and "works" are different claims, and a release that only
advertises the first while sounding like the second is the thing worth not
doing.

Nothing here is a secret or a surprise discovered late. Each item is tracked in
[`ROADMAP.md`](https://github.com/chalkwalk/lockstep/blob/main/ROADMAP.md),
which is the authority; this page is the short version for someone deciding
whether to try it.

## Platforms

| Platform | What is actually true |
|---|---|
| **Linux** | Built, tested and used. Every line was written here. |
| **macOS** | Compiles and passes the full test suite in CI, including the 125-row dispatch golden. **No host has ever loaded the plugin.** No window has been opened, no audio device opened, and the AU has never been through `auval`. |
| **Windows** | Compiles under MSVC and passes the vendored test suites. Its two GUI-dependent suites are **excluded** in CI, because a Windows runner has no interactive desktop session — so they are *unrun*, not passing. |

macOS and Windows were compiled for the first time on 2026-10-04. Everything
before that date was Linux-only. If you run Lockstep on either, a report is the
most useful contribution the project can receive.

## Features designed but not finished

- **The recall unit (`5.3`).** Song and Scene naming, colours, the browser and
  phrase copy/fork all ship. What is missing is the unit you recall a whole
  sound set with; "Kit" was retired as a term and the story needs re-deriving.
- **Sample-playback coherence (`9.23`).** Partially shipped.
- **Per-track scale-quantize (`10.6`)** and **harmonic existing-rhythm
  placement (`10.11`)**.
- **`Cue + Scene` and `Cue + MIDI-out`** — the cue balance core shipped; these
  two gestures did not.
- **Freeze-to-disk** for record buffers.
- **The Machine Module ABI (`6.7`)** is deliberately deferred until a second
  consumer exists. Third-party machines are not loadable yet; the catalogue is
  first-party and statically linked.

## Verification that has not happened

- **Ear tests.** Time-stretch quality, the effects A/B matrix and varispeed
  texture are all marked "pending ear test". They need listening, and nobody
  has done it since the engine changed.
- **The Bungee stretch engine was bumped to v2.4.30** on 2026-10-04, which
  included a change to its phase maths. Stretch output will differ slightly
  from any previous build, and the pending ear test now applies to this
  version.
- **`auval`** has never been run on the AU.
- **Push 1 palette** has never been checked on hardware.

## Known internal debts

These do not affect what you can do with Lockstep, but they are real and are
written down rather than discovered:

- **clang-format is advisory, not enforced.** 233 of 402 files differ from the
  project's own `.clang-format`. The tree was never run through it, and
  reformatting half the source in one commit would destroy `git blame` across
  the codebase.
- **clang-tidy is advisory.** The whole-tree baseline is **16,926 findings**
  across 67 translation units. CI reports 2,227 because its glob is not
  recursive and covers about five of them; widening it measured 7.6× more.
  Findings in a shared header count once per unit including it, so the number
  of distinct issues is smaller — but that is the figure a blocking gate would
  have to drive to zero.
- **Link-time optimisation is off.** There is a circular dependency between two
  static archives (the engine names an editor symbol it does not own), and LTO
  turns it from a link that works by luck into one that fails on some
  toolchains. The workaround is documented in `src/CMakeLists.txt`; the fix is
  to break the cycle.
- **Scene goldens are a Linux-only check.** They compare pixels, and a
  different rasteriser moves about 1% of blocks — so chrome with no model (the
  inspector, transport, timeline strip, MZ rotaries, banners) is unverified on
  macOS and Windows.

## What is solid

For balance, because a page like this is misleading on its own. The test suite
is six binaries and runs in about 70 seconds. Every row of the Critical User
Journey catalogue is implemented. The gesture grammar is covered by a 125-row
behavioural golden that is platform-independent and passes on all three
platforms, and by a modal sweep that drives all 16 modal states through a
battery of interruptions. Several of the guards in this codebase fail the
*build* rather than a test, because the bugs they prevent were invisible on
screen.
