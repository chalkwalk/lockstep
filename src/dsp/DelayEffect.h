#pragma once

#include "../machine/IEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace lockstep
{
    // Simple stereo delay — Time / Feedback / Mix / LPF.
    // Per-sample smoothing on mix and feedback (~5ms); time slews at
    // max 1 sample/sample to prevent read-head glitches on time changes.
    class DelayEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sampleRate_ = sampleRate;
            const float coef = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            smoothCoef_ = coef;
            const int maxDelay = static_cast<int>(sampleRate * 2.0) + 1;
            for (auto& b : buf_) { b.assign(static_cast<std::size_t>(maxDelay), 0.0f); }
            for (auto& h : head_) h = 0;
            for (auto& z : lpfZ_) z = 0.0f;
            for (auto& z : mixZ_) z = 0.3f;
            for (auto& z : fbkZ_) z = 0.4f;
            for (auto& z : timeSamplesF_) z = static_cast<float>(0.25 * sampleRate);
            (void)maxBlockSize;
        }

        void reset() override
        {
            for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& z : lpfZ_) z = 0.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;
            const float timeTarget = static_cast<float>(
                juce::jlimit(0.001, 2.0, static_cast<double>(params.size() > 0 ? params[0] : 0.25f))
                    * sampleRate_);
            const float feedbackTarget = params.size() > 1 ? params[1] : 0.4f;
            const float mixTarget = params.size() > 2 ? params[2] : 0.3f;
            const float lpf = params.size() > 3 ? params[3] : 1.0f;

            const int bufSize = static_cast<int>(buf_[0].size());

            for (int c = 0; c < std::min(ch, 2); ++c)
            {
                auto* data = buffer.getWritePointer(c);
                auto& b = buf_[static_cast<std::size_t>(c)];
                auto& wr = head_[static_cast<std::size_t>(c)];
                auto& lpZ = lpfZ_[static_cast<std::size_t>(c)];
                auto& timeSampF = timeSamplesF_[static_cast<std::size_t>(c)];
                auto& mixZ = mixZ_[static_cast<std::size_t>(c)];
                auto& fbkZ = fbkZ_[static_cast<std::size_t>(c)];

                for (int i = 0; i < numSamples; ++i)
                {
                    // Slew time at max 1 sample/sample to avoid read-head clicks.
                    timeSampF += std::clamp(timeTarget - timeSampF, -1.0f, 1.0f);
                    // Smooth mix and feedback.
                    mixZ += smoothCoef_ * (mixTarget - mixZ);
                    fbkZ += smoothCoef_ * (feedbackTarget - fbkZ);

                    const int dSamples = static_cast<int>(timeSampF);
                    const int rd = (wr - dSamples + bufSize) % bufSize;
                    float wet = b[static_cast<std::size_t>(rd)];
                    lpZ += lpf * (wet - lpZ);
                    b[static_cast<std::size_t>(wr)] = data[i] + lpZ * fbkZ;
                    wr = (wr + 1) % bufSize;
                    data[i] = data[i] * (1.0f - mixZ) + lpZ * mixZ;
                }
            }
            // Mono → copy to remaining channels.
            for (int c = 2; c < ch; ++c)
                buffer.copyFrom(c, 0, buffer, std::min(c, 1), 0, numSamples);
        }

        int numParams() const override { return 4; }

        ParamSpec paramSpec(int i) const override
        {
            static constexpr int kFxSec = 5;
            switch (i)
            {
                case 0: {
                    ParamSpec p;
                    p.id = "lockstep.delay.time";
                    p.label = "Time";
                    p.minValue = 0.001f;
                    p.maxValue = 2.0f;
                    p.defaultValue = 0.25f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 1: {
                    ParamSpec p;
                    p.id = "lockstep.delay.feedback";
                    p.label = "Feedbk";
                    p.maxValue = 0.95f;
                    p.defaultValue = 0.4f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 2: {
                    ParamSpec p;
                    p.id = "lockstep.delay.mix";
                    p.label = "Mix";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.3f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 3: {
                    ParamSpec p;
                    p.id = "lockstep.delay.lpf";
                    p.label = "LPF";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                default: return {};
            }
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "DLY"; }

    private:
        static inline const std::string kId = "lockstep.delay.v1";
        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.005f;
        std::array<std::vector<float>, 2> buf_;
        std::array<int, 2> head_{};
        std::array<float, 2> lpfZ_{};
        std::array<float, 2> mixZ_{ 0.3f, 0.3f };
        std::array<float, 2> fbkZ_{ 0.4f, 0.4f };
        std::array<float, 2> timeSamplesF_{ 11025.0f, 11025.0f };
    };
}
