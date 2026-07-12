// RecordMachineTest -- live-resampler capture into volatile REC buffers (6.2).
//
// Machine-level coverage (no processor): a note-on (the recorder trig) starts an
// overwrite capture of the input buffer into the target volatile pool entry for
// rec_length; a Sample pointed at the same entry plays it back.

#include "TestHarness.h"
#include "../src/machine/RecordMachine.h"
#include "../src/machine/SampleMachine.h"
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

    void runRecordMachineTests()
    {
        constexpr double kSr = 48000.0;

        // Capture writes input for rec_length; output is silent ----------------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));  // 1 s capacity

            RecordMachine rec(pool);
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
            RecordMachine rec(pool);
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
            RecordMachine rec(pool);
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
            RecordMachine rec(pool);
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

        // Live-resample round-trip: a Sample plays the captured buffer --------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            RecordMachine rec(pool);
            rec.prepare(kSr, 512);
            // Capture a non-trivial 0.1 s (4800-sample) constant into REC slot 0.
            // The deck-medium recorder commits at close, so run the take to completion
            // (10 x 512 = 5120 > 4800) before the slot holds it.
            auto params = recFrame(0.1f);
            auto blk = filledBlock(512, 0.6f);
            rec.process(noteOnAt(0), params, blk);
            for (int b = 0; b < 10; ++b)
            {
                blk = filledBlock(512, 0.6f);
                juce::MidiBuffer none;
                rec.process(none, params, blk);
            }

            const Sample* rs = pool.get(idx);
            CHECK(rs != nullptr && rs->pcm.getNumSamples() == 4800,
                  "constant-tempo capture commits the full rec_length (4800)");
            CHECK(rs != nullptr && allClose(rs->pcm, 0, 4800, 0.6f),
                  "constant-tempo capture is bit-identical to the input (unity head)");

            // Sample pointed at the same pool index plays it.
            SampleMachine samp(pool);
            samp.prepare(kSr, 512);
            ParamFrame sp(static_cast<std::size_t>(samp.numParams()), 0.0f);
            for (int i = 0; i < samp.numParams(); ++i)
                sp[static_cast<std::size_t>(i)] = samp.paramSpec(i).defaultValue;
            sp[0] = static_cast<float>(idx);  // sample_id → REC slot

            juce::AudioBuffer<float> out(2, 512);
            out.clear();
            samp.process(noteOnAt(0), sp, out);

            CHECK(out.getMagnitude(0, 512) > 0.01f,
                  "Sample plays back the captured REC buffer (non-silent)");
        }

        // sourceBars stamp from the transport snapshot (B1/B2) ------------------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            RecordMachine rec(pool);
            rec.prepare(kSr, 512);

            TransportInfo tr;
            tr.samplesPerBar = 24000.0;  // 1 bar = 24000 samples
            tr.running = true;
            rec.setTransport(tr);

            // 0.5 s @ 48k = 24000 samples = exactly 1 bar. Commit-at-close, so run
            // the take to completion (48 x 512 = 24576 > 24000).
            auto params = recFrame(0.5f);
            auto blk = filledBlock(512, 0.5f);
            rec.process(noteOnAt(0), params, blk);
            for (int b = 0; b < 48; ++b)
            {
                blk = filledBlock(512, 0.5f);
                juce::MidiBuffer none;
                rec.process(none, params, blk);
            }

            CHECK(feq(static_cast<float>(pool.sourceBars(idx)), 1.0f),
                  "recorder stamps sourceBars = capturedSamples / samplesPerBar");
        }

        // §40.2 tape varispeed: a tempo change mid-take length-warps the take -----
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, 300000);  // room for a lengthened take
            RecordMachine rec(pool);
            rec.prepare(kSr, 512);

            // Calibrate at 48000 samples/bar (barPpq 4 → 12000 samples/quarter).
            TransportInfo a; a.samplesPerBar = 48000.0; a.barPpq = 4.0; a.running = true;
            rec.setTransport(a);

            // rec_length = 1 s = 48000 engine samples (the wall-clock cap).
            auto params = recFrame(1.0f);
            auto blk = filledBlock(512, 0.5f);
            rec.process(noteOnAt(0), params, blk);   // start, r=1
            // First half at tempo A (~24000 engine samples).
            for (int b = 0; b < 46; ++b)
            {
                blk = filledBlock(512, 0.5f);
                juce::MidiBuffer none;
                rec.process(none, params, blk);
            }
            // Halve the tempo (samplesPerBar 96000 → samples/quarter 24000): the head
            // now chases at r = 12000/24000 = 0.5 — the take COMPRESSES from here.
            TransportInfo bpm; bpm.samplesPerBar = 96000.0; bpm.barPpq = 4.0; bpm.running = true;
            rec.setTransport(bpm);
            for (int b = 0; b < 60; ++b)
            {
                blk = filledBlock(512, 0.5f);
                juce::MidiBuffer none;
                rec.process(none, params, blk);
            }

            const Sample* s = pool.get(idx);
            CHECK(s != nullptr, "varispeed take committed");
            const int n = s != nullptr ? s->pcm.getNumSamples() : 0;
            // ~24000 samples at r=1 plus ~24000 engine samples at r=0.5 (~12000 reel)
            // ≈ 36000 — materially SHORTER than the 48000 wall-clock take: the tempo
            // change warped the length (not pitch-preserved).
            CHECK(n > 30000 && n < 44000,
                  "tempo change mid-take length-warps the committed take (got "
                      + juce::String(n) + ", vs 48000 unwarped)");
            CHECK(s != nullptr && s->pcm.getMagnitude(0, n) > 0.1f,
                  "varispeed take is non-silent (constant stays ~constant)");
        }

        // Deck-native undo restores the slot's prior take -----------------------
        {
            SamplePool pool;
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            RecordMachine rec(pool);
            rec.prepare(kSr, 512);

            // First take: 480 samples of 0.5 (closes within one block).
            auto p01 = recFrame(0.01f);
            auto b1 = filledBlock(512, 0.5f);
            rec.process(noteOnAt(0), p01, b1);
            const Sample* s1 = pool.get(idx);
            CHECK(s1 != nullptr && s1->pcm.getNumSamples() == 480
                      && allClose(s1->pcm, 0, 480, 0.5f),
                  "first take committed (480 of 0.5)");

            // Second take overwrites with 0.25.
            auto b2 = filledBlock(512, 0.25f);
            rec.process(noteOnAt(0), p01, b2);
            const Sample* s2 = pool.get(idx);
            CHECK(s2 != nullptr && allClose(s2->pcm, 0, 480, 0.25f),
                  "second take overwrote the slot (0.25)");

            // Undo restores the first take.
            CHECK(rec.undo(), "undo restores the prior take");
            const Sample* s3 = pool.get(idx);
            CHECK(s3 != nullptr && s3->pcm.getNumSamples() == 480
                      && allClose(s3->pcm, 0, 480, 0.5f),
                  "undo brought back the first take (480 of 0.5)");
            // One level only: a second undo is a no-op.
            CHECK(! rec.undo(), "a second undo does nothing (one level)");
        }

        // Monitor mode: On passes the input through; Off (default) is silent ----
        {
            SamplePool pool;
            pool.addVolatile();
            pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            RecordMachine rec(pool);
            rec.prepare(kSr, 512);

            // 4-element frame: input_source, target_buffer, rec_length, monitor.
            ParamFrame on{ 1.0f, 0.0f, 0.01f, 1.0f };
            auto bufOn = filledBlock(512, 0.5f);
            rec.process(noteOnAt(0), on, bufOn);
            CHECK(allClose(bufOn, 0, 512, 0.5f),
                  "monitor On: the input passes through to the output");

            ParamFrame off{ 1.0f, 0.0f, 0.01f, 0.0f };
            auto bufOff = filledBlock(512, 0.5f);
            rec.process(noteOnAt(0), off, bufOff);
            CHECK(allClose(bufOff, 0, 512, 0.0f),
                  "monitor Off: the output is silent (capture-only tap)");
        }
    }
}
