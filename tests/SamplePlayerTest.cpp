// SamplePlayerTest -- loop-seam crossfade (C1). The forward loop used to hard-wrap
// with fmod, stepping the waveform at the seam (an audible click). SamplePlayer now
// crossfades the seam: it borrows real tail material past loopEnd when available
// (loop period preserved) and falls back to eating into the loop at the file end.
//
// Tests run the pure SamplePlayer against a synthetic PCM buffer; position is a
// public member, so loop period is measured directly from wrap events.

#include "TestHarness.h"
#include "../src/machine/SamplePlayer.h"
#include <cmath>
#include <vector>

namespace lockstep
{
    namespace
    {
        // Fill a mono buffer with a continuous sine of the given period (samples).
        juce::AudioBuffer<float> makeSine(int len, double periodSamples)
        {
            juce::AudioBuffer<float> pcm(1, len);
            for (int i = 0; i < len; ++i)
                pcm.setSample(0, i, static_cast<float>(std::sin(
                    2.0 * juce::MathConstants<double>::pi
                    * static_cast<double>(i) / periodSamples)));
            return pcm;
        }

        // Trigger a flat-envelope, always-looping voice over [loopStart, loopEnd].
        SamplePlayer makeLooper(double loopStart, double loopEnd, double xfade)
        {
            SamplePlayer::Spec spec;
            spec.sampleIndex = 0;
            spec.positionStart = 0.0;
            spec.windowStart = 0.0;
            spec.windowEnd = 0.0;  // full sample
            spec.rate = 1.0;
            spec.level = 1.0f;
            spec.sustainLevel = 1.0f;  // flat env (no attack/hold/decay/release ramp)
            spec.loopStart = loopStart;
            spec.loopEnd = loopEnd;
            spec.loopMode = SamplePlayer::LoopMode::All;
            spec.xfadeSamples = xfade;
            SamplePlayer p;
            p.trigger(spec);
            return p;
        }

        // Collect n output samples; report the max per-sample step and any NaN.
        struct Run
        {
            std::vector<float> out;
            std::vector<double> pos;
            float maxDiff = 0.0f;
            bool nan = false;
        };

        Run runSteps(SamplePlayer& p, const juce::AudioBuffer<float>& pcm, int n)
        {
            Run r;
            r.out.reserve(static_cast<std::size_t>(n));
            r.pos.reserve(static_cast<std::size_t>(n));
            float prev = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float x = p.step(pcm);
                if (!std::isfinite(x)) r.nan = true;
                if (i > 0) r.maxDiff = std::max(r.maxDiff, std::abs(x - prev));
                prev = x;
                r.out.push_back(x);
                r.pos.push_back(p.position);
            }
            return r;
        }

        // Median inter-wrap distance: a "wrap" is where position decreases.
        double measurePeriod(const Run& r)
        {
            std::vector<int> wraps;
            for (std::size_t i = 1; i < r.pos.size(); ++i)
                if (r.pos[i] < r.pos[i - 1])
                    wraps.push_back(static_cast<int>(i));
            if (wraps.size() < 2) return -1.0;
            // Use the gap between the last two wraps (steady state).
            return static_cast<double>(wraps.back() - wraps[wraps.size() - 2]);
        }
    }

    void runSamplePlayerTests()
    {
        constexpr int kLen = 2000;
        const auto pcm = makeSine(kLen, /*period*/ 100.0);

        // loopEnd at a sine peak (sin(2pi*225/100)=+1) so a hard wrap to loopStart
        // (value 0) is a ~1.0 step — a clear click to detect.
        const double loopStart = 0.0;
        const double loopEnd = 225.0;
        const double span = loopEnd - loopStart;

        // (1) Seam continuity: crossfade smooths the step the hard wrap produces.
        SamplePlayer hard = makeLooper(loopStart, loopEnd, /*xfade*/ 0.0);
        SamplePlayer soft = makeLooper(loopStart, loopEnd, /*xfade*/ 32.0);
        Run rh = runSteps(hard, pcm, 1000);
        Run rs = runSteps(soft, pcm, 1000);

        CHECK(!rh.nan && !rs.nan, "SamplePlayer loop: no NaN/Inf");
        CHECK(rh.maxDiff > 0.8f,
              "SamplePlayer loop: hard wrap steps hard at the seam (maxDiff="
              + juce::String(rh.maxDiff) + ")");
        CHECK(rs.maxDiff < 0.2f,
              "SamplePlayer loop: crossfade removes the seam step (maxDiff="
              + juce::String(rs.maxDiff) + ")");
        CHECK(rs.maxDiff < rh.maxDiff, "SamplePlayer loop: crossfade < hard wrap");

        // (2) Length preserved when tail material exists (loopEnd < length): the
        // borrow-tail path keeps the loop period exactly equal to the span.
        CHECK(std::abs(measurePeriod(rs) - span) < 1.0,
              "SamplePlayer loop: borrow-tail preserves period (measured="
              + juce::String(measurePeriod(rs)) + ", span=" + juce::String(span) + ")");

        // (3) Crossfade touches only the seam: interior samples match the hard-wrap
        // player exactly (same period keeps them phase-aligned), only ~xfade samples
        // per loop differ.
        int differ = 0;
        for (std::size_t i = 0; i < rh.out.size(); ++i)
            if (std::abs(rh.out[i] - rs.out[i]) > 1.0e-4f)
                ++differ;
        CHECK(differ > 0, "SamplePlayer loop: crossfade actually changes the seam");
        CHECK(differ < 300,
              "SamplePlayer loop: crossfade confined to the seam (differ="
              + juce::String(differ) + " of 1000)");

        // (4) No tail (loopEnd == length): eat-in fallback shortens the period by
        // xfade and never reads out of bounds.
        const double xf = 32.0;
        SamplePlayer eatIn = makeLooper(0.0, static_cast<double>(kLen), xf);
        Run re = runSteps(eatIn, pcm, kLen * 4);
        CHECK(!re.nan, "SamplePlayer loop: eat-in path has no NaN/OOB");
        CHECK(std::abs(measurePeriod(re) - (static_cast<double>(kLen) - xf)) < 1.0,
              "SamplePlayer loop: eat-in period == span - xfade (measured="
              + juce::String(measurePeriod(re)) + ")");
    }
}
