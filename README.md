# Lockstep

> **Lockstep** is a performance-oriented step sequencer plugin
> (VST3 / CLAP / Standalone; AU on macOS).

**Documentation: [lockstep.chalkwalkmusic.com](https://lockstep.chalkwalkmusic.com)**
— tutorial, paradigm, glossary, the full feature reference and the complete
gesture tree. Also mirrored to the
[wiki](https://github.com/chalkwalk/lockstep/wiki).

Lockstep is a step sequencer you play like an instrument. Trigs, conditional
logic and per-step parameter locks replace the continuous click-and-drag
automation of a traditional DAW. It runs as a plugin inside your DAW or as a
standalone application, and the entire editing workflow is reachable from the
computer keyboard — no mouse required.

Three influences are explicit in the design:

- **Digitakt** — the feel of the core: the trig grid, P-Locks, trig conditions,
  fills, performance mutes, and the "control-all" gesture that broadcasts one
  parameter edit across every track at once.
- **Octatrack** — the project hierarchy, and the idea that **every track
  chooses its own sound engine**. A track can host a sampler, a synth, or a
  MIDI-out adapter; the sequencer treats them identically.
- **Squarp Pyramid** — external gear as a first-class citizen.

One rule shapes more of the design than any other: every key earns its place in
a small **scope + verb** grammar, and adding a bespoke single-purpose button is
forbidden. If a feature would need its own key, it is not ready.

## Status

Pre-beta. There are no released builds yet; you build from source.

| Platform | State |
|---|---|
| **Linux** | Built, tested and used. Where every line was written. |
| **macOS** | Compiles and passes the full suite in CI. **Never loaded in a host**; the AU has never been through `auval`. |
| **Windows** | Compiles under MSVC and passes the vendored suites. Its two GUI suites are excluded in CI (no interactive desktop on a runner), so they are **unrun rather than passing**. |

[Implemented vs. planned](https://lockstep.chalkwalkmusic.com/docs/status) states
precisely what exists today against what is designed.

## Building

Clang and C++20 are required, and Clang is load-bearing: the strict warning set
and `-Werror` are gated to it, so a GCC build compiles with neither.

```bash
git clone --recurse-submodules https://github.com/chalkwalk/lockstep.git
cd lockstep
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Full instructions, dependencies and artefact paths:
[Installing](https://lockstep.chalkwalkmusic.com/docs/installing).

## Contributing

What the project most needs, in order: **running Lockstep on macOS or
Windows**; testing in more DAWs and formats; and **modality reports** — if you
get stuck in a mode or a key does nothing its label promises, that is the most
useful bug report available right now.

See [`CONTRIBUTING.md`](CONTRIBUTING.md). For the design reasoning behind any
of this, read `PRINCIPLES.md`, then `DESIGN.md`, then `ROADMAP.md`, in that
order.

## Licensing & third-party

Lockstep is released under the **GPLv3** — the full text is in
[`LICENSE`](LICENSE), and JUCE is used under its GPL option. Contributions are
accepted on those terms; see [`CONTRIBUTING.md`](CONTRIBUTING.md), which also
lists what the project most needs (building and running it on macOS and Windows,
above everything else).

Time-stretching and pitch-shifting for the
sample players (Stretch, Stream) use the **Bungee** engine (MPL-2.0), which vendors
**Eigen** (MPL-2.0) and **PFFFT** (BSD-like) — all GPL-compatible. The surface renders
in **Inter** (SIL Open Font License 1.1), embedded in the binary from
`assets/fonts/`. See [`THIRDPARTY.md`](THIRDPARTY.md) for the full component list and
the MPL file-level-copyleft obligation.

**The typeface is part of the instrument.** Lockstep ships Inter and renders every
label with it rather than asking the host machine for "a sans-serif". The surface then
looks the same on every machine — you can read a screenshot of someone else's set, and
the layout cannot shift under you because a distro changed its default font.

**A440 tuning.** Sample, Slice and Stretch expose an **A440** mode (`Auto` / `Raw`):
in **Auto** (the default) a sample plays with its detected tuning deviation cancelled
so it sits in tune with the project; **Raw** plays it exactly as recorded. A per-sample
**Tune** (±50 cents) rides on top. Detected tempo / key / tuning / one-shot are
**editable** per pool entry and stored as overrides — the pool browser marks an
overridden entry with a trailing `*`.
