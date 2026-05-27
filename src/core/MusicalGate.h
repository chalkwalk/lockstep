#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lockstep
{
    // Musical gate duration: a discrete stepped value combining a base note
    // length (1/64 to 4 whole notes) with a modifier (plain / dotted / triplet).
    // Stored as a uint8_t index: 0 = None (no gate; play to AHDSR end), 1..27 = values.
    //
    // Encoding: index = baseIdx * 3 + modIdx + 1, where
    //   baseIdx ∈ [0..8]: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4 (fraction of whole note)
    //   modIdx  ∈ [0..2]: plain (×1), dotted (×1.5), triplet (×2/3)
    //   None    = 0

    enum class MusicalGate : uint8_t
    {
        None  = 0,
        G1_64 = 1,  G1_64d = 2,  G1_64t = 3,
        G1_32 = 4,  G1_32d = 5,  G1_32t = 6,
        G1_16 = 7,  G1_16d = 8,  G1_16t = 9,
        G1_8  = 10, G1_8d  = 11, G1_8t  = 12,
        G1_4  = 13, G1_4d  = 14, G1_4t  = 15,
        G1_2  = 16, G1_2d  = 17, G1_2t  = 18,
        G1    = 19, G1d    = 20, G1t    = 21,
        G2    = 22, G2d    = 23, G2t    = 24,
        G4    = 25, G4d    = 26, G4t    = 27,
    };

    inline constexpr int kMusicalGateCount = 28;  // 0..27

    // Labels for all 28 values (indexed by static_cast<uint8_t>(gate)).
    inline constexpr const char* kMusicalGateLabels[kMusicalGateCount] =
    {
        "--",
        "1/64",  "1/64.", "1/64T",
        "1/32",  "1/32.", "1/32T",
        "1/16",  "1/16.", "1/16T",
        "1/8",   "1/8.",  "1/8T",
        "1/4",   "1/4.",  "1/4T",
        "1/2",   "1/2.",  "1/2T",
        "1",     "1.",    "1T",
        "2",     "2.",    "2T",
        "4",     "4.",    "4T",
    };

    // Convert a MusicalGate value to duration in seconds at the given BPM.
    // Returns 0.0 for None (no scheduled note-off).
    inline double musicalGateToSeconds(MusicalGate g, double bpm) noexcept
    {
        const auto idx = static_cast<int>(static_cast<uint8_t>(g));
        if (idx <= 0 || bpm <= 0.0) { return 0.0; }

        // Fraction of a whole note for each base index.
        static constexpr double kBaseFractions[9] =
            { 1.0/64.0, 1.0/32.0, 1.0/16.0, 1.0/8.0,
              1.0/4.0,  1.0/2.0,  1.0,       2.0,     4.0 };

        const int baseIdx = (idx - 1) / 3;
        const int modIdx  = (idx - 1) % 3;

        double frac = kBaseFractions[baseIdx];
        if      (modIdx == 1) { frac *= 1.5; }
        else if (modIdx == 2) { frac *= (2.0 / 3.0); }

        // whole note = 4 quarter-note beats
        return frac * 4.0 * (60.0 / bpm);
    }

    // Convert to whole samples (rounded).
    inline int musicalGateToSamples(MusicalGate g, double bpm, double sampleRate) noexcept
    {
        const double sec = musicalGateToSeconds(g, bpm);
        if (sec <= 0.0) { return 0; }
        return static_cast<int>(std::lround(sec * sampleRate));
    }

    // Find the nearest MusicalGate for a gate duration in milliseconds at BPM.
    // Returns None for gateMs <= 0. Falls back to 120 BPM if bpm <= 0.
    inline MusicalGate nearestMusicalGate(float gateMs, double bpm) noexcept
    {
        if (gateMs <= 0.0f) { return MusicalGate::None; }
        if (bpm <= 0.0) { bpm = 120.0; }
        const double targetSec = static_cast<double>(gateMs) * 0.001;
        int    best    = 0;
        double bestErr = std::fabs(targetSec);  // distance from None (0 s)
        for (int i = 1; i < kMusicalGateCount; ++i)
        {
            const double sec = musicalGateToSeconds(static_cast<MusicalGate>(i), bpm);
            const double err = std::fabs(targetSec - sec);
            if (err < bestErr) { bestErr = err; best = i; }
        }
        return static_cast<MusicalGate>(best);
    }
}
