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

        // ── Stop detaches from the timeline ──────────────────────────────────
        tape.applyVerb(2);  // PlayStop → Stopped
        auto silent = tapeBlock(tape, 0.0, n, 0.0f, kSr);
        CHECK(feq(silent.getSample(0, 100), 0.0f), "a stopped tape plays nothing");
        tape.applyVerb(2);  // resume
        auto resumed = tapeBlock(tape, 0.0, n, 0.0f, kSr);
        CHECK(feq(resumed.getSample(0, 100), 0.5f), "resuming plays the reel again");

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

        // ── Depth switch clears the reel (you swapped the tape stock) ────────
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            t.applyVerb(1);
            tapeBlock(t, 0.0, n, 0.5f, kSr);
            t.applyVerb(1);
            CHECK(t.recordedSamples() >= n, "f32 take recorded");
            t.setMediumDepth(1);  // → i16, discards audio
            CHECK(t.recordedSamples() == 0, "switching depth wipes the reel");
            CHECK(feq(tapeBlock(t, 0.0, n, 0.0f, kSr).getSample(0, 100), 0.0f),
                  "and playback is silent after the depth switch");
            // A no-op switch (same depth) does NOT clear.
            t.applyVerb(1); tapeBlock(t, 0.0, n, 0.3f, kSr); t.applyVerb(1);
            t.setMediumDepth(1);  // already i16 → no-op
            CHECK(t.recordedSamples() >= n, "re-selecting the current depth keeps the take");
        }

        // ── Scrub (§40.2): a stopped tape auditions the reel under a moving head ─
        {
            TapeMachine t; t.prepare(kSr, n); t.setMediumSeconds(2.0);
            // Lay 0.6 across a wide region [0, 2048) so the head has content to read.
            t.applyVerb(1);
            for (int blk = 0; blk < 4; ++blk) tapeBlock(t, blk * n, n, 0.6f, kSr);
            t.applyVerb(1);            // punch out → Playing
            t.applyVerb(2);            // PlayStop → Stopped (scrub-able)

            CHECK(! t.scrubActive(), "a stopped tape is not scrubbing until commanded");

            // Wind forward from inside the take. The first wind block's head is still
            // inside the content, so the wind is audible (not silent); over more
            // blocks the head keeps advancing.
            t.setScrubTargetRate(6.0);
            auto wind0 = tapeBlock(t, 1000, n, 0.0f, kSr);   // head ~1000 → still in take
            CHECK(t.scrubActive(), "a non-zero scrub rate is active");
            CHECK(std::abs(wind0.getSample(0, 400)) > 0.1f, "the wind auditions the reel (audible)");
            for (int blk = 0; blk < 8; ++blk) tapeBlock(t, 1000, n, 0.0f, kSr);
            CHECK(t.scrubHeadReelPos() > 1000.0, "winding forward advanced the head");

            // Reverse from a fresh stopped tape at 1500 → the head retreats.
            TapeMachine r; r.prepare(kSr, n); r.setMediumSeconds(2.0);
            r.applyVerb(1);
            for (int blk = 0; blk < 4; ++blk) tapeBlock(r, blk * n, n, 0.6f, kSr);
            r.applyVerb(1); r.applyVerb(2);
            r.setScrubTargetRate(-6.0);
            for (int blk = 0; blk < 8; ++blk) tapeBlock(r, 1500, n, 0.0f, kSr);
            CHECK(r.scrubHeadReelPos() < 1500.0, "winding backward retreated the head");

            // Clamp at the leader: reverse from near 0 stops at 0, never negative.
            TapeMachine c; c.prepare(kSr, n); c.setMediumSeconds(2.0);
            c.applyVerb(1); tapeBlock(c, 0.0, n, 0.6f, kSr); c.applyVerb(1); c.applyVerb(2);
            c.setScrubTargetRate(-6.0);
            for (int blk = 0; blk < 20; ++blk) tapeBlock(c, 50, n, 0.0f, kSr);
            CHECK(c.scrubHeadReelPos() >= 0.0, "winding backward clamps at the leader (>= 0)");
        }
    }
}
