# Vendored-dependency patches

Lockstep pins the `JUCE` and `clap-juce-extensions` submodules to **real
upstream commits** so a fresh `git submodule update --init --recursive`
always succeeds. Any local modification to a vendored dependency is carried
as a patch file in `patches/` and applied at CMake **configure time** —
never as a submodule commit (a forked submodule SHA can't be fetched from
the public upstream, which breaks every clone).

The patch is applied to whichever JUCE tree the build selected --
this repository's submodule, or the shared checkout named by
`CHALKWALK_JUCE_DIR` (`cmake/JuceSource.cmake`). The shared checkout therefore
carries the UNION of the ecosystem's JUCE patches; they touch disjoint files
today, so the union is well defined.

## Active patches

| Patch | Target | Why |
|-------|--------|-----|
| `patches/juce-standalone-onCloseRequested.patch` | `JUCE/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h` | Adds an `onCloseRequested` callback so the standalone quit guard (ROADMAP 9.2 / D2) can intercept window close and run the Save/Discard/Cancel dirty-guard. Stock JUCE quits immediately. |

## How it works

The root `CMakeLists.txt` (just before `add_subdirectory(JUCE)`) runs
`git apply` against the JUCE submodule working tree. It is **idempotent**:
`git apply --reverse --check` first tests whether the patch is already
present, so re-configuring never double-applies or errors.

Consequences:

- After the first configure, `git status` in the superproject shows the
  `JUCE` submodule as *modified content* (the applied patch). That is
  expected — do **not** commit the submodule pointer in that state.
- `git submodule update` will complain the working tree is dirty. To move
  JUCE to a new upstream commit: `git -C JUCE stash` (or check out the new
  ref), bump the gitlink, then regenerate the patch against the new base
  (see below) and re-configure.

## Regenerating a patch after a JUCE bump

```bash
# In the JUCE submodule, apply your edit, then diff against the pinned base:
git -C JUCE diff <pinned-upstream-sha> -- <path/to/file> \
    > patches/<name>.patch
# Reset the submodule working tree back to the pinned commit so the gitlink
# stays clean; CMake re-applies the patch on the next configure.
git -C JUCE checkout -- <path/to/file>
```

Keep patches minimal and self-contained; prefer adding an opt-in hook
(zero-cost when unused) over changing default JUCE behaviour.
