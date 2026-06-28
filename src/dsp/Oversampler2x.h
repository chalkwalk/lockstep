#pragma once

#include <array>
#include <cmath>

namespace lockstep::dsp
{

// Mono 2x oversampler built from a single linear-phase windowed-sinc halfband
// FIR, decomposed into two polyphase branches. One instance per channel/voice.
//
// Two complementary operations share the same kernel:
//   upsample(x, y0, y1)  — interpolate one base-rate sample into two 2x samples
//   decimate(a, b)       — anti-alias filter two 2x samples down to one base sample
//
// Usage A (waveshaper, e.g. HQ saturation):
//   os.upsample(x, a, b);  a = shape(a); b = shape(b);  out = os.decimate(a, b);
// Usage B (generator, e.g. FM operator core): synthesise a,b at the doubled rate
//   directly, then  out = os.decimate(a, b);
//
// The up/down paths keep independent delay lines, so a single instance can run
// both directions concurrently (waveshaper use). Cheap: kBranch MACs per branch.
class Oversampler2x
{
public:
    Oversampler2x() { buildKernel(); reset(); }

    void reset() noexcept
    {
        upLine_.fill(0.0f);
        downLine_.fill(0.0f);
    }

    // Interpolate one base-rate sample into two samples at the doubled rate.
    void upsample(float x, float& y0, float& y1) noexcept
    {
        // Shift newest base-rate sample into the polyphase input line.
        for (int j = kBranch - 1; j > 0; --j)
            upLine_[static_cast<std::size_t>(j)] = upLine_[static_cast<std::size_t>(j - 1)];
        upLine_[0] = x;

        float s0 = 0.0f, s1 = 0.0f;
        for (int j = 0; j < kBranch; ++j)
        {
            const float in = upLine_[static_cast<std::size_t>(j)];
            s0 += hp0_[static_cast<std::size_t>(j)] * in;
            s1 += hp1_[static_cast<std::size_t>(j)] * in;
        }
        // x2 compensates the energy lost to zero-stuffing in the interpolator.
        y0 = 2.0f * s0;
        y1 = 2.0f * s1;
    }

    // Anti-alias filter two consecutive doubled-rate samples and decimate to one
    // base sample. `a` precedes `b` in the 2x stream. Uses a full-kernel
    // convolution over the 2x delay line (one output per input pair) — the
    // textbook form, free of polyphase branch-alignment hazards.
    float decimate(float a, float b) noexcept
    {
        for (int k = kTaps - 1; k > 1; --k)
            downLine_[static_cast<std::size_t>(k)] = downLine_[static_cast<std::size_t>(k - 2)];
        downLine_[1] = a;
        downLine_[0] = b;

        float s = 0.0f;
        for (int k = 0; k < kTaps; ++k)
            s += h_[static_cast<std::size_t>(k)] * downLine_[static_cast<std::size_t>(k)];
        return s;
    }

private:
    static constexpr int kBranch = 16;       // taps per polyphase branch
    static constexpr int kTaps = 2 * kBranch;  // full FIR length

    void buildKernel() noexcept
    {
        // Windowed-sinc lowpass, cutoff at 0.25 cycles/sample of the 2x rate
        // (= the base-rate Nyquist), Blackman window. Normalised to unity DC.
        std::array<float, kTaps> h{};
        constexpr double fc = 0.25;
        constexpr double pi = 3.14159265358979323846;
        const double m = static_cast<double>(kTaps - 1);
        double sum = 0.0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double t = static_cast<double>(n) - m * 0.5;
            const double sinc = (std::abs(t) < 1e-9) ? 2.0 * fc
                                                      : std::sin(2.0 * pi * fc * t) / (pi * t);
            const double w = 0.42 - 0.5 * std::cos(2.0 * pi * n / m)
                                  + 0.08 * std::cos(4.0 * pi * n / m);
            const double v = sinc * w;
            h[static_cast<std::size_t>(n)] = static_cast<float>(v);
            sum += v;
        }
        for (auto& v : h)
            v /= static_cast<float>(sum);
        h_ = h;

        // Polyphase split (used by the interpolator): even-indexed taps -> branch
        // 0, odd-indexed -> branch 1.
        for (int j = 0; j < kBranch; ++j)
        {
            hp0_[static_cast<std::size_t>(j)] = h[static_cast<std::size_t>(2 * j)];
            hp1_[static_cast<std::size_t>(j)] = h[static_cast<std::size_t>(2 * j + 1)];
        }
    }

    std::array<float, kTaps> h_{};            // full kernel (decimation convolution)
    std::array<float, kBranch> hp0_{};        // even polyphase branch (interpolation)
    std::array<float, kBranch> hp1_{};        // odd polyphase branch (interpolation)
    std::array<float, kBranch> upLine_{};     // base-rate input line (interpolation)
    std::array<float, kTaps> downLine_{};     // 2x-rate input line (decimation)
};

}  // namespace lockstep::dsp
