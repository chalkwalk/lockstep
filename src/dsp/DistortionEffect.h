#pragma once

#include "../machine/IEffect.h"
#include <cmath>

namespace lockstep
{
    // Soft-clip distortion — Drive / Tone (post-LPF) / Mix.
    // Per-sample smoothing on drive and mix (~5ms) to prevent zipper noise.
    class DistortionEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sampleRate_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            for (auto& z : lpfZ_) z = 0.0f;
            for (auto& z : driveZ_) z = 1.0f;
            for (auto& z : mixZ_) z = 0.5f;
            (void)maxBlockSize;
        }

        void reset() override
        {
            for (auto& z : lpfZ_) z = 0.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const float driveTarget = 1.0f + (params.size() > 0 ? params[0] : 0.3f) * 19.0f;
            const float tone = params.size() > 1 ? params[1] : 1.0f;
            const float mixTarget = params.size() > 2 ? params[2] : 0.5f;

            for (int c = 0; c < ch; ++c)
            {
                auto* data = buffer.getWritePointer(c);
                const int ci = std::min(c, 1);
                auto& lpZ = lpfZ_[static_cast<std::size_t>(ci)];
                auto& driveZ = driveZ_[static_cast<std::size_t>(ci)];
                auto& mixZ = mixZ_[static_cast<std::size_t>(ci)];
                for (int i = 0; i < numSamples; ++i)
                {
                    driveZ += smoothCoef_ * (driveTarget - driveZ);
                    mixZ += smoothCoef_ * (mixTarget - mixZ);
                    const float dry = data[i];
                    float wet = std::tanh(dry * driveZ);
                    lpZ += tone * (wet - lpZ);
                    data[i] = dry * (1.0f - mixZ) + lpZ * mixZ;
                }
            }
        }

        int numParams() const override { return 3; }

        ParamSpec paramSpec(int i) const override
        {
            static constexpr int kFxSec = 5;
            switch (i)
            {
                case 0: {
                    ParamSpec p;
                    p.id = "lockstep.dist.drive";
                    p.label = "Drive";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.3f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 1: {
                    ParamSpec p;
                    p.id = "lockstep.dist.tone";
                    p.label = "Tone";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 2: {
                    ParamSpec p;
                    p.id = "lockstep.dist.mix";
                    p.label = "Mix";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.5f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                default: return {};
            }
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "DRV"; }

    private:
        static inline const std::string kId = "lockstep.distortion.v1";
        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.005f;
        std::array<float, 2> lpfZ_{};
        std::array<float, 2> driveZ_{ 1.0f, 1.0f };
        std::array<float, 2> mixZ_{ 0.5f, 0.5f };
    };
}
