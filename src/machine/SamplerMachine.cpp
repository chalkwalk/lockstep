#include "SamplerMachine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace lockstep
{
    SamplerMachine::SamplerMachine(SamplePool& pool) : pool_(pool) {}
    SamplerMachine::~SamplerMachine() = default;

    void SamplerMachine::prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        choke_.prepare(sampleRate_, 1.5f);
        juce::ignoreUnused(maxBlockSize);
    }

    void SamplerMachine::reset()
    {
        voice_             = Voice{};
        hasPendingTrigger_ = false;
        choke_.prepare(sampleRate_, 1.5f); // resets fadeRemaining
    }

    // -------------------------------------------------------------------------

    void SamplerMachine::startVoice(const ParamFrame& params)
    {
        const auto msToSamples = [this](float ms) {
            return static_cast<int>(static_cast<double>(ms) * 0.001 * sampleRate_);
        };

        voice_.active         = true;
        voice_.position       = 0.0;
        voice_.sampleIndex    = static_cast<int>(params[static_cast<std::size_t>(kSlotSampleId)]);
        voice_.level          = params[static_cast<std::size_t>(kSlotLevel)];
        voice_.rate           = std::pow(2.0, static_cast<double>(
                                    params[static_cast<std::size_t>(kSlotPitch)]) / 12.0);
        voice_.attackSamples  = msToSamples(params[static_cast<std::size_t>(kSlotAttack)]);
        voice_.holdSamples    = msToSamples(params[static_cast<std::size_t>(kSlotHold)]);
        voice_.decaySamples   = msToSamples(params[static_cast<std::size_t>(kSlotDecay)]);
        voice_.sustainLevel   = params[static_cast<std::size_t>(kSlotSustain)];
        voice_.releaseSamples = msToSamples(params[static_cast<std::size_t>(kSlotRelease)]);
        voice_.envLevel       = 0.0f;
        voice_.stage          = Stage::Attack;

        if (voice_.attackSamples == 0)
        {
            voice_.envLevel = 1.0f;
            advanceStage(voice_);
        }
        else
        {
            voice_.stageRemaining = voice_.attackSamples;
        }
    }

    void SamplerMachine::triggerVoice(const ParamFrame& params)
    {
        if (voice_.active)
        {
            // Voice is busy: arm pending and start (or extend) the choke fade.
            pendingParams_     = params;
            hasPendingTrigger_ = true;
            if (!choke_.isFading())
                choke_.trigger();
            return;
        }
        startVoice(params);
    }

    void SamplerMachine::advanceStage(Voice& v)
    {
        // Walk through zero-duration stages immediately.
        bool done = false;
        while (!done)
        {
            switch (v.stage)
            {
            case Stage::Attack:
                v.envLevel = 1.0f;
                v.stage = Stage::Hold;
                if (v.holdSamples > 0) { v.stageRemaining = v.holdSamples; done = true; }
                break;

            case Stage::Hold:
                v.stage = Stage::Decay;
                if (v.decaySamples > 0) { v.stageRemaining = v.decaySamples; done = true; }
                break;

            case Stage::Decay:
                v.envLevel = v.sustainLevel;
                v.stage = Stage::Sustain;
                v.stageRemaining = std::numeric_limits<int>::max();
                done = true;
                break;

            case Stage::Sustain:
                v.releaseStartLevel = v.envLevel;
                v.stage = Stage::Release;
                if (v.releaseSamples > 0) { v.stageRemaining = v.releaseSamples; done = true; }
                break;

            case Stage::Release:
                v.envLevel = 0.0f;
                v.stage = Stage::Idle;
                v.active = false;
                done = true;
                break;

            case Stage::Idle:
                done = true;
                break;
            }
        }
    }

    float SamplerMachine::nextEnvSample(Voice& v)
    {
        const float level = v.envLevel;

        switch (v.stage)
        {
        case Stage::Attack:
            v.envLevel += 1.0f / static_cast<float>(v.attackSamples);
            if (--v.stageRemaining <= 0)
                advanceStage(v);
            break;

        case Stage::Hold:
            if (--v.stageRemaining <= 0)
                advanceStage(v);
            break;

        case Stage::Decay:
            v.envLevel -= (1.0f - v.sustainLevel) / static_cast<float>(v.decaySamples);
            v.envLevel = std::max(v.envLevel, v.sustainLevel);
            if (--v.stageRemaining <= 0)
                advanceStage(v);
            break;

        case Stage::Sustain:
            break;

        case Stage::Release:
            if (v.releaseSamples > 0)
                v.envLevel -= v.releaseStartLevel / static_cast<float>(v.releaseSamples);
            v.envLevel = std::max(v.envLevel, 0.0f);
            if (--v.stageRemaining <= 0)
                advanceStage(v);
            break;

        case Stage::Idle:
            break;
        }

        return level;
    }

    // -------------------------------------------------------------------------

    void SamplerMachine::process(int triggerAtSample, const ParamFrame& params,
                                 juce::AudioBuffer<float>& buffer)
    {
        if (!voice_.active && !choke_.isFading() && !hasPendingTrigger_ && triggerAtSample < 0)
            return;

        // Sample pointer is resolved after each trigger so sampleIndex is current.
        const Sample* sample = voice_.active ? pool_.get(voice_.sampleIndex) : nullptr;

        const int numOut          = buffer.getNumChannels();
        const int numBlockSamples = buffer.getNumSamples();

        for (int i = 0; i < numBlockSamples; ++i)
        {
            if (triggerAtSample >= 0 && i == triggerAtSample)
            {
                triggerVoice(params);
                if (!choke_.isFading())
                {
                    // No choke needed (was idle): update sample pointer now.
                    sample = pool_.get(voice_.sampleIndex);
                    if (sample == nullptr)
                    {
                        voice_.active = false;
                        break;
                    }
                }
            }

            // Apply choke fade to current voice output; start pending once done.
            const float chokeGain = choke_.isFading() ? choke_.nextGain() : 1.0f;
            if (!choke_.isFading() && hasPendingTrigger_)
            {
                hasPendingTrigger_ = false;
                startVoice(pendingParams_);
                sample = pool_.get(voice_.sampleIndex);
                if (sample == nullptr)
                {
                    voice_.active = false;
                    break;
                }
            }

            if (!voice_.active)
            {
                if (triggerAtSample < 0 || i > triggerAtSample)
                    break;
                continue;
            }

            if (sample == nullptr)
                continue;

            const int numSrcSamples = sample->pcm.getNumSamples();

            const float env = nextEnvSample(voice_);

            float audioOut = 0.0f;
            if (voice_.stage != Stage::Release && voice_.stage != Stage::Idle)
            {
                const int idx0 = static_cast<int>(voice_.position);
                if (idx0 < numSrcSamples)
                {
                    const int idx1 = std::min(idx0 + 1, numSrcSamples - 1);
                    const float frac = static_cast<float>(
                        voice_.position - static_cast<double>(idx0));
                    audioOut = sample->pcm.getSample(0, idx0) * (1.0f - frac)
                             + sample->pcm.getSample(0, idx1) * frac;
                    voice_.position += voice_.rate;

                    if (voice_.position >= static_cast<double>(numSrcSamples)
                        && voice_.stage == Stage::Sustain)
                        advanceStage(voice_);
                }
                else if (voice_.stage == Stage::Sustain)
                {
                    advanceStage(voice_);
                }
            }

            const float out = audioOut * env * voice_.level * chokeGain;
            for (int ch = 0; ch < numOut; ++ch)
                buffer.addSample(ch, i, out);
        }
    }

    // -------------------------------------------------------------------------

    ParamMetadata SamplerMachine::getParamMetadata(int slot) const
    {
        switch (slot)
        {
        case kSlotSampleId: return { "Sample",  0.0f,   127.0f,  0.0f, true  };
        case kSlotPitch:    return { "Pitch",  -24.0f,   24.0f,  0.0f, false };
        case kSlotLevel:    return { "Level",   0.0f,    1.0f,   1.0f, false };
        case kSlotAttack:   return { "Attack",  0.0f, 5000.0f,   2.0f, false };
        case kSlotHold:     return { "Hold",    0.0f, 2000.0f,   0.0f, false };
        case kSlotDecay:    return { "Decay",   0.0f, 5000.0f, 500.0f, false };
        case kSlotSustain:  return { "Sustain", 0.0f,    1.0f,   0.5f, false };
        case kSlotRelease:  return { "Release", 0.0f, 5000.0f, 200.0f, false };
        default:            return {};
        }
    }
}
