#pragma once

#include <algorithm>

namespace lockstep
{
    // R5 (9.25): the number of oversampling stages for a nonlinearity, scaled by
    // the session rate so the internal processed rate stays roughly constant
    // instead of ballooning at high session rates (8x oversampling at 192 kHz is
    // ~1.5 MHz of internal processing for no audible gain over 48 kHz native).
    //
    // `baseStages` is the stage count intended at 48 kHz (the oversampling factor
    // is 2^stages). Drop one stage per octave above ~48 kHz, floored at 1 so the
    // shaper always runs at least 2x oversampled:
    //   48 kHz → baseStages, 96 kHz → baseStages-1, 192 kHz → baseStages-2, …
    // e.g. baseStages 3 gives 8x @48k → 4x @96k → 2x @192k.
    inline int oversamplingStagesForRate(double sampleRate, int baseStages) noexcept
    {
        int reduce = 0;
        double s = sampleRate;
        while (s > 48000.0 * 1.5 && reduce < baseStages - 1)
        {
            s *= 0.5;
            ++reduce;
        }
        return std::max(1, baseStages - reduce);
    }
}
