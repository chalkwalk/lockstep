#pragma once

#include "IMachine.h"
#include "ISliceable.h"
#include "SamplePlayer.h"
#include "SamplePool.h"
#include "VoiceChoke.h"

namespace lockstep
{
    class SamplerMachine : public IMachine, public ISliceable
    {
    public:
        explicit SamplerMachine(SamplePool& pool);
        ~SamplerMachine() override;

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;
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

        bool isVoiceActive()  const override;
        bool hasInternalAmp() const override { return true; }

        // ISliceable — MG.3 slice data. Normalized start positions [0.0, 1.0].
        // Up to 16 slices; each maps to one step key in Slice sub-mode of Retrig.
        static constexpr int kMaxSlices = 16;
        [[nodiscard]] int numSlices()  const override { return numSlices_; }
        void setEqualSlices(int count)       override;
        void clearSlices()                   override;
        void detectTransientSlices()         override {} // wired in step 2

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

        static int msToSamples(float ms, double sampleRate)
        {
            return static_cast<int>(static_cast<double>(ms) * 0.001 * sampleRate);
        }

        SamplePlayer::Spec buildSpec(int midiNote, const ParamFrame& params) const;
        void triggerVoice(int midiNote, const ParamFrame& params);
        void startVoiceAtSlice(int sliceIndex, const ParamFrame& params);

        SamplePool& pool_;
        double      sampleRate_ = 0.0;
        SamplePlayer player_{};
        VoiceChoke  choke_{};
        bool        hasPendingTrigger_ = false;
        int         pendingNote_       = 60;
        ParamFrame  pendingParams_{};

        // MG.3: slice data (audio-thread only).
        std::array<float, kMaxSlices> slicePositions_{};
        int numSlices_ = 0;
    };
}
