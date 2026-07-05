// StretchEngineTest -- Bungee-backed IStretchEngine seam (9.23 Stage 1).
//
// Verifies the pull-model engine on a pure sine through PcmStretchSource:
//   - pitch x2 (time 1): output frequency doubles, length ~ unchanged.
//   - time x2  (pitch 1): output ~2x longer, frequency preserved.
//   - start position respected (half-buffer start ⇒ ~half length).
//   - mono source → stereo out (channel 1 mirrors channel 0).
//   - loop wrap over a short region: continuous, no seam discontinuity.
//   - reverse: runs, finite, preserves the sine frequency.
//   - no NaN/Inf anywhere.

#include "TestHarness.h"
#include "../src/dsp/BungeeStretchEngine.h"
#include "../src/dsp/PcmStretchSource.h"
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
                b.setSample(0, i, static_cast<float>(std::sin(
                    2.0 * juce::MathConstants<double>::pi * freq
                    * static_cast<double>(i) / kSr)));
            return b;
        }

        // Pull output into a flat vector (channel 0), stopping when the engine
        // goes inactive or `cap` samples are collected (loops rely on cap).
        std::vector<float> collect(IStretchEngine& e, int cap, int nch = 1)
        {
            std::vector<float> v;
            juce::AudioBuffer<float> blk(nch, 256);
            int guard = 0;
            while (e.isActive() && static_cast<int>(v.size()) < cap && guard++ < 200000)
            {
                blk.clear();
                e.process(blk, 0, 256);
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

        double measureFreq(const std::vector<float>& v)
        {
            const int n = static_cast<int>(v.size());
            if (n < 8) return 0.0;
            const int lo = n / 4, hi = (3 * n) / 4;
            int crossings = 0;
            for (int i = lo + 1; i < hi; ++i)
                if ((v[static_cast<std::size_t>(i - 1)] <= 0.0f)
                    != (v[static_cast<std::size_t>(i)] <= 0.0f))
                    ++crossings;
            const double dur = static_cast<double>(hi - lo) / kSr;
            return (dur > 0.0) ? (static_cast<double>(crossings) / 2.0) / dur : 0.0;
        }
    }

    void runStretchEngineTests()
    {
        const double f = 1000.0;
        const int srcLen = 24000;  // 0.5 s
        auto src = sineSource(f, srcLen);
        PcmStretchSource source(&src, kSr);

        // Pitch x2, time x1: ~same length, frequency doubled.
        {
            BungeeStretchEngine e;
            e.prepare(kSr, kSr, 2, 512);
            e.start(&source, 0.0, /*time*/ 1.0, /*pitch*/ 2.0);
            auto out = collect(e, srcLen * 4);
            CHECK(!anyNaN(out), "pitch x2: no NaN/Inf");
            const double mf = measureFreq(out);
            CHECK(mf > 2.0 * f * 0.85 && mf < 2.0 * f * 1.15,
                  "pitch x2: frequency doubled (~2000 Hz, got " + juce::String(mf) + ")");
        }

        // Time x2, pitch x1: ~2x length, frequency preserved.
        {
            BungeeStretchEngine e;
            e.prepare(kSr, kSr, 2, 512);
            e.start(&source, 0.0, /*time*/ 2.0, /*pitch*/ 1.0);
            auto out = collect(e, srcLen * 6);
            CHECK(!anyNaN(out), "time x2: no NaN/Inf");
            CHECK(static_cast<int>(out.size()) > srcLen * 3 / 2
                  && static_cast<int>(out.size()) < srcLen * 5 / 2,
                  "time x2: output ~2x source length (got "
                  + juce::String(static_cast<int>(out.size())) + ")");
            const double mf = measureFreq(out);
            CHECK(mf > f * 0.85 && mf < f * 1.15,
                  "time x2: frequency preserved (~1000 Hz, got " + juce::String(mf) + ")");
        }

        // Start position respected: begin at the half-way point ⇒ ~half length.
        {
            BungeeStretchEngine e;
            e.prepare(kSr, kSr, 2, 512);
            e.start(&source, srcLen / 2.0, 1.0, 1.0);
            auto out = collect(e, srcLen * 4);
            CHECK(!anyNaN(out), "start pos: no NaN/Inf");
            CHECK(static_cast<int>(out.size()) > srcLen / 4
                  && static_cast<int>(out.size()) < srcLen * 3 / 4,
                  "start pos: half-buffer start ⇒ ~half length (got "
                  + juce::String(static_cast<int>(out.size())) + ")");
        }

        // Mono source → stereo out: channel 1 replicates channel 0.
        {
            BungeeStretchEngine e;
            e.prepare(kSr, kSr, 2, 512);
            e.start(&source, 0.0, 1.0, 1.0);
            juce::AudioBuffer<float> blk(2, 512);
            blk.clear();
            e.process(blk, 0, 512);
            bool mirrored = true;
            for (int i = 0; i < 512; ++i)
                if (!feq(blk.getSample(0, i), blk.getSample(1, i)))
                    { mirrored = false; break; }
            CHECK(mirrored, "mono→stereo: channel 1 mirrors channel 0");
        }

        // Loop wrap over a short region: continuous (no seam spike), never ends.
        {
            BungeeStretchEngine e;
            e.prepare(kSr, kSr, 2, 512);
            e.start(&source, 0.0, 1.0, 1.0);
            e.setLoop(0, srcLen);
            auto out = collect(e, srcLen * 3);   // > 2 loop lengths (cap-bounded)
            CHECK(!anyNaN(out), "loop: no NaN/Inf");
            CHECK(static_cast<int>(out.size()) >= srcLen * 2,
                  "loop: runs past 2 loop lengths without ending (got "
                  + juce::String(static_cast<int>(out.size())) + ")");
            // Measure the steady-state region (past the onset transient, which the
            // machine's anti-click gate masks). The loop seams fall at multiples of
            // srcLen; a discontinuous wrap would spike the sample-to-sample delta.
            float maxDelta = 0.0f;
            for (std::size_t i = static_cast<std::size_t>(srcLen / 2); i < out.size(); ++i)
                maxDelta = std::max(maxDelta, std::abs(out[i] - out[i - 1]));
            CHECK(maxDelta < 0.3f,
                  "loop: no discontinuity at the seam (max delta "
                  + juce::String(maxDelta) + ")");
        }

        // Reverse: runs, finite, preserves the sine frequency.
        {
            BungeeStretchEngine e;
            e.prepare(kSr, kSr, 2, 512);
            e.setReverse(true);
            e.start(&source, srcLen - 1.0, 1.0, 1.0);
            auto out = collect(e, srcLen * 4);
            CHECK(!anyNaN(out), "reverse: no NaN/Inf");
            CHECK(static_cast<int>(out.size()) > srcLen / 4,
                  "reverse: produces audio (got "
                  + juce::String(static_cast<int>(out.size())) + ")");
            const double mf = measureFreq(out);
            CHECK(mf > f * 0.8 && mf < f * 1.2,
                  "reverse: sine frequency preserved (~1000 Hz, got "
                  + juce::String(mf) + ")");
        }
    }
}
