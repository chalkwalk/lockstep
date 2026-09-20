#include "SamplePlayer.h"
#include "../deckcore/Interpolation.h"
#include "../deckcore/Resampler.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace lockstep
{
    namespace
    {
        // Shared bandlimited resampler (9.25 R3). read() is const, stateless, and
        // allocation-free, so one instance serves every voice; the kernel bank is
        // built once at static-init (off the audio thread). Used only for pitch-up
        // reads (rate > 1) where Hermite would alias; down-pitch stays on Hermite.
        const dc::Resampler& sharedResampler()
        {
            return dc::kernels();   // this project's guard, not the library's
        }
    }

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
        xfadeSamples = spec.xfadeSamples;
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

        // Read audio in all non-Idle stages; the release envelope fades real sound.
        // The position >= effEnd branch below clamps and naturally silences when the
        // sample is exhausted.  loopActive (below) already excludes looping during
        // Release for non-SustAndRel/All modes, so the tail plays straight through.
        const bool readAudio = (stage != Stage::Idle);

        float audioOut = 0.0f;

        if (readAudio)
        {
            const int numSrc = pcm.getNumSamples();
            const double effEnd = (windowEnd > 0.0) ? windowEnd
                                                    : static_cast<double>(numSrc);
            const double effStart = windowStart;
            const bool reverse = (rate < 0.0);

            // loopActive gates whether the loop bounds engage in this stage.
            const bool loopActive =
                (loopMode == LoopMode::Sust && stage == Stage::Sustain) || (loopMode == LoopMode::SustAndRel && (stage == Stage::Sustain || stage == Stage::Release)) || (loopMode == LoopMode::All);

            // Interpolated read of the continuous waveform at fractional `pos`.
            // Direction-agnostic — sampling the reconstructed waveform at `pos` is
            // the same whether the voice plays forward or reverse. Outer neighbour
            // indices are clamped at the buffer edges.
            //
            // R3 (9.25): reading faster than unity (pitch-up) images the source
            // above the destination Nyquist and folds it back as aliasing, worst on
            // bright samples pitched up. For |rate| > 1 route through the shared
            // bandlimited resampler, whose cutoff tracks the rate. At or below unity
            // Hermite is clean (anti-imaging only) and cheaper, so keep it there.
            const double readRate = rate;
            const float* src0 = pcm.getReadPointer(0);
            auto readInterpFwd = [&pcm, src0, numSrc, readRate](double pos) -> float {
                const int i0 = static_cast<int>(pos);
                if (i0 < 0 || i0 >= numSrc)
                    return 0.0f;
                if (std::abs(readRate) > 1.0)
                    return sharedResampler().read(src0, numSrc, pos, readRate);
                const auto clamp = [numSrc](int i) {
                    return juce::jlimit(0, numSrc - 1, i);
                };
                const float ym1 = pcm.getSample(0, clamp(i0 - 1));
                const float y0  = pcm.getSample(0, clamp(i0));
                const float y1  = pcm.getSample(0, clamp(i0 + 1));
                const float y2  = pcm.getSample(0, clamp(i0 + 2));
                const float fr  = static_cast<float>(pos - static_cast<double>(i0));
                return dc::hermite4(ym1, y0, y1, y2, fr);
            };

            const int idx0 = static_cast<int>(position);
            if (idx0 >= 0 && idx0 < numSrc)
            {
                if (reverse)
                {
                    // Reverse: sample the reconstructed waveform at `position` with
                    // the same Hermite read as forward (direction-agnostic). This
                    // also removes the old linear read's ~1-sample bias. Reverse-
                    // seam declick remains out of scope; keep the hard wrap here.
                    audioOut = readInterpFwd(position);

                    position += rate;

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
                            position = effStart;
                    }
                }
                else
                {
                    // Forward. Optional loop-seam crossfade: blend the outgoing
                    // branch with an incoming branch reading from loopStart.
                    //   - Tail available (loopEnd + X <= effEnd): the outgoing
                    //     branch reads real tail material past loopEnd, so the loop
                    //     period stays exactly (loopEnd - loopStart).
                    //   - No tail: eat into the loop (outgoing reads the last X of
                    //     the loop); the effective period shortens by X.
                    const double span = loopEnd - loopStart;
                    const double X = (loopActive && span > 0.0)
                                         ? std::min(xfadeSamples, span * 0.5)
                                         : 0.0;

                    bool crossfading = false;
                    double xfBegin = 0.0;
                    double wrapSub = 0.0;
                    if (X > 0.0)
                    {
                        const bool tail = (loopEnd + X <= effEnd);
                        xfBegin = tail ? loopEnd : (loopEnd - X);
                        wrapSub = tail ? span : (span - X);
                        crossfading = (position >= xfBegin);
                    }

                    if (crossfading)
                    {
                        constexpr double kHalfPi = 1.5707963267948966;
                        const double t = std::min((position - xfBegin) / X, 1.0);
                        const double inPos = loopStart + (position - xfBegin);
                        const float a = std::cos(static_cast<float>(t * kHalfPi));
                        const float b = std::sin(static_cast<float>(t * kHalfPi));
                        audioOut = a * readInterpFwd(position) + b * readInterpFwd(inPos);
                    }
                    else
                    {
                        audioOut = readInterpFwd(position);
                    }

                    position += rate;

                    if (X > 0.0)
                    {
                        // Crossfade active: wrap only at the end of the crossfade
                        // window, never at loopEnd. For the borrow-tail case
                        // xfBegin == loopEnd, so hard-wrapping at loopEnd would skip
                        // the seam blend (the flag is still false one sample before);
                        // let position climb into the window instead.
                        if (position >= xfBegin + X)
                            position -= wrapSub;
                    }
                    else if (loopActive && loopEnd > loopStart && position >= loopEnd)
                    {
                        // Crossfade disabled (X == 0) — hard wrap (old behaviour).
                        position = loopStart + std::fmod(position - loopStart,
                                                         loopEnd - loopStart);
                    }
                    else if (position >= effEnd)
                    {
                        if (stage == Stage::Sustain)
                            advanceStage();
                        else
                            position = effEnd;
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
