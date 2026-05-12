#pragma once

#include "IMachine.h"
#include "SamplePool.h"
#include "VoiceChoke.h"

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
        int           numTrackSections() const override;
        SectionInfo   trackSection(int index) const override;

        int pitchSlot()        const override { return kSlotPitch; }
        int sampleSelectSlot() const override { return kSlotSampleId; }
        int gateSlot()         const override { return kSlotGate; }

    private:
        // Section 0 "Source" — page 0 (slots 0–3)
        static constexpr int kSlotSampleId  = 0;
        static constexpr int kSlotPitch     = 1;  // note slot: semitone offset from MIDI 60
        static constexpr int kSlotLevel     = 2;
        static constexpr int kSlotGate      = 3;  // gate length ms; 0 = hold until retrigger
        // Section 0 page 1 (slots 4–7): spare

        // Section 1 "Env" — page 0 (slots 8–11), page 1 slot 12
        static constexpr int kSlotAttack    = 8;
        static constexpr int kSlotHold      = 9;
        static constexpr int kSlotDecay     = 10;
        static constexpr int kSlotSustain   = 11;
        static constexpr int kSlotRelease   = 12;
        // Slots 13–47: spare (sections 1 page 1 tail + sections 2–5)

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

        void  triggerVoice(const ParamFrame& params); // routes to startVoice or arms choke
        void  startVoice(const ParamFrame& params);   // unconditional voice init
        void  advanceStage(Voice& v);
        float nextEnvSample(Voice& v);

        SamplePool& pool_;
        double      sampleRate_ = 0.0;
        Voice       voice_;
        VoiceChoke  choke_;
        bool        hasPendingTrigger_ = false;
        ParamFrame  pendingParams_{};
    };
}
