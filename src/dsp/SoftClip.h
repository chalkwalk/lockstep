#pragma once

#include <cmath>

namespace lockstep::dsp
{

// Transparent soft-knee clipper for the master output ceiling.
//
// Unlike a plain tanh (which compresses audibly from ~-6 dBFS up), this is
// exactly unity (linear, uncoloured) below a knee threshold and only engages
// above it, smoothly saturating toward a ceiling just under full scale. A
// well-staged mix that stays below the knee passes through untouched; only
// peaks that would otherwise approach/exceed 0 dBFS are caught.
//
// Construction: below |x| = kKnee the response is the identity. Above it, the
// excess is mapped through a tanh scaled so the curve is C1-continuous at the
// knee (slope 1 there) and asymptotes to kCeiling, never exceeding it.
//
//   kKnee    ~= 0.71  (-3 dBFS)   linear/transparent below
//   kCeiling ~= 0.99  (-0.1 dBFS) hard asymptote, never exceeded
//
// Branch-light (sign + one threshold compare) and allocation-free.
inline float softClip(float x) noexcept
{
    constexpr float kKnee = 0.71f;     // linear below this magnitude
    constexpr float kCeiling = 0.99f;  // asymptotic peak ceiling
    constexpr float kRange = kCeiling - kKnee;

    const float a = std::abs(x);
    if (a <= kKnee)
        return x;  // transparent: pass through unchanged

    const float shaped = kKnee + kRange * std::tanh((a - kKnee) / kRange);
    return std::copysign(shaped, x);
}

}  // namespace lockstep::dsp
