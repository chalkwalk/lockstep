# Third-party components

Lockstep is released under the GPL. The vendored third-party components below are
all GPL-compatible. This file records their licences and any obligations.

## Audio / DSP

| Component | Path | Licence | Notes |
|-----------|------|---------|-------|
| **JUCE** | `JUCE/` (submodule) | GPLv3 / commercial | Used under the GPLv3 option for this release. |
| **clap-juce-extensions** | `modules/clap-juce-extensions/` (submodule) | MIT | CLAP wrapper. |
| **Bungee** | `modules/bungee/` (submodule) | **MPL-2.0** | Real-time time/pitch stretch engine (9.23). |
| **Eigen** | `modules/bungee/submodules/eigen/` | **MPL-2.0** | Linear algebra, pulled in by Bungee. |
| **PFFFT** | `modules/bungee/submodules/pffft/` | BSD-like (FFTPACK-derived) | FFT backend for Bungee. |

## MPL-2.0 obligation (Bungee + Eigen)

The Mozilla Public License 2.0 is **file-level copyleft** and is GPL-compatible
(MPL-2.0 §3.2 permits distribution of a covered work as part of a Larger Work under
the GPL). The obligation is per-file: if we modify an MPL-2.0 source file, that
file's source must be made available under the MPL. Practical rule for this repo:

- **Keep Bungee modifications isolated and upstreamable.** Do not fork Bungee's
  internals into the Lockstep tree; consume it through the `IStretchEngine` seam
  (`src/dsp/BungeeStretchEngine.cpp`) so our code stays cleanly GPL and Bungee stays
  cleanly MPL. If a Bungee fix is needed, make it in `modules/bungee/` and push it
  upstream rather than vendoring a divergent copy.

## Deferred / named-but-not-vendored

- **Offline render-to-pool engine** — a possible future max-quality (latency-
  irrelevant) engine behind the same `IStretchEngine` seam. The engine is not chosen:
  candidates (e.g. Rubber Band, GPLv2+) would be benchmarked for quality/CPU before
  adoption, not assumed. None is vendored today.
- **signalsmith-stretch** (MIT) — named real-time fallback if Bungee fails the ear
  test; header-only, not currently vendored.
