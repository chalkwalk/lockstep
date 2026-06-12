#pragma once

#include <algorithm>
#include <limits>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../core/TrackEnvState.h"

namespace lockstep
{
    // Audio-thread envelope DSP — one instance per track (only instantiated for
    // tracks where !hasInternalAmp()). Applies a retriggerable AHDSR envelope
    // (or held-open gate) to a per-track scratch buffer, responding to MIDI
    // note-on/off events. Does not apply level/pan — those live in TrackChannelDsp.
    struct TrackEnvDsp
    {
        enum class Stage
        {
            Idle,
            Attack,
            Hold,
            Decay,
            Sustain,
            Release
        };

        float sampleRate_ = 44100.0f;
        Stage stage_ = Stage::Idle;
        float envLevel_ = 0.0f;
        float releaseStartLevel_ = 0.0f;
        int stageRemaining_ = 0;
        int noteCount_ = 0;

        int attSamples_ = 0;
        int hldSamples_ = 0;
        int decSamples_ = 0;
        float sustainLevel_ = 1.0f;
        int relSamples_ = 0;

        void prepare(double sampleRate) noexcept
        {
            sampleRate_ = static_cast<float>(sampleRate);
            reset();
        }

        void reset() noexcept
        {
            stage_ = Stage::Idle;
            envLevel_ = 0.0f;
            stageRemaining_ = 0;
            noteCount_ = 0;
        }

        bool isIdle() const noexcept { return stage_ == Stage::Idle; }

        void noteOn(const TrackEnvState& env) noexcept
        {
            ++noteCount_;
            if (noteCount_ > 1)
                return;

            const auto msToS = [this](float ms) {
                return static_cast<int>(ms * 0.001f * sampleRate_);
            };
            attSamples_ = msToS(env.attack);
            hldSamples_ = msToS(env.hold);
            decSamples_ = msToS(env.decay);
            sustainLevel_ = juce::jlimit(0.0f, 1.0f, env.sustain);
            relSamples_ = msToS(env.release);

            stage_ = Stage::Attack;
            envLevel_ = 0.0f;
            if (attSamples_ == 0)
            {
                envLevel_ = 1.0f;
                advanceStage();
            }
            else
            {
                stageRemaining_ = attSamples_;
            }
        }

        void noteOff() noexcept
        {
            if (noteCount_ > 0) --noteCount_;
            if (noteCount_ > 0) return;

            if (stage_ == Stage::Idle || stage_ == Stage::Release) return;
            releaseStartLevel_ = envLevel_;
            stage_ = Stage::Release;
            if (relSamples_ == 0)
            {
                envLevel_ = 0.0f;
                stage_ = Stage::Idle;
            }
            else
            {
                stageRemaining_ = relSamples_;
            }
        }

        void advanceStage() noexcept
        {
            bool done = false;
            while (!done)
            {
                switch (stage_)
                {
                    case Stage::Attack:
                        envLevel_ = 1.0f;
                        stage_ = Stage::Hold;
                        if (hldSamples_ > 0)
                        {
                            stageRemaining_ = hldSamples_;
                            done = true;
                        }
                        break;
                    case Stage::Hold:
                        stage_ = Stage::Decay;
                        if (decSamples_ > 0)
                        {
                            stageRemaining_ = decSamples_;
                            done = true;
                        }
                        break;
                    case Stage::Decay:
                        envLevel_ = sustainLevel_;
                        stage_ = Stage::Sustain;
                        stageRemaining_ = std::numeric_limits<int>::max();
                        done = true;
                        break;
                    case Stage::Sustain:
                        releaseStartLevel_ = envLevel_;
                        stage_ = Stage::Release;
                        if (relSamples_ > 0)
                        {
                            stageRemaining_ = relSamples_;
                            done = true;
                        }
                        break;
                    case Stage::Release:
                        envLevel_ = 0.0f;
                        stage_ = Stage::Idle;
                        done = true;
                        break;
                    case Stage::Idle:
                        done = true;
                        break;
                }
            }
        }

        float nextEnvSample() noexcept
        {
            const float level = envLevel_;
            switch (stage_)
            {
                case Stage::Attack:
                    envLevel_ += 1.0f / static_cast<float>(attSamples_);
                    if (--stageRemaining_ <= 0) advanceStage();
                    break;
                case Stage::Hold:
                    if (--stageRemaining_ <= 0) advanceStage();
                    break;
                case Stage::Decay:
                    envLevel_ -= (1.0f - sustainLevel_) / static_cast<float>(decSamples_);
                    envLevel_ = std::max(envLevel_, sustainLevel_);
                    if (--stageRemaining_ <= 0) advanceStage();
                    break;
                case Stage::Sustain:
                    break;
                case Stage::Release:
                    if (relSamples_ > 0)
                        envLevel_ -= releaseStartLevel_ / static_cast<float>(relSamples_);
                    envLevel_ = std::max(envLevel_, 0.0f);
                    if (--stageRemaining_ <= 0) advanceStage();
                    break;
                case Stage::Idle:
                    break;
            }
            return level;
        }

        // Apply the envelope (and gate source) to buf in-place.
        // Does NOT apply level or pan — those are handled by TrackChannelDsp.
        void processBlock(juce::AudioBuffer<float>& buf,
                          const juce::MidiBuffer& midi,
                          const TrackEnvState& env,
                          int numSamples) noexcept
        {
            int noteOnAt = -1, noteOffAt = -1;
            for (const auto& meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn()) noteOnAt = meta.samplePosition;
                if (msg.isNoteOff() && noteOffAt < 0) noteOffAt = meta.samplePosition;
            }
            if (noteOnAt >= 0) noteOnAt = std::clamp(noteOnAt, 0, numSamples - 1);
            if (noteOffAt >= 0) noteOffAt = std::clamp(noteOffAt, 0, numSamples - 1);

            const bool heldOpen = (env.gateSrc >= 0.5f);
            const int numCh = buf.getNumChannels();

            for (int n = 0; n < numSamples; ++n)
            {
                if (noteOffAt >= 0 && n == noteOffAt)
                {
                    noteOff();
                    noteOffAt = -1;
                }
                if (noteOnAt >= 0 && n == noteOnAt)
                {
                    noteOn(env);
                    noteOnAt = -1;
                }

                const float envVal = heldOpen                  ? 1.0f
                                   : (stage_ == Stage::Idle)   ? 0.0f
                                                               : nextEnvSample();

                for (int ch = 0; ch < numCh; ++ch)
                    buf.getWritePointer(ch)[n] *= envVal;
            }
        }
    };
}
