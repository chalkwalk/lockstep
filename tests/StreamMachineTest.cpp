// StreamMachineTest -- disk-streaming sampler (4.5, DESIGN §29.2) on the Bungee
// stretch pipeline (9.23 S3).
//
// Writes short temp WAVs, then verifies StreamMachine opens one, streams it on a
// note-on, stops on note-off, rejects a bad path, and — the S3 wins — resamples a
// file whose rate differs from the engine rate (the missing-resample fix) and
// pitch-shifts without changing tempo. Plus StretchMath unit cases. The audio is
// streamed from disk via a background BufferingAudioReader, never decoded to RAM.

#include "TestHarness.h"
#include "../src/machine/StreamMachine.h"
#include "../src/machine/StretchMath.h"
#include <cmath>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace lockstep
{
    namespace
    {
        // Write a mono sine WAV of `seconds` at rate `sr`, frequency `freq`.
        juce::File writeSineWav(juce::TemporaryFile& tmp, double sr, double freq, double seconds)
        {
            const juce::File& f = tmp.getFile();
            const int len = static_cast<int>(sr * seconds);
            juce::AudioBuffer<float> data(1, len);
            for (int i = 0; i < len; ++i)
                data.setSample(0, i, static_cast<float>(std::sin(
                    2.0 * juce::MathConstants<double>::pi * freq
                    * static_cast<double>(i) / sr)));

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os(f.createOutputStream());
            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate(sr)
                                     .withNumChannels(1)
                                     .withBitsPerSample(16);
            if (auto w = wav.createWriterFor(os, options))
                w->writeFromAudioSampleBuffer(data, 0, len);
            return f;
        }

        juce::MidiBuffer noteOn() { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); return m; }
        juce::MidiBuffer noteOff() { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOff(1, 60), 0); return m; }

        // Play (note held) and collect the first `want` non-silent output samples of
        // channel 0, spinning while the background prefetch fills. Returns fewer than
        // `want` only on timeout.
        std::vector<float> streamAudio(StreamMachine& sm, const ParamFrame& params, int want)
        {
            std::vector<float> v;
            for (int b = 0; b < 1200 && static_cast<int>(v.size()) < want; ++b)
            {
                juce::AudioBuffer<float> out(2, 512);
                out.clear();
                sm.process(b == 0 ? noteOn() : juce::MidiBuffer{}, params, out);
                bool any = false;
                for (int i = 0; i < 512; ++i)
                {
                    const float x = out.getSample(0, i);
                    if (std::abs(x) > 1.0e-3f) { any = true; v.push_back(x); }
                }
                if (!any) juce::Thread::sleep(2);
            }
            return v;
        }

        // Dominant frequency via an FFT spectral peak — unambiguous for a single
        // tone (no zero-crossing harmonic inflation, no autocorrelation octave
        // ambiguity). Hann-windowed; returns the peak bin's centre frequency.
        double fundamentalFreq(const std::vector<float>& v, double outRate)
        {
            constexpr int order = 14;               // 16384-point
            constexpr int size = 1 << order;
            const int n = std::min(size, static_cast<int>(v.size()));
            if (n < 256) return 0.0;

            juce::dsp::FFT fft(order);
            std::vector<float> buf(static_cast<std::size_t>(size) * 2, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
                                                      * i / (n - 1));
                buf[static_cast<std::size_t>(i)] = static_cast<float>(v[static_cast<std::size_t>(i)] * w);
            }
            fft.performFrequencyOnlyForwardTransform(buf.data());

            int peak = 1;
            float best = 0.0f;
            for (int b = 1; b < size / 2; ++b)
                if (buf[static_cast<std::size_t>(b)] > best)
                    { best = buf[static_cast<std::size_t>(b)]; peak = b; }
            return static_cast<double>(peak) * outRate / size;
        }

        // Frame: sample_id, start, pitch, tune, timestretch, loop.
        ParamFrame streamFrame(float start, float pitch, float tune, float ts, float loop)
        {
            return ParamFrame{ 0.0f, start, pitch, tune, ts, loop };
        }
    }

    void runStreamMachineTests()
    {
        // ── StretchMath unit cases (shared with the RAM Stretch player) ──────────
        {
            // bars path: 2 bars over 24000 src frames at matched rate ⇒ 4x.
            CHECK(feq(static_cast<float>(stretchmath::stretchTimeRatio(
                          2.0, 0.0, 24000, 48000.0, 48000.0, 48000.0)),
                      4.0f, 1e-3f),
                  "StretchMath: bars path (2 bars / 24000 frames ⇒ 4.0)");
            // bpm path: 48000 frames @48k = 1 s = 0.5 bar at 120 bpm 4/4 ⇒ 0.5x.
            CHECK(feq(static_cast<float>(stretchmath::stretchTimeRatio(
                          0.0, 120.0, 48000, 48000.0, 48000.0, 48000.0)),
                      0.5f, 1e-3f),
                  "StretchMath: bpm path (120 bpm ⇒ 0.5)");
            // fallback: no bars, no bpm ⇒ native 1.0.
            CHECK(feq(static_cast<float>(stretchmath::stretchTimeRatio(
                          0.0, 0.0, 24000, 48000.0, 48000.0, 48000.0)),
                      1.0f, 1e-6f),
                  "StretchMath: unknown tempo ⇒ 1.0");
            // rate factor: off-rate source folds srcRate/outputRate.
            CHECK(feq(static_cast<float>(stretchmath::stretchTimeRatio(
                          1.0, 0.0, 24000, 48000.0, 44100.0, 44100.0)),
                      2.0f, 1e-3f),
                  "StretchMath: srcRate factor (1 bar / 24000 @48k, spb 44100 ⇒ 2.0)");
        }

        constexpr double kEngineRate = 44100.0;  // harness/engine rate
        constexpr double kFileRate = 48000.0;    // deliberately off-rate file
        constexpr double kTone = 440.0;

        juce::TemporaryFile tmp(".wav");
        const juce::File wav = writeSineWav(tmp, kFileRate, kTone, 2.0);
        CHECK(wav.existsAsFile(), "test WAV written");

        StreamMachine sm;
        sm.prepare(kEngineRate, 512);
        CHECK(sm.setFilePath(wav.getFullPathName()), "Stream opens the file");
        CHECK(sm.filePath() == wav.getFullPathName(), "path is stored");

        // Native-rate fix: a 48 kHz file on a 44.1 kHz engine reproduces the source
        // tone frequency (previously it played ~404 Hz — file samples read 1:1).
        {
            auto out = streamAudio(sm, streamFrame(0.0f, 0.0f, 0.0f, 1.0f, 0.0f), 16384);
            CHECK(static_cast<int>(out.size()) > 4096,
                  "Stream: streams audio from disk on a note-on");
            const double f = fundamentalFreq(out, kEngineRate);
            CHECK(f > kTone * 0.94 && f < kTone * 1.06,
                  "Stream: off-rate file resampled to the correct tone (~440 Hz, got "
                  + juce::String(f) + ")");
        }

        // Note-off stops playback (the gate fades; the next block is silent).
        {
            juce::AudioBuffer<float> out(2, 512);
            out.clear();
            sm.process(noteOff(), streamFrame(0.0f, 0.0f, 0.0f, 1.0f, 0.0f), out);
            juce::AudioBuffer<float> out2(2, 512);
            out2.clear();
            sm.process(juce::MidiBuffer{}, streamFrame(0.0f, 0.0f, 0.0f, 1.0f, 0.0f), out2);
            CHECK(out2.getMagnitude(0, 512) < 1e-3f, "note-off stops the stream");
        }

        // Pitch +12 semitones doubles the tone (duration unchanged under Tempo).
        {
            StreamMachine sp;
            sp.prepare(kEngineRate, 512);
            CHECK(sp.setFilePath(wav.getFullPathName()), "Stream (pitch) opens the file");
            auto out = streamAudio(sp, streamFrame(0.0f, 12.0f, 0.0f, 1.0f, 0.0f), 16384);
            const double f = fundamentalFreq(out, kEngineRate);
            CHECK(f > 2.0 * kTone * 0.9 && f < 2.0 * kTone * 1.1,
                  "Stream: pitch +12 doubles the tone (~880 Hz, got " + juce::String(f) + ")");
        }

        // A bad path is rejected and clears state.
        CHECK(!sm.setFilePath("/no/such/file_xyz.wav"), "missing file rejected");
        CHECK(sm.filePath().isEmpty(), "bad path clears the stored path");

        // Schema: sample_id + start kept; pitch/tune/timestretch/loop appended.
        CHECK(sm.numParams() == 6, "StreamMachine exposes 6 slots after the S3 append");
        CHECK(juce::String(sm.paramSpec(0).id) == "sample_id",
              "sample_id is slot 0 (picker renders first)");
        CHECK(juce::String(sm.paramSpec(1).id) == "start", "start keeps id + slot 1");
        CHECK(juce::String(sm.paramSpec(2).id) == "player_pitch", "slot 2 = player_pitch");
        CHECK(juce::String(sm.paramSpec(3).id) == "player_tune", "slot 3 = player_tune");
        const auto tsSpec = sm.paramSpec(4);
        CHECK(juce::String(tsSpec.id) == "player_timestretch" && feq(tsSpec.defaultValue, 1.0f),
              "slot 4 = player_timestretch default Tempo");
        const auto loopSpec = sm.paramSpec(5);
        CHECK(juce::String(loopSpec.id) == "player_loop" && feq(loopSpec.defaultValue, 0.0f),
              "slot 5 = player_loop default Off");
        CHECK(sm.paramSpec(0).isStepped, "sample_id is stepped (picker index)");
        CHECK(sm.paramSpec(0).sectionIndex == IMachine::kSrcSecIdx, "sample_id lives on SRC");
        CHECK(sm.numSections() == IMachine::kSrcSecIdx + 1,
              "numSections keeps the SRC panel reachable");
        CHECK(sm.sampleClass() == IMachine::SampleClass::Stream,
              "StreamMachine is a Stream sample class");

        // Old two-slot kit: a v29-shaped {sample_id, start} frame loads defaults for
        // the appended slots (Tempo / no pitch / no loop) and still streams.
        {
            StreamMachine so;
            so.prepare(kEngineRate, 512);
            CHECK(so.setFilePath(wav.getFullPathName()), "Stream (legacy frame) opens the file");
            ParamFrame legacy{ 0.0f, 0.0f };  // just sample_id + start
            auto out = streamAudio(so, legacy, 8192);
            CHECK(static_cast<int>(out.size()) > 2048,
                  "Stream: legacy 2-slot frame still streams with appended defaults");
        }
    }
}
