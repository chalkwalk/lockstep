// DeckAdapterTest — the deck_juce seam (DESIGN §40.11).
//
// One header knows both vocabularies. Below it the engine is portable; above it
// it is Lockstep. What has to be true of that seam:
//
//   * a juce::AudioBuffer becomes a medium by LENDING its channels — no copy, no
//     allocation, and writes through the medium land in the buffer;
//   * the transport crosses as a POD snapshot, position included;
//   * a mono buffer binds one channel rather than reading a channel that is not
//     there.

#include "TestHarness.h"
#include "../src/deckcore/Heads.h"
#include "../src/machine/DeckAdapter.h"

namespace lockstep
{
    void runDeckAdapterTests()
    {
        // ── The buffer is lent, not copied ───────────────────────────────────
        {
            juce::AudioBuffer<float> buf{ 2, 64 };
            buf.clear();

            dc::Medium m;
            bindBuffer(m, buf, 64, dc::Topology::Circular, 48000.0);
            CHECK(m.bound() && m.capacity() == 64 && m.channels() == 2,
                  "a stereo buffer binds as a 2-channel medium");

            m.adoptUsed(0, 64);  // the caller vouches for the buffer's content
            m.write(0, 1, 10, 0.5f);
            CHECK(feq(buf.getSample(1, 10), 0.5f),
                  "a write through the medium lands in the caller's buffer");

            buf.setSample(0, 20, 0.25f);
            CHECK(feq(m.read(0, 0, 20), 0.25f), "and a write to the buffer is visible to the medium");

            // Circular topology over the buffer: the seam wraps.
            CHECK(feq(m.read(0, 0, 84), 0.25f), "the loop wraps at capacity");
        }

        // ── Binding does not assume the content is recorded ──────────────────
        // The high-water rule is the caller's to state: a freshly bound buffer
        // reads silent until somebody says what is in it.
        {
            juce::AudioBuffer<float> buf{ 2, 16 };
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 16; ++i) buf.setSample(ch, i, 1.0f);

            dc::Medium m;
            bindBuffer(m, buf, 16, dc::Topology::Circular, 48000.0);
            CHECK(feq(m.read(0, 0, 3), 0.0f),
                  "a bound medium reads silence until the caller commits its content");
            m.adoptUsed(0, 16);
            CHECK(feq(m.read(0, 0, 3), 1.0f), "adoptUsed vouches for what was already there");
            m.ensureCommitted(0, 16);
            CHECK(feq(m.read(0, 0, 3), 1.0f), "and a commit below the mark changes nothing");

            // The other verb wipes: virgin tape is silence, not garbage.
            m.resetUsed(0);
            m.ensureCommitted(0, 16);
            CHECK(feq(m.read(0, 0, 3), 0.0f), "ensureCommitted zeroes the span it claims");
        }

        // ── Mono binds one channel ───────────────────────────────────────────
        {
            juce::AudioBuffer<float> mono{ 1, 32 };
            mono.clear();
            dc::Medium m;
            bindBuffer(m, mono, 32, dc::Topology::Linear, 44100.0);
            CHECK(m.bound() && m.channels() == 1, "a mono buffer binds one channel");
            CHECK(m.topology() == dc::Topology::Linear && feq(static_cast<float>(m.mediumRate()), 44100.0f),
                  "topology and medium rate cross intact");
        }

        // ── Nonsense does not bind ───────────────────────────────────────────
        {
            juce::AudioBuffer<float> buf{ 2, 16 };
            dc::Medium m;
            bindBuffer(m, buf, 64, dc::Topology::Circular, 48000.0);  // longer than the buffer
            CHECK(! m.bound(), "a length past the buffer's end refuses to bind");
        }

        // ── A head over a lent buffer reads what is in it ────────────────────
        {
            juce::AudioBuffer<float> buf{ 1, 256 };
            buf.clear();
            buf.setSample(0, 100, 1.0f);

            dc::Medium m;
            bindBuffer(m, buf, 256, dc::Topology::Circular, 48000.0);
            m.adoptUsed(0, 256);

            dc::ReadHead r;
            r.setPosition(100.0);
            float out = 0.0f;
            r.readFrame(m, 0, &out, 1);
            CHECK(feq(out, 1.0f, 1e-4f), "rate 1 at an integer position reads the sample itself");
        }

        // ── The transport snapshot carries position ──────────────────────────
        {
            TransportInfo t;
            t.bpm = 128.0;
            t.sampleRate = 48000.0;
            t.samplesPerBar = 90000.0;
            t.barPpq = 4.0;
            t.running = true;
            t.transportPhaseSamples = 123456.0;
            t.launchQuantPeriodSamples = 22500.0;
            t.launchQuantPhaseOffsetSamples = 100.0;

            const auto s = toSnapshot(t);
            CHECK(feq(static_cast<float>(s.positionSamples), 123456.0f),
                  "absolute position crosses the boundary");
            CHECK(s.running && feq(static_cast<float>(s.launchQuantPeriodSamples), 22500.0f)
                      && feq(static_cast<float>(s.samplesPerBar), 90000.0f),
                  "so do the grid and the rate");
        }
    }
}
