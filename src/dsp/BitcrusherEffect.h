#pragma once

#include "../machine/IEffect.h"
#include <cmath>

namespace lockstep
{
    // Bitcrusher — bit-depth reduction + sample-rate downsampling.
    // Params: bits (1..16), rate (downsample factor 1..50), mix (0..1).
    class BitcrusherEffect final : public IEffect
    {
    public:
        void prepare(double /*sampleRate*/, int /*maxBlockSize*/) override
        {
            for (auto& h : held_) h = 0.0f;
            for (auto& c : countdown_) c = 0;
            for (auto& z : mixZ_) z = 1.0f;
            smoothCoef_ = 0.005f;
        }

        void reset() override
        {
            for (auto& h : held_) h = 0.0f;
            for (auto& c : countdown_) c = 0;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float bits    = params.size() > 0
                                      ? juce::jlimit(1.0f, 16.0f, params[0])
                                      : 16.0f;
            const int rate      = params.size() > 1
                                      ? juce::jlimit(1, 50, static_cast<int>(params[1]))
                                      : 1;
            const float mixTgt  = params.size() > 2
                                      ? juce::jlimit(0.0f, 1.0f, params[2])
                                      : 1.0f;

            const float levels = std::pow(2.0f, bits) * 0.5f;  // half due to ±1 range

            for (int c = 0; c < std::min(numCh, 2); ++c)
            {
                auto* d = buffer.getWritePointer(c);
                auto& h = held_[static_cast<std::size_t>(c)];
                auto& cd = countdown_[static_cast<std::size_t>(c)];
                auto& mz = mixZ_[static_cast<std::size_t>(c)];

                for (int n = 0; n < numSamples; ++n)
                {
                    mz += smoothCoef_ * (mixTgt - mz);

                    if (cd <= 0)
                    {
                        h = std::floor(d[n] * levels + 0.5f) / levels;
                        cd = rate;
                    }
                    --cd;

                    d[n] = d[n] * (1.0f - mz) + h * mz;
                }
            }
            for (int c = 2; c < numCh; ++c)
                buffer.copyFrom(c, 0, buffer, 0, 0, numSamples);
        }

        [[nodiscard]] int numParams() const override { return 3; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (index)
            {
                case 0:
                    p.id = "lockstep.bitcrush.bits";
                    p.label = "Bits";
                    p.minValue = 1.0f; p.maxValue = 16.0f; p.defaultValue = 16.0f;
                    break;
                case 1:
                    p.id = "lockstep.bitcrush.rate";
                    p.label = "Rate";
                    p.minValue = 1.0f; p.maxValue = 50.0f; p.defaultValue = 1.0f;
                    break;
                default:
                    p.id = "lockstep.bitcrush.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 1.0f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "BIT"; }

    private:
        static inline const std::string kId = "lockstep.bitcrush.v1";
        static constexpr int kFxSec = 5;

        float smoothCoef_ = 0.005f;
        float held_[2] = { 0.0f, 0.0f };
        int   countdown_[2] = { 0, 0 };
        float mixZ_[2] = { 1.0f, 1.0f };
    };
}
