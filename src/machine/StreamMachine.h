#pragma once

#include "IMachine.h"
#include "ITempoAware.h"
#include "../dsp/BungeeStretchEngine.h"
#include "../dsp/ReaderStretchSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
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
    class StreamMachine : public IMachine, public ITempoAware
    {
    public:
        StreamMachine();
        ~StreamMachine() override;

        static constexpr const char* kMachineId = "lockstep.stream.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "STRM"; }

        void setTransport(const TransportInfo& t) noexcept override { transport_ = t; }

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
        static constexpr int kSlotStart = 1;        // trim 0..1 (id "start")
        static constexpr int kSlotPitch = 2;        // ±24 semitones (independent)
        static constexpr int kSlotTune = 3;         // ±50 cents fine-tune
        static constexpr int kSlotTimestretch = 4;  // 0 = Off (native), 1 = Tempo
        static constexpr int kSlotLoop = 5;         // 0 = Off, 1 = On
        static constexpr int kNumSlots = 6;

        [[nodiscard]] double timeRatioFor() const;
        [[nodiscard]] static double pitchRatioFor(const ParamFrame& params);
        void rebuildEngine();
        // Apply the loop window from the cached note-on ingredients. Called at
        // note-on and again from process() when player_loop is toggled mid-voice
        // (the live re-latch; mirrors StretchMachine).
        void applyLoop(bool loop);

        juce::AudioFormatManager formatManager_;
        juce::TimeSliceThread streamThread_{ "lockstep.stream.io" };
        std::unique_ptr<juce::BufferingAudioReader> reader_;
        juce::String path_;
        juce::int64 lengthSamples_ = 0;
        double fileRate_ = 0.0;
        int fileChannels_ = 1;

        BungeeStretchEngine engine_;
        ReaderStretchSource source_;
        TransportInfo transport_{};
        int tsMode_ = 1;   // resolved timestretch mode for the active note

        // Playback state (audio thread).
        bool playing_ = false;
        // Loop-window ingredients cached at note-on so process() can re-apply the
        // window math when player_loop is toggled while the voice sustains.
        bool loopOn_ = false;             // latched loop state for the active note
        juce::int64 startFrame_ = 0;      // trim point (frames) for the active note

        // Anti-click gate: a hard start/stop of a disk stream pops. Ramp the block
        // gain toward (playing ? 1 : 0) so note-on, note-off and end-of-file fade
        // over a few ms instead of stepping. Mirrors StretchMachine's gate.
        double sampleRate_ = 44100.0;
        int maxBlock_ = 512;
        float gain_ = 0.0f;
        float fadeInc_ = 0.0f;

        static constexpr std::array<const char* const, 2> kTsLabels = { "Off", "Tempo" };
        static constexpr std::array<const char* const, 2> kLoopLabels = { "Off", "On" };
    };
}
