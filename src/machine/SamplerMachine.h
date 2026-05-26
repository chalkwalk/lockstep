#pragma once

#include "SamplePlayingMachineBase.h"

namespace lockstep
{
    class SamplerMachine : public SamplePlayingMachineBase
    {
    public:
        explicit SamplerMachine(SamplePool& pool);
        ~SamplerMachine() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge()     const noexcept override { return "SP"; }
        static constexpr const char* kMachineId = "lockstep.sampler.v1";

        int         numParams()          const override { return kNumSlots; }
        ParamSpec   paramSpec(int index) const override;
        int         numSections()        const override { return kNumSections; }
        SectionInfo section(int index)   const override;

        bool hasInternalAmp() const override { return true; }

        // ISliceable detectTransientSlices is inherited (stub from base, wired in step 7).

    private:
        // Dense slot layout — Section 1 "SRC"
        static constexpr int kSlotSampleId  = 0;
        static constexpr int kSlotPitch     = 1;

        // Section 3 "AMP" — level + internal AHDSR
        static constexpr int kSlotLevel     = 2;
        static constexpr int kSlotAttack    = 3;
        static constexpr int kSlotHold      = 4;
        static constexpr int kSlotDecay     = 5;
        static constexpr int kSlotSustain   = 6;
        static constexpr int kSlotRelease   = 7;

        static constexpr int kNumSlots    = 8;
        static constexpr int kNumSections = 4;

        [[nodiscard]] SamplePlayer::Spec buildSpec(int midiNote,
                                                   const ParamFrame& params) const;
        void triggerVoice(int midiNote, const ParamFrame& params);
        void startVoiceAtSlice(int sliceIndex, const ParamFrame& params);
    };
}
