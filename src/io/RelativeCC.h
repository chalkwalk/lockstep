#pragma once

namespace lockstep
{
    // Endless encoders: deltas (+/-1, +/-N) applied directly to the active
    // value. Bypasses soft-takeover.
    class RelativeCCRouter
    {
    public:
        float apply(float currentValue, int delta, float scale = 1.0f / 128.0f) const;
    };
}
