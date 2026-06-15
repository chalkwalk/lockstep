#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// MetricGrid — music-theoretically correct metric-weight primitive.
// JUCE-free, header-only, pure — all functions are stateless and testable.
//
// Based on the Lerdahl–Jackendoff metrical hierarchy: a bar position's weight
// equals the number of hierarchical levels at which it heads a rhythmic group.
// Metric grouping is inferred from the time-signature numerator alone via
// recursive binary/ternary partition (denominator only sets the pulse note value):
//
//   4/4  (4 pulses):  [3,1,2,1]
//   3/4  (3 pulses):  [2,1,1]
//   6/8  (6 pulses):  [3,1,1,2,1,1]      (2 dotted-beat groups ≠ 3/4)
//   9/8  (9 pulses):  [3,1,1,2,1,1,2,1,1]
//   7/8  (7 pulses):  [3,1,1,2,1,2,1]    (front-loaded 3+2+2)
//
// Sub-pulse positions (between pulses) are ranked by a power-of-two sub-grid
// using trailing-zero depth: the "&" (half-beat) outranks "e/a" (quarter-beat).
//
// Reproduces the old 16-step 4/4 trailing-zero grid: counts 5/4/3/2/1 map to
// downbeat / beat-3 / beats-2,4 / "&" / 16th-offbeat.

namespace lockstep::MetricGrid
{
    // -------------------------------------------------------------------------
    // Internal: recursive L-J pulse-weight tree

    struct SpanSplit
    {
        std::array<int, 8> children{};
        std::size_t count = 0;
    };

    // Split n into ordered children summing to n (binary → ternary → greedy 3s).
    static inline SpanSplit splitSpan(int n) noexcept
    {
        SpanSplit s{};
        if (n <= 1) { return s; }
        if (n % 2 == 0)
        {
            s.children[0] = n/2; s.children[1] = n/2; s.count = 2;
            return s;
        }
        if (n % 3 == 0)
        {
            s.children[0] = n/3; s.children[1] = n/3; s.children[2] = n/3; s.count = 3;
            return s;
        }
        // Greedy front-loaded 3s, then handle the remainder (avoid leftover 1).
        int rem = n;
        while (rem > 4) { s.children[s.count++] = 3; rem -= 3; }
        if (rem == 4)
        {
            s.children[s.count++] = 2;
            s.children[s.count++] = 2;
        }
        else
        {
            s.children[s.count++] = rem; // rem == 2 or 3
        }
        return s;
    }

    // Recursively assign L-J hierarchy counts into w (size kMaxPulses, zero-init).
    // w[start] is incremented at every level that begins at `start`.
    static inline void assignWeights(std::array<int, 32>& w,
                                     std::size_t start, int len) noexcept
    {
        w[start] += 1;
        const SpanSplit s = splitSpan(len);
        std::size_t off = 0;
        for (std::size_t i = 0; i < s.count; ++i)
        {
            const auto child = static_cast<std::size_t>(s.children[i]);
            assignWeights(w, start + off, s.children[i]);
            off += child;
        }
    }

    // -------------------------------------------------------------------------
    // Sub-pulse constants

    // Power-of-two sub-step grid within each pulse interval.
    // 4 → matches a 16th-note sub-grid at the pulse level (quarter-note in 4/4):
    //   sub_k ∈ {1,2,3}; tz(2)=1 → the "&"; tz(1)=tz(3)=0 → "e/a" (finest).
    static constexpr int kSubGridSize = 4;

    // Extra hierarchy levels added to every on-pulse position.
    // Must exceed max trailing-zeros among non-zero sub_k values (tz(2)=1 < 2). ✓
    static constexpr int kSubLevels = 2;

    // -------------------------------------------------------------------------
    // Public API

    // Returns the metric weight of a bar position in [0,1].
    //   1.0 = bar downbeat (strongest).
    //   0.0 = finest sub-pulse offbeat (weakest).
    //
    // numerator  — time-signature numerator (number of pulses per bar).
    // denominator — unused (only sets the pulse note value, not grouping).
    // ppqInBar   — step's PPQ position within the bar (wraps via fmod).
    // barPpq     — bar length in the same PPQ unit as ppqInBar.
    inline float metricWeight(double ppqInBar, double barPpq,
                              int numerator, int /*denominator*/) noexcept
    {
        if (barPpq <= 0.0 || numerator <= 0) { return 1.0f; }

        const double phase = std::fmod(ppqInBar, barPpq) / barPpq; // [0,1)

        // --- Pulse-tree weights ---
        constexpr int kMaxPulses = 32;
        std::array<int, kMaxPulses> w{};
        const int numPulses = (numerator < kMaxPulses) ? numerator : kMaxPulses;
        assignWeights(w, 0u, numPulses);

        // Downbeat heads every level → w[0] is the maximum; kSubLevels adds the
        // sub-pulse contribution that every on-pulse position also carries.
        const int downbeatCount = w[0] + kSubLevels;
        if (downbeatCount <= 1) { return 1.0f; } // degenerate: numerator=1

        // --- Locate the nearest pulse and fractional offset within it ---
        const double pulseFloat = phase * static_cast<double>(numPulses);
        const auto   pulseIdx   = static_cast<std::size_t>(
            static_cast<int>(std::floor(pulseFloat)) % numPulses);
        const double f          = pulseFloat - std::floor(pulseFloat); // ∈ [0,1)

        // --- Sub-pulse index (round f to kSubGridSize steps) ---
        const int subK = static_cast<int>(
            std::round(f * static_cast<double>(kSubGridSize)));

        int count;
        if (subK == 0)
        {
            // On the current pulse.
            count = w[pulseIdx] + kSubLevels;
        }
        else if (subK == kSubGridSize)
        {
            // Very close to the next pulse boundary (f ≥ 0.875 with kSubGridSize=4).
            const auto nextIdx = (pulseIdx + 1u) % static_cast<std::size_t>(numPulses);
            count = w[nextIdx] + kSubLevels;
        }
        else
        {
            // Between pulses: more trailing zeros = higher in sub-hierarchy.
            // "&" (subK=2, tz=1) outranks "e/a" (subK=1,3, tz=0); min count = 1.
            unsigned tz = 0u;
            auto tmp = static_cast<unsigned>(subK);
            while ((tmp & 1u) == 0u) { ++tz; tmp >>= 1u; }
            count = static_cast<int>(tz) + 1; // min 1 → normalises to 0.0
        }

        return std::clamp(static_cast<float>(count - 1) /
                          static_cast<float>(downbeatCount - 1),
                          0.0f, 1.0f);
    }

} // namespace lockstep::MetricGrid
