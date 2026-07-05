#pragma once

#include <juce_core/juce_core.h>

namespace lockstep::stretchmath
{
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
