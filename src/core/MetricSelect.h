#pragma once

#include "Euclidean.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// MetricSelect — deterministic Scrub density selection (DESIGN §39).
// JUCE-free, header-only, pure — all functions are stateless and testable.
//
// Implements the fully-deterministic tier+Euclid model (no per-step hash):
//
//   effective = clamp(perTrack + master, 0.01, 1)
//   off       = densityScrubHash(track, 0, 0)   // fixed per-track rotation
//
//   Uniform  (whole loop, period L):
//     Tl = round(effective * L)
//     survive(loopPos) = euclidHit(loopPos, L, Tl, off)
//
//   Metric   (global bar, period N):
//     T = round(effective * N)
//     survive(barStep) = metric[T] bit barStep   // tier+Euclid, no rotation
//
//   Mixed    (global bar, period N):
//     T = round(effective * N);  P = round(0.5 * T)
//     survive(barStep) = mixed[T] bit barStep
//       where mixed[T] = metric[P]               // protect top-P (no rotation)
//                      | euclidFill(unprotected, T-P, off)  // even fill, rotated

namespace lockstep::MetricSelect
{
    // Precomputed per-bar table.
    //   metric[T] — bit s set iff step s is in the metric-importance top-T set.
    //   mixed[T]  — bit s set iff step s survives at target T in Mixed musicality:
    //               the top-P=round(0.5T) positions from metric[P], plus a
    //               per-track-rotated Euclidean fill of T-P positions from the rest.
    // n is the step count the table was built for (0 = uninitialized).
    struct Table
    {
        int n = 0;
        std::array<std::uint64_t, 65> metric{};
        std::array<std::uint64_t, 65> mixed{};
    };

    // Build a Table from per-position metric weights.
    // weights[i] = MetricGrid::metricWeight(...) for position i (i in 0..n-1).
    // trackOffset = fixed per-track rotation for the Mixed fill (and Uniform, handled
    //   at call site via euclidHit). Use densityScrubHash(track, 0, 0).
    // n must be in [1, 64]; returns a zeroed Table for invalid n.
    inline Table build(const std::array<float, 64>& weights, int n,
                       int trackOffset = 0)
    {
        Table t;
        if (n <= 0 || n > 64) return t;
        t.n = n;
        t.metric[0] = 0;
        t.mixed[0]  = 0;

        struct WP { float w; int pos; };
        std::array<WP, 64> wp{};
        for (int i = 0; i < n; ++i)
            wp[static_cast<std::size_t>(i)] = { weights[static_cast<std::size_t>(i)], i };

        // Stable sort descending by weight — equal-weight positions keep index order.
        std::stable_sort(wp.begin(), wp.begin() + n,
                         [](const WP& a, const WP& b) { return a.w > b.w; });

        // --- metric[] ---
        for (int T = 1; T <= n; ++T)
        {
            std::uint64_t mask = 0;
            int accumulated = 0;
            int i = 0;
            while (i < n && accumulated < T)
            {
                const float tierW = wp[static_cast<std::size_t>(i)].w;
                const int tierStart = i;
                while (i < n && wp[static_cast<std::size_t>(i)].w == tierW) ++i;
                const int tierSize = i - tierStart;
                const int need = T - accumulated;

                if (tierSize <= need)
                {
                    for (int j = tierStart; j < i; ++j)
                        mask |= (std::uint64_t(1) << wp[static_cast<std::size_t>(j)].pos);
                    accumulated += tierSize;
                }
                else
                {
                    // Boundary tier: select `need` of `tierSize` via Euclidean spread.
                    // No rotation on the metric mask — downbeat must stay at step 0.
                    const auto euclid = bjorklund(tierSize, need);
                    for (int j = 0; j < tierSize; ++j)
                        if (euclid[static_cast<std::size_t>(j)])
                            mask |= (std::uint64_t(1) << wp[static_cast<std::size_t>(tierStart + j)].pos);
                    accumulated += need;
                }
            }
            t.metric[static_cast<std::size_t>(T)] = mask;
        }

        // --- mixed[] ---
        // For each T, protect the top P = round(0.5*T) positions via metric[P],
        // then fill the remaining T-P slots from the unprotected positions using
        // bjorklund(n-P, T-P, trackOffset) — even and rotated per track.
        for (int T = 1; T <= n; ++T)
        {
            const int P = static_cast<int>(std::round(0.5f * static_cast<float>(T)));
            const std::uint64_t metricP = t.metric[static_cast<std::size_t>(P)];

            // Collect positions not in metric[P], ascending.
            std::array<int, 64> remaining{};
            int remCount = 0;
            for (int s = 0; s < n; ++s)
                if (!(metricP & (std::uint64_t(1) << s)))
                    remaining[static_cast<std::size_t>(remCount++)] = s;

            std::uint64_t mixedMask = metricP;
            const int fill = T - P;
            if (fill > 0 && remCount > 0)
            {
                const auto euclid = bjorklund(remCount, std::min(fill, remCount), trackOffset);
                for (int j = 0; j < remCount; ++j)
                    if (euclid[static_cast<std::size_t>(j)])
                        mixedMask |= (std::uint64_t(1) << remaining[static_cast<std::size_t>(j)]);
            }
            t.mixed[static_cast<std::size_t>(T)] = mixedMask;
        }

        return t;
    }

    // --- Survival helpers for each Scrub musicality ---

    // Metric: step survives iff it is in the metric-importance top-T set.
    inline bool metricSurvives(const Table& t, int barStep, int T) noexcept
    {
        if (t.n <= 0 || barStep < 0 || barStep >= t.n) return false;
        T = std::clamp(T, 0, t.n);
        if (T == 0) return false;
        return (t.metric[static_cast<std::size_t>(T)] & (std::uint64_t(1) << barStep)) != 0;
    }

    // Mixed: top-P metric-protected positions always kept; the rest filled by a
    // per-track-rotated Euclidean spread. Exactly T survivors over N positions.
    inline bool mixedSurvives(const Table& t, int barStep, int T) noexcept
    {
        if (t.n <= 0 || barStep < 0 || barStep >= t.n) return false;
        T = std::clamp(T, 0, t.n);
        if (T == 0) return false;
        return (t.mixed[static_cast<std::size_t>(T)] & (std::uint64_t(1) << barStep)) != 0;
    }

    // Uniform: O(1) Euclidean membership test over the whole loop (period L).
    // loopPos in [0, L); Tl = round(effective * L); offset = fixed per-track rotation.
    inline bool uniformSurvives(int loopPos, int L, int Tl, int offset) noexcept
    {
        return euclidHit(loopPos, L, Tl, offset);
    }

} // namespace lockstep::MetricSelect
