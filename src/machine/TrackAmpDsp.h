#pragma once

#include <algorithm>
#include <limits>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../core/TrackAmpState.h"

namespace lockstep
{
    // Audio-thread AMP DSP state — one instance per track in the processor.
    // Applies Level, Pan, and a retriggerable AHDSR envelope (or held-open gate)
    // to a per-track scratch buffer, responding to MIDI note-on/off events.
    struct TrackAmpDsp
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
        int noteCount_ = 0;  // polyphonic hold count

        // Envelope parameters cached at note-on (sample counts / levels).
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

        void noteOn(const TrackAmpState& amp) noexcept
        {
            ++noteCount_;
            if (noteCount_ > 1)
                return;  // already sounding — stay in current stage, no retrigger

            const auto msToS = [this](float ms) {
                return static_cast<int>(ms * 0.001f * sampleRate_);
            };
            attSamples_ = msToS(amp.attack);
            hldSamples_ = msToS(amp.hold);
            decSamples_ = msToS(amp.decay);
            sustainLevel_ = juce::jlimit(0.0f, 1.0f, amp.sustain);
            relSamples_ = msToS(amp.release);

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
            if (noteCount_ > 0) return;  // other notes still held

            // All notes released — start release from wherever the envelope is.
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

        void processBlock(juce::AudioBuffer<float>& buf,
                          const juce::MidiBuffer& midi,
                          const TrackAmpState& amp,
                          int numSamples) noexcept
        {
            // Pre-scan for note-on/off positions (last note-on wins; first note-off wins)
            int noteOnAt = -1, noteOffAt = -1;
            for (const auto& meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn()) noteOnAt = meta.samplePosition;
                if (msg.isNoteOff() && noteOffAt < 0) noteOffAt = meta.samplePosition;
            }
            if (noteOnAt >= 0) noteOnAt = std::clamp(noteOnAt, 0, numSamples - 1);
            if (noteOffAt >= 0) noteOffAt = std::clamp(noteOffAt, 0, numSamples - 1);

            const bool heldOpen = (amp.gateSrc >= 0.5f);

            // Pan: linear constant-power approximation
            const float pan = juce::jlimit(-1.0f, 1.0f, amp.pan);
            const float panL = 1.0f - std::max(pan, 0.0f);
            const float panR = 1.0f + std::min(pan, 0.0f);

            const int numCh = std::min(buf.getNumChannels(), 2);
            float* chL = (numCh >= 1) ? buf.getWritePointer(0) : nullptr;
            float* chR = (numCh >= 2) ? buf.getWritePointer(1) : nullptr;

            for (int n = 0; n < numSamples; ++n)
            {
                // Apply note events at their exact sample positions
                if (noteOffAt >= 0 && n == noteOffAt)
                {
                    noteOff();
                    noteOffAt = -1;
                }
                if (noteOnAt >= 0 && n == noteOnAt)
                {
                    noteOn(amp);
                    noteOnAt = -1;
                }

                const float env = heldOpen                  ? 1.0f
                                  : (stage_ == Stage::Idle) ? 0.0f
                                                            : nextEnvSample();

                if (chL != nullptr) chL[n] *= amp.level * env * panL;
                if (chR != nullptr) chR[n] *= amp.level * env * panR;
            }
        }
    };
}
