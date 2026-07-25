# Test audio assets — licence and contents

## Licence

These three audio files are **licensed separately from the rest of this repository**.

- **Author:** the Lockstep project owner, who composed and rendered them specifically
  as test fixtures for this suite.
- **Grant:** [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)
  (public domain dedication). Anyone who clones, forks or vendors this repository may
  use, modify and redistribute them for any purpose, with no attribution required.

**Why a separate grant.** The code here is expected to ship under GPL-3.0. A copyleft
*software* licence applies awkwardly to a rendered audio file — "corresponding source"
has no useful meaning for a bounced WAV, and a test fixture should never be the reason
someone hesitates to fork the tests. CC0 removes the question entirely: the code
licence governs the code, and these three files carry their own dedication.

*(To change the grant, edit this file — it is the single statement of record, and
nothing in the test code depends on which licence is named.)*

## Contents

All three are PCM WAV, **16-bit stereo, 48 kHz**, with no lead-in silence.

| File | Musical facts | Measured |
|---|---|---|
| `drum_loop_110bpm.wav` | 110 BPM, 4/4, **2 bars** (4.36 s) | peak 0.99, ~16 onsets (eighths), true stereo, loops flush at both ends |
| `music_loop_95bpm_Cmin.wav` | 95 BPM, **C minor**, 4 bars (10.11 s) | peak **0.24 (≈ −12 dBFS)**, ~3 onsets — sustained, not percussive |
| `full_song_105bpm_Dmin.wav` | 105 BPM, **D minor**, 43.43 s | peak 1.00, ends with **≈ 2 s of silence** |

## What each one is for, and what to watch

- **`drum_loop_110bpm.wav`** — the transient fixture. Slice-point detection, take
  chopping, anything that has to *find* onsets. It is the only file here with real
  percussive attacks; use it whenever a test needs transients rather than sound.
- **`music_loop_95bpm_Cmin.wav`** — the musical fixture: key detection (C minor),
  tempo-aware loop fitting, pitch-preserving stretch. **It is quiet.** A test that
  asserts an absolute amplitude threshold should not assume a near-unity peak — assert
  *non-silence* or compare against a measured baseline, or it will fail for a reason
  that has nothing to do with the behaviour under test.
- **`full_song_105bpm_Dmin.wav`** — the long-form bed: StreamMachine's disk streaming,
  tape-length behaviour, capture over a real duration. **Its last ~2 s are silent**, so
  a test that reads audio near the end of the file will read zeros and conclude the
  stream died. Sample from the body, or account for the tail deliberately.

Both quirks are properties of the material, not defects: they are recorded here so the
next person to write a test against these files does not have to rediscover them by
watching a test fail.
