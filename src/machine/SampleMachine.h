#pragma once

#include "SamplePlayingMachineBase.h"

namespace lockstep
{
    class SampleMachine : public SamplePlayingMachineBase
    {
    public:
        explicit SampleMachine(SamplePool& pool);
        ~SampleMachine() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "SMPL"; }
        static constexpr const char* kMachineId = "lockstep.sample.v1";

        static constexpr int kNumSlots = 18;

        int numParams() const override { return kNumSlots; }
        ParamSpec paramSpec(int index) const override;
        int numSections() const override { return kNumSections; }
        SectionInfo section(int index) const override;

        bool hasInternalAmp() const override { return true; }

        // ISliceable detectTransientSlices is inherited (stub from base, wired in step 7).

    private:
        // Dense slot layout — Section 1 "SRC"
        static constexpr int kSlotSampleId = 0;
        static constexpr int kSlotPitch = 1;
        static constexpr int kSlotStart = 2;  // normalised [0..1], ZC-snap
        static constexpr int kSlotLength = 3;  // normalised (0..1], ZC-snap

        static constexpr int kSlotLoopMode = 4;  // 0=Off, 1=Sust, 2=S+R, 3=All
        static constexpr int kSlotLoopStart = 5;  // normalised [0..1], ZC-snap
        static constexpr int kSlotLoopLen = 6;  // normalised (0..1], ZC-snap

        // Section 3 "AMP" — level + internal AHDSR
        static constexpr int kSlotLevel = 7;
        static constexpr int kSlotAttack = 8;
        static constexpr int kSlotHold = 9;
        static constexpr int kSlotDecay = 10;
        static constexpr int kSlotSustain = 11;
        static constexpr int kSlotRelease = 12;
        static constexpr int kSlotRetrig = 13;  // 0=LEGATO 1=RETRIG 2=FREE
        static constexpr int kSlotVelSens = 14; // velocity → level scaling 0..1 (default 0 = off)
        static constexpr int kSlotLoopXfade = 15; // loop-seam crossfade, ms (0 = hard wrap)
        // 9.23 S7 — SRC fine-tune + A440 mode (appended to keep slot indices stable).
        static constexpr int kSlotTune = 16;     // ±50 cents
        static constexpr int kSlotTuneMode = 17; // 0 = Auto (cancel detected deviation), 1 = Raw

        static constexpr int kNumSections = 4;

        [[nodiscard]] SamplePlayer::Spec buildSpec(int midiNote, float velocity,
                                                   const ParamFrame& params) const;
        void triggerVoice(int midiNote, float velocity, const ParamFrame& params);
        void startVoiceAtSlice(int sliceIndex, float velocity, const ParamFrame& params);
    };
}
