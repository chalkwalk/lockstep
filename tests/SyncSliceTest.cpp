// SyncSliceTest.cpp — 4.9 beat-grid slicing (TransientDetector.h placeSyncSlices
// + findFirstOnsetSample). Synthetic click grids at a known tempo; verify the
// grid anchors on the first onset, spaces by the chosen division, honours the
// 16-slice cap, and degrades safely on bad input.

#include "TestHarness.h"
#include "../src/machine/TransientDetector.h"
#include <cmath>
#include <vector>

namespace lockstep
{
    namespace
    {
        // A click grid: short impulses every `beatSamples`, after `leadSilence`
        // samples of silence. Clicks are a few-ms decaying blip so the envelope
        // follower sees a real onset.
        juce::AudioBuffer<float> clickGrid(double sr, double seconds,
                                           int beatSamples, int leadSilence)
        {
            const int n = static_cast<int>(sr * seconds);
            juce::AudioBuffer<float> buf(1, n);
            buf.clear();
            float* d = buf.getWritePointer(0);
            const int blip = static_cast<int>(sr * 0.005);   // 5 ms
            for (int pos = leadSilence; pos < n; pos += beatSamples)
            {
                for (int i = 0; i < blip && pos + i < n; ++i)
                {
                    const double env = 1.0 - static_cast<double>(i) / blip;
                    d[pos + i] = static_cast<float>(0.8 * env
                        * std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / sr));
                }
            }
            return buf;
        }
    }

    void runSyncSliceTests()
    {
        const double sr = 44100.0;
        const double bpm = 120.0;
        const int beatSamples = static_cast<int>((60.0 / bpm) * sr);   // 0.5 s
        const int divBeat = 4;   // kSyncBeatsPerSlice index 4 == 1 beat/slice

        // Grid with 120 ms of leading silence (> kMinSliceMs, so it survives as
        // a real anchor rather than collapsing to 0) -> grid anchors on the
        // first onset.
        {
            const int lead = static_cast<int>(sr * 0.120);
            auto buf = clickGrid(sr, 4.0, beatSamples, lead);
            const auto ba = analyseSample(buf, sr);

            const int onset = findFirstOnsetSample(ba, buf.getNumSamples());
            CHECK(std::abs(onset - lead) < static_cast<int>(sr * 0.015),
                  "first onset detected within ~15 ms of the leading silence");

            const auto sl = placeSyncSlices(buf, sr, bpm, divBeat, ba);
            CHECK(sl.size() >= 3, "1-beat division yields several slices");
            CHECK(sl[0] == 0, "slice 0 is always at 0");
            CHECK(std::abs(sl[1] - lead) < static_cast<int>(sr * 0.015),
                  "first grid boundary lands near the anchored onset");
            CHECK(std::abs((sl[2] - sl[1]) - beatSamples) < static_cast<int>(sr * 0.015),
                  "grid spacing matches one beat at 120 bpm");
        }

        // Zero-offset grid: onset at 0 collapses the anchor to a plain grid with
        // no duplicate boundary at 0.
        {
            auto buf = clickGrid(sr, 4.0, beatSamples, 0);
            const auto ba = analyseSample(buf, sr);
            const auto sl = placeSyncSlices(buf, sr, bpm, divBeat, ba);
            CHECK(sl[0] == 0, "zero-offset: slice 0 at 0");
            CHECK(sl.size() >= 2 && sl[1] > beatSamples / 2,
                  "zero-offset: no duplicate boundary at 0; first step ~one beat in");
        }

        // Guards: bpm <= 0 -> single {0}; positions stay in range.
        {
            auto buf = clickGrid(sr, 2.0, beatSamples, 0);
            const auto ba = analyseSample(buf, sr);
            const auto none = placeSyncSlices(buf, sr, 0.0, divBeat, ba);
            CHECK(none.size() == 1 && none[0] == 0, "bpm 0 -> single slice {0}");
        }

        // 16-cap: a long buffer at a fine division stops at exactly 16 slices,
        // strictly increasing, all in [0, numSamples).
        {
            auto buf = clickGrid(sr, 20.0, beatSamples, 0);
            const auto ba = analyseSample(buf, sr);
            const int div16th = 6;   // 0.25 beats/slice (1/16)
            const auto sl = placeSyncSlices(buf, sr, bpm, div16th, ba);
            CHECK(sl.size() == 16, "fine division over a long buffer caps at 16 slices");
            bool increasing = true;
            for (std::size_t i = 1; i < sl.size(); ++i)
                if (sl[i] <= sl[i - 1]) increasing = false;
            CHECK(increasing, "slice positions strictly increasing");
            CHECK(sl.back() < buf.getNumSamples(), "last slice within the buffer");
        }
    }
}
