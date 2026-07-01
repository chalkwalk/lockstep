#pragma once

#include "../machine/TransientDetector.h"  // BlockAnalysis
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lockstep
{
    // Energy-based tempo (BPM) estimate for a loaded loop, computed from the
    // per-block RMS envelope that analyseSample() already produces (no extra
    // decode pass, no FFT). Message-thread only; a pure function of `ba`.
    //
    // Approach: build an onset-novelty signal (half-wave-rectified first
    // difference of the block RMS), autocorrelate it over the musical lag range
    // (kMinBpm..kMaxBpm), pick the strongest lag, and fold octave-ambiguous
    // results into a musical range. Returns 0.0 when the material is too short,
    // silent, or non-rhythmic — callers treat 0 as "unknown", exactly like
    // sourceBars <= 0 (DESIGN §28).
    inline constexpr double kMinBpm = 60.0;
    inline constexpr double kMaxBpm = 200.0;
    // Octave-fold target: only correct a raw peak that lands outside the range
    // of common musical tempos by halving/doubling. Kept wide (covers half-time
    // hip-hop up through drum'n'bass) so genuine tempos are never folded away.
    inline constexpr double kFoldLoBpm = 70.0;
    inline constexpr double kFoldHiBpm = 180.0;
    // Minimum normalised autocorrelation (0..1) at the winning lag for the
    // estimate to be trusted. A clean beat scores ~0.8+ regardless of tempo;
    // aperiodic material stays near 0. Scale-free and harmonic-insensitive.
    inline constexpr double kMinAutocorr = 0.2;

    inline double estimateBpm(const BlockAnalysis& ba)
    {
        const int n = ba.numBlocks;
        if (n < 8 || ba.sampleRate <= 0.0 || ba.blockSize < 1)
            return 0.0;
        if (static_cast<int>(ba.rms.size()) < n)
            return 0.0;

        const double blocksPerSec = ba.sampleRate / static_cast<double>(ba.blockSize);

        // Onset novelty: half-wave-rectified first difference of the RMS env.
        std::vector<double> nov(static_cast<std::size_t>(n), 0.0);
        double mean = 0.0;
        for (int i = 1; i < n; ++i)
        {
            const double d = static_cast<double>(ba.rms[static_cast<std::size_t>(i)])
                           - static_cast<double>(ba.rms[static_cast<std::size_t>(i - 1)]);
            const double v = d > 0.0 ? d : 0.0;
            nov[static_cast<std::size_t>(i)] = v;
            mean += v;
        }
        mean /= static_cast<double>(n);
        if (mean <= 1e-9)
            return 0.0;  // no rising energy → not a rhythmic loop

        // Remove the mean so the DC term does not dominate the autocorrelation.
        for (auto& v : nov)
            v -= mean;

        // Lag search window (in blocks) from the tempo bounds.
        const int lagMin = std::max(1, static_cast<int>(std::floor(60.0 * blocksPerSec / kMaxBpm)));
        const int lagMax = static_cast<int>(std::ceil(60.0 * blocksPerSec / kMinBpm));
        // Need at least ~two periods of the slowest tempo to trust a peak.
        if (lagMax < lagMin || n < 2 * lagMax)
            return 0.0;

        // Total novelty energy normalises the autocorrelation so the peak is a
        // 0..1 correlation independent of level, tempo, and harmonic count.
        double energy = 0.0;
        for (int i = 0; i < n; ++i)
            energy += nov[static_cast<std::size_t>(i)] * nov[static_cast<std::size_t>(i)];
        if (energy <= 0.0)
            return 0.0;

        double bestCorr = -1.0;
        int bestLag = 0;
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            double s = 0.0;
            for (int i = lag; i < n; ++i)
                s += nov[static_cast<std::size_t>(i)] * nov[static_cast<std::size_t>(i - lag)];
            const double r = s / energy;
            if (r > bestCorr)
            {
                bestCorr = r;
                bestLag = lag;
            }
        }
        // Confidence: the winning lag must be a strong correlation, not noise.
        if (bestLag < 1 || bestCorr < kMinAutocorr)
            return 0.0;

        double bpm = 60.0 * blocksPerSec / static_cast<double>(bestLag);

        // Octave fold: nudge into the musical range by halving/doubling.
        while (bpm < kFoldLoBpm)
            bpm *= 2.0;
        while (bpm > kFoldHiBpm)
            bpm *= 0.5;

        return bpm;
    }
}
