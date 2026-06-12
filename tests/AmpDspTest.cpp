// AmpDspTest — post-machine envelope gate behaviour and track-filter drive.
//
// Envelope tests: regression for the inaudible-one-shot ("subby kick sounds
// high-passed") bug: with the old Envelope default, a one-shot sample's body
// was released on the note-off at the step-gate end, leaving only the
// transient. Held-open is now the default and must play the full source
// regardless of the gate.
//
// Filter drive tests: A2 regression — the old formula tanh(s*g)/g is nearly
// linear for normal signal amplitudes (gives near-unity output at drive=1,
// ~18 dB drop at high drive). The correct unity-DC-gain form is
// tanh(g*s)/tanh(g), matching the VA synth implementation.

#include "TestHarness.h"
#include "../src/machine/TrackEnvDsp.h"
#include "../src/core/TrackEnvState.h"
#include "../src/machine/TrackFltrDsp.h"
#include "../src/core/TrackFltrState.h"

namespace lockstep
{
    // Run one block of constant-1.0 audio through the envelope DSP with a note-on at
    // sample 0 and a note-off at `noteOffAt`, returning the gain at `probe`.
    static float ampGainAt(const TrackEnvState& env, int noteOffAt, int probe)
    {
        TrackEnvDsp dsp;
        dsp.prepare(48000.0);

        const int n = 64;
        juce::AudioBuffer<float> buf(1, n);
        for (int i = 0; i < n; ++i)
            buf.setSample(0, i, 1.0f);

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        if (noteOffAt >= 0)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), noteOffAt);

        dsp.processBlock(buf, midi, env, n);
        return buf.getSample(0, probe);
    }

    static void testDefaultIsHeldOpen()
    {
        TrackEnvState env;  // defaults
        CHECK(env.gateSrc >= 0.5f, "default ENV gateSrc is Held-open (one-shots play fully)");
    }

    static void testHeldOpenIgnoresNoteOff()
    {
        TrackEnvState env;  // gateSrc default = Held-open
        // Note-off early at sample 8; held-open must keep full gain afterwards.
        CHECK(feq(ampGainAt(env, 8, 40), 1.0f),
              "held-open: sample plays at full gain after the note-off (not cut)");
    }

    static void testEnvelopeReleasesOnNoteOff()
    {
        TrackEnvState env;
        env.gateSrc = 0.0f;         // Envelope mode (gate-following)
        env.attack = 0.0f;          // instant on
        env.release = 0.0f;         // instant off so the cut is unambiguous
        // Before the note-off the envelope is open; after it, the source is cut.
        CHECK(feq(ampGainAt(env, 8, 4), 1.0f),
              "envelope: full gain while the gate is open");
        CHECK(feq(ampGainAt(env, 8, 40), 0.0f),
              "envelope: source cut after the note-off (the old subby-kick bug)");
    }

    // -----------------------------------------------------------------------
    // A2: filter drive unity-DC-gain formula.
    // Build a 0.25-amplitude 1 kHz sine (444 samples = ~1/108 s at 48 kHz),
    // pass it through the track filter in LP mode at max cutoff (nearly flat),
    // and compare drive=0 vs drive=1.0 (max).
    //   Old formula: tanh(s*g)/g — output ≈ s for normal amplitudes, drops ~18 dB.
    //   New formula: tanh(g*s)/tanh(g) — unity DC gain, peak within 1 dB.
    static void testFilterDriveUnityGain()
    {
        constexpr int kN = 444;
        constexpr double kSr = 48000.0;
        constexpr float kFreq = 1000.0f;
        constexpr float kAmp = 0.25f;

        auto makeSine = [&]() {
            juce::AudioBuffer<float> buf(2, kN);
            for (int i = 0; i < kN; ++i)
            {
                const float v = kAmp * std::sin(2.0f * 3.14159265f * kFreq * static_cast<float>(i) / static_cast<float>(kSr));
                buf.setSample(0, i, v);
                buf.setSample(1, i, v);
            }
            return buf;
        };

        TrackFltrState fltr;
        fltr.mode = 0.0f;       // LP
        fltr.cutoff = 1.0f;     // max cutoff (near flat)
        fltr.resonance = 0.0f;
        fltr.envToCutoff = 0.0f;

        // drive=0: reference RMS
        fltr.drive = 0.0f;
        TrackFltrDsp dspOff;
        dspOff.prepare(kSr);
        juce::MidiBuffer noMidi;
        auto bufOff = makeSine();
        dspOff.processBlock(bufOff, noMidi, fltr, kN);

        // drive=1: test RMS
        fltr.drive = 1.0f;
        TrackFltrDsp dspOn;
        dspOn.prepare(kSr);
        auto bufOn = makeSine();
        dspOn.processBlock(bufOn, noMidi, fltr, kN);

        double sumOff = 0.0, sumOn = 0.0;
        for (int i = 0; i < kN; ++i)
        {
            const double v0 = static_cast<double>(bufOff.getSample(0, i));
            const double v1 = static_cast<double>(bufOn.getSample(0, i));
            sumOff += v0 * v0;
            sumOn  += v1 * v1;
        }
        const float rmsOff = static_cast<float>(std::sqrt(sumOff / kN));
        const float rmsOn  = static_cast<float>(std::sqrt(sumOn  / kN));

        CHECK(rmsOn > 1e-4f,
              "A2 filter drive: drive=1.0 output is silent (RMS=" + juce::String(rmsOn) + ")");

        // New formula: peak within 1 dB of reference (20*log10(rmsOn/rmsOff) > -1 dB)
        const float ratio = rmsOn / (rmsOff + 1e-9f);
        CHECK(ratio > 0.891f,
              "A2 filter drive: drive=1.0 output is > 1 dB below drive=0 "
              "(ratio=" + juce::String(ratio, 4) + ") — old tanh(s*g)/g formula still in effect");
    }

    void runAmpDspTests()
    {
        testDefaultIsHeldOpen();
        testHeldOpenIgnoresNoteOff();
        testEnvelopeReleasesOnNoteOff();
        testFilterDriveUnityGain();
    }
}
