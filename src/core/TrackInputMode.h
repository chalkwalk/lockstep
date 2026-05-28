#pragma once

#include <array>
#include <cstdint>

namespace lockstep
{
    // Per-track input mode. Determines how incoming MIDI and step-key presses
    // are interpreted on the focused track. Applies to the focused track only.
    // Default is PLAY (standard step-toggle + P-Lock editing).
    //
    // RAM-only; not serialised until play-testing proves it performance-sticky.
    enum class TrackInputMode : std::uint8_t
    {
        Play      = 0,  // Default: step grid is the trig/P-Lock editor.
        Chromatic = 1,  // Step cells are a chromatic piano keyboard for live play.
        Levels    = 2,  // Step cells are 16 velocity buckets (1/16..16/16 of 127).
    };

    // Piano layout for CHROMATIC mode.
    // Maps step index (0-15) to semitone offset from the root C, or -1 (dead key).
    //
    // Physical layout: bottom row (steps 8-15, keys C V B N M , . /) = white keys;
    //                  top row    (steps 0-7,  keys D F G H J K L ;) = black keys + 3 dead.
    //
    //   top row:    dead  C#   D#  dead  F#   G#  A#  dead
    //               [ 0] [ 1] [ 2] [ 3] [ 4] [ 5] [ 6] [ 7]
    //   bottom row:  C    D    E    F    G    A    B    C+1
    //               [ 8] [ 9] [10] [11] [12] [13] [14] [15]
    static constexpr std::array<int, 16> kPianoNoteOffset = {{
        -1,  // step  0 (D) = dead
         1,  // step  1 (F) = C#
         3,  // step  2 (G) = D#
        -1,  // step  3 (H) = dead
         6,  // step  4 (J) = F#
         8,  // step  5 (K) = G#
        10,  // step  6 (L) = A#
        -1,  // step  7 (;) = dead
         0,  // step  8 (C) = C
         2,  // step  9 (V) = D
         4,  // step 10 (B) = E
         5,  // step 11 (N) = F
         7,  // step 12 (M) = G
         9,  // step 13 (,) = A
        11,  // step 14 (.) = B
        12,  // step 15 (/) = C (next octave)
    }};

    // Display names for each piano cell (parallel to kPianoNoteOffset); nullptr = dead key.
    static constexpr std::array<const char*, 16> kPianoNoteNames = {{
        nullptr, "C#", "D#", nullptr, "F#", "G#", "A#", nullptr,
        "C",     "D",  "E",  "F",    "G",  "A",  "B",  "C",
    }};
}
