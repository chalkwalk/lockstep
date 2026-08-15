# The `Tone` machine's sound bank

`GeneralUser-GS.sf3` is the bundled General MIDI bank played by the **Tone**
machine (ROADMAP 4.10, DESIGN §29.3). It is embedded in the binary via
`juce_add_binary_data` and served to FluidLite from memory through a custom
`fluid_fileapi_t` — there is no runtime file to find, and no install path to
resolve.

## What it is

**GeneralUser GS v2.0.3** by S. Christian Collins
(`mrbumpy409/GeneralUser-GS`, <https://www.schristiancollins.com/generaluser>),
converted from the 30.82 MB SF2 to SF3 (Ogg-Vorbis sample data) at **quality
0.8 → 10.07 MB**. 261 instrument presets, 13 drum kits.

The quality was chosen **by ear**, not by the size column; the full ladder was
converted and listened to. q0.8 lands just over the old "<10 MB" target, so the
target was restated rather than the bank degraded to defend a number. The
measured ladder is recorded in ROADMAP 4.10.

**SF3 buys distribution size, not footprint.** FluidLite decodes every Vorbis
sample to PCM at load, so the resident cost is the SF2's ~30 MB whenever any
track holds a Tone.

**SF3 also needs a loader patch.** FluidLite judges an SF3 sample's loop
"fowled" whenever its loop runs to the end of the sample — comparing the
spec's *exclusive* `loopend` against an *inclusive* last-sample index — and
repairs it by looping the whole sample. Most of this bank's Grand Piano
samples loop to the end, so a held piano note repeated every ~2 s, quietly,
under the decay. `patches/fluidlite-sf3-loop-offbyone.patch` (applied by the
root `CMakeLists.txt`) carries the one-character fix, the diagnosis and the
measurements; `ToneEngineTest` asserts the audio so the patch cannot go
missing quietly. **It is a loader bug, not a bank bug** — the same file plays
correctly under fluidsynth 2.4.8, as does the source SF2, which is what ruled
the conversion out.

## Licence — separate from this repository's

`GeneralUser-GS-LICENSE.txt` is the bank's own licence, verbatim and unmodified.
It is **bundled data, not linked code**, so it does not entangle the GPLv3
binary.

The grant permits use, modification and redistribution in software projects,
private or commercial. Two things must be carried forward honestly, and are
repeated here so nobody has to go looking:

- It is **permissive but not an OSI/DFSG-free licence**. Do not describe the
  bank as open source.
- The author states he **cannot be certain of every sample's origin** — the bank
  began as a personal project — and asks to be told if a restricted sample is
  found so it can be replaced. He notes no ownership complaint has been received
  since the original release in 2000.

## Do not "improve" it

The `Tone` brief is a cheap home keyboard with a questionable GM bank, and the
character is the **point** (DESIGN §29.3). Preset quality is not a bug report,
and the bank's voicing is not to be tuned. If a preset sounds cheesy, that is
the machine working.
