# Contributing to Lockstep

Lockstep is a performance-oriented step sequencer you play like an instrument.
It is licensed **GPLv3** (JUCE is used under its GPL option) — contributions are
accepted on those terms. `THIRDPARTY.md` records every dependency's licence and
the obligations that travel with it.

Read [`PRINCIPLES.md`](PRINCIPLES.md) before proposing anything structural, and
[`NON-GOALS.md`](NON-GOALS.md) before proposing a feature. A surprising number
of reasonable-sounding requests already have a written answer there, and the
answer is usually "not that, but here is the thing we offer instead". If your
idea cannot be expressed within the principles, say so in the issue — that is a
useful conversation, not a rejection.

**One rule governs more design arguments here than any other:** every key earns
its place in a small scope + verb grammar, and *adding a bespoke
single-purpose button is forbidden*. If a feature needs its own key, it is not
ready. See `DESIGN.md` §13.

## 1. What is most needed

In priority order. The first item is worth more than everything below it
combined.

1. **Building and running Lockstep somewhere that is not Linux.** Every line of
   this has been built and tested on Linux with Clang. The macOS and Windows CI
   jobs are marked experimental because nothing has ever *loaded* the plugin in
   a host on either — no window opened, no device opened. Compilation fixes,
   runtime reports, and "it crashed on load in Ableton" are all valuable. This
   is the single biggest gap between "works" and "released".
2. **Testing in more DAWs and more formats.** CLAP and VST3 have had far more
   exercise than AU, which has had none at all — it is compiled only by CI, no
   host has ever loaded it, and it has never been through `auval`. If you have
   Logic Pro or GarageBand, simply reporting whether Lockstep appears and passes
   audio is a real contribution.
3. **Modality reports.** Lockstep has sixteen modal states and a grammar of
   eight modifiers and five verbs. If you get *stuck* in a mode, or a key does
   nothing when its label says it should, that is the most useful bug report
   this project can receive — say exactly which keys you pressed, in order, and
   whether any were still held. Milestone `6.9` is building an automated sweep
   for this, but a human finds things a sweep cannot model.
4. **Ear reports on the DSP.** Several items are marked "pending ear test" in
   `ROADMAP.md` — time-stretch quality, the effects A/B matrix, varispeed
   texture. These need listening, not measuring.
5. **Ordinary bugs and UI work**, once the above are moving.

## 2. Issues before pull requests

- **Architectural changes, new features, large refactors:** open an issue first
  and describe the change against `PRINCIPLES.md`. Work that arrives as a
  surprise pull request may be turned down for reasons that would have taken one
  paragraph to establish beforehand.
- **Bug fixes, documentation, typos, build fixes:** just open the pull request.

Bug reports are most useful with the platform, DAW, plugin format, sample rate
and buffer size — and for anything gestural, the exact key sequence.

## 3. Workflow

1. Fork, and branch from the default branch.
2. Make the change, with tests (see §5).
3. Commit describing the **effect rather than the mechanism**. Read
   `git log --oneline -20` for the register; it is declarative and concrete, not
   conventional-commits prefixes.
4. **Documentation lands before or with the code**, never after — see §7.
5. Open a pull request.

## 4. Coding standards

- **C++20, and Clang.** C++20 is pinned project-wide (`CMAKE_CXX_STANDARD 20` in
  the root `CMakeLists.txt`); per-target `cxx_std_20` is *not* sufficient,
  because `juce_add_plugin` floors at `cxx_std_17` PRIVATE and the mismatch
  creates an ODR hazard around `__cpp_char8_t`-gated JUCE inlines. Toolchain
  floor: Clang 10 / GCC 10 / MSVC 19.29.

  > **Build with Clang, and this is load-bearing, not a preference.** The strict
  > warning set and `-Werror` are gated to Clang in `src/CMakeLists.txt` and
  > `tests/CMakeLists.txt`. A **GCC build silently skips all of it** and falls
  > back to JUCE's recommended flags with no `-Werror`, so warnings that fail CI
  > pass on your machine. See [`docs/BUILD.md`](docs/BUILD.md).

  The set is `-Wall -Wextra -Wpedantic -Wsign-conversion -Wfloat-conversion
  -Wshadow -Wimplicit-fallthrough -Werror`.

- **Switch hygiene.** Every `case` carrying code ends in
  `break`/`return`/`throw`/`[[fallthrough]]`; intentional fall-through needs
  `[[fallthrough]];` **plus** a comment naming where it falls to. There is **no**
  "always add `default:`" rule — switches exhaustive over a closed enum omit it
  on purpose so `-Wswitch` flags a new unhandled value. Do not add a `default:`
  that masks one.

- **Non-ASCII: comments yes, string literals almost never.** Unrestricted in
  comments. In *string literals*, keep to a small deliberate set (icon glyphs,
  ellipsis, em dash) and never write `juce::String s = "\xee\x84\x82";` — it
  compiles and asserts at runtime, because `juce::String(const char*)` asserts
  on any non-ASCII byte. Use `u8"…"` (binds the `const char8_t*` overload) or
  `juce::String { juce::CharPointer_UTF8("…") }`.

  *(This differs from the sibling projects, which are ASCII-only throughout.)*

- **`clang-format` is enforced at a pinned version.** Configuration is
  `.clang-format` in the root. Run `clang-format -i` on what you touched, not on
  files you did not. **Use the version CI pins** — major versions disagree with
  each other (18 and 20 format `({})` differently), so a mismatch fails a gate
  on lines you never edited:

  ```bash
  pip install "clang-format==20.1.8"
  ```

- **`clang-tidy`** is configured in `.clang-tidy` and runs advisory while an
  existing backlog is worked down. Do not add new findings.

- **Single owner for shared state.** A value with more than one reader or writer
  routes through one setter; do not hand-sync two stores. Track length goes
  through `setTrackLength`, never a raw `Track.length` write. Surface redraw goes
  through `refreshSurface()`, never a bare `repaint()` — and that one is enforced
  at build time, see §5.

- **Audio-thread code must not allocate, lock, do file I/O, or log.**

- Further conventions, and the traps that cost real time, are in
  [`AGENTS.md`](AGENTS.md). It is written for AI assistants working in this
  repository but is accurate for humans too, and it is the fastest way to
  understand the codebase's rules — in particular its "Load-bearing lessons"
  section, which is a list of bugs that already happened.

## 5. Testing

Lockstep has a real test suite and it is not optional.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j $(nproc)
ctest --test-dir build --output-on-failure
```

Six suites, around three minutes in Debug. Two are ours (`SurfaceModelTest`,
`DispatchGoldenTest`); the rest come from the vendored libraries and run here on
purpose, so that — for instance — the SF3 sustain-loop assertion covers the
FluidLite we actually built rather than a pristine upstream one.

Rules worth stating here:

- **Every new input modality ships with a unit test.** Any new meta-band, sticky
  mode, or scope-routing path needs coverage of *resolution* (does the right
  value reach the band?), *scope routing* (does the held modifier land writes in
  the right scope?), and a *round trip* where state is involved (write →
  serialise → deserialise → value survives). Corollary, meant literally: if you
  add a modality without a test, assume it is broken.

- **Audio-path tests must call `proc.setRateAndBufferSizeDetails(44100, 512)`
  before the first block.** Without it `getSampleRate()` returns 0, gate lengths
  collapse to zero, and the machine produces silence — a harness artifact that
  has been mistaken for a DSP bug more than once.

- **Two guards fail the build rather than a test.** `SurfaceInvalidationGuardTest`
  rejects a bare `repaint()` in the surface-owning files, and
  `LayerRemapReachabilityTest` rejects a layer remap that silently swallows a
  compound gesture. Both exist because the failure they catch is invisible on
  screen. If one fires, read its header comment — each explains the bug it is
  preventing and why that bug kept coming back.

- **Whole-journey tests live in `tests/CUJ_CATALOGUE.md`.** It is the automated
  equivalent of a manual test sheet, written the way a person performs the task.
  If you ship a new user-facing flow, add a journey.

- **Sanitiser sweep for anything touching memory, buffers or lifetimes:**

  ```bash
  cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLOCKSTEP_SANITIZE=asan \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
  cmake --build build-asan --target lockstep_tests lockstep_dispatch_tests -j $(nproc)
  ctest --test-dir build-asan/tests --output-on-failure
  ```

  **Name the compiler** — a sanitiser tree left to the default `c++` has
  silently become GCC before. Build the **test targets only**: VST3 and CLAP are
  shared modules and the sanitiser runtime is deliberately absent from them, so
  a whole-tree build hard-errors on `__asan_*` by design.

  **Then confirm the sweep was actually instrumented**, which has self-disabled
  three separate ways. Ask the runtime, not the symbol table — `nm -D` reports
  nothing useful because Clang links the runtime statically:

  ```bash
  ASAN_OPTIONS=help=1 build-asan/tests/lockstep_tests 2>&1 | head -1
  # -> "Available flags for AddressSanitizer:"
  ```

Anything touching the UI or the audio thread also wants a Standalone launch
before you call it done:
`build/src/Lockstep_artefacts/Debug/Standalone/Lockstep`.

## 6. Documentation

Ship the documentation change with the code change — and for anything
structural, *before* it.

The ordering is **`PRINCIPLES.md` → `DESIGN.md` → `ROADMAP.md`**, and it is not
ceremonial: if a feature cannot be expressed within the existing scope + verb
grammar (`DESIGN.md` §13), it is not ready to be a roadmap item, let alone a
patch. Each root file owns one job and a fact belongs in exactly one of them:

| File | Owns |
|---|---|
| `PRINCIPLES.md` | The rules every feature must satisfy |
| `DESIGN.md` | Architecture, the grammar, surface layout |
| `NON-GOALS.md` | What Lockstep refuses to become, and why |
| `ROADMAP.md` | Milestones and status — **the only authority on status** |
| `README.md` | The user manual |
| `THIRDPARTY.md` | Dependency licences and obligations |
| `AGENTS.md` | Conventions, gotchas, agent workflow |

**`README.md` must stay true.** Anything it marks as *implemented* has to be
correct — real bindings, real behaviour. It may run ahead of reality for
*planned* items. When a planned feature ships, move it into the working set in
the same change and verify the shortcut table.

Shipped roadmap work is compressed in place rather than deleted; the convention
is documented in the `ROADMAP.md` preamble.
