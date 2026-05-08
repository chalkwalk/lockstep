#pragma once

#include "IMachine.h"
#include "SamplePool.h"

namespace lockstep
{
    class SamplerMachine : public IMachine
    {
    public:
        explicit SamplerMachine(SamplePool& pool);
        ~SamplerMachine() override;

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;
        void process(int triggerAtSample, const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;
        ParamMetadata getParamMetadata(int slot) const override;

    private:
        // Slot assignments (Page 0 = sample, Page 1+2 = envelope)
        static constexpr int kSlotSampleId  = 0;
        static constexpr int kSlotPitch     = 1;
        static constexpr int kSlotLevel     = 2;
        static constexpr int kSlotAttack    = 4;
        static constexpr int kSlotHold      = 5;
        static constexpr int kSlotDecay     = 6;
        static constexpr int kSlotSustain   = 7;
        static constexpr int kSlotRelease   = 8;

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

        void  triggerVoice(const ParamFrame& params);
        void  advanceStage(Voice& v);
        float nextEnvSample(Voice& v);

        SamplePool& pool_;
        double sampleRate_ = 0.0;
        Voice  voice_;
    };
}
