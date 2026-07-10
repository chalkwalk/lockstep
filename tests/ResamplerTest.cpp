// ResamplerTest — the shared bandlimited resampler (9.25 R1).
//
// Verifies the three properties the DSP paths rely on:
//   * unity passthrough — rate 1 at integer positions reproduces the source.
//   * DC gain — a constant reads back as itself at every fraction and rate.
//   * pitch-up purity / anti-aliasing — reading faster than unity band-limits
//     the source so out-of-band content does NOT fold back, unlike a naive
//     Hermite read (the "bright sample pitched up aliases" defect, 9.24 audit).

#include "TestHarness.h"
#include "SpectralMeasure.h"
#include "../src/dsp/Resampler.h"
#include "../src/dsp/Interpolation.h"

#include <cmath>
#include <vector>

namespace lockstep
{
    namespace
    {
        // Naive 4-point Hermite read at a fractional position (the pre-9.25 path),
        // for the anti-aliasing comparison.
        float hermiteRead(const std::vector<float>& s, double pos)
        {
            const int i0 = static_cast<int>(std::floor(pos));
            const int n = static_cast<int>(s.size());
            const auto at = [&](int i) {
                i = i < 0 ? 0 : (i >= n ? n - 1 : i);
                return s[static_cast<std::size_t>(i)];
            };
            const auto fr = static_cast<float>(pos - std::floor(pos));
            return hermite4(at(i0 - 1), at(i0), at(i0 + 1), at(i0 + 2), fr);
        }
    }

    void runResamplerTests()
    {
        const Resampler rs;
        constexpr double kSr = 48000.0;

        // ── Unity passthrough ────────────────────────────────────────────────
        // rate 1, integer positions in the interior reproduce the source (the
        // kernel's group delay is compensated). A mixed-frequency signal.
        {
            const int n = 2048;
            std::vector<float> s(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                const double x = static_cast<double>(i);
                s[static_cast<std::size_t>(i)] = static_cast<float>(
                    0.5 * std::sin(0.013 * x) + 0.3 * std::sin(0.11 * x));
            }
            float worst = 0.0f;
            for (int p = 16; p < n - 16; ++p)
            {
                const float got = rs.read(s.data(), n, static_cast<double>(p), 1.0);
                worst = std::max(worst, std::abs(got - s[static_cast<std::size_t>(p)]));
            }
            CHECK(worst < 1.0e-3f,
                  "unity integer read reproduces the source (max err "
                  + juce::String(worst) + ")");
        }

        // ── DC gain ──────────────────────────────────────────────────────────
        // A constant reads back as itself at every fraction and read rate.
        {
            std::vector<float> s(512, 0.75f);
            float worst = 0.0f;
            for (double rate : { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0 })
                for (double frac = 0.0; frac < 1.0; frac += 0.125)
                {
                    const float got = rs.read(s.data(), 512, 128.0 + frac, rate);
                    worst = std::max(worst, std::abs(got - 0.75f));
                }
            CHECK(worst < 5.0e-3f,
                  "DC passes at unity gain across fractions and rates (max err "
                  + juce::String(worst) + ")");
        }

        // ── In-band content is preserved on pitch-up ─────────────────────────
        // A 4 kHz tone read at rate 2 (up an octave) → 8 kHz, well inside the
        // rate-2 cutoff (~12 kHz), so it survives with high purity.
        {
            // Long buffers so the tone resolves to a narrow FFT main lobe (a short
            // segment would smear it past the purity band and look impure).
            const int nIn = 40000;
            std::vector<float> s(static_cast<std::size_t>(nIn));
            for (int i = 0; i < nIn; ++i)
                s[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(
                    2.0 * juce::MathConstants<double>::pi * 4000.0 * i / kSr));

            const int nOut = 16384;
            juce::AudioBuffer<float> out(1, nOut);
            for (int i = 0; i < nOut; ++i)
                out.setSample(0, i, rs.read(s.data(), nIn, 8.0 + 2.0 * i, 2.0));

            const float purity = sinePurityDb(out, 8000.0, kSr);
            CHECK(purity > 40.0f,
                  "in-band pitch-up stays pure (8 kHz @ rate 2, purity "
                  + juce::String(purity) + " dB)");
        }

        // ── Out-of-band content is rejected on pitch-up (anti-aliasing) ──────
        // An 18 kHz tone read at rate 2 would image to 36 kHz and fold back to
        // 12 kHz. The rate-2 kernel (cutoff ~12 kHz) attenuates the 18 kHz source,
        // so the resampler's aliased output is far quieter than a naive Hermite
        // read that passes the alias straight through.
        {
            const int nIn = 8192;
            std::vector<float> s(static_cast<std::size_t>(nIn));
            for (int i = 0; i < nIn; ++i)
                s[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(
                    2.0 * juce::MathConstants<double>::pi * 18000.0 * i / kSr));

            const int nOut = 3072;
            juce::AudioBuffer<float> band(1, nOut);
            juce::AudioBuffer<float> naive(1, nOut);
            for (int i = 0; i < nOut; ++i)
            {
                const double pos = 8.0 + 2.0 * i;
                band.setSample(0, i, rs.read(s.data(), nIn, pos, 2.0));
                naive.setSample(0, i, hermiteRead(s, pos));
            }
            const double bandRms = band.getRMSLevel(0, 0, nOut);
            const double naiveRms = naive.getRMSLevel(0, 0, nOut);
            CHECK(bandRms < naiveRms * 0.5,
                  "pitch-up band-limits out-of-band content vs Hermite (band "
                  + juce::String(bandRms) + " vs naive " + juce::String(naiveRms) + ")");
            CHECK(bandRms < 0.1,
                  "aliased image is strongly attenuated (rms "
                  + juce::String(bandRms) + ")");
        }

        // ── Scatter-add (R4): the transpose of read() ────────────────────────
        // At rate 1 on integer positions the kernel is a delta, so a scatter is a
        // bit-exact `+= in` at that position (the unity-overdub guarantee).
        {
            std::vector<float> buf(64, 0.0f);
            rs.scatterAddCircular(buf.data(), 64, 20.0, 1.0, 0.5f);
            CHECK(std::abs(buf[20] - 0.5f) < 1.0e-4f,
                  "scatter at an integer position, rate 1 → bit-exact delta");
            float leak = 0.0f;
            for (int i = 0; i < 64; ++i)
                if (i != 20) leak += std::abs(buf[static_cast<std::size_t>(i)]);
            CHECK(leak < 1.0e-4f, "scatter rate-1 delta has no spread into neighbours");
        }

        // A constant input stream scattered at rate 1 reconstructs the constant
        // (DC passes at unity through the scatter, circularly), matching read()'s DC.
        {
            std::vector<float> buf(128, 0.0f);
            for (int i = 0; i < 256; ++i)  // two full circular passes
                rs.scatterAddCircular(buf.data(), 128, static_cast<double>(i), 1.0, 0.75f);
            float worst = 0.0f;
            for (float v : buf) worst = std::max(worst, std::abs(v - 2.0f * 0.75f));
            CHECK(worst < 5.0e-3f,
                  "constant scattered over the loop reconstructs the constant (worst "
                  + juce::String(worst) + ")");
        }

        // ── Scatter gain is amplitude-invariant across rates (9.28.1) ────────
        // The head signal law (DESIGN §40.10): a write at any speed reads back at
        // the input's amplitude. Kernel density is 1/rate, so without the |rate|
        // deposit gain a rate-2 pass would land at 0.5x and a half-speed pass at
        // 2x. Two full circular passes at each rate → expect exactly 2 * in.
        {
            for (const double rate : { 0.5, 2.0 })
            {
                const int len = 128;
                std::vector<float> buf(static_cast<std::size_t>(len), 0.0f);
                const int nIn = static_cast<int>(2.0 * len / rate);  // two passes
                for (int i = 0; i < nIn; ++i)
                    rs.scatterAddCircular(buf.data(), len,
                                          static_cast<double>(i) * rate, rate, 0.75f);
                float worst = 0.0f;
                for (float v : buf) worst = std::max(worst, std::abs(v - 2.0f * 0.75f));
                CHECK(worst < 2.0e-2f,
                      "varispeed scatter is amplitude-invariant (rate "
                      + juce::String(rate) + ", worst " + juce::String(worst) + ")");
            }
        }

        // ── A stalled write head writes (almost) nothing (9.28.1) ────────────
        // rate → 0 used to pile unbounded energy onto one spot; the |rate| gain
        // makes the deposit vanish with the speed — a scrub through zero fades.
        {
            const int len = 64;
            std::vector<float> buf(static_cast<std::size_t>(len), 0.0f);
            double pos = 20.0;
            for (int i = 0; i < 1000; ++i)
            {
                rs.scatterAddCircular(buf.data(), len, pos, 1.0e-4, 1.0f);
                pos += 1.0e-4;
            }
            float peak = 0.0f;
            for (float v : buf) peak = std::max(peak, std::abs(v));
            CHECK(peak < 0.2f,
                  "near-stall scatter deposits near-nothing (peak "
                  + juce::String(peak) + ", was ~62 uncompensated)");
        }

        // ── Reverse write is the mirror of forward (9.28.1) ──────────────────
        // A constant written while the head runs backwards reconstructs the same
        // constant — the gain and cutoff depend on |rate| only.
        {
            const int len = 128;
            std::vector<float> fwd(static_cast<std::size_t>(len), 0.0f);
            std::vector<float> rev(static_cast<std::size_t>(len), 0.0f);
            for (int i = 0; i < 2 * len; ++i)
            {
                rs.scatterAddCircular(fwd.data(), len,  static_cast<double>(i), 1.0, 0.5f);
                rs.scatterAddCircular(rev.data(), len, -static_cast<double>(i), 1.0, 0.5f);
            }
            float worst = 0.0f;
            for (int i = 0; i < len; ++i)
                worst = std::max(worst, std::abs(rev[static_cast<std::size_t>(i)]
                                                 - fwd[static_cast<std::size_t>(i)]));
            CHECK(worst < 5.0e-3f,
                  "reverse scatter mirrors forward (worst " + juce::String(worst) + ")");
        }

        // ── readCircular: seam continuity + rate-axis corners (9.28.3) ───────
        // On a buffer holding exactly one period of a tone, a circular read is
        // periodic across the seam (pos and pos+len agree), matches read() in the
        // interior, holds a defined value at rate 0, and is direction-agnostic.
        {
            const int len = 512;
            std::vector<float> s(static_cast<std::size_t>(len));
            for (int i = 0; i < len; ++i)  // 8 cycles → exactly periodic over len
                s[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(
                    2.0 * juce::MathConstants<double>::pi * 8.0 * i / len));

            float worstSeam = 0.0f, worstInner = 0.0f, worstDir = 0.0f;
            for (double frac = 0.0; frac < 1.0; frac += 0.093)
            {
                // Periodicity: reads one whole loop apart agree, including reads
                // whose window straddles the seam.
                const double nearSeam = static_cast<double>(len) - 2.0 + frac;
                worstSeam = std::max(worstSeam,
                    std::abs(rs.readCircular(s.data(), len, nearSeam, 1.0)
                           - rs.readCircular(s.data(), len, nearSeam - len, 1.0)));

                // Interior agreement with the clamping read (window touches no edge).
                const double inner = 100.0 + frac;
                worstInner = std::max(worstInner,
                    std::abs(rs.readCircular(s.data(), len, inner, 1.0)
                           - rs.read(s.data(), len, inner, 1.0)));

                // Direction-agnostic: the reconstructed waveform at a position does
                // not depend on the travel direction (|rate| picks the kernel).
                worstDir = std::max(worstDir,
                    std::abs(rs.readCircular(s.data(), len, inner, 2.0)
                           - rs.readCircular(s.data(), len, inner, -2.0)));
            }
            CHECK(worstSeam < 1.0e-4f,
                  "circular read is periodic across the seam (worst "
                  + juce::String(worstSeam) + ")");
            CHECK(worstInner < 1.0e-4f,
                  "circular read matches read() in the interior (worst "
                  + juce::String(worstInner) + ")");
            CHECK(worstDir < 1.0e-6f,
                  "circular read is direction-agnostic (worst "
                  + juce::String(worstDir) + ")");

            // Rate 0 (a parked or turning head) holds a finite, sensible value.
            const float held = rs.readCircular(s.data(), len, 100.25, 0.0);
            CHECK(std::isfinite(held) && std::abs(held) <= 0.6f,
                  "rate-0 circular read holds a bounded sample ("
                  + juce::String(held) + ")");
        }
    }
}
