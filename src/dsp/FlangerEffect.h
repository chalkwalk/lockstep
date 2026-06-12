#pragma once

#include "../machine/IEffect.h"
#include <cmath>
#include <vector>

namespace lockstep
{
    // Flanger — modulated short delay (0.5..8 ms) with per-sample smoothing.
    // R channel LFO offset 90 degrees for stereo width.
    class FlangerEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sampleRate_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            const int bufLen = static_cast<int>(sampleRate * 0.01) + 2;  // 10ms max + 2
            for (auto& b : buf_) b.assign(static_cast<std::size_t>(bufLen), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& ph : phase_) ph = 0.0f;
            for (auto& z : fbkZ_) z = 0.0f;
            for (auto& z : mixZ_) z = 0.5f;
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
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float rate    = params.size() > 0
                                      ? juce::jlimit(0.02f, 5.0f, params[0])
                                      : 0.5f;
            const float depth   = params.size() > 1
                                      ? juce::jlimit(0.0f, 1.0f, params[1])
                                      : 0.5f;
            const float fbkTgt  = params.size() > 2
                                      ? juce::jlimit(-0.95f, 0.95f, params[2])
                                      : 0.0f;
            const float mixTgt  = params.size() > 3
                                      ? juce::jlimit(0.0f, 1.0f, params[3])
                                      : 0.5f;

            // Delay range: 0.5..8ms
            const float minSamples = static_cast<float>(0.0005 * sampleRate_);
            const float maxSamples = static_cast<float>(0.008  * sampleRate_);
            const float phInc = static_cast<float>(
                rate * juce::MathConstants<double>::twoPi / sampleRate_);

            for (int c = 0; c < std::min(numCh, 2); ++c)
            {
                auto* d = buffer.getWritePointer(c);
                auto& b = buf_[static_cast<std::size_t>(c)];
                auto& wr = head_[static_cast<std::size_t>(c)];
                auto& ph = phase_[static_cast<std::size_t>(c)];
                auto& fz = fbkZ_[static_cast<std::size_t>(c)];
                auto& mz = mixZ_[static_cast<std::size_t>(c)];
                const float phOffset = (c == 1)
                    ? static_cast<float>(juce::MathConstants<double>::halfPi)
                    : 0.0f;

                for (int n = 0; n < numSamples; ++n)
                {
                    fz += smoothCoef_ * (fbkTgt - fz);
                    mz += smoothCoef_ * (mixTgt - mz);

                    const float lfo = 0.5f + 0.5f * std::sin(ph + phOffset);
                    const float delaySamples = minSamples + lfo * depth * (maxSamples - minSamples);
                    ph += phInc;
                    if (ph >= static_cast<float>(juce::MathConstants<double>::twoPi))
                        ph -= static_cast<float>(juce::MathConstants<double>::twoPi);

                    // Linear interpolation into the delay buffer.
                    const int bufLen = static_cast<int>(b.size());
                    const float di = delaySamples;
                    const int i0 = static_cast<int>(di);
                    const float frac = di - static_cast<float>(i0);
                    const int rd0 = (wr - i0 + bufLen) % bufLen;
                    const int rd1 = (wr - i0 - 1 + bufLen) % bufLen;
                    const float delayed = b[static_cast<std::size_t>(rd0)] * (1.0f - frac)
                                        + b[static_cast<std::size_t>(rd1)] * frac;

                    b[static_cast<std::size_t>(wr)] = d[n] + delayed * fz;
                    wr = (wr + 1) % bufLen;

                    d[n] = d[n] * (1.0f - mz) + delayed * mz;
                }
            }
            for (int c = 2; c < numCh; ++c)
                buffer.copyFrom(c, 0, buffer, 0, 0, numSamples);
        }

        [[nodiscard]] int numParams() const override { return 4; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (index)
            {
                case 0:
                    p.id = "lockstep.flanger.rate";
                    p.label = "Rate";
                    p.minValue = 0.02f; p.maxValue = 5.0f; p.defaultValue = 0.5f;
                    p.skew = 0.5f;
                    break;
                case 1:
                    p.id = "lockstep.flanger.depth";
                    p.label = "Depth";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.5f;
                    break;
                case 2:
                    p.id = "lockstep.flanger.feedback";
                    p.label = "Feedbk";
                    p.minValue = -0.95f; p.maxValue = 0.95f; p.defaultValue = 0.0f;
                    break;
                default:
                    p.id = "lockstep.flanger.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.5f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "FLG"; }

    private:
        static inline const std::string kId = "lockstep.flanger.v1";
        static constexpr int kFxSec = 5;

        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.01f;
        std::vector<float> buf_[2];
        int   head_[2] = { 0, 0 };
        float phase_[2] = { 0.0f, 0.0f };
        float fbkZ_[2] = { 0.0f, 0.0f };
        float mixZ_[2] = { 0.5f, 0.5f };
    };
}
