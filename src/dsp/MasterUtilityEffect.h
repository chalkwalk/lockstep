#pragma once

#include "../machine/IEffect.h"
#include <cmath>

namespace lockstep
{
    // Master Utility — tilt EQ + M/S width + trim.
    // Tilt: same one-pole shelf pivot as TiltEQEffect.
    // Width: 0=mono, 1=unity, 2=double-wide (encode M/S, scale S, decode).
    // Trim: -12..+12 dB, smoothed.
    // masterOnly = true.
    class MasterUtilityEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;

    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            for (auto& z : lowZ_) z = 0.0f;
            for (auto& z : tiltZ_) z = 0.0f;
            for (auto& z : trimZ_) z = 1.0f;
            for (auto& z : widthZ_) z = 1.0f;
        }

        void reset() override
        {
            for (auto& z : lowZ_) z = 0.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float tiltTgt  = params.size() > 0
                                       ? juce::jlimit(-1.0f, 1.0f, params[0])
                                       : 0.0f;
            const float widthTgt = params.size() > 1
                                       ? juce::jlimit(0.0f, 2.0f, params[1])
                                       : 1.0f;
            const float trimTgt  = params.size() > 2
                                       ? juce::Decibels::decibelsToGain(
                                             juce::jlimit(-12.0f, 12.0f, params[2]))
                                       : 1.0f;

            const float alpha = static_cast<float>(
                1.0 - std::exp(-2.0 * M_PI * 700.0 / sr_));

            auto* dL = numCh > 0 ? buffer.getWritePointer(0) : nullptr;
            auto* dR = numCh > 1 ? buffer.getWritePointer(1) : nullptr;

            for (int n = 0; n < numSamples; ++n)
            {
                tiltZ_[0] += smoothCoef_ * (tiltTgt  - tiltZ_[0]);
                trimZ_[0] += smoothCoef_ * (trimTgt  - trimZ_[0]);
                widthZ_[0] += smoothCoef_ * (widthTgt - widthZ_[0]);

                const float tilt = tiltZ_[0];
                const float trim = trimZ_[0];
                const float wid  = widthZ_[0];

                if (dL)
                {
                    lowZ_[0] += alpha * (dL[n] - lowZ_[0]);
                    const float hp = dL[n] - lowZ_[0];
                    const float lg = juce::Decibels::decibelsToGain(-tilt * 6.0f);
                    const float hg = juce::Decibels::decibelsToGain( tilt * 6.0f);
                    dL[n] = (lowZ_[0] * lg + hp * hg) * trim;
                }

                if (dR)
                {
                    lowZ_[1] += alpha * (dR[n] - lowZ_[1]);
                    const float hp = dR[n] - lowZ_[1];
                    const float lg = juce::Decibels::decibelsToGain(-tilt * 6.0f);
                    const float hg = juce::Decibels::decibelsToGain( tilt * 6.0f);
                    dR[n] = (lowZ_[1] * lg + hp * hg) * trim;
                }

                // M/S width (stereo only).
                if (dL && dR)
                {
                    const float m = (dL[n] + dR[n]) * 0.5f;
                    const float s = (dL[n] - dR[n]) * 0.5f * wid;
                    dL[n] = m + s;
                    dR[n] = m - s;
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
                    p.id = "lockstep.mutility.tilt";
                    p.label = "Tilt";
                    p.minValue = -1.0f; p.maxValue = 1.0f; p.defaultValue = 0.0f;
                    break;
                case 1:
                    p.id = "lockstep.mutility.width";
                    p.label = "Width";
                    p.minValue = 0.0f; p.maxValue = 2.0f; p.defaultValue = 1.0f;
                    break;
                default:
                    p.id = "lockstep.mutility.trim";
                    p.label = "Trim";
                    p.minValue = -12.0f; p.maxValue = 12.0f; p.defaultValue = 0.0f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "UTL"; }

    private:
        static inline const std::string kId = "lockstep.mutility.v1";

        double sr_ = 44100.0;
        float smoothCoef_ = 0.01f;
        float lowZ_[2]   = { 0.0f, 0.0f };
        float tiltZ_[1]  = { 0.0f };
        float trimZ_[1]  = { 1.0f };
        float widthZ_[1] = { 1.0f };
    };
}
