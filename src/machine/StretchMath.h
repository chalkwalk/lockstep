#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace lockstep::stretchmath
{
    // FreeLen pitch-preserved fit target (S7). Round a recorded loop length UP to
    // the next launch-quant multiple so the loop becomes grid-alignable and the
    // stretch ratio (target / recorded) is **extend-only** (>= 1):
    //
    //   period > 0 : target = ceil(recordedLen / period) * period, EXCEPT a take
    //                that sits a hair ABOVE a multiple (within `tolerance` samples
    //                — capture jitter) snaps down to that multiple instead of
    //                jumping a whole period up. An exact multiple returns itself
    //                (ratio 1, no stretch).
    //   period <= 0: fall back to `barPeriod` (one bar) when given; else no grid
    //                is known and the recorded length is returned unchanged
    //                (native, ratio 1).
    //
    // `quantPeriod` is `TransportInfo::launchQuantPeriodSamples`. Returns a length
    // >= recordedLen except for the sub-`tolerance` jitter snap-down; recordedLen
    // <= 0 returns 0.
    [[nodiscard]] inline juce::int64 fitTargetLength(juce::int64 recordedLen,
                                                     double quantPeriod,
                                                     double tolerance = 0.0,
                                                     double barPeriod = 0.0)
    {
        if (recordedLen <= 0) return 0;
        const double period = quantPeriod > 0.0 ? quantPeriod
                                                : (barPeriod > 0.0 ? barPeriod : 0.0);
        if (period <= 0.0) return recordedLen;  // no grid → native, no fit

        const double rl = static_cast<double>(recordedLen);
        const double lower = std::floor(rl / period) * period;
        const double upper = lower + period;
        // A take that is essentially on the lower multiple (exact, or a hair above
        // it — capture jitter) snaps to it rather than stretching a whole period.
        // Never snap to 0 (a sub-period take extends up to one period).
        const double target = (lower > 0.0 && rl - lower <= tolerance) ? lower : upper;
        return static_cast<juce::int64>(std::llround(target));
    }

    // Shared tempo-tracking time-ratio for the Stretch (RAM) and Stream (disk)
    // players, so the two cannot drift (9.23 S3).
    //
    // Returns the stretch engine's timeRatio = output duration / input duration,
    // such that `bars` musical bars of the source play over the project tempo:
    //   output_time = bars * samplesPerBar / outputRate   (seconds)
    //   input_time  = srcLenSamples / srcRate             (seconds)
    //   timeRatio   = output_time / input_time
    //               = bars * samplesPerBar * srcRate / (srcLenSamples * outputRate)
    //
    // The srcRate/outputRate factor is what makes an off-rate source track tempo
    // correctly (it reduces to 1 when the source and engine rates match, so the
    // equal-rate RAM case is unchanged). When the source carries no explicit
    // musical length, `effBpm` (> 0) derives bars from srcLenSamples at 4/4.
    // Falls back to 1.0 (native, no tracking) when the musical length is unknown.
    [[nodiscard]] inline double stretchTimeRatio(double sourceBars, double effBpm,
                                                 juce::int64 srcLenSamples,
                                                 double srcRate,
                                                 double samplesPerBar,
                                                 double outputRate)
    {
        double bars = sourceBars;
        if (bars <= 0.0 && effBpm > 0.0 && srcRate > 0.0 && srcLenSamples > 0)
        {
            constexpr double kBeatsPerBar = 4.0;  // 4/4 assumption
            const double srcSeconds = static_cast<double>(srcLenSamples) / srcRate;
            bars = srcSeconds * effBpm / 60.0 / kBeatsPerBar;
        }
        if (bars <= 0.0 || samplesPerBar <= 0.0 || srcLenSamples <= 0
            || srcRate <= 0.0 || outputRate <= 0.0)
            return 1.0;
        return bars * samplesPerBar * srcRate
               / (static_cast<double>(srcLenSamples) * outputRate);
    }
}
