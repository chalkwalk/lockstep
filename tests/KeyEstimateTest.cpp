// KeyEstimateTest.cpp — 4.9 key + tuning detection (dsp/KeyEstimate.h).
// Synthetic tonal fixtures: an in-key arpeggio should resolve to the right
// root+brightness; a detuned reference should surface in tuningCents; noise
// and silence must return "unknown" (root == -1).

#include "TestHarness.h"
#include "../src/dsp/KeyEstimate.h"
#include "../src/core/Scale.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <random>
#include <vector>

namespace lockstep
{
    namespace
    {
        // Render a buffer of scale-tone partials. `refHz` is the tuning
        // reference for A4 (440 by default); `pcs` are pitch classes voiced
        // (octave 4..5), with `weights` scaling each pc's amplitude so the
        // tonic/dominant dominate (mirrors real tonal energy).
        juce::AudioBuffer<float> renderTones(const std::vector<int>& pcs,
                                             const std::vector<double>& weights,
                                             double sampleRate, double seconds,
                                             double refHz = 440.0)
        {
            const int n = static_cast<int>(sampleRate * seconds);
            juce::AudioBuffer<float> buf(1, n);
            buf.clear();
            float* d = buf.getWritePointer(0);
            for (std::size_t k = 0; k < pcs.size(); ++k)
            {
                // MIDI note in octave 4 (pc 0 == C4 == midi 60).
                const int midi = 60 + pcs[k];
                const double hz = refHz * std::pow(2.0, (midi - 69) / 12.0);
                const double amp = 0.2 * weights[k];
                for (int i = 0; i < n; ++i)
                    d[i] += static_cast<float>(amp * std::sin(2.0 * juce::MathConstants<double>::pi
                                                              * hz * i / sampleRate));
            }
            return buf;
        }
    }

    void runKeyEstimateTests()
    {
        const double sr = 44100.0;

        // A natural minor (Aeolian): A B C D E F G = pcs 9 11 0 2 4 5 7,
        // root/dominant weighted heavier.
        {
            std::vector<int>    pcs { 9, 11, 0, 2, 4, 5, 7 };
            std::vector<double> w   { 3.0, 1.0, 1.5, 1.5, 2.0, 1.0, 1.0 };
            auto buf = renderTones(pcs, w, sr, 3.0);
            const KeyEstimate ke = estimateKey(buf, sr);
            CHECK(ke.root == 9, "A-minor fixture should detect root A (9), got "
                                    + juce::String(ke.root));
            CHECK(ke.brightness == kAeolian, "A-minor fixture should read Aeolian, got "
                                    + juce::String(ke.brightness));
        }

        // C major (Ionian): C D E F G A B = pcs 0 2 4 5 7 9 11.
        {
            std::vector<int>    pcs { 0, 2, 4, 5, 7, 9, 11 };
            std::vector<double> w   { 3.0, 1.0, 2.0, 1.0, 2.0, 1.0, 1.0 };
            auto buf = renderTones(pcs, w, sr, 3.0);
            const KeyEstimate ke = estimateKey(buf, sr);
            CHECK(ke.root == 0, "C-major fixture should detect root C (0), got "
                                    + juce::String(ke.root));
            CHECK(ke.brightness == kIonian, "C-major fixture should read Ionian, got "
                                    + juce::String(ke.brightness));
        }

        // Same C major rendered against a 445 Hz reference (~+19.6 cents sharp).
        // Kept off the +-50-cent wrap boundary so the circular mean does not
        // straddle it (a +46-cent detune sits right on the fold and is
        // intentionally not tested here).
        {
            std::vector<int>    pcs { 0, 2, 4, 5, 7, 9, 11 };
            std::vector<double> w   { 3.0, 1.0, 2.0, 1.0, 2.0, 1.0, 1.0 };
            auto buf = renderTones(pcs, w, sr, 3.0, 445.0);
            const KeyEstimate ke = estimateKey(buf, sr);
            const double expected = 1200.0 * std::log2(445.0 / 440.0);   // ~19.6 cents
            CHECK(std::abs(ke.tuningCents - expected) < 8.0,
                  "445Hz reference should surface ~+19.6 cents, got "
                      + juce::String(ke.tuningCents));
            CHECK(ke.root == 0, "detuned C-major should still detect root C (0), got "
                                    + juce::String(ke.root));
        }

        // White noise -> unknown.
        {
            const int n = static_cast<int>(sr * 2.0);
            juce::AudioBuffer<float> buf(1, n);
            std::mt19937 rng(1234);
            std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
            float* d = buf.getWritePointer(0);
            for (int i = 0; i < n; ++i)
                d[i] = dist(rng);
            const KeyEstimate ke = estimateKey(buf, sr);
            CHECK(ke.root == -1, "white noise should be unknown key, got root "
                                    + juce::String(ke.root));
        }

        // Silence and sub-frame buffers -> unknown, no crash.
        {
            juce::AudioBuffer<float> silent(1, static_cast<int>(sr));
            silent.clear();
            CHECK(estimateKey(silent, sr).root == -1, "silence should be unknown key");

            juce::AudioBuffer<float> tiny(1, 100);
            tiny.clear();
            CHECK(estimateKey(tiny, sr).root == -1, "sub-FFT buffer should be unknown key");

            juce::AudioBuffer<float> empty(1, 0);
            CHECK(estimateKey(empty, sr).root == -1, "empty buffer should be unknown key");
            CHECK(estimateKey(silent, 0.0).root == -1, "zero sample-rate should be unknown");
        }

        // Single A4 sine: must not crash, tuning near 0 (root may be 9 or
        // unknown depending on the single-pc scoring — don't over-assert).
        {
            const int n = static_cast<int>(sr * 2.0);
            juce::AudioBuffer<float> buf(1, n);
            float* d = buf.getWritePointer(0);
            for (int i = 0; i < n; ++i)
                d[i] = static_cast<float>(0.3 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                         * 440.0 * i / sr));
            const KeyEstimate ke = estimateKey(buf, sr);
            CHECK(std::abs(ke.tuningCents) < 5.0,
                  "pure A440 should read ~0 cents, got " + juce::String(ke.tuningCents));
        }
    }
}
