// LooperMachineTest -- overdub looper state machine (6.3, DESIGN §29.2).
//
// Drives the machine through Idle → Record → Play → Overdub → Undo → Clear via the
// command mailbox (one command drained per block) and checks the loop audio.

#include "TestHarness.h"
#include "../src/machine/LooperMachine.h"
#include "../src/machine/SamplerMachine.h"
#include "../src/machine/SamplePool.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        using Cmd = LooperMachine::Cmd;
        using State = LooperMachine::State;

        // Run one block with a constant input value; returns the output buffer.
        juce::AudioBuffer<float> runBlock(LooperMachine& m, int n, float inValue,
                                          Cmd cmd = Cmd::None)
        {
            if (cmd != Cmd::None) m.postCommand(cmd);
            juce::AudioBuffer<float> buf(2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample(ch, i, inValue);
            juce::MidiBuffer midi;
            ParamFrame params{ 1.0f, 0.0f };  // input_source=External, target_buffer=0
            m.process(midi, params, buf);
            return buf;
        }

    }

    void runLooperMachineTests()
    {
        constexpr double kSr = 48000.0;
        constexpr int n = 512;

        // The loop lives in a volatile pool slot (B3); prepare one with capacity.
        SamplePool pool;
        pool.addVolatile();
        pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));  // 1 s capacity

        LooperMachine loop(pool);
        loop.prepare(kSr, n);
        CHECK(loop.state() == State::Idle, "starts Idle");

        // #1: an idle insert looper monitors its input (default params = External
        // source, Auto monitor → On), so live audio passes through BEFORE recording.
        // (Pre-#1 this was silent — you couldn't hear what you were about to record.)
        {
            auto out = runBlock(loop, n, 0.5f);
            CHECK(feq(out.getSample(0, 0), 0.5f) && feq(out.getSample(0, 256), 0.5f),
                  "#1: idle insert monitors live input (no loop yet)");
        }

        // Record first layer (Idle → Recording): monitors input while capturing.
        {
            auto out = runBlock(loop, n, 0.5f, Cmd::RecordCycle);
            CHECK(loop.state() == State::Recording, "Record enters Recording");
            CHECK(feq(out.getSample(0, 0), 0.5f), "monitors input while recording");
        }

        // Close loop (Recording → Playing): plays back the recorded layer.
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::RecordCycle);
            CHECK(loop.state() == State::Playing, "second Record closes loop → Playing");
            CHECK(feq(out.getSample(0, 0), 0.5f) && feq(out.getSample(1, 256), 0.5f),
                  "plays back the recorded layer (0.5)");
        }

        // Overdub (Playing → Overdubbing): sums new input onto the loop.
        {
            auto out = runBlock(loop, n, 0.25f, Cmd::RecordCycle);
            CHECK(loop.state() == State::Overdubbing, "third Record → Overdubbing");
            CHECK(feq(out.getSample(0, 0), 0.75f), "overdub sums input onto loop (0.5+0.25)");
        }

        // Undo: reverts the overdub layer back to the pre-overdub loop (0.5).
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::Undo);
            CHECK(loop.state() == State::Playing, "Undo returns to Playing");
            CHECK(feq(out.getSample(0, 0), 0.5f), "Undo reverts to pre-overdub loop");
        }

        // Play/Stop toggles playback.
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::PlayStop);
            CHECK(loop.state() == State::Stopped, "PlayStop stops");
            CHECK(feq(out.getSample(0, 0), 0.0f), "stopped output is silent");
            auto out2 = runBlock(loop, n, 0.0f, Cmd::PlayStop);
            CHECK(loop.state() == State::Playing, "PlayStop resumes");
            CHECK(feq(out2.getSample(0, 0), 0.5f), "resumed playback (0.5)");
        }

        // Clear: empties the loop, back to Idle/silent.
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::Clear);
            CHECK(loop.state() == State::Idle, "Clear returns to Idle");
            CHECK(feq(out.getSample(0, 0), 0.0f), "cleared output is silent");
        }

        // B3: the loop lives in the pool slot — a Sampler plays it, and an overdub
        // sums in place (still one volatile sample). sourceBars is stamped on close.
        {
            SamplePool p2;
            const int idx = p2.addVolatile();
            p2.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LooperMachine lp(p2);
            lp.prepare(kSr, n);

            // 1 bar = 2 blocks of 512 = 1024 samples, for the stamp check.
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.running = true;
            lp.setTransport(tr);

            runBlock(lp, n, 0.5f, Cmd::RecordCycle);   // record 512 of 0.5
            runBlock(lp, n, 0.5f);                      // record another 512 (1024 total)
            runBlock(lp, n, 0.0f, Cmd::RecordCycle);    // close → Playing, loopLen 1024

            CHECK(p2.get(idx) != nullptr && p2.get(idx)->pcm.getNumSamples() == 1024,
                  "B3: pool slot holds the captured loop (1024 samples)");
            CHECK(feq(static_cast<float>(p2.sourceBars(idx)), 1.0f),
                  "B3: sourceBars stamped = loopLen / samplesPerBar (1 bar)");

            // A Sampler pointed at the same slot plays the loop back (non-silent).
            SamplerMachine samp(p2);
            samp.prepare(kSr, n);
            ParamFrame sp(static_cast<std::size_t>(samp.numParams()), 0.0f);
            for (int i = 0; i < samp.numParams(); ++i)
                sp[static_cast<std::size_t>(i)] = samp.paramSpec(i).defaultValue;
            sp[0] = static_cast<float>(idx);  // sample_id → the looper's slot
            juce::AudioBuffer<float> out(2, n);
            out.clear();
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            samp.process(m, sp, out);
            CHECK(out.getMagnitude(0, n) > 0.01f,
                  "B3: a Sampler plays the looper's pool slot (non-silent)");

            // Overdub onto the playing loop sums in place; still one volatile sample.
            const int before = p2.size();
            runBlock(lp, n, 0.25f, Cmd::RecordCycle);   // Playing → Overdubbing
            CHECK(p2.size() == before, "B3: overdub does not add a pool entry");
        }

        // Helper: run a block with explicit params (loop_sync etc.).
        auto runP = [](LooperMachine& m, int len, float inVal, Cmd cmd,
                       const ParamFrame& pr, int impulseAt = -1) {
            if (cmd != Cmd::None) m.postCommand(cmd);
            juce::AudioBuffer<float> b(2, len);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < len; ++i)
                    b.setSample(ch, i, (i == impulseAt) ? 1.0f : inVal);
            juce::MidiBuffer midi;
            ParamFrame params = pr;
            m.process(midi, params, b);
            return b;
        };

        // C4: bar-quantized record auto-closes at the bar length.
        {
            SamplePool p3;
            const int idx = p3.addVolatile();
            p3.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LooperMachine lp(p3);
            lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.running = true;
            lp.setTransport(tr);

            ParamFrame fr{ 1.0f, 0.0f, 2.0f };  // input=Ext, target=0, loop_sync=1 Bar
            runP(lp, 512, 0.5f, Cmd::RecordCycle, fr);  // start (512 recorded)
            CHECK(lp.state() == State::Recording, "1 Bar: still recording at 512 < 1024");
            runP(lp, 512, 0.5f, Cmd::None, fr);         // reaches 1024 → auto-close
            CHECK(lp.state() == State::Playing, "1 Bar: auto-closes at the bar length");
            CHECK(p3.get(idx) != nullptr && p3.get(idx)->pcm.getNumSamples() == 1024,
                  "1 Bar: auto-closed loop length == 1 bar (1024)");
        }

        // C4: Free-Len varispeed — halving the project tempo halves the loop's
        // playback rate, so an impulse loop wraps ~half as often.
        auto countImpulseWraps = [&runP](double projectSpb) {
            SamplePool p;
            p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LooperMachine lp(p);
            lp.prepare(kSr, 512);
            TransportInfo tr; tr.samplesPerBar = 1000.0; tr.running = false;  // free-run
            lp.setTransport(tr);

            ParamFrame fr{ 1.0f, 0.0f, 1.0f };  // Free Len
            // Record a 1024-sample loop with a single impulse at sample 0.
            runP(lp, 512, 0.0f, Cmd::RecordCycle, fr, /*impulseAt*/ 0);
            runP(lp, 512, 0.0f, Cmd::None, fr);
            runP(lp, 1, 0.0f, Cmd::RecordCycle, fr);  // close → Playing (loopLen 1024)

            // Now set the project tempo and collect output, counting impulse wraps.
            TransportInfo tp; tp.samplesPerBar = projectSpb; tp.running = false;
            lp.setTransport(tp);
            int wraps = 0;
            float prev = 0.0f;
            juce::MidiBuffer none;
            for (int b = 0; b < 32; ++b)  // 32 * 512 = 16384 output samples
            {
                juce::AudioBuffer<float> buf(2, 512);
                buf.clear();  // no live input — the engine zeroes the input buffer
                ParamFrame pp = fr;
                lp.process(none, pp, buf);
                for (int i = 0; i < 512; ++i)
                {
                    const float x = buf.getSample(0, i);
                    if (x > 0.5f && prev <= 0.5f) ++wraps;
                    prev = x;
                }
            }
            return wraps;
        };

        {
            const int wraps1x = countImpulseWraps(1000.0);  // rate 1 → period ~1024
            const int wraps2x = countImpulseWraps(2000.0);  // rate 0.5 → period ~2048
            CHECK(wraps1x > 0 && wraps2x > 0, "Free Len: impulse loop wraps at both tempos");
            const double ratio = static_cast<double>(wraps1x) / std::max(1, wraps2x);
            CHECK(ratio > 1.5 && ratio < 2.6,
                  "Free Len: halving tempo ~halves the wrap rate (ratio=" + juce::String(ratio) + ")");
        }

        // C5: loop-wrap crossfade removes the click at a sharp head/tail seam.
        // Record a 0→1 ramp loop (head≈0, tail≈1: a ~1.0 discontinuity at the wrap);
        // with the crossfade the largest sample-to-sample jump stays small.
        {
            SamplePool p;
            const int L = 1024;
            p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LooperMachine lp(p);
            lp.prepare(kSr, 512);

            ParamFrame fr{ 1.0f, 0.0f, 0.0f };  // Free
            // Record the ramp: input at record-sample k = k / L.
            for (int blk = 0; blk < L / 512; ++blk)
            {
                lp.postCommand(blk == 0 ? Cmd::RecordCycle : Cmd::None);
                juce::AudioBuffer<float> b(2, 512);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = static_cast<float>(blk * 512 + i) / static_cast<float>(L);
                    b.setSample(0, i, v); b.setSample(1, i, v);
                }
                juce::MidiBuffer none; ParamFrame pp = fr;
                lp.process(none, pp, b);
            }
            runP(lp, 1, 0.0f, Cmd::RecordCycle, fr);  // close → Playing

            // Collect ~3 loops and find the largest consecutive-sample jump.
            float maxJump = 0.0f, prev = 0.0f;
            bool first = true;
            juce::MidiBuffer none;
            for (int blk = 0; blk < 7; ++blk)
            {
                juce::AudioBuffer<float> b(2, 512);
                b.clear();  // no live input — measure the loop output alone
                ParamFrame pp = fr;
                lp.process(none, pp, b);
                for (int i = 0; i < 512; ++i)
                {
                    const float x = b.getSample(0, i);
                    if (!first) maxJump = std::max(maxJump, std::abs(x - prev));
                    prev = x; first = false;
                }
            }
            // The raw head/tail discontinuity here is ~1.0; the crossfade cuts it to
            // ~0.24 (≈12 dB) — the overlap-crossfade floor for this worst-case ramp
            // (typical correlated loop content smooths far better).
            CHECK(maxJump < 0.3f,
                  "C5: loop-wrap crossfade keeps the seam jump small (got " + juce::String(maxJump) + ")");
        }

        // #4 monitor: live-thru is governed by the monitor mode. Record a 0.5 loop,
        // then play it back while feeding 0.3 live, under each monitor setting.
        //   On  → output = loop + live (0.8);  Off → loop only (0.5).
        //   Recording under Off → silent output (still captures).
        {
            SamplePool p;
            p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LooperMachine lp(p);
            lp.prepare(kSr, n);

            constexpr float kMonOn = 1.0f, kMonOff = 2.0f;
            // input=Ext, target=0, loop_sync=Free, monitor=Off.
            ParamFrame frOff{ 1.0f, 0.0f, 0.0f, kMonOff };
            ParamFrame frOn { 1.0f, 0.0f, 0.0f, kMonOn  };

            // Record one block of 0.5 with monitor Off → output silent while it
            // still captures the input.
            auto recOut = runP(lp, n, 0.5f, Cmd::RecordCycle, frOff);
            CHECK(std::abs(recOut.getSample(0, 0)) < 1e-4f,
                  "#4: monitor Off mutes the live-thru while recording");
            runP(lp, n, 0.0f, Cmd::RecordCycle, frOff);  // close → Playing (loop=0.5)
            CHECK(lp.state() == State::Playing, "#4: loop closed to Playing");

            // Play with monitor Off, feeding 0.3 live → loop only (0.5).
            auto offOut = runP(lp, n, 0.3f, Cmd::None, frOff);
            CHECK(std::abs(offOut.getSample(0, 0) - 0.5f) < 1e-3f,
                  "#4: monitor Off plays loop only (no live-thru)");
            // Same loop, monitor On → loop + live (0.5 + 0.3).
            auto onOut = runP(lp, n, 0.3f, Cmd::None, frOn);
            CHECK(std::abs(onOut.getSample(0, 0) - 0.8f) < 1e-3f,
                  "#4: monitor On passes live-thru on top of the loop");
        }

        // #4 Auto resolution: On for None/External (insert), Off for a Track/Master
        // tap (the tapped source is already audible). Pure predicate check.
        {
            using K = InputSourceKind;
            CHECK(LooperMachine::resolveMonitor(0, K::None),     "Auto: None monitors (insert)");
            CHECK(LooperMachine::resolveMonitor(0, K::External), "Auto: External monitors");
            CHECK(!LooperMachine::resolveMonitor(0, K::Track),   "Auto: Track tap is loop-only");
            CHECK(!LooperMachine::resolveMonitor(0, K::Master),  "Auto: Master tap is loop-only");
            CHECK(LooperMachine::resolveMonitor(1, K::Track),    "On overrides Auto (Track)");
            CHECK(!LooperMachine::resolveMonitor(2, K::None),    "Off overrides Auto (None)");
        }
    }
}
