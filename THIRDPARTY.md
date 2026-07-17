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
| **Signalsmith DSP** | `modules/signalsmith-dsp/` (submodule) | MIT | Header-only DSP primitives: fractional-delay interpolators (Lagrange/Kaiser-sinc/Hermite), biquad/allpass filters, envelopes, spectral. Vendored + `-Werror`/C++20-validated in the test suite (9.24 S1) and available for future use. *In practice the shipped 9.24 effects use the project's own 4-point `hermite4` (`src/dsp/Interpolation.h`) for fractional taps — already needed by SamplePlayer — and a homegrown FIR Hilbert for the SSB frequency shifter (Signalsmith ships no Hilbert), so Signalsmith is currently a validated dependency rather than a load-bearing one.* |

## Fonts

| Component | Path | Licence | Notes |
|-----------|------|---------|-------|
| **Inter** | `assets/fonts/Inter-{Regular,Bold}.ttf` | **OFL-1.1** (`assets/fonts/OFL.txt`) | The product typeface (9.35). Inter 4.1 upstream; the two static cuts the surface asks for, embedded via `juce_add_binary_data(lockstep_fonts …)` and installed as the default LookAndFeel's typeface. The variable font is deliberately not vendored — a face instanced per size is exactly the render nondeterminism the embed exists to remove. |

**OFL-1.1 obligation.** The licence permits bundling and redistribution with software,
including sale of that software, provided the font itself is not sold on its own and
the licence text travels with it (`assets/fonts/OFL.txt`, embedded copies excepted).
The Reserved Font Name is *Inter*: a **modified** version may not be distributed under
that name. Lockstep ships the cuts unmodified, so no rename is required — but that
constraint binds anyone who re-generates or subsets them.

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
  test; header-only, not currently vendored. (Distinct from the vendored
  **signalsmith-dsp** primitives library above — the *stretch* engine is a separate
  Signalsmith repo and remains deferred.)
- **signalsmith-basics** — assessed for the 9.24 FOSS-DSP overhaul and **dropped**:
  the `signalsmith-dsp` primitives (allpass/biquad filters + fractional-delay
  interpolators) plus `juce::dsp` (LadderFilter, Limiter, Convolution, Oversampling)
  cover every effect need, so no second Signalsmith submodule is carried.
