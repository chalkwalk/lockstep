// TempoEstimateTest -- energy-based BPM detection (WI-4, DESIGN §28).
//
// estimateBpm() is a pure function of a BlockAnalysis (the per-block RMS
// envelope analyseSample() caches). We synthesise envelopes directly — a
// periodic onset train at a known BPM, silence, and a flat (non-rhythmic)
// signal — and assert the estimate recovers the tempo (correct octave, tight
// tolerance) or reports 0 = unknown.

#include "TestHarness.h"
#include "../src/dsp/TempoEstimate.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        constexpr double kSr = 44100.0;

        // Build a BlockAnalysis whose RMS envelope has a sharp onset every beat
        // at `bpm`, spanning `seconds`. blockSize matches analyseSample()'s 5 ms.
        BlockAnalysis onsetEnvelope(double bpm, double seconds)
        {
            BlockAnalysis ba;
            ba.sampleRate = kSr;
            ba.blockSize = static_cast<int>(kBlockMs * 0.001 * kSr + 0.5);
            const double blocksPerSec = kSr / static_cast<double>(ba.blockSize);
            ba.numBlocks = static_cast<int>(seconds * blocksPerSec);
            ba.rms.assign(static_cast<std::size_t>(ba.numBlocks), 0.02f);  // quiet floor

            const double beatBlocks = 60.0 * blocksPerSec / bpm;
            for (double pos = 0.0; pos < ba.numBlocks; pos += beatBlocks)
            {
                const int b = static_cast<int>(pos + 0.5);
                if (b >= 0 && b < ba.numBlocks)
                {
                    // A two-block rising edge so the first-difference novelty is
                    // strong at the onset (mimics a percussive hit's attack).
                    ba.rms[static_cast<std::size_t>(b)] = 1.0f;
                    if (b + 1 < ba.numBlocks)
                        ba.rms[static_cast<std::size_t>(b + 1)] = 0.6f;
                }
            }
            return ba;
        }
    }

    void runTempoEstimateTests()
    {
        // Recovers common tempos within a tight tolerance and correct octave.
        for (double bpm : { 90.0, 120.0, 174.0 })
        {
            const BlockAnalysis ba = onsetEnvelope(bpm, 8.0);
            const double est = estimateBpm(ba);
            CHECK(std::abs(est - bpm) <= 3.0,
                  (juce::String("estimateBpm recovers ") + juce::String(bpm)
                   + " (got " + juce::String(est) + ")").toRawUTF8());
        }

        // Silence → no onsets → unknown (0).
        {
            BlockAnalysis ba;
            ba.sampleRate = kSr;
            ba.blockSize = static_cast<int>(kBlockMs * 0.001 * kSr + 0.5);
            ba.numBlocks = 1600;
            ba.rms.assign(static_cast<std::size_t>(ba.numBlocks), 0.0f);
            CHECK(estimateBpm(ba) == 0.0, "silence returns 0 (unknown)");
        }

        // Flat non-zero (a held tone, no rhythmic onsets) → unknown (0).
        {
            BlockAnalysis ba;
            ba.sampleRate = kSr;
            ba.blockSize = static_cast<int>(kBlockMs * 0.001 * kSr + 0.5);
            ba.numBlocks = 1600;
            ba.rms.assign(static_cast<std::size_t>(ba.numBlocks), 0.5f);
            CHECK(estimateBpm(ba) == 0.0, "flat sustain returns 0 (non-rhythmic)");
        }

        // Too short to see two periods of the slowest tempo → unknown (0).
        {
            const BlockAnalysis ba = onsetEnvelope(120.0, 0.4);
            CHECK(estimateBpm(ba) == 0.0, "sub-second clip returns 0 (too short)");
        }

        // Empty analysis is safe.
        {
            BlockAnalysis ba;
            CHECK(estimateBpm(ba) == 0.0, "empty analysis returns 0");
        }
    }
}
