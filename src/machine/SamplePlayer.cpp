#include "SamplePlayer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace lockstep
{
    void SamplePlayer::trigger(const Spec& spec)
    {
        sampleIndex = spec.sampleIndex;
        windowStart = spec.windowStart;
        windowEnd = spec.windowEnd;
        rate = spec.rate;
        // Reverse playback starts from the far end of the window.
        position = (spec.rate < 0.0 && spec.windowEnd > 0.0)
                       ? spec.windowEnd - 1.0
                       : spec.positionStart;
        level = spec.level;
        loopStart = spec.loopStart;
        loopEnd = spec.loopEnd;
        loopMode = spec.loopMode;
        attackSamples = spec.attackSamples;
        holdSamples = spec.holdSamples;
        decaySamples = spec.decaySamples;
        sustainLevel = spec.sustainLevel;
        releaseSamples = spec.releaseSamples;
        envLevel = 0.0f;
        active = true;
        stage = Stage::Attack;

        if (attackSamples == 0)
        {
            envLevel = 1.0f;
            advanceStage();
        }
        else
        {
            stageRemaining = attackSamples;
        }
    }

    void SamplePlayer::release()
    {
        if (stage == Stage::Idle || stage == Stage::Release)
            return;
        // Jump directly to Release from any active stage so note-off works
        // even when the envelope hasn't reached Sustain yet (e.g. long Decay).
        releaseStartLevel = envLevel;
        stage = Stage::Release;
        if (releaseSamples > 0)
            stageRemaining = releaseSamples;
        else
            advanceStage();  // instant release → Idle
    }

    void SamplePlayer::advanceStage()
    {
        bool done = false;
        while (!done)
        {
            switch (stage)
            {
                case Stage::Attack:
                    envLevel = 1.0f;
                    stage = Stage::Hold;
                    if (holdSamples > 0)
                    {
                        stageRemaining = holdSamples;
                        done = true;
                    }
                    break;

                case Stage::Hold:
                    stage = Stage::Decay;
                    if (decaySamples > 0)
                    {
                        stageRemaining = decaySamples;
                        done = true;
                    }
                    break;

                case Stage::Decay:
                    envLevel = sustainLevel;
                    stage = Stage::Sustain;
                    stageRemaining = std::numeric_limits<int>::max();
                    done = true;
                    break;

                case Stage::Sustain:
                    releaseStartLevel = envLevel;
                    stage = Stage::Release;
                    if (releaseSamples > 0)
                    {
                        stageRemaining = releaseSamples;
                        done = true;
                    }
                    break;

                case Stage::Release:
                    envLevel = 0.0f;
                    stage = Stage::Idle;
                    active = false;
                    done = true;
                    break;

                case Stage::Idle:
                    done = true;
                    break;
            }
        }
    }

    float SamplePlayer::nextEnvSample()
    {
        const float out = envLevel;

        switch (stage)
        {
            case Stage::Attack:
                envLevel += 1.0f / static_cast<float>(attackSamples);
                if (--stageRemaining <= 0) advanceStage();
                break;

            case Stage::Hold:
                if (--stageRemaining <= 0) advanceStage();
                break;

            case Stage::Decay:
                envLevel -= (1.0f - sustainLevel) / static_cast<float>(decaySamples);
                envLevel = std::max(envLevel, sustainLevel);
                if (--stageRemaining <= 0) advanceStage();
                break;

            case Stage::Sustain:
                break;

            case Stage::Release:
                if (releaseSamples > 0)
                    envLevel -= releaseStartLevel / static_cast<float>(releaseSamples);
                envLevel = std::max(envLevel, 0.0f);
                if (--stageRemaining <= 0) advanceStage();
                break;

            case Stage::Idle:
                break;
        }

        return out;
    }

    float SamplePlayer::step(const juce::AudioBuffer<float>& pcm)
    {
        if (!active || stage == Stage::Idle)
            return 0.0f;

        const float env = nextEnvSample();

        // During Release (no-loop case) and Idle, yield only the envelope fade —
        // the sampler mutes audio once the sample position is exhausted.
        // Modes SustAndRel and All continue reading audio during Release.
        const bool readAudio = (stage != Stage::Release && stage != Stage::Idle) || loopMode == LoopMode::SustAndRel || loopMode == LoopMode::All;

        float audioOut = 0.0f;

        if (readAudio)
        {
            const int numSrc = pcm.getNumSamples();
            const double effEnd = (windowEnd > 0.0) ? windowEnd
                                                    : static_cast<double>(numSrc);
            const double effStart = windowStart;
            const bool reverse = (rate < 0.0);

            const int idx0 = static_cast<int>(position);
            if (idx0 >= 0 && idx0 < numSrc)
            {
                if (reverse)
                {
                    // For reverse: interpolate between idx0 and idx0-1.
                    const int idx1 = std::max(idx0 - 1, 0);
                    const float frac = static_cast<float>(
                        position - static_cast<double>(idx0));
                    audioOut = pcm.getSample(0, idx0) * (1.0f - frac) + pcm.getSample(0, idx1) * frac;
                }
                else
                {
                    const int idx1 = std::min(idx0 + 1, numSrc - 1);
                    const float frac = static_cast<float>(
                        position - static_cast<double>(idx0));
                    audioOut = pcm.getSample(0, idx0) * (1.0f - frac) + pcm.getSample(0, idx1) * frac;
                }

                position += rate;

                // Loop handling
                const bool loopActive =
                    (loopMode == LoopMode::Sust && stage == Stage::Sustain) || (loopMode == LoopMode::SustAndRel && (stage == Stage::Sustain || stage == Stage::Release)) || (loopMode == LoopMode::All);

                if (reverse)
                {
                    if (loopActive && loopEnd > loopStart && position < loopStart)
                    {
                        const double span = loopEnd - loopStart;
                        position = loopEnd - std::fmod(loopStart - position, span);
                    }
                    else if (position < effStart)
                    {
                        if (stage == Stage::Sustain)
                            advanceStage();
                        else
                        {
                            position = effStart;
                        }
                    }
                }
                else
                {
                    if (loopActive && loopEnd > loopStart && position >= loopEnd)
                    {
                        position = loopStart + std::fmod(position - loopStart,
                                                         loopEnd - loopStart);
                    }
                    else if (position >= effEnd)
                    {
                        if (stage == Stage::Sustain)
                            advanceStage();
                        else if (stage == Stage::Release || stage == Stage::Attack || stage == Stage::Hold || stage == Stage::Decay)
                        {
                            position = effEnd;
                        }
                    }
                }
            }
            else if (stage == Stage::Sustain)
            {
                advanceStage();
            }
        }

        return audioOut * env * level;
    }
}
