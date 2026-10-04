---
sidebar_position: 3
title: "Troubleshooting"
---

# Troubleshooting {#troubleshooting}

## The build {#the-build}

**CMake says "No JUCE."**
The submodules are not checked out. Run
`git submodule update --init --recursive`. The error says this, and also
mentions `CHALKWALK_JUCE_DIR`, which points at a shared JUCE checkout instead —
useful if you build several JUCE projects and do not want a copy of it per
repository.

**The build succeeded but warnings you expected are missing.**
Check which compiler you actually got:

```bash
grep -E "^CMAKE_CXX_COMPILER:" build/CMakeCache.txt
```

If that says `/usr/bin/c++` you are probably building with GCC, and the strict
warning set and `-Werror` are both gated to Clang — so the build compiles your
code without enforcing the project's standard. Configure with
`-DCMAKE_CXX_COMPILER=clang++`.

**A dependency behaves unlike the committed version.**
Check for an override:

```bash
grep -i "CHALKWALK.*DIR" build/CMakeCache.txt
```

`CHALKWALK_<LIB>_DIR` redirects a vendored library to a working checkout, which
is how a library change is tested without a commit. While one is set, the
submodule SHA no longer describes what you built.

**MSVC or AppleClang fails where Linux did not.**
Expected, and worth reporting. Linux is the only platform with real mileage.
Known classes: MSVC does not define `M_PI` without `_USE_MATH_DEFINES`; Apple's
libc++ has never shipped the C++17 special maths functions; and
`std::array::const_iterator` is a raw pointer in libstdc++ and libc++ but a
class type in MSVC's standard library, so `const auto*` does not bind to it.

## Running it {#running-it}

**The standalone opens but is silent.**
That is correct for an empty project — Lockstep makes no sound until a track
has a machine and the sequence has trigs.

**Audio-path behaviour differs between the standalone and the plugin.**
Both are meant to be co-equal, and a difference is a bug worth reporting with
the host, format, sample rate and buffer size.

**A key does nothing, or you get stuck in a mode.**
This is the most useful bug report Lockstep can receive right now. The surface
has sixteen modal states and a grammar of eight modifiers and five verbs, and
the known weak point is that mode *entry* and mode *exit* are enforced
asymmetrically. Say exactly which keys you pressed, in order, and whether any
were still held when it went wrong.

## Reporting {#reporting}

Open an issue at
[github.com/chalkwalk/lockstep/issues](https://github.com/chalkwalk/lockstep/issues).
Include your platform, DAW, plugin format, sample rate and buffer size — and
for anything gestural, the exact key sequence.
