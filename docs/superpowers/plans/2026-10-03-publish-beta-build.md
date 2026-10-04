# Lockstep → public beta — build plan

> **Status:** plan, 2026-10-03. Self-contained: an execution session needs this
> file, `CLAUDE.md`, and nothing else from the session that wrote it.
>
> **Decisions taken at plan time** (do not re-litigate):
> 1. **"Lockstep" is the final product name.** The 6.8 rename item is closed as
>    *decided, not deferred*. `lockstep.chalkwalkmusic.com` is the docs domain,
>    `chalkwalk/lockstep` the repo.
> 2. **Ship a beta with gaps documented.** 5.3's recall unit, 9.23, 10.6, 10.11
>    and the user-gated ear tests do **not** block publication. They are listed
>    openly, in the honest register antiphon's CI comments already use
>    ("builds and tests clean" ≠ "supported").
> 3. **Modality hardening is one arc, not two passes.** Build the sweep *and*
>    funnel overlay entry through the reducer in the same arc, then fix what the
>    sweep finds. Chosen over sweep-then-refactor to avoid reworking tests.

## Why this order

Arc P (publish infrastructure) runs **before** Arc M (modality), even though M is
the more interesting work, for one reason: **nothing in this codebase has ever
been compiled on macOS or Windows.** Antiphon still marks both `experimental` for
want of a host load, and Lockstep is a far larger surface — FluidLite, Bungee,
Eigen, PFFFT, chalkwalk-tape, 74k lines tuned against Clang `-Werror`. An MSVC or
AppleClang break is the single largest unbounded risk on the path to beta, and the
cheapest way to size it is to let CI tell us in an afternoon. Arc M's cost is
bounded; Arc P's is not, until it runs once.

## Baseline, measured 2026-10-03

Verified on this tree, not assumed:

- `cmake --build build -j` → exit 0.
- `ctest --test-dir build` → **6/6 passed, 172s** (`sf3-sustain-loop`,
  `chalkwalk_tape` 79s, `chalkwalk_tape_layering`, `chalkwalk_seed`,
  `SurfaceModelTest` 54s, `DispatchGoldenTest` 40s).
- 465 tracked files, 170 MB `.git`, largest tracked blobs
  `assets/bank/GeneralUser-GS.sf3` (11 MB) and
  `tests/assets/full_song_105bpm_Dmin.wav` (8 MB) — both well under GitHub's
  100 MB per-file limit, no LFS needed.
- ~30 `TODO`/`FIXME`/`HACK` across `src/` + `libs/`.
- `tests/CUJ_CATALOGUE.md`: **33 rows, 33 implemented**, zero partial or planned.
- `gh auth status` → logged in as `chalkwalk`, ssh. No `chalkwalk/lockstep` repo
  exists yet.

## Corrections to CLAUDE.md to land first (Stage P0)

CLAUDE.md has drifted from the tree. These are wrong *today* and will mislead an
execution session:

- **`.claude/commands/` and `.claude/hooks/` do not exist.** The checkout has only
  `.claude/settings.local.json`. So `/build`, `/test`, `/asan`, `/commit-work`,
  `/align-roadmap`, `/execute`, `guard-git-staging.sh` and
  `check-bare-repaint.sh` are all documented but absent. The staging guard is the
  urgent one: it is what keeps the deliberately-untracked `AGENTS.md` /
  `CLAUDE.md` / `GEMINI.md` / `.claude/` out of commits, and the cost of it
  failing rises sharply the moment the repo is public.
- **`libs/music/` is not in this tree.** `.gitmodules` lists seven submodules —
  JUCE, clap-juce-extensions, bungee, signalsmith-dsp, soundfont, tape, seed —
  and no music. The "Canonical commands" section's `chalkwalk_music_tests` and
  its standalone-build boundary test do not apply here. (The library exists at
  `chalkwalk/chalkwalk-music`; it is simply not a dependency of Lockstep.)
- **The active-focus block is dated 2026-07-14**; `ROADMAP.md`'s own is
  2026-07-25 and is the authority. CLAUDE.md says to update `ROADMAP.md`, not
  itself — so the fix is to shrink CLAUDE.md's status section to a pointer.

**P0 tasks** — *all three done 2026-10-03.*
- [x] ~~Restore from a sibling repo~~ — **nothing to restore.** Checked: antiphon
      has no `.claude/` at all, star-canopy has only worktrees, arps-euclidya
      only a `settings.local.json`. The tooling AGENTS.md described had never
      been built anywhere, so it was **written** from AGENTS.md's own spec: six
      commands (`/build`, `/test`, `/asan`, `/commit-work`, `/align-roadmap`,
      `/execute`) and both hooks, wired in `.claude/settings.json` and proven
      live (an attempted `add` of `CLAUDE.md` was blocked, and nothing staged).
      - The guard needed one fix past its spec: it read the whole Bash command
        string, so a heredoc *documenting* the staging rule tripped it. It now
        strips heredoc bodies. `.claude/hooks/guard-cases.txt` pins 26 cases,
        including one proving a real `add` after a heredoc is still caught.
- [x] Strike the `libs/music/` references. Replaced with the six suites ctest
      actually runs and an explicit note that no such submodule exists here.
- [x] Replace the dated status prose with a pointer to `ROADMAP.md`. The durable
      half was not status and was kept: a new **Load-bearing lessons** section
      holds the verb-matrix rule, the invalidation channel, the chalkwalk-tape
      seam, the `kMinGap` trap and the seed-derivation change.

---

# Arc P — publish infrastructure

The template is antiphon (`/home/programming/antiphon`), whose three workflows
port nearly verbatim. Read them before writing ours; their comments record
measured failures (MinGW vs MSVC, the `-g` artefact-size blowup, the clang-format
version skew) that we would otherwise rediscover.

## P1 — Licence and contributor files

Lockstep links JUCE under the GPLv3 option, and `THIRDPARTY.md` already asserts
GPL release and audits every dependency for compatibility (JUCE GPLv3,
clap-juce-extensions MIT, Bungee + Eigen MPL-2.0 file-level copyleft, PFFFT
BSD-like, Signalsmith MIT, Inter OFL-1.1, FluidLite LGPL-2.1+). That audit is
already the best of the four sibling repos; it just has no `LICENSE` beside it.

- [x] `LICENSE` — GPLv3 verbatim (674 lines; md5 matches antiphon's, so it is the
      unmodified FSF text).
- [x] `CONTRIBUTING.md` — ported and adapted. Antiphon's accessibility gate is
      replaced by Lockstep's real ones: Clang-only build, the six-suite `ctest`,
      the ASan sweep with its `ASAN_OPTIONS=help=1` instrumentation check, and
      the two guards that fail the *build* rather than a test
      (`SurfaceInvalidationGuardTest`, `LayerRemapReachabilityTest`).
      - Three rules differ from the siblings and had to be stated rather than
        copied: **C++20 pinned at project scope** (per-target is an ODR hazard),
        **non-ASCII allowed in comments but not string literals** (antiphon is
        ASCII-only throughout; here `juce::String(const char*)` asserts), and
        the **no-bespoke-button** grammar rule, which settles more design
        arguments than anything else in the file.
      - "What is most needed" leads with macOS/Windows, as antiphon's does, then
        asks for **modality reports** — the `6.9` sweep cannot model a human
        getting stuck.
- [x] Add a short "Licence" section to `README.md` pointing at both files.

## P2 — GitHub repository

*Done 2026-10-03. Decided at the time: **public immediately** (nothing here is
secret, and a red first macOS tick is an honest look for a pre-beta project),
and **`master` → `main`** so the sibling workflows port unchanged.*

- [x] `gh repo create chalkwalk/lockstep --public` — https://github.com/chalkwalk/lockstep
- [x] Old remote kept as `pi`; `origin` is now GitHub. 1487 commits of history
      still have their home on the pi.
- [x] `master` renamed to `main`, pushed, default branch confirmed `main`.
- [x] Verified **against the remote, not the local index**: the GitHub contents
      API lists no `AGENTS.md` / `CLAUDE.md` / `GEMINI.md` / `.claude`. `JUCE`
      appears as a gitlink, as it should.
- [x] All seven submodule URLs confirmed reachable unauthenticated — the
      recursive checkout CI does cannot fail on a private dependency.

## P3 — Build workflow

Port `antiphon/.github/workflows/build.yml`. Lockstep-specific deltas:

- [ ] **Submodules.** `checkout@v7` with `submodules: recursive` — seven
      submodules, one of them (`bungee`) carrying its own (eigen, pffft).
- [ ] **The JUCE patch is already CI-safe.** Root `CMakeLists.txt` lines 45–88
      apply it at configure time against `CHALKWALK_JUCE_ROOT`; no workflow step
      is needed. **Verify** this path works when `CHALKWALK_JUCE_DIR` is unset
      and `JUCE/` is a freshly-checked-out submodule — that is the CI case and it
      is not the case anyone builds locally.
- [ ] **Artefact paths** are `build/src/Lockstep_artefacts/**` (`juce_add_plugin`
      lives in `src/CMakeLists.txt`), covering VST3, CLAP, Standalone and AU.
- [ ] **Strip the Linux artefacts** — same rationale and the same
      `--strip-unneeded`; confirm `clap_entry` survives.
- [ ] **Matrix:** Linux non-experimental; macOS and Windows `experimental: true`
      with names that say what they are, exactly as antiphon does. Take the flag
      off a platform the day that platform earns it, never to hide a break.
- [ ] **Expect MSVC and AppleClang warnings.** `src/CMakeLists.txt` and
      `tests/CMakeLists.txt` gate the strict set *and* `-Werror` to Clang, so
      neither new compiler will fail on warnings — but both will surface genuine
      portability errors (the two int64 narrowings the tape extraction already
      found are the shape to expect). Budget a stage for this; it is the whole
      reason Arc P runs first.
- [ ] **Test runtime.** 172s in Debug locally; Release should be faster, but
      `chalkwalk_tape` (79s) and the two golden suites (94s) dominate. If CI
      exceeds ~10 min, split the suites across jobs rather than trimming
      coverage.
- [ ] **Lint job** — `.clang-format` and `.clang-tidy` both exist. Port
      antiphon's job: clang-format pinned to `20.1.8` from PyPI and blocking;
      clang-tidy advisory until its backlog is zero. Measure Lockstep's finding
      count and record it in `ROADMAP.md` the way antiphon records its 142.

## P4 — Documentation site

- [ ] `website/` from antiphon's Docusaurus 3.9.2 / React 19 / Node 20 scaffold.
- [ ] `website/static/CNAME` → `lockstep.chalkwalkmusic.com`; `url` and
      `organizationName`/`projectName` in `docusaurus.config.ts` to match.
- [ ] `onBrokenLinks: 'throw'` stays — a dead cross-reference should fail the
      deploy, not ship.
- [ ] **Pages.** `README.md` is 2365 lines and is already a user manual with a
      table of contents: it is the source, not a thing to rewrite. Split along
      its existing seams — what Lockstep is / the paradigm / glossary / tutorial /
      feature reference / gesture tree — plus `getting-started` (install per
      format) and `troubleshooting`. **The split must not fork the manual**:
      decide at execution time whether `README.md` becomes a short landing page
      pointing at the site, or the site generates from it. Two divergent copies
      of a 2365-line gesture reference is a worse outcome than either.
- [ ] Carry CLAUDE.md's standing rule across: anything the docs mark
      *implemented* must be true. Planned may run ahead; implemented may not.
- [ ] `deploy.yml` verbatim from antiphon.
- [ ] DNS: `lockstep` CNAME → `chalkwalk.github.io`, then enable Pages with the
      custom domain and wait for the cert.

## P5 — Wiki sync

- [ ] `wiki-sync.yml` plus `.github/scripts/wiki_transform.py` from antiphon.
- [ ] Enable **Settings → Features → Wikis** and save one page by hand. GitHub
      does not create `<repo>.wiki.git` until that happens, and the workflow
      detects this and explains it rather than failing — but it still needs doing.

## P6 — Release honesty

- [ ] A `docs/` or site page listing what is **not** done, by name: the 5.3 recall
      unit (needs its own design session), 9.23 Bungee integration, 10.6
      per-track scale-quantize, 10.11 harmonic placement, 6.7 (deferred pending a
      second consumer), `Cue+Scene` / `Cue+MIDI-out`, freeze-to-disk, and every
      user-gated ear test and verification sweep.
- [ ] Close 6.8's rename item as **decided**, and strike README's "the final
      product name is not yet chosen" caveat.

---

# Arc M — modality hardening

## What is already right

Say this plainly so the arc does not get mistaken for a rewrite. The modal
architecture is sound:

- `activeModal()` (`src/ui/mode/ModalState.h`) collapses every scattered flag
  into **one 16-value `Modal` enum** with a documented priority order.
- `FuncReskin` gives the five Func-layer pickers one priority order and one exit
  (`exitFuncReskin`), and that priority mirrors `resolveActiveLayer`.
- `layerBanner` is exhaustive over 17 `SurfaceLayer` values with no `default:`,
  so a new layer without a banner is a build error.
- `LayerRemapReachabilityTest` guards the remap rule that has bitten four times,
  and its header comment measures its own sharpness by breaking it.

## The actual gap

**Entry and exit are enforced asymmetrically.**

- Exit is funnelled: 19 `escapeOverlay()` calls, one `exitFuncReskin()`, and a
  `kOverlays` descriptor table whose `ExitPolicy` has no default, so omitting a
  field is a compile error.
- Entry is not: **six raw `ui.overlay = Overlay::X` writes in
  `PluginEditor.cpp`** (SampleProps, Density, Vel, Cue, Identity, Browser) set
  the field directly, bypassing the table that owns the matching exit.

And `ModalStateTest` tests the **pure state** — it constructs a `UiState`, sets a
flag, and checks `activeModal()` agrees. It never asks whether a real gesture can
*reach* that state, or whether any gesture can leave it. Every one of the ten
defects the CUJ arc found was found end-to-end, by `UiDriver`, not by the unit
tests over the same state. That is the method this arc repeats.

## M1 — Funnel entry through the reducer

- [ ] Give each of the six overlays an entry path through `handleOverlayEvent`
      the way exit already goes through `escapeOverlay`, so the `kOverlays` row
      is the single owner of both ends of a mode's life.
- [ ] Preserve the stated separation: commit steps that touch `processor_` stay
      editor-owned; only `UiState` transitions move into the reducer. This is the
      same line `exitFuncReskin` already draws, and it is drawn for a reason.
- [ ] Add a guard test in the spirit of `SurfaceInvalidationGuardTest`: a raw
      `overlay = Overlay::` assignment outside `ModeReducer.cpp` and `UiState.h`
      fails the build unless marked with a reason comment. **Codify what bites**
      — CLAUDE.md's own rule is that a recurring mistake becomes structure, not
      prose, and this is the second time the repo has needed exactly this shape
      of guard.

## M2 — The modal sweep

A new `tests/ModalSweepTest.cpp` in `lockstep_dispatch_tests` (the only suite
that links the real headless editor + `UiDriver`).

For each of the 16 `Modal` values:

- [ ] **Entry.** Drive the documented real gesture and assert `activeModal()`
      reports that value — the `expectReached` discipline the CUJ catalogue
      already mandates. *A mode with no reachable entry gesture is itself a
      finding*, and is the single most likely thing this sweep turns up.
- [ ] **Interrupt battery.** From that state, apply each of: `Esc`; `Func`
      double-tap (the universal escape); a tap of each of the eight modifiers;
      transport start and stop; and entry of a *different* modal. After each,
      assert either a defined destination or a clean return to rest.
- [ ] **No illegal co-existence.** After every interrupt, assert exactly one
      modal is live and that the `SurfaceLayer` the surface displays is the one
      `activeModal()` reports. This is the 9.14 lesson generalised: *a row is a
      promise the matrix must keep*. Here, the banner is a promise dispatch must
      keep.
- [ ] **No stuck state.** From every post-interrupt state, assert a bounded
      escape sequence reaches `Modal::None`.

16 × ~12 ≈ 200 cases, all mechanical. `UiDriver` already supplies
`press`/`release`/`tap`/`chord`/`doubleTap`/`longPress`/`gap` on a virtual clock.

**Known harness gotchas** (from `project_cuj_suite_arc`, do not rediscover):
`gap()` between same-modifier chords or the second press latches; mute is
launch-quantized so it needs blocks run before asserting; `Track+Clear` needs a
confirm; UI tests asserting engine state need a `processBlock`.

## M3 — Fix what it finds

- [ ] Triage into: genuine modality bugs, undocumented-but-intended behaviour
      (fix the docs), and gestures that are dead (retire them — the layer-remap
      rule has already killed five, and a dead gesture wearing a label is the
      exact failure 9.14 named).
- [ ] One focused commit per fix, per the standing convention.
- [ ] Every fix gets its sweep case, so the net holds.

## M4 — Land it as a standing net

- [ ] Add a Group I to `tests/CUJ_CATALOGUE.md` for the sweep, so it is tracked
      where every other journey is tracked.
- [ ] Note in `ROADMAP.md` that the sweep is a net, not an arc: run it, and add a
      row when a new modal ships.

---

# Sequencing and commits

Docs-first ordering holds: `PRINCIPLES` → `DESIGN` → `ROADMAP` before
implementation, and a focused commit as each slice lands.

1. **P0** — CLAUDE.md corrections + restore the hooks. *Do the staging guard
   before P2.*
2. **ROADMAP** — rewrite 6.8 as the publish arc (P1–P6), add the modality arc as
   a new milestone, record the three plan-time decisions in the locked-decisions
   list.
3. **P1, P2** — licence, contributor docs, repo, push.
4. **P3** — build workflow. **Stop here and read the macOS/Windows result before
   committing to a beta date.** This is the schedule gate.
5. **Arc M** — M1 → M2 → M3 → M4, in order.
6. **P4, P5** — site and wiki. Last, because the gesture reference should
   describe modality *after* M3 has changed it.
7. **P6** — the honest gaps page, then tag the beta.

## Open questions for the execution session

- **Branch rename.** `master` → `main`, or three edited trigger blocks?
  Recommendation: rename.
- **Manual-vs-site.** Does `README.md` stay the manual with the site generated
  from it, or become a landing page? Must be decided before P4 writes a word, or
  the repo ends up with two gesture references that drift.
- **macOS AU.** `juce_add_plugin` builds AU on macOS; nothing has ever validated
  it with `auval`. Treat as untested-in-a-host alongside VST3 and CLAP, and say
  so rather than implying support.
