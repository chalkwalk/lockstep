// RecorderMachineTest -- live-resampler capture into volatile REC buffers (6.2).
//
// Machine-level coverage (no processor): a note-on (the recorder trig) starts an
// overwrite capture of the input buffer into the target volatile pool entry for
// rec_length; a Sampler pointed at the same entry plays it back.

#include "TestHarness.h"
#include "../src/machine/RecorderMachine.h"
#include "../src/machine/SamplerMachine.h"
#include "../src/machine/SamplePool.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        // input_source=External, target_buffer=0, rec_length=seconds.
        ParamFrame recFrame(float recSeconds, int targetSlot = 0)
        {
            return ParamFrame{ 1.0f, static_cast<float>(targetSlot), recSeconds };
        }

        juce::AudioBuffer<float> filledBlock(int numSamples, float value)
        {
            juce::AudioBuffer<float> b(2, numSamples);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    b.setSample(ch, i, value);
            return b;
        }

        juce::MidiBuffer noteOnAt(int samplePos)
        {
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), samplePos);
            return m;
        }

        bool allClose(const juce::AudioBuffer<float>& b, int from, int to, float v)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = from; i < to; ++i)
                    if (std::abs(b.getSample(ch, i) - v) > 1e-5f) return false;
            return true;
        }
    }

    void runRecorderMachineTests()
    {
        constexpr double kSr = 48000.0;

        // Capture writes input for rec_length; output is silent ----------------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));  // 1 s capacity

            RecorderMachine rec(pool);
            rec.prepare(kSr, 512);

            auto params = recFrame(0.01f);  // 480 samples
            auto buf = filledBlock(512, 0.5f);
            auto midi = noteOnAt(0);
            rec.process(midi, params, buf);

            const Sample* s = pool.get(idx);
            CHECK(s != nullptr && s->pcm.getNumSamples() == 480,
                  "captured length == rec_length samples");
            CHECK(s != nullptr && allClose(s->pcm, 0, 480, 0.5f),
                  "captured region holds the input signal");
            CHECK(allClose(buf, 0, 512, 0.0f),
                  "recorder output is silent (capture-only tap)");
        }

        // A second trig overwrites (no append, no accumulate) ------------------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            RecorderMachine rec(pool);
            rec.prepare(kSr, 512);

            auto params = recFrame(0.01f);
            auto first = filledBlock(512, 0.5f);
            auto m1 = noteOnAt(0);
            rec.process(m1, params, first);

            auto second = filledBlock(512, 0.25f);
            auto m2 = noteOnAt(0);
            rec.process(m2, params, second);

            const Sample* s = pool.get(idx);
            CHECK(s != nullptr && allClose(s->pcm, 0, 480, 0.25f),
                  "second capture overwrites the first");
        }

        // rec_length clamps to the buffer capacity -----------------------------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, 1000);  // tiny 1000-sample capacity
            RecorderMachine rec(pool);
            rec.prepare(kSr, 2048);

            auto params = recFrame(1.0f);  // wants 48000 samples
            auto buf = filledBlock(2048, 0.5f);
            auto midi = noteOnAt(0);
            rec.process(midi, params, buf);

            const Sample* s = pool.get(idx);
            CHECK(s != nullptr && s->pcm.getNumSamples() == 1000,
                  "capture truncates at buffer capacity");
        }

        // Capture continues across block boundaries ----------------------------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            RecorderMachine rec(pool);
            rec.prepare(kSr, 512);

            auto params = recFrame(0.02f);  // 960 samples > one 512 block
            juce::MidiBuffer none;

            auto b1 = filledBlock(512, 0.5f);
            rec.process(noteOnAt(0), params, b1);   // captures 512
            auto b2 = filledBlock(512, 0.75f);
            rec.process(none, params, b2);          // continues 448 more

            const Sample* s = pool.get(idx);
            CHECK(s != nullptr && s->pcm.getNumSamples() == 960,
                  "cross-block capture reaches full rec_length");
            CHECK(s != nullptr && allClose(s->pcm, 0, 512, 0.5f),
                  "first block's input captured");
            CHECK(s != nullptr && allClose(s->pcm, 512, 960, 0.75f),
                  "second block's input captured");
        }

        // Live-resample round-trip: a Sampler plays the captured buffer --------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            RecorderMachine rec(pool);
            rec.prepare(kSr, 512);
            // Capture a non-trivial 0.1 s tone-ish constant into REC slot 0.
            auto params = recFrame(0.1f);
            auto src = filledBlock(512, 0.6f);
            rec.process(noteOnAt(0), params, src);

            // Sampler pointed at the same pool index plays it.
            SamplerMachine samp(pool);
            samp.prepare(kSr, 512);
            ParamFrame sp(static_cast<std::size_t>(samp.numParams()), 0.0f);
            for (int i = 0; i < samp.numParams(); ++i)
                sp[static_cast<std::size_t>(i)] = samp.paramSpec(i).defaultValue;
            sp[0] = static_cast<float>(idx);  // sample_id → REC slot

            juce::AudioBuffer<float> out(2, 512);
            out.clear();
            samp.process(noteOnAt(0), sp, out);

            CHECK(out.getMagnitude(0, 512) > 0.01f,
                  "Sampler plays back the captured REC buffer (non-silent)");
        }
    }
}
