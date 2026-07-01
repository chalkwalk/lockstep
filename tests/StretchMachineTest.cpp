// StretchMachineTest -- the Flex-analog Player (C3): independent pitch + tempo via
// the WSOLA TimeStretch voice. Verifies the decoupling that distinguishes it from
// the rate-based Sample:
//   - transposing (note up an octave) does NOT shorten the output (pitch != speed).
//   - timestretch=Tempo stretches the buffer to the project tempo (duration tracks).

#include "TestHarness.h"
#include "../src/machine/StretchMachine.h"
#include "../src/machine/SamplePool.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        constexpr double kSr = 48000.0;

        // Build a volatile pool entry holding a sine, stamped with sourceBars.
        int makeSine(SamplePool& pool, int len, double freq, double bars)
        {
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 1, len);
            auto* pcm = pool.mutableVolatilePcm(idx);
            for (int i = 0; i < len; ++i)
                pcm->setSample(0, i, static_cast<float>(
                    std::sin(2.0 * juce::MathConstants<double>::pi * freq
                             * static_cast<double>(i) / kSr)));
            pool.setSourceBars(idx, bars);
            return idx;
        }

        // Count non-silent output samples while holding `note`, over up to maxLen.
        int activeSamples(StretchMachine& p, const ParamFrame& params, int note, int maxLen)
        {
            juce::AudioBuffer<float> blk(1, 256);
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
            juce::MidiBuffer none;
            int total = 0, count = 0;
            bool nan = false;
            for (int b = 0; total < maxLen; ++b)
            {
                blk.clear();
                p.process(b == 0 ? on : none, params, blk);
                for (int i = 0; i < 256; ++i)
                {
                    const float x = blk.getSample(0, i);
                    if (!std::isfinite(x)) nan = true;
                    if (std::abs(x) > 1.0e-3f) ++count;
                    ++total;
                }
            }
            CHECK(!nan, "Player: no NaN/Inf in output");
            return count;
        }

        ParamFrame playerFrame(int sampleId, float pitch, float tsMode)
        {
            // sample_id, pitch, timestretch, start
            return ParamFrame{ static_cast<float>(sampleId), pitch, tsMode, 0.0f };
        }
    }

    void runStretchMachineTests()
    {
        const int srcLen = 24000;  // 0.5 s
        const int cap = srcLen * 4;

        // Off mode at root: non-silent, active ~ source length.
        SamplePool pool;
        const int idx = makeSine(pool, srcLen, 440.0, /*bars*/ 1.0);

        int activeRoot = 0;
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            auto fr = playerFrame(idx, 0.0f, 0.0f);  // Off
            activeRoot = activeSamples(p, fr, 60, cap);
            CHECK(activeRoot > srcLen / 2,
                  "Player: root note produces audio (active=" + juce::String(activeRoot) + ")");
        }

        // Off mode, octave up: duration must NOT shrink (pitch decoupled from speed).
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            auto fr = playerFrame(idx, 0.0f, 0.0f);  // Off
            const int activeOct = activeSamples(p, fr, 72, cap);
            const double ratio = static_cast<double>(activeOct) / std::max(1, activeRoot);
            CHECK(ratio > 0.7 && ratio < 1.4,
                  "Player: transposing an octave keeps the duration (ratio=" + juce::String(ratio) + ")");
        }

        // Tempo mode with a doubled bar length: output stretched ~2x.
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            TransportInfo tr;
            tr.samplesPerBar = 2.0 * static_cast<double>(srcLen);  // project bar = 2x the capture bar
            tr.running = true;
            p.setTransport(tr);
            auto fr = playerFrame(idx, 0.0f, 1.0f);  // Tempo
            const int activeTempo = activeSamples(p, fr, 60, srcLen * 6);
            const double ratio = static_cast<double>(activeTempo) / std::max(1, activeRoot);
            CHECK(ratio > 1.5 && ratio < 2.6,
                  "Player: Tempo mode stretches to the project tempo (~2x, ratio=" + juce::String(ratio) + ")");
        }
    }
}
