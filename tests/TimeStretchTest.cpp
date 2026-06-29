// TimeStretchTest -- WSOLA time-stretch + resample voice (C1).
//
// Verifies independent time and pitch on a pure sine:
//   - time-stretch x2 (pitch 1): output ~2x longer, SAME frequency (pitch kept).
//   - pitch x2  (time 1): output ~same length, DOUBLE frequency.
//   - no NaN/Inf anywhere.

#include "TestHarness.h"
#include "../src/dsp/TimeStretch.h"
#include <cmath>
#include <vector>

namespace lockstep
{
    namespace
    {
        constexpr double kSr = 48000.0;

        juce::AudioBuffer<float> sineSource(double freq, int len)
        {
            juce::AudioBuffer<float> b(1, len);
            for (int i = 0; i < len; ++i)
                b.setSample(0, i, std::sin(2.0 * juce::MathConstants<double>::pi
                                           * freq * static_cast<double>(i) / kSr));
            return b;
        }

        std::vector<float> collect(TimeStretch& ts, int cap)
        {
            std::vector<float> v;
            juce::AudioBuffer<float> blk(1, 256);
            int guard = 0;
            while (ts.isActive() && static_cast<int>(v.size()) < cap && guard++ < 100000)
            {
                blk.clear();
                ts.process(blk, 0, 256);
                for (int i = 0; i < 256; ++i) v.push_back(blk.getSample(0, i));
            }
            return v;
        }

        bool anyNaN(const std::vector<float>& v)
        {
            for (float x : v)
                if (!std::isfinite(x)) return true;
            return false;
        }

        // Dominant frequency from zero-crossings over the middle half of the signal.
        double measureFreq(const std::vector<float>& v)
        {
            const int n = static_cast<int>(v.size());
            const int lo = n / 4, hi = (3 * n) / 4;
            int crossings = 0;
            for (int i = lo + 1; i < hi; ++i)
                if ((v[static_cast<std::size_t>(i - 1)] <= 0.0f) != (v[static_cast<std::size_t>(i)] <= 0.0f))
                    ++crossings;
            const double dur = static_cast<double>(hi - lo) / kSr;
            return (dur > 0.0) ? (static_cast<double>(crossings) / 2.0) / dur : 0.0;
        }
    }

    void runTimeStretchTests()
    {
        const double f = 1000.0;
        const int srcLen = 24000;  // 0.5 s
        auto src = sineSource(f, srcLen);

        // Time-stretch x2, pitch x1: ~2x length, frequency preserved.
        {
            TimeStretch ts;
            ts.prepare(kSr, 1);
            ts.start(&src, 0.0, /*time*/ 2.0, /*pitch*/ 1.0);
            auto out = collect(ts, srcLen * 4);

            CHECK(!anyNaN(out), "stretch x2: no NaN/Inf");
            CHECK(static_cast<int>(out.size()) > srcLen * 3 / 2
                  && static_cast<int>(out.size()) < srcLen * 5 / 2,
                  "stretch x2: output is ~2x the source length (got "
                  + juce::String(static_cast<int>(out.size())) + ")");
            const double mf = measureFreq(out);
            CHECK(mf > f * 0.9 && mf < f * 1.1,
                  "stretch x2: frequency preserved (~1000 Hz, got " + juce::String(mf) + ")");
        }

        // Pitch x2, time x1: ~same length, frequency doubled.
        {
            TimeStretch ts;
            ts.prepare(kSr, 1);
            ts.start(&src, 0.0, /*time*/ 1.0, /*pitch*/ 2.0);
            auto out = collect(ts, srcLen * 4);

            CHECK(!anyNaN(out), "pitch x2: no NaN/Inf");
            CHECK(static_cast<int>(out.size()) > srcLen / 2
                  && static_cast<int>(out.size()) < srcLen * 3 / 2,
                  "pitch x2: output is ~same length as source (got "
                  + juce::String(static_cast<int>(out.size())) + ")");
            const double mf = measureFreq(out);
            CHECK(mf > 2.0 * f * 0.9 && mf < 2.0 * f * 1.1,
                  "pitch x2: frequency doubled (~2000 Hz, got " + juce::String(mf) + ")");
        }

        // Identity (time 1, pitch 1): frequency preserved, ~same length.
        {
            TimeStretch ts;
            ts.prepare(kSr, 1);
            ts.start(&src, 0.0, 1.0, 1.0);
            auto out = collect(ts, srcLen * 4);
            CHECK(!anyNaN(out), "identity: no NaN/Inf");
            const double mf = measureFreq(out);
            CHECK(mf > f * 0.9 && mf < f * 1.1,
                  "identity: frequency preserved (~1000 Hz, got " + juce::String(mf) + ")");
        }
    }
}
