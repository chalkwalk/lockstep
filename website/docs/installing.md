---
sidebar_position: 2
title: "Installing"
---

# Installing

There are **no released builds yet**. Lockstep is built from source.

CI produces artefacts for Linux and macOS on every push to `main` — VST3, CLAP,
standalone, and the macOS AU — and you can download those from a run's
**Artifacts** section on the
[Actions tab](https://github.com/chalkwalk/lockstep/actions). They are
unsigned, untagged, and built from whatever `main` was at the time. Treat them
as something to experiment with, not as a release.

## Building from source

Lockstep needs **Clang** and **C++20**, and Clang is load-bearing rather than a
preference: the strict warning set and `-Werror` are gated to it, so a GCC build
silently compiles with neither.

```bash
git clone --recurse-submodules https://github.com/chalkwalk/lockstep.git
cd lockstep

cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`--recurse-submodules` is not optional. There are seven of them, and one
(Bungee) carries its own.

### Linux build dependencies

On Debian or Ubuntu:

```bash
sudo apt-get install -y \
  libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
  libfreetype6-dev libfontconfig1-dev libcurl4-openssl-dev \
  libgl1-mesa-dev libglu1-mesa-dev ninja-build
```

### Where the artefacts land

`juce_add_plugin` lives in `src/CMakeLists.txt`, so everything is built under
`build/src/`, not `build/`:

```
build/src/Lockstep_artefacts/Release/Standalone/Lockstep
build/src/Lockstep_artefacts/Release/VST3/Lockstep.vst3
build/src/Lockstep_artefacts/Release/CLAP/Lockstep.clap
build/src/Lockstep_artefacts/Release/AU/Lockstep.component   (macOS)
```

Copy the VST3 or CLAP into wherever your host scans, or run the standalone
directly.

## Platform caveats worth knowing before you start

- **Linux** is the developed and tested platform.
- **macOS** compiles and passes the full suite in CI, but no host has ever
  loaded the plugin and the AU has never been through `auval`. Expect to be the
  first person to find out.
- **Windows** compiles under MSVC. Its two GUI test suites are excluded in CI
  because the runner has no interactive desktop, so they are unrun rather than
  passing.

If something goes wrong on macOS or Windows, that is a genuinely valuable bug
report rather than a nuisance — it is the top item in
[CONTRIBUTING.md](https://github.com/chalkwalk/lockstep/blob/main/CONTRIBUTING.md).
