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

    void SamplerMachine::startVoice(int midiNote, const ParamFrame& params)
    {
        const auto msToSamples = [this](float ms) {
            return static_cast<int>(static_cast<double>(ms) * 0.001 * sampleRate_);
        };

        const double pitchOffset = static_cast<double>(
            params[static_cast<std::size_t>(kSlotPitch)]);
        const double semitones   = static_cast<double>(midiNote - 60) + pitchOffset;

        voice_.active         = true;
        voice_.position       = 0.0;
        voice_.sampleIndex    = static_cast<int>(params[static_cast<std::size_t>(kSlotSampleId)]);
        voice_.level          = params[static_cast<std::size_t>(kSlotLevel)];
        voice_.rate           = std::pow(2.0, semitones / 12.0);
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

    void SamplerMachine::triggerVoice(int midiNote, const ParamFrame& params)
    {
        if (voice_.active)
        {
            // Voice is busy: arm pending and start (or extend) the choke fade.
            pendingNote_       = midiNote;
            pendingParams_     = params;
            hasPendingTrigger_ = true;
            if (!choke_.isFading())
                choke_.trigger();
            return;
        }
        startVoice(midiNote, params);
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

    void SamplerMachine::process(const juce::MidiBuffer& events,
                                 const ParamFrame& params,
                                 juce::AudioBuffer<float>& buffer)
    {
        // Scan events: last note-on wins (monophonic), first note-off triggers release.
        int triggerAt  = -1;
        int triggerNote = 60;
        int releaseAt  = -1;
        for (const auto& meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                triggerAt   = meta.samplePosition;
                triggerNote = msg.getNoteNumber();
            }
            else if (msg.isNoteOff() && releaseAt < 0)
                releaseAt = meta.samplePosition;
        }

        const int numBlockSamples = buffer.getNumSamples();
        if (triggerAt >= 0)
            triggerAt = std::clamp(triggerAt, 0, numBlockSamples - 1);
        if (releaseAt >= 0)
            releaseAt = std::clamp(releaseAt, 0, numBlockSamples - 1);

        if (!voice_.active && !choke_.isFading() && !hasPendingTrigger_ && triggerAt < 0)
            return;

        // Sample pointer is resolved after each trigger so sampleIndex is current.
        const Sample* sample = voice_.active ? pool_.get(voice_.sampleIndex) : nullptr;

        const int numOut = buffer.getNumChannels();

        for (int i = 0; i < numBlockSamples; ++i)
        {
            // Note-off: if sustaining, start the release phase.
            if (releaseAt >= 0 && i == releaseAt)
            {
                if (voice_.active && voice_.stage == Stage::Sustain)
                    advanceStage(voice_);
                releaseAt = -1;
            }

            // Note-on: trigger the voice at the scheduled offset.
            if (triggerAt >= 0 && i == triggerAt)
            {
                triggerVoice(triggerNote, params);
                if (!choke_.isFading())
                {
                    sample = pool_.get(voice_.sampleIndex);
                    if (sample == nullptr)
                    {
                        voice_.active = false;
                        break;
                    }
                }
                triggerAt = -1;
            }

            // Apply choke fade; start the pending voice once the fade completes.
            const float chokeGain = choke_.isFading() ? choke_.nextGain() : 1.0f;
            if (!choke_.isFading() && hasPendingTrigger_)
            {
                hasPendingTrigger_ = false;
                startVoice(pendingNote_, pendingParams_);
                sample = pool_.get(voice_.sampleIndex);
                if (sample == nullptr)
                {
                    voice_.active = false;
                    break;
                }
            }

            if (!voice_.active)
            {
                if (triggerAt < 0)
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

    bool SamplerMachine::isVoiceActive() const
    {
        return voice_.active || choke_.isFading() || hasPendingTrigger_;
    }

    // -------------------------------------------------------------------------

    ParamSpec SamplerMachine::paramSpec(int index) const
    {
        using U = ParamSpec::Unit;
        using R = ParamSpec::Role;
        switch (index)
        {
        // Section 0 "Source"
        case kSlotSampleId: return { "sample_id", "Sample",   0.0f,    63.0f,   0.0f, true,  U::None,      0, R::None    };
        case kSlotPitch:    return { "pitch",      "Pitch",  -24.0f,   24.0f,   0.0f, false, U::Semitones, 0, R::Pitch   };
        case kSlotLevel:    return { "level",      "Level",   0.0f,     1.0f,   1.0f, false, U::Percent,   0, R::Level   };
        // Section 1 "Env"
        case kSlotAttack:   return { "attack",     "Attack",  0.0f,  5000.0f,   2.0f, false, U::Ms,        1, R::Attack  };
        case kSlotHold:     return { "hold",       "Hold",    0.0f,  2000.0f,   0.0f, false, U::Ms,        1, R::Hold    };
        case kSlotDecay:    return { "decay",      "Decay",   0.0f,  5000.0f, 500.0f, false, U::Ms,        1, R::Decay   };
        case kSlotSustain:  return { "sustain",    "Sustain", 0.0f,     1.0f,   0.5f, false, U::Percent,   1, R::Sustain };
        case kSlotRelease:  return { "release",    "Release", 0.0f,  5000.0f, 200.0f, false, U::Ms,        1, R::Release };
        default:            return {};
        }
    }

    // -------------------------------------------------------------------------

    SectionInfo SamplerMachine::section(int index) const
    {
        switch (index)
        {
        case 0: return { "Source" };
        case 1: return { "Env"    };
        default: return {};
        }
    }
}
