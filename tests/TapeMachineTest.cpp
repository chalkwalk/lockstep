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
    }
}
