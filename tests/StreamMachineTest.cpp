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

        // Total non-silent samples the stream produces for one note, played to EOF.
        // Duration is the observable that tempo-tracking moves: a stream that follows
        // the project tempo plays LONGER at a slower tempo (same pitch, more time).
        int streamDuration(StreamMachine& sm, const ParamFrame& params)
        {
            int total = 0;
            int silentRun = 0;
            bool started = false;
            for (int b = 0; b < 2000; ++b)
            {
                juce::AudioBuffer<float> out(2, 512);
                out.clear();
                sm.process(b == 0 ? noteOn() : juce::MidiBuffer{}, params, out);

                int loud = 0;
                for (int i = 0; i < 512; ++i)
                    if (std::abs(out.getSample(0, i)) > 1.0e-3f) ++loud;

                total += loud;
                if (loud > 0) { started = true; silentRun = 0; }
                else
                {
                    // Before the first sample this is the background prefetch; after it,
                    // a long silent run means the stream reached EOF.
                    if (started && ++silentRun > 8) break;
                    juce::Thread::sleep(2);
                }
            }
            return total;
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

        SamplePool pool;
        StreamMachine sm(pool);
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
            SamplePool ppool;
            StreamMachine sp(ppool);
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

        // Schema: sample_id + start kept; pitch/tune/timestretch/loop/release appended,
        // then A440 (9.23) at the end. Base params are id-keyed on disk, so appending is
        // safe: an older 7-slot frame loads with the new slot at its default (Auto).
        CHECK(sm.numParams() == 8, "StreamMachine exposes 8 slots (after the A440 append)");
        CHECK(juce::String(sm.paramSpec(7).id) == "player_tune_mode", "slot 7 = A440 mode");
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
        CHECK(juce::String(sm.paramSpec(6).id) == "player_release"
              && feq(sm.paramSpec(6).defaultValue, 0.5f),
              "slot 6 = player_release default 0.5 (graceful-stop fade)");
        CHECK(sm.paramSpec(0).isStepped, "sample_id is stepped (picker index)");
        CHECK(sm.paramSpec(0).sectionIndex == IMachine::kSrcSecIdx, "sample_id lives on SRC");
        CHECK(sm.numSections() == IMachine::kSrcSecIdx + 1,
              "numSections keeps the SRC panel reachable");
        CHECK(sm.sampleClass() == IMachine::SampleClass::Stream,
              "StreamMachine is a Stream sample class");

        // Live loop re-latch (9.26): player_loop is note-on latched, but a sustaining
        // voice never re-fires, so a mid-voice toggle must be honoured live in
        // process(). ts=Off gives a deterministic natural end (~length in output
        // samples). Off→On keeps it sounding past that end; On→Off stops it.
        {
            juce::TemporaryFile stmp(".wav");
            const juce::File swav = writeSineWav(stmp, kFileRate, kTone, 0.5);

            auto runReLatch = [&](float startLoop, float endLoop) {
                SamplePool lpool;
                StreamMachine sm2(lpool);
                sm2.prepare(kEngineRate, 512);
                sm2.setFilePath(swav.getFullPathName());
                double tailEnergy = 0.0;
                int producedSamples = 0;
                bool started = false;
                for (int b = 0; b < 1200; ++b)
                {
                    juce::AudioBuffer<float> out(2, 512);
                    out.clear();
                    const bool flip = producedSamples >= 8000;  // flip mid-voice, pre-end
                    sm2.process(b == 0 ? noteOn() : juce::MidiBuffer{},
                                streamFrame(0.0f, 0.0f, 0.0f, 0.0f /*ts Off*/,
                                            flip ? endLoop : startLoop),
                                out);
                    const float mag = out.getMagnitude(0, 512);
                    if (!started)
                    {
                        if (mag > 1.0e-3f) started = true;
                        else { juce::Thread::sleep(2); continue; }
                    }
                    producedSamples += 512;
                    if (producedSamples >= 40000)  // well past the ~22050-sample end
                        tailEnergy += static_cast<double>(mag) * mag;
                    if (producedSamples >= 80000) break;
                }
                return tailEnergy;
            };

            CHECK(runReLatch(0.0f, 1.0f) > 1.0e-3,
                  "Stream loop re-latch: Off→On mid-voice keeps it sounding past the end");
            CHECK(runReLatch(1.0f, 0.0f) < 1.0e-4,
                  "Stream loop re-latch: On→Off mid-voice stops it after the pass");
        }

        // Old two-slot kit: a v29-shaped {sample_id, start} frame loads defaults for
        // the appended slots (Tempo / no pitch / no loop) and still streams.
        {
            SamplePool opool;
            StreamMachine so(opool);
            so.prepare(kEngineRate, 512);
            CHECK(so.setFilePath(wav.getFullPathName()), "Stream (legacy frame) opens the file");
            ParamFrame legacy{ 0.0f, 0.0f };  // just sample_id + start
            auto out = streamAudio(so, legacy, 8192);
            CHECK(static_cast<int>(out.size()) > 2048,
                  "Stream: legacy 2-slot frame still streams with appended defaults");
        }

        // 9.23 — Stream follows the POOL's tempo.
        //
        // Stream hardcoded effBpm = 0 and so ignored the pool entirely: a song tagged
        // 120 BPM streamed at its native rate no matter what the project was doing,
        // while Stretch (reading the same pool, through the same helper) tracked it.
        // The observable is duration: at half the tempo the same file must take twice
        // as long, at the same pitch.
        //
        // The control matters as much as the assertion. With NO tempo on the entry the
        // two tempos must produce the SAME duration — that is what proves this test
        // measures the pool wiring rather than some other tempo dependence, and it is
        // exactly the (ratio 1.0) behaviour the old hardcode produced for every entry.
        {
            constexpr double kBeats = 4.0;
            auto barSamplesAt = [&](double bpm) { return kBeats * kEngineRate * 60.0 / bpm; };

            juce::TemporaryFile ttmp(".wav");
            const juce::File twav = writeSineWav(ttmp, kFileRate, kTone, 1.0);  // 1 s @ 48k

            auto durationAtTempo = [&](double projectBpm, double entryBpm) {
                SamplePool pool;
                const int idx = pool.addStreamRef(twav.getFullPathName());
                if (entryBpm > 0.0) pool.setUserBpm(idx, entryBpm);

                StreamMachine sm(pool);
                sm.prepare(kEngineRate, 512);
                TransportInfo t;
                t.bpm = projectBpm;
                t.sampleRate = kEngineRate;
                t.samplesPerBar = barSamplesAt(projectBpm);
                sm.setTransport(t);
                sm.setFilePath(twav.getFullPathName(), idx);
                // Tempo mode on, no loop.
                return streamDuration(sm, streamFrame(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
            };

            // Entry tagged 120 BPM: native at 120, stretched ~2x at 60.
            const int at120 = durationAtTempo(120.0, 120.0);
            const int at60  = durationAtTempo(60.0, 120.0);
            CHECK(at120 > 8192, "Stream (tempo): the 120 BPM case produced audio");
            const double grew = static_cast<double>(at60) / std::max(1, at120);
            CHECK(grew > 1.6 && grew < 2.4,
                  "Stream follows the pool's BPM: half the tempo, ~2x the duration");

            // Control: an entry with no tempo tracks nothing, at either tempo.
            const int un120 = durationAtTempo(120.0, 0.0);
            const int un60  = durationAtTempo(60.0, 0.0);
            const double drift = static_cast<double>(un60) / std::max(1, un120);
            CHECK(drift > 0.85 && drift < 1.15,
                  "an untagged stream is not warped -- unknown tempo means play it native");
        }
    }
}
