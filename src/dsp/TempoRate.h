#pragma once

// Tempo-relative modulation rates (9.31, DESIGN §32.1z).
//
// A modulation rate is stored as a PERIOD IN BEATS, not a frequency in Hz. That
// is the unit that stays musical across a tempo change: "one cycle per bar" is a
// number a performer can hold and reason about, "0.5 Hz" is arithmetic they have
// to redo every time the song's tempo moves. It also makes the encoder's two
// readings fall out for free -- a bare turn snaps to the lattice below (a cycle
// per bar, per two bars, per beat...), Func+turn sweeps freely between them, and
// BOTH stay tempo-relative because both are beats.
//
// One home for the lattice, the labels and the conversion, so the chorus, the
// flanger, the phaser and the Analog LFO cannot drift apart on what "1 bar" is.

#include <cstddef>
#include <span>

namespace lockstep::dsp
{
    // Period in beats (1.0 = a quarter note), ascending = slower.
    // Tops out at 64 beats -- 32 s at 120 BPM -- so the ultra-slow evolving sweeps
    // that the old 0.1 Hz floor allowed are still reachable.
    inline constexpr float kModPeriodBeats[] = {
        0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f
    };
    inline constexpr const char* const kModPeriodLabels[] = {
        "1/16", "1/8", "1/4", "1/4.", "1/2", "3/4", "1 bar", "2 bar",
        "4 bar", "8 bar", "16 bar"
    };
    inline constexpr int kNumModPeriods = 11;
    inline constexpr float kMinModPeriod = kModPeriodBeats[0];
    inline constexpr float kMaxModPeriod = kModPeriodBeats[kNumModPeriods - 1];

    [[nodiscard]] inline std::span<const float> modPeriodDetents() noexcept
    {
        return { kModPeriodBeats, static_cast<std::size_t>(kNumModPeriods) };
    }
    [[nodiscard]] inline std::span<const char* const> modPeriodLabels() noexcept
    {
        return { kModPeriodLabels, static_cast<std::size_t>(kNumModPeriods) };
    }

    // period (beats) -> rate (Hz) at a given tempo. The only conversion; every
    // modulator calls it rather than reinventing 60/bpm somewhere private.
    [[nodiscard]] inline float rateHzFromPeriodBeats(float periodBeats, double bpm) noexcept
    {
        const double p = periodBeats > 1.0e-4f ? static_cast<double>(periodBeats) : 1.0e-4;
        const double b = bpm > 0.0 ? bpm : 120.0;
        return static_cast<float>(b / (60.0 * p));
    }
}
