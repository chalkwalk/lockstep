#pragma once

#include "IMachine.h"
#include "ITempoAware.h"
#include "SamplePool.h"
#include "../dsp/TimeStretch.h"
#include <array>

namespace lockstep
{
    // StretchMachine — the Flex analog (DESIGN §29.2): independent pitch + tempo
    // playback of a pool buffer via the WSOLA TimeStretch voice (C1/C3). Unlike the
    // rate-based SamplerMachine (pitch=speed, the turntable), the Player decouples
    // them: `pitch` transposes without changing duration, and `timestretch=Tempo`
    // stretches the buffer to the project tempo using its stamped sourceBars — so a
    // captured loop stays in time as the BPM changes (what the looper's varispeed
    // self-play and the rate Sampler cannot do).
    //
    // Monophonic v1 (one stretch voice), gated like the Static machine
    // (hasInternalAmp suppresses the track ENVELOPE; level/pan come from CHANNEL).
    // A short anti-click fade gates note-on/off. Polyphony + AHDSR are future work.
    class StretchMachine : public IMachine, public ITempoAware
    {
    public:
        explicit StretchMachine(SamplePool& pool) : pool_(pool) {}

        static constexpr const char* kMachineId = "lockstep.stretch.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "STCH"; }

        void setTransport(const TransportInfo& t) noexcept override { transport_ = t; }

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

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

        [[nodiscard]] bool hasInternalAmp() const override { return true; }
        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V1;
        }

    private:
        static constexpr int kSlotSampleId = 0;
        static constexpr int kSlotPitch = 1;        // ±24 semitones (independent)
        static constexpr int kSlotTimestretch = 2;  // 0 = Off (native), 1 = Tempo
        static constexpr int kSlotStart = 3;        // trim 0..1
        static constexpr int kNumSlots = 4;

        [[nodiscard]] double timeRatioFor(int playedLen) const;
        void startNote(int midiNote, const ParamFrame& params);

        SamplePool& pool_;
        double sampleRate_ = 44100.0;
        TimeStretch ts_;
        TransportInfo transport_{};

        bool playing_ = false;
        int activeNote_ = -1;
        int activeSampleId_ = -1; // pool index of the playing buffer
        int playedLen_ = 0;       // played region length (samples) at note-on
        int tsMode_ = 1;          // resolved timestretch mode for the active note
        float gain_ = 0.0f;       // anti-click gate gain
        float fadeInc_ = 0.0f;    // per-sample gate ramp

        static constexpr std::array<const char* const, 2> kTsLabels = { "Off", "Tempo" };
    };
}
