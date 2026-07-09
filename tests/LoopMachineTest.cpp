// LoopMachineTest -- overdub looper state machine (6.3, DESIGN §29.2).
//
// Drives the machine through Idle → Record → Play → Overdub → Undo → Clear via the
// command mailbox (one command drained per block) and checks the loop audio.

#include "TestHarness.h"
#include "../src/machine/LoopMachine.h"
#include "../src/machine/SampleMachine.h"
#include "../src/machine/SamplePool.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        using Cmd = LoopMachine::Cmd;
        using State = LoopMachine::State;

        // Run one block with a constant input value; returns the output buffer.
        juce::AudioBuffer<float> runBlock(LoopMachine& m, int n, float inValue,
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

    void runLoopMachineTests()
    {
        constexpr double kSr = 48000.0;
        constexpr int n = 512;

        // The loop lives in a volatile pool slot (B3); prepare one with capacity.
        SamplePool pool;
        pool.addVolatile();
        pool.prepareVolatile(kSr, 2, static_cast<int>(kSr));  // 1 s capacity

        LoopMachine loop(pool);
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

        // B3: the loop lives in the pool slot — a Sample plays it, and an overdub
        // sums in place (still one volatile sample). sourceBars is stamped on close.
        {
            SamplePool p2;
            const int idx = p2.addVolatile();
            p2.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p2);
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

            // A Sample pointed at the same slot plays the loop back (non-silent).
            SampleMachine samp(p2);
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
                  "B3: a Sample plays the looper's pool slot (non-silent)");

            // Overdub onto the playing loop sums in place; still one volatile sample.
            const int before = p2.size();
            runBlock(lp, n, 0.25f, Cmd::RecordCycle);   // Playing → Overdubbing
            CHECK(p2.size() == before, "B3: overdub does not add a pool entry");
        }

        // Helper: run a block with explicit params (loop_sync etc.).
        auto runP = [](LoopMachine& m, int len, float inVal, Cmd cmd,
                       const ParamFrame& pr, int impulseAt = -1, bool immediate = false) {
            if (cmd != Cmd::None) m.postCommand(cmd, immediate);
            juce::AudioBuffer<float> b(2, len);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < len; ++i)
                    b.setSample(ch, i, (i == impulseAt) ? 1.0f : inVal);
            juce::MidiBuffer midi;
            ParamFrame params = pr;
            m.process(midi, params, b);
            return b;
        };

        // S1: Sync record auto-closes at the track-grid length (length × step PPQ),
        // pushed via ILoopGridAware. A 16-step 1/16 track (16 × 0.25 = 4 quarters =
        // 1 bar) with a 1024-sample bar → loop length 1024.
        {
            SamplePool p3;
            const int idx = p3.addVolatile();
            p3.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p3);
            lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            lp.setTransport(tr);
            lp.setLoopGrid(16, 0.25);  // 16 steps × 1/16 = 1 bar

            ParamFrame fr{ 1.0f, 0.0f, 2.0f };  // input=Ext, target=0, loop_sync=Sync
            runP(lp, 512, 0.5f, Cmd::RecordCycle, fr);  // start (512 recorded)
            CHECK(lp.state() == State::Recording, "Sync: still recording at 512 < 1024");
            runP(lp, 512, 0.5f, Cmd::None, fr);         // reaches 1024 → auto-close
            CHECK(lp.state() == State::Playing, "Sync: auto-closes at the grid length");
            CHECK(p3.get(idx) != nullptr && p3.get(idx)->pcm.getNumSamples() == 1024,
                  "Sync: auto-closed loop length == track grid (1024)");
        }

        // S1: the loop length tracks the *track grid*, not a machine param — a
        // shorter/finer track grid yields a shorter loop. 8 steps × 1/16 = 2 quarters
        // = half a bar → 512 samples with the same 1024-sample bar.
        {
            SamplePool p;
            const int idx = p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            lp.setTransport(tr);
            lp.setLoopGrid(8, 0.25);  // 8 steps × 1/16 = half a bar
            ParamFrame fr{ 1.0f, 0.0f, 2.0f };  // Sync
            runP(lp, 512, 0.5f, Cmd::RecordCycle, fr);  // phase 0: starts at i=0, fills 512
            CHECK(lp.state() == State::Playing, "Sync: auto-closes at the grid length");
            CHECK(p.get(idx) != nullptr && p.get(idx)->pcm.getNumSamples() == 512,
                  "Sync: loop length follows the track grid (8 steps = 512)");
        }

        // DESIGN (layered stop): a Sync (grid-locked) loop is subordinate to the
        // main transport — its playback goes silent when the transport stops (holding
        // position for an in-phase resume), and a per-track loop_freewheel opts out.
        // (A Free loop plays transport-stopped — the S2 stopped-take block covers it.)
        {
            SamplePool p; p.addVolatile(); p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            lp.setLoopGrid(16, 0.25);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            lp.setTransport(tr);
            ParamFrame sync{ 1.0f, 0.0f, 2.0f };  // input=Ext, target=0, loop_sync=Sync
            runP(lp, 512, 0.5f, Cmd::RecordCycle, sync);
            runP(lp, 512, 0.5f, Cmd::None, sync);  // → Playing, loop of 0.5

            auto playing = runP(lp, 256, 0.0f, Cmd::None, sync);
            CHECK(std::abs(playing.getSample(0, 10)) > 0.1f,
                  "subordinate: Sync loop plays with the transport running");

            tr.running = false; lp.setTransport(tr);
            auto stopped = runP(lp, 256, 0.0f, Cmd::None, sync);
            CHECK(std::abs(stopped.getSample(0, 10)) < 1.0e-4f,
                  "subordinate: Sync loop is silent when the transport stops");

            // Freewheel opt-out (loop_freewheel = 1): plays regardless of transport.
            ParamFrame fw{ 1.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f };
            auto freed = runP(lp, 256, 0.0f, Cmd::None, fw);
            CHECK(std::abs(freed.getSample(0, 10)) > 0.1f,
                  "freewheel: loop plays despite the stopped transport");
        }

        // S2: loop-position chrome for the mini-seq. A playing loop publishes a phase
        // 0..1 (continuous playhead); idle/stopped publishes -1.
        {
            SamplePool p; const int idx = p.addVolatile(); (void) idx;
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = false;
            lp.setTransport(tr);
            lp.setLoopGrid(16, 0.25);
            ParamFrame fr{ 1.0f, 0.0f, 0.0f };  // Free (no quantize) for a stopped-transport take
            CHECK(lp.phase01() < 0.0f, "S2: idle looper publishes phase -1");
            runP(lp, 256, 0.5f, Cmd::RecordCycle, fr);  // record
            runP(lp, 256, 0.5f, Cmd::RecordCycle, fr);  // close → Playing
            runP(lp, 128, 0.0f, Cmd::None, fr);         // advance playback
            const float ph = lp.phase01();
            CHECK(ph >= 0.0f && ph <= 1.0f, "S2: playing looper publishes phase in [0,1]");
        }

        // S2: pendingEdge is true while a quantized record is Armed (drives the
        // mini-seq landing pip), and false once it has fired.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            // 9.17: the processor delivers the launch-quantize edge period (one bar here).
            tr.launchQuantPeriodSamples = 1024.0;
            tr.transportPhaseSamples = 200.0; lp.setTransport(tr);
            lp.setLoopGrid(16, 0.25);
            ParamFrame fr{ 1.0f, 0.0f, 2.0f };  // Sync (quantized)
            CHECK(!lp.pendingEdge(), "S2: no pending edge before arming");
            lp.postCommand(Cmd::RecordCycle);
            runP(lp, 256, 0.5f, Cmd::None, fr);         // arms, no boundary
            CHECK(lp.state() == State::Armed && lp.pendingEdge(),
                  "S2: armed record reports a pending edge");
            tr.transportPhaseSamples = 900.0; lp.setTransport(tr);
            runP(lp, 256, 0.5f, Cmd::None, fr);         // crosses 1024 → fires
            CHECK(!lp.pendingEdge(), "S2: pending edge clears once the take starts");
        }

        // #2: a quantized Record press mid-bar ARMS — it does not start recording
        // until the transport phase crosses the next bar-grid boundary.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            tr.launchQuantPeriodSamples = 1024.0;   // 9.17: one-bar launch grid
            tr.transportPhaseSamples = 200.0;  // mid-bar, away from a boundary
            lp.setTransport(tr);
            lp.setLoopGrid(16, 0.25);          // 1-bar grid
            ParamFrame fr{ 1.0f, 0.0f, 2.0f };  // Sync (quantized)

            lp.postCommand(Cmd::RecordCycle);          // single tap → arm
            runP(lp, 256, 0.5f, Cmd::None, fr);        // phase 200..455: no boundary
            CHECK(lp.state() == State::Armed, "#2: quantized Record arms (waits for the bar)");

            tr.transportPhaseSamples = 900.0;          // next block straddles 1024
            lp.setTransport(tr);
            runP(lp, 256, 0.5f, Cmd::None, fr);        // phase 900..1155 crosses 1024
            CHECK(lp.state() == State::Recording, "#2: armed take starts at the bar boundary");
        }

        // #2: a double-tap (immediate) Record bypasses quantize and starts now, even
        // mid-bar in a synced mode.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            tr.transportPhaseSamples = 200.0; lp.setTransport(tr);
            lp.setLoopGrid(16, 0.25);
            ParamFrame fr{ 1.0f, 0.0f, 2.0f };  // Sync

            lp.postCommand(Cmd::RecordCycle, /*immediate*/ true);
            runP(lp, 256, 0.5f, Cmd::None, fr);
            CHECK(lp.state() == State::Recording, "#2: double-tap Record starts immediately");
        }

        // C4: Free-Len varispeed — halving the project tempo halves the loop's
        // playback rate, so an impulse loop wraps ~half as often.
        auto countImpulseWraps = [&runP](double projectSpb) {
            SamplePool p;
            p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p);
            lp.prepare(kSr, 512);
            TransportInfo tr; tr.samplesPerBar = 1000.0; tr.running = false;  // free-run
            lp.setTransport(tr);

            ParamFrame fr{ 1.0f, 0.0f, 1.0f };  // Free Len
            // Record a 1024-sample loop with a single impulse at sample 0. Free Len
            // would normally arm to the bar grid; record immediately here (transport
            // is stopped for the free-run varispeed measurement that follows).
            runP(lp, 512, 0.0f, Cmd::RecordCycle, fr, /*impulseAt*/ 0, /*immediate*/ true);
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
            LoopMachine lp(p);
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
            LoopMachine lp(p);
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
            CHECK(LoopMachine::resolveMonitor(0, K::None),     "Auto: None monitors (insert)");
            CHECK(LoopMachine::resolveMonitor(0, K::External), "Auto: External monitors");
            CHECK(!LoopMachine::resolveMonitor(0, K::Track),   "Auto: Track tap is loop-only");
            CHECK(!LoopMachine::resolveMonitor(0, K::Master),  "Auto: Master tap is loop-only");
            CHECK(LoopMachine::resolveMonitor(1, K::Track),    "On overrides Auto (Track)");
            CHECK(!LoopMachine::resolveMonitor(2, K::None),    "Off overrides Auto (None)");
        }

        // #4 Overdub decay: overdubbing a full loop with decay=1 (full fade) and
        // silent input erases the loop (kept = old*0, written = 0 + 0).
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, 512);

            // input=Ext, target=0, sync=Free, monitor=Off, decay, decay_mode.
            ParamFrame rec{ 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };  // decay 0 while recording
            runP(lp, 512, 0.5f, Cmd::RecordCycle, rec);            // record 512 of 0.5
            runP(lp, 512, 0.0f, Cmd::RecordCycle, rec);           // close → Playing (loop 0.5)
            CHECK(lp.state() == State::Playing, "#4: decay test loop closed");

            ParamFrame od{ 1.0f, 0.0f, 0.0f, 2.0f, 1.0f, 0.0f };  // decay=1 full, Overdub mode
            runP(lp, 512, 0.0f, Cmd::RecordCycle, od);            // Playing→Overdubbing: erases
            auto out = runP(lp, 512, 0.0f, Cmd::RecordCycle, od); // Overdubbing→Playing
            CHECK(out.getMagnitude(0, 512) < 1e-3f,
                  "#4: Overdub decay=1 erases the loop (got "
                      + juce::String(out.getMagnitude(0, 512)) + ")");
        }

        // #4 Always decay: the whole loop fades each iteration even without overdub.
        // A 512-sample loop in a 512-sample block wraps once per block, so decay=0.5
        // halves the stored loop every block.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, 512);

            ParamFrame rec{ 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f };  // Always mode, decay 0 to record
            runP(lp, 512, 0.5f, Cmd::RecordCycle, rec);
            runP(lp, 512, 0.0f, Cmd::RecordCycle, rec);            // close → Playing (loop 0.5)

            ParamFrame al{ 1.0f, 0.0f, 0.0f, 2.0f, 0.5f, 1.0f };   // decay 0.5, Always
            const float m1 = runP(lp, 512, 0.0f, Cmd::None, al).getMagnitude(0, 512);
            runP(lp, 512, 0.0f, Cmd::None, al);
            const float m3 = runP(lp, 512, 0.0f, Cmd::None, al).getMagnitude(0, 512);
            CHECK(m1 > 0.3f && m3 < m1 * 0.5f,
                  "#4: Always decay fades the loop each iteration (m1=" + juce::String(m1)
                      + " m3=" + juce::String(m3) + ")");
        }

        // R4: the overdub is a bandlimited layer folded into the loop per iteration,
        // add-only — and bit-exact at unity rate. Overdub 0.25 onto a 0.5 loop with
        // decay off (hold) → the committed loop is exactly 0.75, not smeared or
        // attenuated by the fractional-write path (Free mode = rate 1, integer pos).
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, 512);

            ParamFrame rec{ 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };  // Free, Off, decay 0
            runP(lp, 512, 0.5f, Cmd::RecordCycle, rec);            // record 512 of 0.5
            runP(lp, 512, 0.0f, Cmd::RecordCycle, rec);            // close → Playing (loop 0.5)
            CHECK(lp.state() == State::Playing, "R4: loop closed to Playing");

            ParamFrame od{ 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };   // decay 0 (hold), Overdub
            runP(lp, 512, 0.25f, Cmd::RecordCycle, od);            // Playing→Overdubbing (lay B)
            CHECK(lp.state() == State::Overdubbing, "R4: overdubbing");
            auto out = runP(lp, 512, 0.0f, Cmd::RecordCycle, od);  // Overdubbing→Playing (fold)
            CHECK(lp.state() == State::Playing, "R4: overdub committed, back to Playing");
            // Early-loop mean, before the equal-power wrap crossfade (last loopLen/4
            // = 128 samples, i.e. 384..512, which peaks a constant loop above its
            // value): must be exactly 0.75.
            double sum = 0.0;
            for (int i = 50; i < 350; ++i) sum += out.getSample(0, i);
            const double mean = sum / 300.0;
            CHECK(mean > 0.74 && mean < 0.76,
                  "R4: unity overdub adds exactly (0.5 + 0.25 = 0.75, got "
                      + juce::String(mean) + ")");
        }

        // S4: Halve/Double resize the loop *window* with no resample (no pitch change).
        // A manual length edit detaches from grid-lock (manualLen_) so it plays native
        // at rate 1 — the constant loop value is reproduced exactly, not interpolated.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, 512);

            // Record a two-block loop: first 512 = 0.5, second 512 = 0.25 → loopLen 1024.
            runBlock(lp, 512, 0.5f, Cmd::RecordCycle);
            runBlock(lp, 512, 0.25f);
            runBlock(lp, 512, 0.0f, Cmd::RecordCycle);   // close → Playing
            CHECK(lp.loopLengthSamples() == 1024, "S4: recorded loop is 1024 samples");

            // Halve: window → 512; playback now only sees the first half (all 0.5).
            auto h = runBlock(lp, 512, 0.0f, Cmd::Halve);
            CHECK(lp.loopLengthSamples() == 512, "S4: Halve halves the loop window");
            CHECK(feq(h.getSample(0, 0), 0.5f) && feq(h.getSample(0, 256), 0.5f),
                  "S4: halved loop plays the first half unresampled (0.5, no pitch change)");

            // Double: window → 1024; the second half is a copy of the first (both 0.5).
            runBlock(lp, 512, 0.0f, Cmd::Double);
            CHECK(lp.loopLengthSamples() == 1024, "S4: Double doubles the loop window");
            auto d = runBlock(lp, 512, 0.0f);
            CHECK(feq(d.getSample(0, 0), 0.5f) && feq(d.getSample(0, 256), 0.5f),
                  "S4: doubled loop duplicates content into the second half (0.5, no pitch)");
        }

        // S5: beat-repeat captures the grid cell under the playhead and loops it while
        // held (no jump at press), then resyncs on release. Loop = 512 samples of two
        // 256 halves (0.8 | 0.2). Close on a 1-sample block so the playhead sits at ~0;
        // with samplesPerBar=512 a 1/2-bar cell = 256, so the captured first cell is
        // all 0.8 — while held the 0.2 half is never heard.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, 512);
            TransportInfo tr; tr.samplesPerBar = 512.0; tr.running = true;
            lp.setTransport(tr);

            runBlock(lp, 256, 0.8f, Cmd::RecordCycle);  // [0,256) = 0.8
            runBlock(lp, 256, 0.2f);                     // [256,512) = 0.2
            runBlock(lp, 1, 0.0f, Cmd::RecordCycle);     // close → Playing, playhead ~0

            // Engage 1/2 beat-repeat (rate idx 3): cell = 512 * 1/2 = 256 = first half.
            lp.postPerf(Cmd::BeatRepeat, /*pressed*/ true, 3);
            auto b0 = runBlock(lp, 512, 0.0f);   // runs to the cell boundary, jumps back
            auto b1 = runBlock(lp, 512, 0.0f);   // now looping [0,256) = 0.8
            CHECK(lp.beatRepeatRate() == 3, "S5: beat-repeat rate mirror reflects the held cell");
            CHECK(feq(b0.getMagnitude(0, 512), 0.8f)
                      && b1.findMinMax(0, 0, 512).getStart() > 0.7f,
                  "S5: held beat-repeat loops the captured cell (0.8), never the 0.2 half");

            // Release resyncs to the free-running position; the loop resumes full
            // traversal, so the quiet 0.2 half is reached again within the next block.
            lp.postPerf(Cmd::BeatRepeat, /*pressed*/ false);
            auto r = runBlock(lp, 512, 0.0f);
            CHECK(lp.beatRepeatRate() == -1, "S5: release clears the beat-repeat rate mirror");
            CHECK(r.findMinMax(0, 0, 512).getStart() < 0.3f,
                  "S5: after release the loop resyncs and plays the second half (0.2)");
        }

        // S6: tape FX — momentary playback-rate envelopes. Chrome cell tracks the held
        // effect; half-speed advances the loop slower than native; tape-stop braked to
        // a standstill lands in a graceful Stopped.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, 512);
            runBlock(lp, 512, 0.5f, Cmd::RecordCycle);  // record a uniform 1024 loop
            runBlock(lp, 512, 0.5f);
            runBlock(lp, 1, 0.0f, Cmd::RecordCycle);     // close → Playing

            // Reverse lights its cell (idx 3) while held; release clears it.
            lp.postPerf(Cmd::Reverse, /*pressed*/ true);
            runBlock(lp, 256, 0.0f);
            CHECK(lp.tapeFx() == 3, "S6: held Reverse lights its tape cell");
            lp.postPerf(Cmd::Reverse, /*pressed*/ false);
            runBlock(lp, 256, 0.0f);   // resync
            runBlock(lp, 256, 0.0f);
            CHECK(lp.tapeFx() == -1, "S6: releasing a tape fx clears the tape cell");

            // Half-speed advances the loop ~half as fast as native. Warm up until the
            // rate multiplier has settled to 0.5, then compare one block's phase delta.
            // The tape-FX glide is a ~100 ms one-pole (W4), so settle several tau
            // (~40 × 512 ≈ 0.43 s) before sampling.
            lp.postPerf(Cmd::HalfSpeed, /*pressed*/ true);
            for (int k = 0; k < 40; ++k) runBlock(lp, 512, 0.0f);  // multiplier settled to ~0.5
            const float pA = lp.phase01();
            runBlock(lp, 256, 0.0f);
            const float pB = lp.phase01();
            const float delta = pB - pA;        // native would be 256/1024 = 0.25
            CHECK(delta > 0.08f && delta < 0.18f,
                  "S6: half-speed advances the loop at ~half rate (delta="
                      + juce::String(delta) + ")");
            lp.postPerf(Cmd::HalfSpeed, /*pressed*/ false);

            // Tape-stop: a fresh loop, held to a standstill → graceful Stopped.
            SamplePool p2; p2.addVolatile();
            p2.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine ls(p2); ls.prepare(kSr, 512);
            runBlock(ls, 512, 0.5f, Cmd::RecordCycle);
            runBlock(ls, 512, 0.5f);
            runBlock(ls, 1, 0.0f, Cmd::RecordCycle);     // close → Playing
            ls.postPerf(Cmd::TapeStop, /*pressed*/ true);
            for (int k = 0; k < 80; ++k) runBlock(ls, 512, 0.0f);  // ~0.85 s of braking
            CHECK(ls.state() == State::Stopped,
                  "S6: tape-stop braked to a standstill lands in Stopped");
            CHECK(ls.tapeFx() == -1, "S6: tape-stop clears its cell once stopped");
        }

        // S7: state-aware Auto monitor. An insert looper monitors live input while
        // Idle/Recording, drops to loop-only once the take is Playing (the capture
        // replaced the live source), and restores monitoring when Stopped.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, 512);
            ParamFrame fr{ 1.0f, 0.0f, 0.0f };  // External insert, Free, Auto monitor

            auto idle = runP(lp, 512, 0.4f, Cmd::None, fr);
            CHECK(feq(idle.getSample(0, 0), 0.4f), "S7: Auto insert monitors while Idle");

            runP(lp, 512, 0.5f, Cmd::RecordCycle, fr);          // Idle → Recording (Free)
            auto rec = runP(lp, 512, 0.5f, Cmd::None, fr);
            CHECK(feq(rec.getSample(0, 0), 0.5f), "S7: Auto monitors while Recording");

            runP(lp, 512, 0.0f, Cmd::RecordCycle, fr);          // close → Playing (loop 0.5)
            auto play = runP(lp, 512, 0.3f, Cmd::None, fr);
            CHECK(feq(play.getSample(0, 0), 0.5f),
                  "S7: Auto drops live-thru on record->play (loop only, 0.5 not 0.8)");

            runP(lp, 512, 0.0f, Cmd::PlayStop, fr);             // Playing → Stopped
            auto stop = runP(lp, 512, 0.35f, Cmd::None, fr);
            CHECK(feq(stop.getSample(0, 0), 0.35f),
                  "S7: Auto restores monitoring when Stopped");
        }

        // S7: quantized punch-out. Stopping a quantized recording arms the close for
        // the next bar boundary (stays Recording, pending edge shown) and lands on the
        // bar → Playing, rather than cutting instantly.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            tr.launchQuantPeriodSamples = 1024.0;   // 9.17: edges arm to the one-bar launch grid
            tr.transportPhaseSamples = 200.0; lp.setTransport(tr);
            ParamFrame fr{ 1.0f, 0.0f, 1.0f };  // Free Len (quantize starts/stops to the bar)

            lp.postCommand(Cmd::RecordCycle);
            runP(lp, 256, 0.5f, Cmd::None, fr);        // 200..456: arms record
            CHECK(lp.state() == State::Armed, "S7: Free-Len record arms to the bar");
            tr.transportPhaseSamples = 900.0; lp.setTransport(tr);
            runP(lp, 256, 0.5f, Cmd::None, fr);        // 900..1156 crosses 1024 → Recording
            CHECK(lp.state() == State::Recording, "S7: record starts on the bar");

            tr.transportPhaseSamples = 1200.0; lp.setTransport(tr);
            lp.postCommand(Cmd::RecordCycle);          // punch-out
            runP(lp, 256, 0.5f, Cmd::None, fr);        // 1200..1456: no boundary yet
            CHECK(lp.state() == State::Recording && lp.pendingEdge(),
                  "S7: quantized punch-out stays Recording until the bar (pending edge)");
            tr.transportPhaseSamples = 1900.0; lp.setTransport(tr);
            runP(lp, 256, 0.5f, Cmd::None, fr);        // 1900..2156 crosses 2048 → close
            CHECK(lp.state() == State::Playing,
                  "S7: punch-out lands on the bar boundary → Playing");
        }

        // 9.17: the REC edge arms to the delivered launch-quantize period,
        // independent of loop_sync (length only). A 2-bar period (2048) arms
        // mid-bar and fires when the transport phase crosses 2048.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            tr.launchQuantPeriodSamples = 2048.0;      // 2-bar launch grid
            tr.transportPhaseSamples = 200.0; lp.setTransport(tr);
            ParamFrame fr{ 1.0f, 0.0f, 2.0f };  // Sync (length only)
            lp.postCommand(Cmd::RecordCycle);
            runP(lp, 256, 0.5f, Cmd::None, fr);        // 200..456: arms, no 2048 boundary
            CHECK(lp.state() == State::Armed, "9.17: REC arms to the delivered 2-bar period");
            tr.transportPhaseSamples = 1900.0; lp.setTransport(tr);
            runP(lp, 256, 0.5f, Cmd::None, fr);        // 1900..2156 crosses 2048 → fires
            CHECK(lp.state() == State::Recording, "9.17: REC fires at the 2-bar period");
        }

        // 9.17: period 0 (Instant grid) → the edge fires immediately, no arm.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            tr.launchQuantPeriodSamples = 0.0;         // Instant grid
            tr.transportPhaseSamples = 200.0; lp.setTransport(tr);
            ParamFrame fr{ 1.0f, 0.0f, 2.0f };
            runP(lp, 256, 0.5f, Cmd::RecordCycle, fr);
            CHECK(lp.state() == State::Recording, "9.17: period 0 fires the REC edge instantly");
        }

        // 9.17: loop_sync still governs LENGTH — a Sync loop auto-closes at the
        // track-grid length (16 × 0.25 × 256 spq = 1024) regardless of the edge
        // period delivered by the authority.
        {
            SamplePool p; p.addVolatile();
            p.prepareVolatile(kSr, 2, static_cast<int>(kSr));
            LoopMachine lp(p); lp.prepare(kSr, n);
            TransportInfo tr; tr.samplesPerBar = 1024.0; tr.barPpq = 4.0; tr.running = true;
            tr.launchQuantPeriodSamples = 4096.0;      // a wildly different edge period
            lp.setTransport(tr);
            lp.setLoopGrid(16, 0.25);
            ParamFrame fr{ 1.0f, 0.0f, 2.0f };
            runP(lp, n, 0.5f, Cmd::RecordCycle, fr, -1, /*immediate*/ true);  // 512 recorded
            runP(lp, n, 0.5f, Cmd::None, fr);                                 // reaches 1024 → close
            CHECK(lp.state() == State::Playing,
                  "9.17: Sync length stays steps×stepPpq (1024), edge period independent");
        }

        // C5: a fresh LoopMachine defaults loop_sync to Sync (2), not Free (0). Free
        // ignores tempo and is the hardest mode to reason about; grid-locked Sync is
        // the sane default for a new track.
        {
            SamplePool p;
            LoopMachine lp(p);
            const auto sync = lp.paramSpec(2);  // slot 2 = loop_sync (Free|FreeLen|Sync)
            CHECK(sync.id == juce::String("loop_sync"), "C5: slot 2 is loop_sync");
            CHECK(sync.defaultValue == 2.0f, "C5: loop_sync defaults to Sync (2)");
        }
    }
}
