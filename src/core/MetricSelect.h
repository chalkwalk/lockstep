#pragma once

#include "Euclidean.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// MetricSelect — deterministic Scrub density selection (DESIGN §39).
// JUCE-free, header-only, pure — all functions are stateless and testable.
//
// Implements the tier+Euclid model:
//   T = round(effective * N)   — target survivor count over the bar grid
//   P = round(m * T)           — count protected by metric importance
//
//   survive(s) =
//     metricMask[P] bit s      — s in the top-P metric set → keep
//     OR hashFrac < (T−P)/(N−P) — else uniform fill over the remainder
//
// The metric mask for T is built by filling importance tiers strongest-first;
// when a tier is partially needed, bjorklund(tierSize, k) evenly spreads
// the k slots. This is recomputed per density level — monotonic membership
// is NOT a goal; uniform spread at each count is.

namespace lockstep::MetricSelect
{
    // Precomputed per-bar table. mask[T] is a uint64_t where bit s is set iff
    // step-in-bar position s is in the metric-importance top-T set.
    // n is the step count the table was built for (0 = uninitialized).
    struct Table
    {
        int n = 0;
        std::array<std::uint64_t, 65> mask{};
    };

    // Build a Table from per-position metric weights.
    // weights[i] = MetricGrid::metricWeight(...) for position i (i in 0..n-1).
    // n must be in [1, 64]; returns a zeroed Table for invalid n.
    inline Table build(const std::array<float, 64>& weights, int n)
    {
        Table t;
        if (n <= 0 || n > 64) return t;
        t.n = n;
        t.mask[0] = 0;

        struct WP { float w; int pos; };
        std::array<WP, 64> wp{};
        for (int i = 0; i < n; ++i)
            wp[static_cast<std::size_t>(i)] = { weights[static_cast<std::size_t>(i)], i };

        // Stable sort descending by weight — equal-weight positions keep index order.
        std::stable_sort(wp.begin(), wp.begin() + n,
                         [](const WP& a, const WP& b) { return a.w > b.w; });

        for (int T = 1; T <= n; ++T)
        {
            std::uint64_t mask = 0;
            int accumulated = 0;
            int i = 0;
            while (i < n && accumulated < T)
            {
                // Identify the extent of this tier (all positions with the same weight).
                const float tierW = wp[static_cast<std::size_t>(i)].w;
                const int tierStart = i;
                while (i < n && wp[static_cast<std::size_t>(i)].w == tierW) ++i;
                const int tierSize = i - tierStart;
                const int need = T - accumulated;

                if (tierSize <= need)
                {
                    // Whole tier fits — include all positions in this tier.
                    for (int j = tierStart; j < i; ++j)
                        mask |= (std::uint64_t(1) << wp[static_cast<std::size_t>(j)].pos);
                    accumulated += tierSize;
                }
                else
                {
                    // Boundary tier: select `need` of `tierSize` via Euclidean distribution.
                    // Per-count recompute — spread uniformity at each density matters more
                    // than monotonic add/remove across density levels.
                    const auto euclid = bjorklund(tierSize, need);
                    for (int j = 0; j < tierSize; ++j)
                    {
                        if (euclid[static_cast<std::size_t>(j)])
                            mask |= (std::uint64_t(1) << wp[static_cast<std::size_t>(tierStart + j)].pos);
                    }
                    accumulated += need;
                }
            }
            t.mask[static_cast<std::size_t>(T)] = mask;
        }

        return t;
    }

    // Deterministic Scrub survival decision.
    // stepInBar — position within the bar's grid [0, n).
    // T         — target survivor count = round(effective * n).
    // P         — metric-protected count = round(m * T).
    // hash      — Density::densityScrubHash(track, stepPos, qLevel).
    //
    // Boundary behaviour:
    //   Uniform (P=0): purely hash < T/N — identical to old Scrub-uniform.
    //   Metric  (P=T): purely metricMask[T] — tier+Euclid, no hash.
    //   Mixed   (P=T/2): top T/2 protected, remainder hash-filled at (T-P)/(N-P).
    inline bool scrubSurvives(const Table& t, int stepInBar, int n, int T, int P,
                               std::uint32_t hash) noexcept
    {
        if (n <= 0) return true;
        T = std::clamp(T, 0, n);
        P = std::clamp(P, 0, T);
        if (stepInBar < 0 || stepInBar >= n) return false;
        if (T == 0) return false;

        // Metric-protected set: check mask[P].
        if (P > 0)
        {
            const std::uint64_t bit = std::uint64_t(1) << stepInBar;
            if (t.mask[static_cast<std::size_t>(P)] & bit) return true;
        }

        // Hash fills the uniform remainder.
        const int hashNumer = T - P;
        const int hashDenom = n - P;
        if (hashNumer <= 0 || hashDenom <= 0) return false;
        const float hashFrac = static_cast<float>(hash % 10000u) / 10000.0f;
        return hashFrac < static_cast<float>(hashNumer) / static_cast<float>(hashDenom);
    }

} // namespace lockstep::MetricSelect
