// StreamMachineTest -- disk-streaming sampler (4.5, DESIGN §29.2).
//
// Writes a short temp WAV, then verifies StreamMachine opens it, streams it on a
// note-on (non-silent output), stops on note-off, and rejects a bad path. The
// audio is streamed from disk via a background BufferingAudioReader and never
// decoded into a SamplePool.

#include "TestHarness.h"
#include "../src/machine/StreamMachine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace lockstep
{
    namespace
    {
        // Write a 0.5 s mono tone-ish constant WAV; returns the file (caller keeps
        // the TemporaryFile alive).
        juce::File writeTestWav(juce::TemporaryFile& tmp, double sr)
        {
            const juce::File& f = tmp.getFile();
            const int len = static_cast<int>(sr * 0.5);
            juce::AudioBuffer<float> data(1, len);
            for (int i = 0; i < len; ++i)
                data.setSample(0, i, 0.5f);

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
    }

    void runStreamMachineTests()
    {
        constexpr double kSr = 48000.0;

        juce::TemporaryFile tmp(".wav");
        const juce::File wav = writeTestWav(tmp, kSr);
        CHECK(wav.existsAsFile(), "test WAV written");

        StreamMachine sm;
        sm.prepare(kSr, 512);
        CHECK(sm.setFilePath(wav.getFullPathName()), "Static opens the file");
        CHECK(sm.filePath() == wav.getFullPathName(), "path is stored");

        // Stream on a note-on. The prefetch runs on a background thread with a
        // non-blocking read, so spin a bounded number of blocks until audio arrives.
        ParamFrame params{ 0.0f };  // start = 0
        bool gotAudio = false;
        for (int b = 0; b < 200 && !gotAudio; ++b)
        {
            juce::AudioBuffer<float> out(2, 512);
            out.clear();
            sm.process(b == 0 ? noteOn() : juce::MidiBuffer{}, params, out);
            if (out.getMagnitude(0, 512) > 0.1f) gotAudio = true;
            else juce::Thread::sleep(2);
        }
        CHECK(gotAudio, "Static streams audio from disk on a note-on");

        // Note-off stops playback (next block is silent).
        {
            juce::AudioBuffer<float> out(2, 512);
            out.clear();
            sm.process(noteOff(), params, out);
            juce::AudioBuffer<float> out2(2, 512);
            out2.clear();
            sm.process(juce::MidiBuffer{}, params, out2);
            CHECK(out2.getMagnitude(0, 512) < 1e-4f, "note-off stops the stream");
        }

        // A bad path is rejected and clears state.
        CHECK(!sm.setFilePath("/no/such/file_xyz.wav"), "missing file rejected");
        CHECK(sm.filePath().isEmpty(), "bad path clears the stored path");

        // Item 6: the sample_id slot is what makes the MZ show a sample picker and
        // lets the streamed source live in the SamplePool. Verify the schema.
        CHECK(sm.numParams() == 2, "StreamMachine exposes start + sample_id");
        bool foundSampleId = false;
        for (int i = 0; i < sm.numParams(); ++i)
        {
            const auto spec = sm.paramSpec(i);
            if (juce::String(spec.id) == "sample_id")
            {
                foundSampleId = true;
                CHECK(spec.isStepped, "sample_id is stepped (picker index)");
                CHECK(spec.sectionIndex == IMachine::kSrcSecIdx, "sample_id lives on SRC");
            }
        }
        CHECK(foundSampleId, "StreamMachine has a sample_id slot");
        CHECK(sm.numSections() == IMachine::kSrcSecIdx + 1,
              "numSections keeps the SRC panel reachable");

        // 9.18: Sample is slot 0 so its picker button renders first in the MZ.
        CHECK(juce::String(sm.paramSpec(0).id) == "sample_id",
              "sample_id is slot 0 (picker renders first)");
        CHECK(juce::String(sm.paramSpec(1).id) == "start",
              "start is slot 1");

        // 9.18: capability tag keeps the two picker families disjoint — a
        // StreamMachine offers Stream entries, a PCM player offers resident PCM.
        CHECK(sm.sampleClass() == IMachine::SampleClass::Stream,
              "StreamMachine is a Stream sample class");
    }
}
