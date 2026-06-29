#pragma once

#include "IMachine.h"
#include "InputSource.h"
#include "SamplePool.h"
#include <array>

namespace lockstep
{
    // RecorderMachine — live resampler (Octatrack track recorder, DESIGN §29.2 /
    // §30). It captures `input_source` audio into a volatile REC buffer (§28),
    // overwriting it each time a trig fires. It synthesises nothing: the sequencer
    // fills the track buffer from the chosen source before process(), and the
    // recorder copies that input into the target buffer for `rec_length`.
    //
    // "Recorder trig" is contextual, not a stored step field: any trig on a
    // Recorder track is a capture trigger (a note-on on these tracks is meaningless
    // as a pitch). A plain trig re-captures every loop; a one-shot trig captures
    // once (the existing TrigCondition::oneShot composes — no machine work). The
    // captured buffer is immediately playable from a Sampler/Slicer track pointed
    // at the same pool index; freeze-to-disk (§22) is a later milestone.
    //
    // currentVoices() = V1 so the sequencer emits exactly one note-on per trig;
    // the recorder treats any incoming note-on as the capture-start edge.
    class RecorderMachine : public IMachine
    {
    public:
        explicit RecorderMachine(SamplePool& pool) : pool_(pool) {}

        static constexpr const char* kMachineId = "lockstep.recorder.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "REC"; }

        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        }
        void reset() override
        {
            capturing_ = false;
            writePos_ = 0;
            samplesRemaining_ = 0;
            target_ = nullptr;
        }

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        [[nodiscard]] int numSections() const override { return 1; }
        [[nodiscard]] SectionInfo section(int index) const override
        {
            if (index == kSrcSecIdx) return { "SRC" };
            return {};
        }

        // One capture trigger per trig; the note pitch/velocity are ignored.
        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V1;
        }

    private:
        static constexpr int kSlotInputSource = 0;
        static constexpr int kSlotTargetBuffer = 1;
        static constexpr int kSlotRecLength = 2;
        static constexpr int kNumSlots = 3;

        // rec_length bounds (seconds). The default project's volatile slots are
        // sized to a fixed capacity; capture truncates at the buffer length.
        static constexpr float kMinRecSeconds = 0.1f;
        static constexpr float kMaxRecSeconds = 12.0f;
        static constexpr float kDefaultRecSeconds = 2.0f;

        void startCapture(int targetSlot, float recSeconds);
        void writeInput(const juce::AudioBuffer<float>& input, int startSample, int numSamples);

        SamplePool& pool_;
        double sampleRate_ = 44100.0;

        // Capture state (audio-thread only).
        bool capturing_ = false;
        int writePos_ = 0;
        int samplesRemaining_ = 0;
        juce::AudioBuffer<float>* target_ = nullptr;

        static constexpr std::array<const char* const, 3> kSourceLabels = {
            "None", "Ext", "Master"
        };
        // One label per reserved REC slot (kNumVolatileSlots = 8 in the processor).
        static constexpr std::array<const char* const, 8> kBufferLabels = {
            "REC1", "REC2", "REC3", "REC4", "REC5", "REC6", "REC7", "REC8"
        };
    };
}
