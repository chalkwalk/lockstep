#pragma once

#include "IMachine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace lockstep
{
    // StreamMachine — disk-streaming sampler for long-form material (full songs,
    // long field recordings) that should not be decoded into RAM (DESIGN §29.2,
    // PRINCIPLES §12). Unlike the Flex SampleMachine (which plays decoded PCM from
    // the SamplePool), Static streams its source from disk via a BufferingAudioReader
    // on a background thread, so the audio never enters RAM wholesale or the project
    // state — only the file path persists (held per-Kit, not in the SamplePool).
    //
    // A trig (note-on) starts playback from `start`; note-off stops it (the track
    // gate governs duration). Monophonic (V1). Level/pan come from the track CHANNEL
    // block (hasInternalAmp suppresses the track ENVELOPE — Static is a gated stream,
    // not an enveloped one-shot). Resampling on a file/engine rate mismatch is a
    // later refinement; v1 streams at the engine rate.
    class StreamMachine : public IMachine
    {
    public:
        StreamMachine();
        ~StreamMachine() override;

        static constexpr const char* kMachineId = "lockstep.stream.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "STRM"; }

        // 9.18: references a disk file streamed on the fly — Stream-origin entries
        // only (keeps Stream out of the PCM players' pickers, and vice versa).
        [[nodiscard]] SampleClass sampleClass() const override { return SampleClass::Stream; }

        // Open (or clear, if empty) the streamed source file. Message-thread only,
        // and must be called with the engine quiesced (it swaps the reader the audio
        // thread reads from). Returns true if the file opened.
        bool setFilePath(const juce::String& path);
        [[nodiscard]] juce::String filePath() const { return path_; }

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        // kSrcSecIdx + 1, not a count: section() gates on `sectionIndex <
        // numSections()`, so this must exceed the highest sectionIndex used or the
        // SRC panel is unreachable (the looper/recorder "no source panel" bug).
        [[nodiscard]] int numSections() const override { return kSrcSecIdx + 1; }
        [[nodiscard]] SectionInfo section(int index) const override
        {
            if (index == kSrcSecIdx) return { "SRC" };
            return {};
        }

        [[nodiscard]] bool hasInternalAmp() const override { return true; }
        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V1;
        }

        // Item 6: the streamed source is a Stream-origin SamplePool entry; the
        // sample_id slot is the picker handle. Slot index is public so the
        // processor's writeParam hook can detect a sample_id write on a stream
        // track and open the reader for the picked entry.
        // 9.18: Sample is slot 0 so the sample-picker button renders first in the MZ
        // (the source you pick before you set its start offset). Base params are
        // id-keyed on disk, so this reorder is serialization-safe.
        static constexpr int kSlotSampleId = 0;

    private:
        static constexpr int kSlotStart = 1;
        static constexpr int kNumSlots = 2;

        juce::AudioFormatManager formatManager_;
        juce::TimeSliceThread streamThread_{ "lockstep.stream.io" };
        std::unique_ptr<juce::BufferingAudioReader> reader_;
        juce::String path_;
        juce::int64 lengthSamples_ = 0;

        // Playback state (audio thread).
        bool playing_ = false;
        juce::int64 readPos_ = 0;

        // Anti-click gate: a hard start/stop of a disk stream pops. Ramp the block
        // gain toward (playing ? 1 : 0) so note-on, note-off and end-of-file fade
        // over a few ms instead of stepping. Mirrors StretchMachine's gate.
        double sampleRate_ = 44100.0;
        float gain_ = 0.0f;
        float fadeInc_ = 0.0f;
    };
}
