#pragma once

namespace lockstep::dsp
{

// Band-limited step correction for the naive saw and pulse oscillators.
//
// A naive digital saw jumps by 2 once per cycle. That discontinuity has
// infinite bandwidth, so every partial above Nyquist folds back down into the
// audible band as inharmonic tones — the sound people mean by "cheap digital
// synth", and it gets worse the higher the note. polyBLEP subtracts a
// polynomial approximation of the offending step's spectrum around the moment
// it happens, which cancels most of that fold-back for two multiplies.
//
// THE SIGN IS THE WHOLE THING, and it is why this lives in a header of its own
// rather than being written out at each call site.
//
// The polynomial below is itself a downward step: it rises to +1 just before
// the discontinuity and resumes at -1 just after. So it must be SUBTRACTED
// from a waveform that also steps downward there, and ADDED where the waveform
// steps up. Get it backwards and the correction does not merely fail — it
// doubles the discontinuity, and the oscillator aliases considerably worse than
// if it had no correction at all.
//
// That was the state of the Analog machine's saw, pulse and sub oscillators
// until this file existed. Measured as the energy below the fundamental of a
// 5 kHz saw at 48 kHz, where a correct saw has nothing at all and everything
// present is fold-back:
//
//     naive, no correction   0.071
//     signs inverted         0.129     — 82% worse than doing nothing
//     as written here        0.033     — 53% better than doing nothing
//
// Callers should use polyBlepSaw and polyBlepPulse rather than the polynomial,
// so that there is one place for the sign to be right.
//
// Triangle and sine need none of this: a triangle is continuous in value and
// only breaks in slope (which wants polyBLAMP, a much smaller effect since its
// partials already fall as 1/k²), and a sine has no partials to fold.

// `t` is the phase in cycles, 0..1. `dt` is the phase increment per sample,
// which is frequency / sampleRate.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline float polyBlep(double t, double dt) noexcept
{
    if (dt <= 0.0)
    {
        return 0.0f;
    }

    if (t < dt)
    {
        const auto x = static_cast<float>(t / dt);
        return x + x - (x * x) - 1.0f;
    }
    if (t > 1.0 - dt)
    {
        const auto x = static_cast<float>((t - 1.0) / dt);
        return (x * x) + x + x + 1.0f;
    }
    return 0.0f;
}

// A rising saw, -1 to +1 across the cycle, with its one downward step
// corrected.
[[nodiscard]] inline float polyBlepSaw(double phase, double increment) noexcept
{
    return static_cast<float>((2.0 * phase) - 1.0) - polyBlep(phase, increment);
}

// A pulse: high for the first `width` of the cycle, low for the rest. Two
// steps, in opposite directions, and so with opposite signs.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline float polyBlepPulse(double phase, double increment,
                                         double width) noexcept
{
    float s = phase < width ? 1.0f : -1.0f;

    // Up at the start of the cycle...
    s += polyBlep(phase, increment);

    // ...and down at the width.
    double atWidth = phase - width;
    if (atWidth < 0.0)
    {
        atWidth += 1.0;
    }
    s -= polyBlep(atWidth, increment);

    return s;
}

} // namespace lockstep::dsp
