#pragma once

#include "SamplePlayingMachineBase.h"

namespace lockstep
{
    class SliceMachine : public SamplePlayingMachineBase
    {
    public:
        explicit SliceMachine(SamplePool& pool);
        ~SliceMachine() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "SLCE"; }
        static constexpr const char* kMachineId = "lockstep.slice.v1";

        static constexpr int kNumSlots = 15;

        int numParams() const override { return kNumSlots; }
        ParamSpec paramSpec(int index) const override;
        int numSections() const override { return kNumSections; }
        SectionInfo section(int index) const override;

        bool hasInternalAmp() const override { return false; }

        [[nodiscard]] Polyphony currentVoices(const ParamFrame& params) const override;

    private:
        // Section 1 "SRC"
        static constexpr int kSlotSampleId = 0;
        static constexpr int kSlotMode = 1;  // 0=SLICE, 1=SCRUB
        static constexpr int kSlotSliceSrc = 2;  // 0=EQUAL, 1=TRANS
        static constexpr int kSlotSliceCount = 3;  // 1..16
        static constexpr int kSlotRate = 4;  // -2.0..2.0
        static constexpr int kSlotStart = 5;  // normalised [0..1], ZC-snap
        static constexpr int kSlotLength = 6;  // normalised (0..1], ZC-snap
        static constexpr int kSlotLoopMode = 7;  // 0=Off 1=Sust 2=S+R 3=All
        static constexpr int kSlotLoopStart = 8;  // normalised [0..1], ZC-snap
        static constexpr int kSlotLoopLen = 9;  // normalised (0..1], ZC-snap
        static constexpr int kSlotPitch = 10; // ±24 semitones

        // Extension section 6 "VOICE"
        static constexpr int kSlotVoiceMode = 11; // 0=MONO, 1=POLY
        static constexpr int kSlotFade = 12; // 0..20 ms anti-click fade
        // 9.23 S7 — SRC fine-tune + A440 mode (appended to keep slot indices stable).
        static constexpr int kSlotTune = 13;     // ±50 cents
        static constexpr int kSlotTuneMode = 14; // 0 = Auto, 1 = Raw

        static constexpr int kNumSections = 7;  // sections 1 + 6

        [[nodiscard]] SamplePlayer::Spec buildSpec(int midiNote,
                                                   const ParamFrame& params) const;
        void triggerVoice(int midiNote, const ParamFrame& params);
    };
}
