#pragma once

#include "../machine/IEffect.h"
#include <cmath>
#include <vector>

namespace lockstep
{
    // Simple chorus — Rate (Hz) / Depth (0-1) / Mix.
    // Per-sample smoothing on mix (~5ms).
    class ChorusEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sampleRate_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            const int bufLen = static_cast<int>(sampleRate * 0.05) + 1;  // 50ms max
            for (auto& b : buf_) b.assign(static_cast<std::size_t>(bufLen), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& ph : phase_) ph = 0.0f;
            for (auto& z : mixZ_) z = 0.5f;
            (void)maxBlockSize;
        }

        void reset() override
        {
            for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& ph : phase_) ph = 0.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const float rate = params.size() > 0
                                   ? juce::jlimit(0.1f, 5.0f, params[0] * 5.0f)
                                   : 0.5f;
            const float depth = params.size() > 1 ? params[1] * 0.025f : 0.01f;
            const float mixTarget = params.size() > 2 ? params[2] : 0.5f;
            const float phInc = static_cast<float>(rate * juce::MathConstants<double>::twoPi / sampleRate_);

            for (int c = 0; c < std::min(ch, 2); ++c)
            {
                auto* data = buffer.getWritePointer(c);
                auto& b = buf_[static_cast<std::size_t>(c)];
                auto& wr = head_[static_cast<std::size_t>(c)];
                auto& ph = phase_[static_cast<std::size_t>(c)];
                auto& mixZ = mixZ_[static_cast<std::size_t>(c)];
                const int bufLen = static_cast<int>(b.size());

                for (int i = 0; i < numSamples; ++i)
                {
                    mixZ += smoothCoef_ * (mixTarget - mixZ);
                    const float lfo = std::sin(ph) * 0.5f + 0.5f;
                    const float delaySec = depth * lfo;
                    const int dSamples = static_cast<int>(delaySec * sampleRate_) + 1;
                    const int rd = (wr - dSamples + bufLen) % bufLen;
                    const float wet = b[static_cast<std::size_t>(rd)];
                    b[static_cast<std::size_t>(wr)] = data[i];
                    wr = (wr + 1) % bufLen;
                    data[i] = data[i] * (1.0f - mixZ) + wet * mixZ;
                    ph += phInc;
                    if (ph > static_cast<float>(juce::MathConstants<double>::twoPi))
                        ph -= static_cast<float>(juce::MathConstants<double>::twoPi);
                }
            }
            for (int c = 2; c < ch; ++c)
                buffer.copyFrom(c, 0, buffer, std::min(c, 1), 0, numSamples);
        }

        int numParams() const override { return 3; }

        ParamSpec paramSpec(int i) const override
        {
            static constexpr int kFxSec = 5;
            switch (i)
            {
                case 0: {
                    ParamSpec p;
                    p.id = "lockstep.chorus.rate";
                    p.label = "Rate";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.1f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 1: {
                    ParamSpec p;
                    p.id = "lockstep.chorus.depth";
                    p.label = "Depth";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.3f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 2: {
                    ParamSpec p;
                    p.id = "lockstep.chorus.mix";
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
        juce::String badge() const override { return "CHR"; }

    private:
        static inline const std::string kId = "lockstep.chorus.v1";
        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.005f;
        std::array<std::vector<float>, 2> buf_;
        std::array<int, 2> head_{};
        std::array<float, 2> phase_{};
        std::array<float, 2> mixZ_{ 0.5f, 0.5f };
    };
}
