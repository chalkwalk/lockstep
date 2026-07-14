#pragma once

#include "../machine/IEffect.h"
#include "TempoRate.h"
#include <cmath>

namespace lockstep
{
    // Phaser — 4 first-order allpass stages per channel, LFO-swept centre.
    // Params: rate (Hz), depth (0-1), centre (Hz), feedback (0..0.9), mix.
    class PhaserEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sampleRate_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            for (auto& ph : lfoPhase_) ph = 0.0f;
            for (auto& ap : apState_)
                for (auto& s : ap) s = 0.0f;
            for (auto& z : fbkZ_) z = 0.0f;
            for (auto& z : mixZ_) z = 0.5f;
        }

        void reset() override
        {
            for (auto& ap : apState_)
                for (auto& s : ap) s = 0.0f;
            for (auto& z : fbkZ_) z = 0.0f;
        }

        // 9.31: tempo, broadcast per block by the processor. The modulation rate is
        // a period in beats, so the sweep tracks the song rather than the wall clock.
        void setTimeInfo(double bpm) override { bpm_ = bpm > 0.0 ? bpm : 120.0; }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            // 9.31: Rate is a PERIOD IN BEATS -- the sweep follows the tempo.
            const float periodBeats = params.size() > 0
                                      ? juce::jlimit(dsp::kMinModPeriod, dsp::kMaxModPeriod,
                                                     params[0])
                                      : 4.0f;
            const float rate = dsp::rateHzFromPeriodBeats(periodBeats, bpm_);
            const float depth   = params.size() > 1
                                      ? juce::jlimit(0.0f, 1.0f, params[1])
                                      : 0.5f;
            const float centreHz = params.size() > 2
                                       ? juce::jlimit(200.0f, 4000.0f, params[2])
                                       : 800.0f;
            const float fbkTgt  = params.size() > 3
                                      ? juce::jlimit(0.0f, 0.9f, params[3])
                                      : 0.0f;
            const float mixTgt  = params.size() > 4
                                      ? juce::jlimit(0.0f, 1.0f, params[4])
                                      : 0.5f;

            const float phInc = static_cast<float>(
                rate * juce::MathConstants<double>::twoPi / sampleRate_);
            const float logMin = std::log(centreHz * (1.0f - depth * 0.9f));
            const float logMax = std::log(centreHz * (1.0f + depth * 0.9f + 0.01f));

            for (int c = 0; c < std::min(numCh, 2); ++c)
            {
                auto* d = buffer.getWritePointer(c);
                auto& ph  = lfoPhase_[static_cast<std::size_t>(c)];
                auto& ap  = apState_[static_cast<std::size_t>(c)];
                auto& fz  = fbkZ_[static_cast<std::size_t>(c)];
                auto& mz  = mixZ_[static_cast<std::size_t>(c)];
                const float phOffset = (c == 1)
                    ? static_cast<float>(juce::MathConstants<double>::halfPi)
                    : 0.0f;

                for (int n = 0; n < numSamples; ++n)
                {
                    fz += smoothCoef_ * (fbkTgt - fz);
                    mz += smoothCoef_ * (mixTgt  - mz);

                    const float lfo = 0.5f + 0.5f * std::sin(ph + phOffset);
                    ph += phInc;
                    if (ph >= static_cast<float>(juce::MathConstants<double>::twoPi))
                        ph -= static_cast<float>(juce::MathConstants<double>::twoPi);

                    const float fc = std::exp(logMin + lfo * (logMax - logMin));
                    // Allpass coefficient: a = (tan(pi*fc/sr) - 1) / (tan(pi*fc/sr) + 1)
                    const float tan_fc = std::tan(
                        static_cast<float>(M_PI) * juce::jlimit(1.0f, static_cast<float>(sampleRate_ * 0.49), fc)
                        / static_cast<float>(sampleRate_));
                    const float a = (tan_fc - 1.0f) / (tan_fc + 1.0f);

                    // Feedback from last stage; clamp to prevent allpass-state
                    // divergence at high feedback + extreme LFO sweep combinations.
                    float s = juce::jlimit(-2.0f, 2.0f, d[n] + fz * ap[3]);
                    for (int stage = 0; stage < 4; ++stage)
                    {
                        const float out = a * s + ap[static_cast<std::size_t>(stage)];
                        ap[static_cast<std::size_t>(stage)] = s - a * out;
                        s = out;
                    }
                    d[n] = d[n] * (1.0f - mz) + s * mz;
                }
            }
            for (int c = 2; c < numCh; ++c)
                buffer.copyFrom(c, 0, buffer, 0, 0, numSamples);
        }

        [[nodiscard]] int numParams() const override { return 5; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (index)
            {
                case 0:
                    p.id = "lockstep.phaser.rate";
                    p.label = "Rate";
                    p.minValue = dsp::kMinModPeriod;
                    p.maxValue = dsp::kMaxModPeriod;
                    p.defaultValue = 4.0f;      // one cycle per bar (~0.5 Hz at 120)
                    p.skew = 0.35f;
                    p.unit = ParamSpec::Unit::Beats;
                    p.detents = dsp::modPeriodDetents();
                    p.valueLabels = dsp::modPeriodLabels();
                    break;
                case 1:
                    p.id = "lockstep.phaser.depth";
                    p.label = "Depth";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.5f;
                    break;
                case 2:
                    p.id = "lockstep.phaser.centre";
                    p.label = "Centre";
                    p.minValue = 200.0f; p.maxValue = 4000.0f; p.defaultValue = 800.0f;
                    p.skew = 0.4f;
                    break;
                case 3:
                    p.id = "lockstep.phaser.feedback";
                    p.label = "Feedbk";
                    p.minValue = 0.0f; p.maxValue = 0.9f; p.defaultValue = 0.0f;
                    break;
                default:
                    p.id = "lockstep.phaser.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.5f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "PHA"; }

    private:
        double bpm_ = 120.0;   // 9.31 — fed per block by setTimeInfo
        static inline const std::string kId = "lockstep.phaser.v1";
        static constexpr int kFxSec = 5;

        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.01f;
        float lfoPhase_[2] = { 0.0f, 0.0f };
        float apState_[2][4] = {};
        float fbkZ_[2] = { 0.0f, 0.0f };
        float mixZ_[2] = { 0.5f, 0.5f };
    };
}
