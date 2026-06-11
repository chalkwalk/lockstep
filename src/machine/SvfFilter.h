#pragma once

namespace lockstep
{
    // Cytomic-form state-variable filter — one 12 dB/oct pole-pair.
    // Call setCoeffs() when g or k change; call process() per sample.
    struct SvfFilter
    {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;
        float a1_ = 1.0f, a2_ = 0.0f, a3_ = 0.0f, k_ = 0.0f;

        void setCoeffs(float g, float k) noexcept
        {
            k_ = k;
            a1_ = 1.0f / (1.0f + g * (g + k));
            a2_ = g * a1_;
            a3_ = g * a2_;
        }

        // mode: 0=LP, 1=HP, 2=BP, 3=Notch
        float process(float v, int mode) noexcept
        {
            const float v3 = v - ic2eq;
            const float v1 = a1_ * ic1eq + a2_ * v3;
            const float v2 = ic2eq + a2_ * ic1eq + a3_ * v3;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;
            switch (mode)
            {
                case 0:  return v2;                  // LP
                case 1:  return v - k_ * v1 - v2;   // HP
                case 2:  return v1;                  // BP
                default: return v - k_ * v1;         // Notch
            }
        }

        void reset() noexcept { ic1eq = ic2eq = 0.0f; }
    };
}
