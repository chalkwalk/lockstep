#pragma once

namespace dc
{
    // 4-point, 3rd-order Hermite (Catmull-Rom) interpolation of the continuous
    // waveform at fractional position `t` in [0,1) between y0 and y1; ym1/y2 are
    // the outer neighbours. Single shared implementation for every fractional
    // audio read (SamplePlayer, LoopMachine, ...). Replaces 2-point linear reads,
    // which image/alias badly at non-unity playback rates (the highest audible
    // defect flagged in the 9.24 DSP audit). Callers supply the four neighbouring
    // samples; edge/wrap handling (clamp vs circular) is the caller's choice.
    inline float hermite4(float ym1, float y0, float y1, float y2, float t)
    {
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
}
