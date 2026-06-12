#pragma once

#include "../machine/IEffect.h"
#include <cmath>
#include <vector>

namespace lockstep
{
    // HQ Delay — tempo-synced ping-pong with color filter in feedback path.
    // Uses setTimeInfo(bpm) to update tempo. Mix defaults to 1.0.
    // masterOnly = true.
    class HQDelayEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;
        // Divisions: 1/16, 1/8T, 1/8, 1/8., 1/4, 1/4., 1/2
        static constexpr double kDivBeats[] = {
            0.25, 1.0/3.0, 0.5, 0.75, 1.0, 1.5, 2.0
        };
        static constexpr const char* const kDivLabels[] = {
            "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2"
        };
        static constexpr int kNumDivs = 7;

    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            const int maxBufLen = static_cast<int>(sampleRate * 2.1);  // ~2s max
            for (auto& b : buf_) b.assign(static_cast<std::size_t>(maxBufLen), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& z : fbkZ_) z = 0.0f;
            for (auto& z : mixZ_) z = 1.0f;
            for (auto& z : colorZ_) z = 0.0f;
            updateDelayLen();
        }

        void reset() override
        {
            for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& z : fbkZ_) z = 0.0f;
        }

        void setTimeInfo(double bpm) override
        {
            bpm_ = bpm > 0.0 ? bpm : 120.0;
            updateDelayLen();
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const int divIdx   = params.size() > 0
                                     ? juce::jlimit(0, kNumDivs - 1, static_cast<int>(params[0]))
                                     : 4;
            const float fbkTgt = params.size() > 1
                                     ? juce::jlimit(0.0f, 1.1f, params[1])
                                     : 0.4f;
            const float color  = params.size() > 2
                                     ? juce::jlimit(-1.0f, 1.0f, params[2])
                                     : 0.0f;
            const float width  = params.size() > 3
                                     ? juce::jlimit(0.0f, 1.0f, params[3])
                                     : 1.0f;
            const float mixTgt = params.size() > 4
                                     ? juce::jlimit(0.0f, 1.0f, params[4])
                                     : 1.0f;

            if (divIdx != lastDivIdx_) { lastDivIdx_ = divIdx; updateDelayLen(); }

            const float colorAlpha = 1.0f - std::exp(
                -2.0f * static_cast<float>(M_PI) * (color >= 0.0f ? 3000.0f : 200.0f)
                / static_cast<float>(sr_));

            for (int n = 0; n < numSamples; ++n)
            {
                const float inL = numCh > 0 ? buffer.getReadPointer(0)[n] : 0.0f;
                const float inR = numCh > 1 ? buffer.getReadPointer(1)[n] : inL;

                // Read delay (ping-pong: L reads from R's delay, R from L's delay).
                const int bufLen = static_cast<int>(buf_[0].size());
                const int rdL = (head_[0] - delayLen_ + bufLen) % bufLen;
                const int rdR = (head_[1] - delayLen_ + bufLen) % bufLen;
                float dlyL = buf_[0][static_cast<std::size_t>(rdR)];  // ping-pong swap
                float dlyR = buf_[1][static_cast<std::size_t>(rdL)];

                // Color filter in feedback (tilt-like: LP if color<0, HP if color>0).
                for (int c = 0; c < 2; ++c)
                {
                    auto& cz = colorZ_[static_cast<std::size_t>(c)];
                    const float dly = (c == 0) ? dlyL : dlyR;
                    cz += colorAlpha * (dly - cz);
                    const float filtered = (color >= 0.0f) ? (dly - cz) : cz;
                    if (c == 0) dlyL = filtered; else dlyR = filtered;
                }

                // Feedback limiting (soft-clip at 1.0).
                const auto softClip = [](float x) {
                    return x / (1.0f + std::abs(x));
                };
                fbkZ_[0] += smoothCoef_ * (fbkTgt - fbkZ_[0]);
                fbkZ_[1] += smoothCoef_ * (fbkTgt - fbkZ_[1]);
                mixZ_[0] += smoothCoef_ * (mixTgt - mixZ_[0]);
                mixZ_[1] += smoothCoef_ * (mixTgt - mixZ_[1]);

                // Write: L channel gets direct input + R's delayed feedback.
                buf_[0][static_cast<std::size_t>(head_[0])] = inL + softClip(dlyR * fbkZ_[0]);
                buf_[1][static_cast<std::size_t>(head_[1])] = inR + softClip(dlyL * fbkZ_[1]);
                head_[0] = (head_[0] + 1) % bufLen;
                head_[1] = (head_[1] + 1) % bufLen;

                // Width: blend ping-pong with mono delay.
                const float mono = (dlyL + dlyR) * 0.5f;
                const float outL = mono * (1.0f - width) + dlyL * width;
                const float outR = mono * (1.0f - width) + dlyR * width;

                if (numCh > 0)
                    buffer.getWritePointer(0)[n] = inL * (1.0f - mixZ_[0]) + outL * mixZ_[0];
                if (numCh > 1)
                    buffer.getWritePointer(1)[n] = inR * (1.0f - mixZ_[1]) + outR * mixZ_[1];
            }
            for (int c = 2; c < numCh; ++c)
                buffer.copyFrom(c, 0, buffer, 0, 0, numSamples);
        }

        [[nodiscard]] int numParams() const override { return 5; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            static const std::span<const char* const> kDivSpan{ kDivLabels,
                static_cast<std::size_t>(kNumDivs) };
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (index)
            {
                case 0:
                    p.id = "lockstep.delayhq.time";
                    p.label = "Time";
                    p.minValue = 0.0f; p.maxValue = static_cast<float>(kNumDivs - 1);
                    p.defaultValue = 4.0f; p.isStepped = true;
                    p.valueLabels = kDivSpan;
                    break;
                case 1:
                    p.id = "lockstep.delayhq.feedback";
                    p.label = "Feedbk";
                    p.minValue = 0.0f; p.maxValue = 1.1f; p.defaultValue = 0.4f;
                    break;
                case 2:
                    p.id = "lockstep.delayhq.color";
                    p.label = "Color";
                    p.minValue = -1.0f; p.maxValue = 1.0f; p.defaultValue = 0.0f;
                    break;
                case 3:
                    p.id = "lockstep.delayhq.width";
                    p.label = "Width";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 1.0f;
                    break;
                default:
                    p.id = "lockstep.delayhq.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 1.0f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "DLH"; }

    private:
        void updateDelayLen()
        {
            const double divBeats = kDivBeats[static_cast<std::size_t>(
                juce::jlimit(0, kNumDivs - 1, lastDivIdx_))];
            const double beatSecs = 60.0 / bpm_;
            const int maxLen = static_cast<int>(buf_[0].size());
            delayLen_ = juce::jlimit(1, maxLen - 1,
                static_cast<int>(divBeats * beatSecs * sr_));
        }

        static inline const std::string kId = "lockstep.delayhq.v1";

        double sr_ = 44100.0;
        double bpm_ = 120.0;
        float smoothCoef_ = 0.01f;

        std::vector<float> buf_[2];
        int head_[2] = { 0, 0 };
        int delayLen_ = 22050;
        int lastDivIdx_ = 4;

        float fbkZ_[2] = { 0.0f, 0.0f };
        float mixZ_[2] = { 1.0f, 1.0f };
        float colorZ_[2] = { 0.0f, 0.0f };
    };
}
