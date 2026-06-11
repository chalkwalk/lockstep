#pragma once

// Shared utilities for IControllerSurface implementations.
// This header is JUCE-free so the functions can be tested in lockstep_core.

namespace lockstep::ctrl
{
    // Relative-encoder CC decode — X-Touch Mini (signed-magnitude).
    // CW = 1..63 (+delta), CCW = 65..127 (-(value-64)).
    // Each unit equals exactly one parameter step in either direction.
    constexpr int decodeSignedMagnitudeDelta(int v) noexcept
    {
        if (v >= 1  && v <= 63)  return  v;
        if (v >= 65 && v <= 127) return -(v - 64);
        return 0;
    }

    // Relative-encoder CC decode — Push 1 (two's-complement).
    // CW = 1..63 (+delta), CCW = 64..127 (-(128-value)).
    // The device sends larger magnitudes for faster turns, unlike signed-magnitude.
    constexpr int decodeTwosComplementDelta(int v) noexcept
    {
        if (v >= 1 && v <= 63) return  v;
        if (v >= 64)           return -(128 - v);
        return 0;
    }
} // namespace lockstep::ctrl
