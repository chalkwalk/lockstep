#pragma once

// Spectral measurement helpers for the 9.24 DSP-quality assertions (S3).
// Shared by MachineDspTest (oversampling/alias checks) and SamplePlayerTest
// (interpolation-purity check). A Blackman-Harris window (-92 dB sidelobes) is
// used so a pure tone reads as genuinely pure, keeping the >55 dB purity
// assertions reachable. Header-only (inline) — no ODR hazard across TUs.

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lockstep
{
    // Number of bins the exclusion band must span to clear a windowed peak's
    // main lobe (Blackman-Harris is ~4 bins each side).
    inline constexpr int kSpectralBand = 6;

    // Magnitude spectrum of channel 0 (Blackman-Harris windowed, zero-padded to
    // fftSize). Returns fftSize/2 linear magnitudes (bin i -> i*sr/fftSize Hz).
    inline std::vector<float> spectrumOf(const juce::AudioBuffer<float>& buf, int fftOrder)
    {
        const int fftSize = 1 << fftOrder;
        std::vector<float> fftData(static_cast<std::size_t>(fftSize) * 2, 0.0f);
        const int n = std::min(fftSize, buf.getNumSamples());
        const float* src = buf.getReadPointer(0);
        // 4-term Blackman-Harris window (sidelobes ~-92 dB).
        const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
        const double denom = std::max(1, n - 1);
        for (int i = 0; i < n; ++i)
        {
            const double x = 2.0 * juce::MathConstants<double>::pi * i / denom;
            const double w = a0 - a1 * std::cos(x) + a2 * std::cos(2.0 * x)
                                - a3 * std::cos(3.0 * x);
            fftData[static_cast<std::size_t>(i)] = src[i] * static_cast<float>(w);
        }
        juce::dsp::FFT fft(fftOrder);
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        std::vector<float> mags(static_cast<std::size_t>(fftSize / 2));
        for (int i = 0; i < fftSize / 2; ++i)
            mags[static_cast<std::size_t>(i)] = fftData[static_cast<std::size_t>(i)];
        return mags;
    }

    // Ratio of energy at the fundamental f (± a small band) to all other energy,
    // in dB. Higher = purer tone. A clean sine ~ >70 dB; interpolation artefacts
    // (imaging) or aliasing pull it down. `f` in Hz, `sr` the sample rate.
    inline float sinePurityDb(const juce::AudioBuffer<float>& buf, double f, double sr,
                              int fftOrder = 15)
    {
        const auto mags = spectrumOf(buf, fftOrder);
        const int fftSize = 1 << fftOrder;
        const double binHz = sr / fftSize;
        const int fundBin = static_cast<int>(std::lround(f / binHz));
        const int band = kSpectralBand;
        double fund = 0.0, rest = 0.0;
        for (int i = 1; i < static_cast<int>(mags.size()); ++i)
        {
            const double e = static_cast<double>(mags[static_cast<std::size_t>(i)])
                           * static_cast<double>(mags[static_cast<std::size_t>(i)]);
            if (std::abs(i - fundBin) <= band) fund += e;
            else                               rest += e;
        }
        if (rest < 1e-20) return 120.0f;
        return static_cast<float>(10.0 * std::log10(fund / rest));
    }

    // Alias/inharmonic energy relative to the fundamental, in dB (negative =
    // alias below fundamental). For a memoryless shaper driven by a sine f0, true
    // harmonics land at k*f0; everything NOT within a band of some k*f0 is
    // aliasing + noise. Lower (more negative) is cleaner.
    inline float aliasRatioDb(const juce::AudioBuffer<float>& buf, double f0, double sr,
                              int fftOrder = 15)
    {
        const auto mags = spectrumOf(buf, fftOrder);
        const int fftSize = 1 << fftOrder;
        const double binHz = sr / fftSize;
        const int band = kSpectralBand;
        const int nyqBin = static_cast<int>(mags.size());
        double fund = 0.0, alias = 0.0;
        for (int i = 1; i < nyqBin; ++i)
        {
            const double hz = i * binHz;
            // Nearest integer multiple of f0.
            const long k = std::lround(hz / f0);
            const double harmHz = static_cast<double>(k) * f0;
            const bool isHarmonic = std::abs(hz - harmHz) <= band * binHz;
            const double e = static_cast<double>(mags[static_cast<std::size_t>(i)])
                           * static_cast<double>(mags[static_cast<std::size_t>(i)]);
            if (k == 1 && isHarmonic) fund += e;
            else if (!isHarmonic)     alias += e;
            // harmonics k>=2 are neither fundamental nor alias — ignored.
        }
        if (fund < 1e-20) return 0.0f;
        return static_cast<float>(10.0 * std::log10(alias / fund));
    }

    // Largest absolute jump between consecutive samples, across all channels.
    // A click / discontinuity (bad loop seam, integer-tap retune) spikes this.
    inline float maxSampleStep(const juce::AudioBuffer<float>& buf)
    {
        float worst = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float* p = buf.getReadPointer(ch);
            for (int i = 1; i < buf.getNumSamples(); ++i)
                worst = std::max(worst, std::abs(p[i] - p[i - 1]));
        }
        return worst;
    }

    // Fill channel 0 (and 1 if present) with a sine of frequency f at amplitude a.
    inline void fillSine(juce::AudioBuffer<float>& buf, double f, double sr, float a = 0.5f)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample(ch, i, a * static_cast<float>(
                    std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr)));
    }
}
