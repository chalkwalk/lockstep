// AmpDspTest — the post-machine AMP gate behaviour. Regression for the
// inaudible-one-shot ("subby kick sounds high-passed") bug: with the old
// Envelope default, a one-shot sample's body was released on the note-off at
// the step-gate end, leaving only the transient. Held-open is now the default
// and must play the full source regardless of the gate.

#include "TestHarness.h"
#include "../src/machine/TrackAmpDsp.h"
#include "../src/core/TrackAmpState.h"

namespace lockstep
{
    // Run one block of constant-1.0 audio through the AMP with a note-on at
    // sample 0 and a note-off at `noteOffAt`, returning the gain at `probe`.
    static float ampGainAt(const TrackAmpState& amp, int noteOffAt, int probe)
    {
        TrackAmpDsp dsp;
        dsp.prepare(48000.0);

        const int n = 64;
        juce::AudioBuffer<float> buf(1, n);
        for (int i = 0; i < n; ++i)
            buf.setSample(0, i, 1.0f);

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        if (noteOffAt >= 0)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), noteOffAt);

        dsp.processBlock(buf, midi, amp, n);
        return buf.getSample(0, probe);
    }

    static void testDefaultIsHeldOpen()
    {
        TrackAmpState amp;  // defaults
        CHECK(amp.gateSrc >= 0.5f, "default AMP gateSrc is Held-open (one-shots play fully)");
    }

    static void testHeldOpenIgnoresNoteOff()
    {
        TrackAmpState amp;          // gateSrc default = Held-open
        amp.level = 1.0f;           // unity so the gain == the envelope
        // Note-off early at sample 8; held-open must keep full gain afterwards.
        CHECK(feq(ampGainAt(amp, 8, 40), 1.0f),
              "held-open: sample plays at full gain after the note-off (not cut)");
    }

    static void testEnvelopeReleasesOnNoteOff()
    {
        TrackAmpState amp;
        amp.gateSrc = 0.0f;         // Envelope mode (gate-following)
        amp.level   = 1.0f;
        amp.attack  = 0.0f;         // instant on
        amp.release = 0.0f;         // instant off so the cut is unambiguous
        // Before the note-off the envelope is open; after it, the source is cut.
        CHECK(feq(ampGainAt(amp, 8, 4), 1.0f),
              "envelope: full gain while the gate is open");
        CHECK(feq(ampGainAt(amp, 8, 40), 0.0f),
              "envelope: source cut after the note-off (the old subby-kick bug)");
    }

    void runAmpDspTests()
    {
        testDefaultIsHeldOpen();
        testHeldOpenIgnoresNoteOff();
        testEnvelopeReleasesOnNoteOff();
    }
}
