#pragma once

#include "../machine/IEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace lockstep
{
    // Master-bus safety limiter (9.24 S14) wrapping juce::dsp::Limiter — a
    // zero-lookahead two-stage limiter (no PDC, so it drops straight onto the
    // master with the rest of the zero-latency chain). Gain (drive in) / Ceiling /
    // Release. It is deliberately a *safety* limiter, not a lookahead brickwall
    // maximiser: with no lookahead a hard transient can momentarily overshoot the
    // ceiling by a fraction of a dB. A true lookahead brickwall is a future
    // PDC-milestone item. master-only. id lockstep.limiter.v1.
    class LimiterEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;

    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            juce::dsp::ProcessSpec spec{ sampleRate,
                static_cast<juce::uint32>(std::max(1, maxBlockSize)), 2 };
            limiter_.prepare(spec);
            reset();
        }

        void reset() override
        {
            limiter_.reset();
            gainZ_ = 1.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const float gainDb = params.size() > 0 ? juce::jlimit(-12.0f, 24.0f, params[0]) : 0.0f;
            const float ceilDb = params.size() > 1 ? juce::jlimit(-24.0f, 0.0f, params[1]) : -0.3f;
            const float relMs  = params.size() > 2 ? juce::jlimit(1.0f, 500.0f, params[2]) : 100.0f;

            const float gainTarget = std::pow(10.0f, gainDb / 20.0f);
            limiter_.setThreshold(ceilDb);
            limiter_.setRelease(relMs);

            const int nCh = std::min(ch, 2);
            // Smooth the input gain so a moved fader doesn't zipper.
            for (int c = 0; c < nCh; ++c)
            {
                auto* data = buffer.getWritePointer(c);
                float gz = gainZ_;
                for (int i = 0; i < numSamples; ++i)
                {
                    gz += smoothCoef_ * (gainTarget - gz);
                    data[i] *= gz;
                }
                if (c == nCh - 1) gainZ_ = gz;
            }

            juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                static_cast<std::size_t>(nCh), static_cast<std::size_t>(numSamples));
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            limiter_.process(ctx);

            for (int c = 2; c < ch; ++c)
                buffer.copyFrom(c, 0, buffer, std::min(c, 1), 0, numSamples);
        }

        int numParams() const override { return 3; }

        ParamSpec paramSpec(int i) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (i)
            {
                case 0:
                    p.id = "lockstep.limiter.gain";
                    p.label = "Gain";
                    p.minValue = -12.0f; p.maxValue = 24.0f; p.defaultValue = 0.0f;
                    break;
                case 1:
                    p.id = "lockstep.limiter.ceil";
                    p.label = "Ceiling";
                    p.minValue = -24.0f; p.maxValue = 0.0f; p.defaultValue = -0.3f;
                    break;
                default:
                    p.id = "lockstep.limiter.rel";
                    p.label = "Release";
                    p.minValue = 1.0f; p.maxValue = 500.0f; p.defaultValue = 100.0f;
                    p.skew = 0.3f;
                    break;
            }
            return p;
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "LIM"; }

    private:
        static inline const std::string kId = "lockstep.limiter.v1";
        double sr_ = 44100.0;
        float smoothCoef_ = 0.005f;
        float gainZ_ = 1.0f;
        juce::dsp::Limiter<float> limiter_;
    };
}
