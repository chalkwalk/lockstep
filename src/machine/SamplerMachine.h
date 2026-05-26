#pragma once

#include "IMachine.h"
#include "ISliceable.h"
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

        // Schema — dense indices 0..kNumSlots-1
        int       numParams()          const override { return kNumSlots; }
        ParamSpec paramSpec(int index) const override;
        int       numSections()        const override { return kNumSections; }
        SectionInfo section(int index) const override;

        bool isVoiceActive()    const override;
        bool hasInternalAmp()   const override { return true; }

        // ISliceable — MG.3 slice data.  Normalized start positions [0.0, 1.0].
        // Up to 16 slices; each maps to one step key in Slice sub-mode of Retrig.
        static constexpr int kMaxSlices = 16;
        [[nodiscard]] int numSlices()  const override { return numSlices_; }
        void setEqualSlices(int count)           override;
        void clearSlices()                       override;
        void detectTransientSlices()             override {} // wired in step 2

    private:
        // Dense slot layout — Section 0 "Source"
        static constexpr int kSlotSampleId  = 0;
        static constexpr int kSlotPitch     = 1;  // fine-tune semitone offset
        static constexpr int kSlotLevel     = 2;

        // Section 1 "Env"
        static constexpr int kSlotAttack    = 3;
        static constexpr int kSlotHold      = 4;
        static constexpr int kSlotDecay     = 5;
        static constexpr int kSlotSustain   = 6;
        static constexpr int kSlotRelease   = 7;

        static constexpr int kNumSlots    = 8;
        // Canonical section indices: SRC=1 (sample_id, pitch), AMP=3 (level + envelope).
        // numSections() returns 4 (highest index used + 1) so the renderer checks all 0..3.
        static constexpr int kNumSections = 4;

        enum class Stage { Idle, Attack, Hold, Decay, Sustain, Release };

        struct Voice {
            bool   active  = false;
            Stage  stage   = Stage::Idle;
            double position = 0.0;
            double rate     = 1.0;
            int    sampleIndex = -1;
            float  level       = 1.0f;
            float  envLevel    = 0.0f;
            float  releaseStartLevel = 0.0f;
            int    stageRemaining = 0;
            int    attackSamples  = 0;
            int    holdSamples    = 0;
            int    decaySamples   = 0;
            float  sustainLevel   = 0.5f;
            int    releaseSamples = 0;
        };

        void  triggerVoice(int midiNote, const ParamFrame& params);
        void  startVoice(int midiNote, const ParamFrame& params);
        void  advanceStage(Voice& v);
        float nextEnvSample(Voice& v);

        void startVoiceAtSlice(int sliceIndex, const ParamFrame& params);

        SamplePool& pool_;
        double      sampleRate_ = 0.0;
        Voice       voice_;
        VoiceChoke  choke_;
        bool        hasPendingTrigger_ = false;
        int         pendingNote_   = 60;
        ParamFrame  pendingParams_{};

        // MG.3: slice data (audio-thread only).
        std::array<float, kMaxSlices> slicePositions_{};  // normalized [0.0, 1.0]
        int numSlices_ = 0;
    };
}
