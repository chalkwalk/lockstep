// TapeMachineTest — the Tape face of the deck engine (DESIGN §40.2/§40.3).
//
// A tape is made of position. What that means, and what this file pins down:
//   * playback reads whatever is on the reel at the transport position;
//   * recording writes the input there, replacing what was;
//   * a LOCATE winds the tape — jump the transport position and the reel follows,
//     because the reel IS the position;
//   * a reel is a linear medium with an honest end — run off it and nothing plays
//     or records.

#include "TestHarness.h"
#include "../src/machine/TapeMachine.h"


namespace lockstep
{
    namespace
    {
        // Drive one block at a given transport position with a constant input.
        // Returns the output buffer.
        juce::AudioBuffer<float> tapeBlock(TapeMachine& t, double posSamples,
                                           int n, float inValue, double sr)
        {
            TransportInfo tr;
            tr.sampleRate = sr;
            tr.running = true;
            tr.transportPhaseSamples = posSamples;
            t.setTransport(tr);

            juce::AudioBuffer<float> b(2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) b.setSample(ch, i, inValue);

            // Monitor Off (default), source External. A short param frame → defaults.
            juce::MidiBuffer none;
            ParamFrame pf{ 1.0f, 0.0f, 0.0f };
            t.process(none, pf, b);
            return b;
        }

        // Chase-locked drive (§40.2): give the machine full musical context — a
        // ppq position and a samples-per-ppq tempo — so a CALIBRATED reel addresses
        // itself by `ppq × K`. barPpq is fixed at 4; only spp (the tempo) varies.
        juce::AudioBuffer<float> tapeBlockT(TapeMachine& t, double ppq, double spp,
                                            int n, float inValue, double sr)
        {
            TransportInfo tr;
            tr.sampleRate = sr;
            tr.running = true;
            tr.barPpq = 4.0;
            tr.samplesPerBar = 4.0 * spp;
            tr.transportPpq = ppq;
            tr.transportPhaseSamples = ppq * spp;
            t.setTransport(tr);

            juce::AudioBuffer<float> b(2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) b.setSample(ch, i, inValue);

            juce::MidiBuffer none;
            ParamFrame pf{ 1.0f, 0.0f, 0.0f };
            t.process(none, pf, b);
            return b;
        }

        // Set musical context without processing a block (to read positionSamples()).
        void setTapePos(TapeMachine& t, double ppq, double spp, double sr)
        {
            TransportInfo tr;
            tr.sampleRate = sr;
            tr.barPpq = 4.0;
            tr.samplesPerBar = 4.0 * spp;
            tr.transportPpq = ppq;
            tr.transportPhaseSamples = ppq * spp;
            t.setTransport(tr);
        }
    }

    void runTapeMachineTests()
    {
        constexpr double kSr = 48000.0;
        constexpr int n = 512;

        // A short reel keeps the test cheap (the default is 5 minutes).
        TapeMachine tape;
        tape.prepare(kSr, n);
        tape.setMediumSeconds(2.0);   // 2 s reel = 96000 samples
        CHECK(tape.recordedSamples() == 0, "a fresh reel has recorded nothing");

        // Stage 1: SRC-page param polish. Reel reads as a duration in seconds; Bits
        // runs 16i (min/anticlockwise) → 32f (max/clockwise) and defaults to 32f.
        {
            const auto reel = tape.paramSpec(1);
            CHECK(reel.unit == ParamSpec::Unit::Seconds, "Reel is a seconds duration (was mislabelled ms)");
            const auto bits = tape.paramSpec(3);
            CHECK(juce::String(bits.valueLabels[0]) == "16i", "Bits[0] = 16i (anticlockwise)");
            CHECK(juce::String(bits.valueLabels[1]) == "32f", "Bits[1] = 32f (clockwise)");
            CHECK(feq(bits.defaultValue, 1.0f), "Bits defaults to 32f (value 1)");
        }

        // Stage 2: a stopped transport must not replay the frozen head (the "tiny
        // loop" buzz). Record a take, then render with the transport stopped → silent.
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            t.applyVerb(1);                          // punch in
            tapeBlock(t, 0.0, n, 0.5f, kSr);         // record 0.5 (running)
            t.applyVerb(1);                          // punch out → playing
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 100), 0.5f, 1e-3f),
                  "Stage 2: a running transport plays the take back");
            // Stop the transport at a frozen position → silence (was a repeating buzz).
            TransportInfo tr; tr.sampleRate = kSr; tr.running = false;
            tr.transportPhaseSamples = 100.0; t.setTransport(tr);
            juce::AudioBuffer<float> b(2, n);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) b.setSample(ch, i, 0.0f);
            juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f }; t.process(none, pf, b);
            CHECK(feq(b.getSample(0, 100), 0.0f),
                  "Stage 2: stopped transport → silence, no frozen-head buzz");
        }

        // Stage 1b: changing Bits CONVERTS the reel in place (it used to WIPE it).
        // Record at 32f, switch to 16i (take survives, quantised), switch back (still
        // there — the round-trip is non-destructive to the recording's existence).
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);  // default 32f
            t.applyVerb(1);
            tapeBlock(t, 0.0, n, 0.5f, kSr);
            t.applyVerb(1);
            const int rec = t.recordedSamples();
            CHECK(rec > 0, "Stage 1b: recorded a take at 32f");

            t.setMediumDepth(1);   // → 16i (convert, not wipe)
            CHECK(t.recordedSamples() == rec, "Stage 1b: 16i convert preserves the extent");
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 100), 0.5f, 1.0f / 32767.0f),
                  "Stage 1b: the take survives the 32f→16i convert (quantised)");

            t.setMediumDepth(0);   // → back to 32f (lossless from 16i)
            CHECK(t.recordedSamples() == rec, "Stage 1b: 32f convert-back preserves the extent");
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 100), 0.5f, 1.0f / 32767.0f),
                  "Stage 1b: the take survives the depth round-trip (no wipe)");
        }

        // ── Record along the timeline ────────────────────────────────────────
        // Punch in, lay 0.5 across positions [0, 512), punch out.
        tape.applyVerb(1);  // RecordCycle → Recording
        CHECK(tape.state() == dc::DeckState::Recording, "RecordCycle punches in");
        auto rec = tapeBlock(tape, 0.0, n, 0.5f, kSr);
        CHECK(feq(rec.getSample(0, 100), 0.5f), "recording monitors what it lays down");
        CHECK(tape.recordedSamples() >= n, "the reel's recorded length grew to the write");

        tape.applyVerb(1);  // RecordCycle → Playing (punch out)
        CHECK(tape.state() == dc::DeckState::Playing, "RecordCycle punches out");

        // ── Play it back from the same position ──────────────────────────────
        auto play = tapeBlock(tape, 0.0, n, 0.0f, kSr);  // silent input
        CHECK(feq(play.getSample(0, 100), 0.5f), "playback reads the reel at the position");
        CHECK(feq(play.getSample(1, 300), 0.5f), "on both channels");

        // ── Positions never recorded are silent ─────────────────────────────
        auto empty = tapeBlock(tape, 20000.0, n, 0.0f, kSr);
        CHECK(feq(empty.getSample(0, 100), 0.0f), "unrecorded tape is silent, not garbage");

        // ── Record a second region further along the reel ────────────────────
        tape.applyVerb(1);  // punch in
        tapeBlock(tape, 48000.0, n, -0.3f, kSr);  // lay -0.3 at [48000, 48512)
        tape.applyVerb(1);  // punch out

        // ── A locate winds the tape — both regions are where we left them ────
        auto atZero = tapeBlock(tape, 0.0, n, 0.0f, kSr);
        CHECK(feq(atZero.getSample(0, 100), 0.5f), "locate to 0: the first take is still there");
        auto atOne = tapeBlock(tape, 48000.0, n, 0.0f, kSr);
        CHECK(feq(atOne.getSample(0, 100), -0.3f), "locate to 1 s: the second take is there");
        // Between them: silence (nothing recorded at 24000).
        auto between = tapeBlock(tape, 24000.0, n, 0.0f, kSr);
        CHECK(feq(between.getSample(0, 100), 0.0f), "between the takes the reel is blank");

        // ── The reel has an end ──────────────────────────────────────────────
        // Position past the 2 s reel: the input still monitors (you hear yourself),
        // but nothing is written — the tape ran out. The invariant that matters is
        // that the over-run corrupts nothing: no wrap onto the reel's start.
        tape.applyVerb(1);  // punch in
        auto offEnd = tapeBlock(tape, 200000.0, n, 0.9f, kSr);  // way past 96000
        tape.applyVerb(1);
        CHECK(feq(offEnd.getSample(0, 100), 0.9f),
              "off the end the input still monitors (nothing to record onto)");
        auto stillZero = tapeBlock(tape, 0.0, n, 0.0f, kSr);
        CHECK(feq(stillZero.getSample(0, 100), 0.5f),
              "and the over-run wrote nothing — the first take is intact, not wrapped over");

        // ── Stage 4: the tape follows the main transport (no separate Stop) ─────
        // A parked (not-running) transport plays nothing; a running one chases.
        {
            TransportInfo tr; tr.sampleRate = kSr; tr.running = false; tape.setTransport(tr);
            juce::AudioBuffer<float> b(2, n);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) b.setSample(ch, i, 0.0f);
            juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f }; tape.process(none, pf, b);
            CHECK(feq(b.getSample(0, 100), 0.0f), "a parked transport plays nothing");
        }
        auto resumed = tapeBlock(tape, 0.0, n, 0.0f, kSr);  // running=true → chase
        CHECK(feq(resumed.getSample(0, 100), 0.5f), "a running transport plays the reel again");

        // ── Clear wipes the reel ─────────────────────────────────────────────
        tape.applyVerb(3);
        CHECK(tape.recordedSamples() == 0, "Clear resets the reel to blank");
        auto cleared = tapeBlock(tape, 0.0, n, 0.0f, kSr);
        CHECK(feq(cleared.getSample(0, 100), 0.0f), "and playback is silent after Clear");

        // ── Punch is non-destructive; Undo restores the original (fence #8) ──
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(4.0);

            // Lay an original take of 0.5 across [0, 512).
            t.applyVerb(1);
            tapeBlock(t, 0.0, n, 0.5f, kSr);
            t.applyVerb(1);
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 100), 0.5f), "original take laid");

            // Punch over the middle of it with -0.4 at [100, 300).
            t.applyVerb(1);  // punch in
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true;
                tr.transportPhaseSamples = 100.0; t.setTransport(tr);
                juce::AudioBuffer<float> b(2, 200);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 200; ++i) b.setSample(ch, i, -0.4f);
                juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f }; t.process(none, pf, b);
            }
            t.applyVerb(1);  // punch out
            CHECK(t.canUndo(), "a punch is undoable");

            auto punched = tapeBlock(t, 0.0, n, 0.0f, kSr);
            CHECK(feq(punched.getSample(0, 50), 0.5f), "before the punch, the original survives");
            CHECK(feq(punched.getSample(0, 150), -0.4f), "inside the punch, the new take");
            CHECK(feq(punched.getSample(0, 400), 0.5f), "after the punch, the original survives");

            // Undo restores the punched span to its pre-punch content.
            t.applyVerb(4);  // Undo
            CHECK(! t.canUndo(), "undo is one level — nothing left to undo");
            auto restored = tapeBlock(t, 0.0, n, 0.0f, kSr);
            CHECK(feq(restored.getSample(0, 150), 0.5f), "undo restored the original over the punched span");
            CHECK(feq(restored.getSample(0, 50), 0.5f) && feq(restored.getSample(0, 400), 0.5f),
                  "and left the untouched regions alone");
        }

        // ── Markers (§40.4): drop at the position, cue back to it ────────────
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(4.0);

            // Drop a marker at 1 s and another at 3 s (set the position via a block).
            tapeBlock(t, 48000.0, n, 0.0f, kSr);
            t.dropMarkerHere();
            tapeBlock(t, 144000.0, n, 0.0f, kSr);
            t.dropMarkerHere();
            CHECK(t.markerCount() == 2, "two markers dropped");

            // From 2 s: nearest is the 1 s mark (48000, distance 48000) vs 3 s
            // (144000, distance 48000) — a tie resolves to the first found (1 s).
            tapeBlock(t, 96000.0, n, 0.0f, kSr);
            CHECK(t.cuePrev() == 48000.0, "cuePrev from 2 s = the 1 s mark");
            CHECK(t.cueNext() == 144000.0, "cueNext from 2 s = the 3 s mark");

            // Clear the reel — markers survive audio operations (§40.4).
            t.applyVerb(3);
            CHECK(t.markerCount() == 2, "Clear wipes audio, not markers");
            t.clearMarkers();
            CHECK(t.markerCount() == 0, "clearMarkers is the explicit delete");
        }

        // ── Chase-lock (§40.2): position is musical, calibration latches ─────
        {
            constexpr double spp0 = 480.0;   // calibration tempo (samples per ppq)
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(4.0);
            CHECK(t.calibrationSamplesPerPpq() == 0.0, "a fresh reel is uncalibrated");

            // First record onto the empty reel latches K from the current tempo.
            t.applyVerb(1);  // punch in
            tapeBlockT(t, 0.0, spp0, n, 0.5f, kSr);
            CHECK(feq(static_cast<float>(t.calibrationSamplesPerPpq()), 480.0f),
                  "calibration latches to the first-record tempo");
            CHECK(feq(static_cast<float>(t.chaseRatio()), 1.0f),
                  "the first record is unity (K == current tempo)");
            t.applyVerb(1);  // punch out

            // Reel position is a pure function of musical time: ppq × K, the SAME
            // at any tempo. This is what makes cue/punch bar-aligned by construction.
            setTapePos(t, 1.0, spp0, kSr);
            CHECK(feq(static_cast<float>(t.positionSamples()), 480.0f),
                  "reel position = ppq × K at the calibration tempo");
            setTapePos(t, 1.0, spp0 * 0.5, kSr);  // double tempo (half spp)
            CHECK(feq(static_cast<float>(t.positionSamples()), 480.0f),
                  "reel position is tempo-invariant — a bar is a bar under any BPM");
            CHECK(feq(static_cast<float>(t.chaseRatio()), 2.0f),
                  "double tempo → chase ratio ×2 (surfaced on the strip)");

            // Play the take back at double tempo: varispeed read of the recorded
            // region returns the recorded amplitude (chipmunk pitch, same content).
            auto fast = tapeBlockT(t, 0.0, spp0 * 0.5, n, 0.0f, kSr);
            CHECK(feq(fast.getSample(0, 40), 0.5f, 0.02f),
                  "varispeed playback reads the take (bandlimited, ~unity amplitude)");
        }

        // ── Mid-record tempo change: no head jump, take stays contiguous ─────
        {
            constexpr double spp0 = 480.0;
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(4.0);

            t.applyVerb(1);  // punch in
            // Block 1 at cal tempo: reel [0, 512), calibrates K = 480.
            tapeBlockT(t, 0.0, spp0, n, 0.5f, kSr);
            // Block 2 at DOUBLE tempo (r = 2). The head is ppq × K, so it starts at
            // reel 512 — contiguous — not ppq × spp(current) = 256 (the old bug,
            // which would jump BACK and overwrite block 1).
            const double ppq2 = static_cast<double>(n) / spp0;   // ppq after block 1
            tapeBlockT(t, ppq2, spp0 * 0.5, n, -0.3f, kSr);
            t.applyVerb(1);  // punch out

            // Block 1's audio is intact at the start (not overwritten by a jump).
            auto atStart = tapeBlockT(t, 0.0, spp0, n, 0.0f, kSr);
            CHECK(feq(atStart.getSample(0, 100), 0.5f),
                  "mid-record tempo change did not jump the head back over block 1");
            // The second region exists further along (reel >= 512), non-silent.
            CHECK(t.recordedSamples() > n, "the take extended past block 1, contiguous");
        }

        // ── Varispeed record level law: |rate| write gain (§40.10) ───────────
        {
            constexpr double spp0 = 480.0;
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(4.0);

            t.applyVerb(1);  // punch in
            // Block 1 calibrates K = 480 (unity), reel [0, 512) = 0.4.
            tapeBlockT(t, 0.0, spp0, n, 0.4f, kSr);
            // Block 2 at HALF tempo (spp = 960 → r = 0.5): records into reel
            // [512, 768) at half rate. With the |rate| gain the deposited amplitude
            // stays ~0.4; drop the gain and a half-speed write lands ~0.8.
            const double ppq2 = static_cast<double>(n) / spp0;
            tapeBlockT(t, ppq2, spp0 * 2.0, n, 0.4f, kSr);
            t.applyVerb(1);  // punch out

            // Read the interior of the r = 0.5 region (reel ~640) at unity.
            auto region = tapeBlockT(t, 640.0 / spp0, spp0, n, 0.0f, kSr);
            CHECK(feq(region.getSample(0, 0), 0.4f, 0.05f),
                  "varispeed record is amplitude-invariant (|rate| gain holds level)");
        }

        // ── Punch + undo at varispeed restores the original exactly ──────────
        {
            constexpr double spp0 = 480.0;
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(4.0);

            // Original take across reel [0, 512) at cal tempo (calibrates K).
            t.applyVerb(1);
            tapeBlockT(t, 0.0, spp0, n, 0.5f, kSr);
            t.applyVerb(1);
            CHECK(feq(tapeBlockT(t, 0.0, spp0, n, 0.0f, kSr).getSample(0, 100), 0.5f),
                  "original take laid at cal tempo");

            // Punch over the middle at DOUBLE tempo (r = 2), musical ppq ~0.4..
            t.applyVerb(1);  // punch in
            tapeBlockT(t, 200.0 / spp0, spp0 * 0.5, 128, -0.4f, kSr);
            t.applyVerb(1);  // punch out
            CHECK(t.canUndo(), "a varispeed punch is undoable");

            // Undo restores the original content over the whole punched span.
            t.applyVerb(4);
            auto restored = tapeBlockT(t, 0.0, spp0, n, 0.0f, kSr);
            CHECK(feq(restored.getSample(0, 100), 0.5f),
                  "undo restored the original over the varispeed punch");
            CHECK(feq(restored.getSample(0, 400), 0.5f),
                  "and left the untouched tail alone");
        }

        // ── i16 reel (§40.10 medium_depth): record → playback round-trip ─────
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            t.setMediumDepth(1);  // I16
            CHECK(t.depthI16(), "medium_depth switched the reel to 16-bit");

            // 0.5 is exactly representable in i16 (0.5 × 32767 rounds cleanly), so
            // the round-trip is lossless to within the 1/32767 quantum.
            t.applyVerb(1);
            tapeBlock(t, 0.0, n, 0.5f, kSr);
            t.applyVerb(1);
            auto play16 = tapeBlock(t, 0.0, n, 0.0f, kSr);
            CHECK(feq(play16.getSample(0, 100), 0.5f, 1.0f / 32767.0f),
                  "i16 record → playback round-trips within the quantisation step");
            CHECK(feq(play16.getSample(1, 300), 0.5f, 1.0f / 32767.0f), "on both channels");

            // Punch + undo on the i16 reel (undo backing is i16 too).
            t.applyVerb(1);
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true;
                tr.transportPhaseSamples = 100.0; t.setTransport(tr);
                juce::AudioBuffer<float> b(2, 200);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 200; ++i) b.setSample(ch, i, -0.25f);
                juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f }; t.process(none, pf, b);
            }
            t.applyVerb(1);
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 150), -0.25f, 1.0f / 32767.0f),
                  "i16 punch lays the new take");
            t.applyVerb(4);  // undo
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 150), 0.5f, 1.0f / 32767.0f),
                  "i16 punch undo restores the original from the i16 undo backing");
        }

        // ── Depth switch CONVERTS the reel in place (Stage 1b — was a wipe) ──────
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            t.applyVerb(1);
            tapeBlock(t, 0.0, n, 0.5f, kSr);
            t.applyVerb(1);
            CHECK(t.recordedSamples() >= n, "f32 take recorded");
            t.setMediumDepth(1);  // → i16, converts (no longer discards)
            CHECK(t.recordedSamples() >= n, "switching depth converts, keeping the take");
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 100), 0.5f, 1.0f / 32767.0f),
                  "and the take plays back after the depth switch (quantised to 16-bit)");
            // A no-op switch (same depth) leaves it untouched.
            t.setMediumDepth(1);  // already i16 → no-op
            CHECK(t.recordedSamples() >= n, "re-selecting the current depth keeps the take");
        }

        // ── Scrub (§40.2, Stage 4): a PARKED transport auditions the reel under a
        // moving head. Winding is available whenever the song is stopped — no Stop
        // button — so the scrub reads render with the transport not running.
        {
            // Render one block with the transport PARKED at `pos` (so the scrub
            // branch runs). Recording still uses tapeBlock (running).
            const auto parkedBlock = [&](TapeMachine& m, double pos) {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = false;
                tr.transportPhaseSamples = pos; m.setTransport(tr);
                juce::AudioBuffer<float> b(2, n);
                juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f };
                m.process(none, pf, b);
                return b;
            };

            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            // Lay 0.6 across a wide region [0, 2048) so the head has content to read.
            t.applyVerb(1);
            for (int blk = 0; blk < 4; ++blk) tapeBlock(t, blk * n, n, 0.6f, kSr);
            t.applyVerb(1);            // punch out → Playing (chasing)

            parkedBlock(t, 1000);      // park the transport
            CHECK(! t.scrubActive(), "a parked tape is not scrubbing until commanded");

            // Wind forward from inside the take (head ~1000, still in content).
            t.setScrubTargetRate(6.0);
            auto wind0 = parkedBlock(t, 1000);
            CHECK(t.scrubActive(), "a non-zero scrub rate is active");
            CHECK(std::abs(wind0.getSample(0, 400)) > 0.1f, "the wind auditions the reel (audible)");
            for (int blk = 0; blk < 8; ++blk) parkedBlock(t, 1000);
            CHECK(t.scrubHeadReelPos() > 1000.0, "winding forward advanced the head");

            // Reverse from a fresh parked tape at 1500 → the head retreats.
            TapeMachine r; r.prepare(kSr, n); r.setMediumSeconds(2.0);
            r.applyVerb(1);
            for (int blk = 0; blk < 4; ++blk) tapeBlock(r, blk * n, n, 0.6f, kSr);
            r.applyVerb(1);
            r.setScrubTargetRate(-6.0);
            for (int blk = 0; blk < 8; ++blk) parkedBlock(r, 1500);
            CHECK(r.scrubHeadReelPos() < 1500.0, "winding backward retreated the head");

            // Clamp at the leader: reverse from near 0 stops at 0, never negative.
            TapeMachine c; c.prepare(kSr, n); c.setMediumSeconds(2.0);
            c.applyVerb(1); tapeBlock(c, 0.0, n, 0.6f, kSr); c.applyVerb(1);
            c.setScrubTargetRate(-6.0);
            for (int blk = 0; blk < 20; ++blk) parkedBlock(c, 50);
            CHECK(c.scrubHeadReelPos() >= 0.0, "winding backward clamps at the leader (>= 0)");

            // Jog (encoder rock): a one-shot nudge moves the head and then COASTS to
            // rest (decaying velocity), unlike the steady wind.
            TapeMachine j; j.prepare(kSr, n); j.setMediumSeconds(2.0);
            j.applyVerb(1);
            for (int blk = 0; blk < 4; ++blk) tapeBlock(j, blk * n, n, 0.6f, kSr);
            j.applyVerb(1);
            parkedBlock(j, 1000);          // seed the head at 1000
            const double h0 = j.scrubHeadReelPos();
            j.nudgeScrub(800.0);           // one forward jog impulse
            parkedBlock(j, 1000);
            const double h1 = j.scrubHeadReelPos();
            CHECK(h1 > h0 + 1.0, "a jog nudge moves the head forward");
            // With no further nudges the velocity decays; the head settles (inactive).
            for (int blk = 0; blk < 40; ++blk) parkedBlock(j, 1000);
            CHECK(! j.scrubActive(), "the jog coasts to rest (scrub goes inactive)");
        }

        // ── Stage 6a: the Tape is a 4-sub-track deck (widened medium + count) ────
        // The subtrack_count param exists (1..4, default 1) and drives the deck's
        // advisory count. The reel is bound at full deck width, and a single-sub-
        // track tape (the default) records/plays byte-identically to before.
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            const auto sc = t.paramSpec(4);
            CHECK(juce::String(sc.id) == "subtrack_count", "Stage 6a: slot 4 = subtrack_count");
            CHECK(feq(sc.defaultValue, 1.0f), "Stage 6a: subtrack_count defaults to 1");
            CHECK(feq(sc.maxValue, 4.0f), "Stage 6a: up to 4 sub-tracks");
            CHECK(t.subTrackCount() == 1, "Stage 6a: a fresh tape is single-sub");

            // Ask for 4 sub-tracks via the param frame → the deck reports 4.
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true;
                tr.transportPhaseSamples = 0.0; t.setTransport(tr);
                juce::AudioBuffer<float> b(2, n);
                juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f, 1.0f, 4.0f };
                t.process(none, pf, b);
            }
            CHECK(t.subTrackCount() == 4, "Stage 6a: subtrack_count raises the deck width");

            // Record on sub 0 with the deck widened → still byte-identical playback.
            t.applyVerb(1);
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true;
                tr.transportPhaseSamples = 0.0; t.setTransport(tr);
                juce::AudioBuffer<float> b(2, n);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) b.setSample(ch, i, 0.5f);
                juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f, 1.0f, 4.0f };
                t.process(none, pf, b);
            }
            t.applyVerb(1);
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true;
                tr.transportPhaseSamples = 0.0; t.setTransport(tr);
                juce::AudioBuffer<float> b(2, n);
                juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f, 1.0f, 4.0f };
                t.process(none, pf, b);
                CHECK(feq(b.getSample(0, 100), 0.5f),
                      "Stage 6a: a widened deck still records/plays sub 0 unchanged");
            }
        }

        // ── Stage 6b: per-sub input sources + IMultiInput plumbing ───────────────
        // Subs 1..3 declare their own input_source_2/3/4 slots (default None), and
        // the deck exposes IMultiInput so the processor can feed each sub its input.
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            for (int sub = 1; sub <= 3; ++sub)
            {
                const auto ss = t.paramSpec(4 + sub);   // kSlotSubSrcBase = 5 → slots 5,6,7
                CHECK(juce::String(ss.id) == juce::String("input_source_") + juce::String(sub + 1),
                      "Stage 6b: sub input source is input_source_2/3/4");
                CHECK(feq(ss.defaultValue, 0.0f), "Stage 6b: an extra sub defaults to None");
            }

            // IMultiInput: numInputSubTracks tracks the deck width; each extra sub's
            // buffer is writable and block-sized.
            auto* mi = static_cast<IMultiInput*>(&t);
            CHECK(mi->numInputSubTracks() == 1, "Stage 6b: single-sub tape = 1 input sub-track");
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true; t.setTransport(tr);
                juce::AudioBuffer<float> b(2, n);
                juce::MidiBuffer none; ParamFrame pf{ 1.0f, 0.0f, 0.0f, 1.0f, 3.0f, 0.0f, 0.0f, 0.0f };
                t.process(none, pf, b);
            }
            CHECK(mi->numInputSubTracks() == 3, "Stage 6b: numInputSubTracks follows subtrack_count");
            CHECK(mi->inputSubTrackBuffer(1).getNumSamples() >= n,
                  "Stage 6b: extra-sub input buffer is block-sized");
        }

        // ── Stage 6c: armed multi-sub record + mute/solo/pan mix ─────────────────
        {
            // Build a full 24-slot param frame: two subs, both armed (source != None),
            // both at unity level / center pan, with per-sub mix overrides.
            const int kSlots = 24;   // kNumSlots
            const auto frame = [&](int subs, float s1mute, float s1solo)
            {
                ParamFrame pf(static_cast<std::size_t>(kSlots), 0.0f);
                pf[0] = 1.0f;                       // sub0 source = External (armed)
                pf[3] = 1.0f;                       // Bits 32f
                pf[4] = static_cast<float>(subs);   // subtrack_count
                pf[5] = 1.0f;                       // sub1 source = External (armed)
                pf[8]  = 1.0f;                      // sub0 level
                pf[12] = 1.0f;                      // sub1 level
                pf[14] = s1mute;                    // sub1 mute
                pf[15] = s1solo;                    // sub1 solo
                return pf;
            };

            // Drive one running block: sub0 input from `buffer`, sub1 from its buffer.
            const auto driveRec = [&](TapeMachine& m, float in0, float in1, const ParamFrame& pf)
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true;
                tr.transportPhaseSamples = 0.0; m.setTransport(tr);
                auto& s1 = m.inputSubTrackBuffer(1);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i) s1.setSample(ch, i, in1);
                juce::AudioBuffer<float> b(2, n);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) b.setSample(ch, i, in0);
                juce::MidiBuffer none; m.process(none, pf, b);
            };
            const auto playBk = [&](TapeMachine& m, const ParamFrame& pf)
            {
                TransportInfo tr; tr.sampleRate = kSr; tr.running = true;
                tr.transportPhaseSamples = 0.0; m.setTransport(tr);
                juce::AudioBuffer<float> b(2, n);
                juce::MidiBuffer none; m.process(none, pf, b);
                return b;
            };

            // Arm both subs, record sub0=0.5 and sub1=-0.3 across [0,512).
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            const auto pf = frame(2, 0.0f, 0.0f);
            t.applyVerb(1);
            driveRec(t, 0.5f, -0.3f, pf);
            t.applyVerb(1);
            CHECK(t.subArmed(0) && t.subArmed(1), "Stage 6c: a sourced sub auto-arms");

            // Playback sums both armed subs: 0.5 + (-0.3) = 0.2.
            CHECK(feq(playBk(t, pf).getSample(0, 100), 0.2f, 1e-3f),
                  "Stage 6c: playback sums both recorded sub-tracks");
            // Mute sub1 → only sub0 (0.5).
            CHECK(feq(playBk(t, frame(2, 1.0f, 0.0f)).getSample(0, 100), 0.5f, 1e-3f),
                  "Stage 6c: muting a sub drops it from the mix");
            // Solo sub1 → only sub1 (-0.3).
            CHECK(feq(playBk(t, frame(2, 0.0f, 1.0f)).getSample(0, 100), -0.3f, 1e-3f),
                  "Stage 6c: solo is subtractive — only the soloed sub plays");

            // A disarmed sub (source None) is not written by a punch.
            TapeMachine d; d.prepare(kSr, n); d.setMediumSeconds(2.0);
            ParamFrame pfDis = frame(2, 0.0f, 0.0f);
            pfDis[5] = 0.0f;   // sub1 source = None → disarmed
            d.applyVerb(1);
            driveRec(d, 0.5f, -0.3f, pfDis);
            d.applyVerb(1);
            CHECK(d.subArmed(0) && ! d.subArmed(1), "Stage 6c: a None-source sub is disarmed");
            // Sub1 was never written → playback is just sub0 (0.5), not 0.2.
            CHECK(feq(playBk(d, pfDis).getSample(0, 100), 0.5f, 1e-3f),
                  "Stage 6c: a disarmed sub records nothing (its channel stays silent)");

            // Whole-deck undo restores every armed sub over the punched span.
            TapeMachine u; u.prepare(kSr, n); u.setMediumSeconds(2.0);
            u.applyVerb(1); driveRec(u, 0.5f, 0.3f, pf); u.applyVerb(1);    // original → 0.8
            u.applyVerb(1); driveRec(u, 0.1f, 0.1f, pf); u.applyVerb(1);    // punch over → 0.2
            CHECK(u.canUndo(), "Stage 6c: a multi-sub punch is undoable");
            CHECK(feq(playBk(u, pf).getSample(0, 100), 0.2f, 1e-3f), "Stage 6c: punched = 0.1+0.1");
            u.applyVerb(4);  // undo
            CHECK(feq(playBk(u, pf).getSample(0, 100), 0.8f, 1e-3f),
                  "Stage 6c: undo restores both armed subs (0.5 + 0.3)");
        }

        // §40.13 retroactive double-tap on the Tape: a punch-in double-tap backfills
        // the run-up before the punch from the pre-roll ring. Uncalibrated reel =
        // rate 1 (the v1 restriction). One play block fills the ring with 0.6; the
        // punch records 0.9 at reel 300; retroExtend(100) backfills [200,300) = 0.6.
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);

            tapeBlock(t, 0.0, n, 0.6f, kSr);      // playing: fills the pre-roll with 0.6
            t.applyVerb(1);                        // Playing → Recording (punch in)
            tapeBlock(t, 300.0, n, 0.9f, kSr);     // records [300, 812) = 0.9; undoLo_ = 300
            CHECK(t.recording(), "tape retro: punched in (Recording)");
            t.retroExtend(100);                    // backfill [200, 300) from the ring
            t.applyVerb(1);                        // punch out → committed
            CHECK(t.canUndo(), "tape retro: the retro-extended punch is undoable");

            juce::AudioBuffer<float> reel(2, 900);
            t.copyReelTo(reel, 900);
            CHECK(feq(reel.getSample(0, 400), 0.9f, 1e-3f),
                  "tape retro: the live punch is on the reel (0.9)");
            CHECK(feq(reel.getSample(0, 250), 0.6f, 1e-3f),
                  "tape retro: the run-up before the punch is backfilled from the pre-roll (0.6)");
            CHECK(feq(reel.getSample(0, 100), 0.0f, 1e-3f),
                  "tape retro: the reel before the backfill window is untouched (0.0)");

            t.applyVerb(4);                        // whole-punch undo (incl the retro span)
            juce::AudioBuffer<float> reel2(2, 900);
            t.copyReelTo(reel2, 900);
            CHECK(feq(reel2.getSample(0, 250), 0.0f, 1e-3f) && feq(reel2.getSample(0, 400), 0.0f, 1e-3f),
                  "tape retro: undo restores the backfilled run-up and the punch to silence");
        }
    }
}
