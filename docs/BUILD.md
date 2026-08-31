# Building Lockstep

## Toolchain: Clang + C++20 (required)

Lockstep builds with **Clang** and **C++20**. C++20 is pinned project-wide
(`CMAKE_CXX_STANDARD 20` in the root `CMakeLists.txt`); per-target `cxx_std_20`
is *not* sufficient (see the CLAUDE.md "C++20 pinned" note).

> **Clang is the supported compiler, and it is load-bearing for warnings.**
> The strict warning set and `-Werror` are gated to Clang in
> `src/CMakeLists.txt` / `tests/CMakeLists.txt`:
> `-Wall -Wextra -Wpedantic -Wsign-conversion -Wfloat-conversion -Wshadow -Werror`.
> A GCC build silently skips all of these and falls back to
> `juce::juce_recommended_warning_flags` with **no `-Werror`** — so warnings
> that would fail CI pass locally. Always build with Clang.

## First-time clone

```bash
git submodule update --init --recursive
```

## Configure & build

On Linux/macOS, configure with Clang. Pass `llvm-ar`/`llvm-ranlib` matching the
Clang version to avoid LTO plugin mismatches on the (LTO-enabled) plugin target:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_AR=$(command -v llvm-ar) -DCMAKE_RANLIB=$(command -v llvm-ranlib)
cmake --build build -j
```

If your `llvm-ar`/`llvm-ranlib` are version-suffixed (e.g. `llvm-ar-21` to match
`clang` 21), point at those explicitly:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_AR=/usr/bin/llvm-ar-21 -DCMAKE_RANLIB=/usr/bin/llvm-ranlib-21
cmake --build build -j
```

Targets: `Lockstep_Standalone` / `Lockstep_VST3` / `Lockstep_CLAP`. Artefacts land
under `build/src/Lockstep_artefacts/` (because `juce_add_plugin` lives in
`src/CMakeLists.txt`).

## Vendored-JUCE patch

Configure applies the standalone quit-guard patch to the JUCE submodule working
tree automatically and idempotently. See [PATCHES.md](PATCHES.md) for the full
flow (the submodule stays pinned to a real upstream commit; the patch is never
committed into the submodule).

## FluidLite comes from chalkwalk-soundfont

The SoundFont engine behind `Tone` is not a submodule here any more. It arrives
through `libs/soundfont`
([chalkwalk-soundfont](https://github.com/chalkwalk/chalkwalk-soundfont)), which
owns the FluidLite pin, the one-character SF3 loop-end patch, the writeup that
makes it re-derivable, and the regression test that proves it is still applied.
That test runs in this project's `ctest`, so the assertion covers the build you
made rather than one some other CI checked.

`ToneEngine` did not move: it lives in `src/tonecore` and drives FluidLite
directly. The shared repository is a vendoring, not a wrapper.

To iterate on it without a commit and a push, point at a working checkout:

```bash
cmake -B build -DCHALKWALK_SOUNDFONT_DIR=$HOME/Programming/chalkwalk-soundfont
```

Configure prints `OVERRIDE` when one is in use, because the submodule SHA no
longer describes what you built -- so CI must not use it, and neither should
anything meant to be attributable.

## Tests & gates

- Format gate: `tools/check.sh` (clang-format dry-run).
- Headless tests: `build/tests/lockstep_tests`.
- Sanitizers: configure a separate dir with `-DLOCKSTEP_SANITIZE=asan` (or `tsan`).
