#pragma once

#include "../machine/IEffect.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    // Reverb using JUCE's built-in Reverb algorithm — Room / Damping / Mix / Width.
    class ReverbEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            reverb_.setSampleRate(sampleRate);
            reverb_.reset();
            (void)maxBlockSize;
        }

        void reset() override { reverb_.reset(); }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            juce::Reverb::Parameters p;
            p.roomSize = params.size() > 0 ? params[0] : 0.5f;
            p.damping = params.size() > 1 ? params[1] : 0.5f;
            p.wetLevel = params.size() > 2 ? params[2] : 0.3f;
            p.dryLevel = 1.0f - p.wetLevel;
            p.width = params.size() > 3 ? params[3] : 1.0f;
            p.freezeMode = 0.0f;
            reverb_.setParameters(p);

            if (ch >= 2)
            {
                reverb_.processStereo(buffer.getWritePointer(0),
                                      buffer.getWritePointer(1),
                                      numSamples);
            }
            else
            {
                reverb_.processMono(buffer.getWritePointer(0), numSamples);
            }
        }

        int numParams() const override { return 4; }

        ParamSpec paramSpec(int i) const override
        {
            static constexpr int kFxSec = 5;
            switch (i)
            {
                case 0: {
                    ParamSpec p;
                    p.id = "lockstep.verb.room";
                    p.label = "Room";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.5f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 1: {
                    ParamSpec p;
                    p.id = "lockstep.verb.damp";
                    p.label = "Damp";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.5f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 2: {
                    ParamSpec p;
                    p.id = "lockstep.verb.mix";
                    p.label = "Mix";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.3f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 3: {
                    ParamSpec p;
                    p.id = "lockstep.verb.width";
                    p.label = "Width";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                default: return {};
            }
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "REV"; }

    private:
        static inline const std::string kId = "lockstep.reverb.v1";
        juce::Reverb reverb_;
    };
}
