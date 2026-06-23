#pragma once

#include "../machine/IEffect.h"
#include <cmath>

namespace lockstep
{
    // Bus Compressor — feed-forward, soft knee (6dB), RMS-ish detector,
    // program-dependent dual-time-constant release, sidechain HPF.
    // masterOnly = true.
    class BusCompressorEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;

    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sr_ = sampleRate;
            rmsZ_ = 0.0f;
            gainZ_ = 1.0f;
            hpfZ_[0] = hpfZ_[1] = 0.0f;
        }

        void reset() override
        {
            rmsZ_ = 0.0f;
            gainZ_ = 1.0f;
            hpfZ_[0] = hpfZ_[1] = 0.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float threshDb  = params.size() > 0
                                        ? juce::jlimit(-36.0f, 0.0f, params[0])
                                        : -18.0f;
            // Ratio: continuous (log-scaled in the UI via paramSpec.skew).
            const float ratio     = params.size() > 1
                                        ? juce::jlimit(1.0f, 20.0f, params[1])
                                        : 2.0f;
            // Attack/release are continuous times in ms; 0 = Auto (program-dependent).
            const float attackMs  = params.size() > 2
                                        ? juce::jlimit(0.0f, 30.0f, params[2])
                                        : 5.0f;
            const float releaseMs = params.size() > 3
                                        ? juce::jlimit(0.0f, 1200.0f, params[3])
                                        : 250.0f;
            const float schpfHz   = params.size() > 4
                                        ? juce::jlimit(20.0f, 300.0f, params[4])
                                        : 60.0f;
            const float makeupDb  = params.size() > 5
                                        ? juce::jlimit(0.0f, 12.0f, params[5])
                                        : 0.0f;
            const float mixTgt    = params.size() > 6
                                        ? juce::jlimit(0.0f, 1.0f, params[6])
                                        : 1.0f;

            const bool autoAtk = (attackMs  <= 0.0f);
            const bool autoRel = (releaseMs <= 0.0f);
            auto coefMs = [&](double ms) {
                return 1.0f - std::exp(-1.0f / static_cast<float>(0.001 * ms * sr_));
            };

            const float threshLin   = juce::Decibels::decibelsToGain(threshDb);
            const float makeupLin   = juce::Decibels::decibelsToGain(makeupDb);
            // Auto attack = a quick program-independent default; auto release = a
            // medium base that speeds up below threshold (applied per-sample below).
            const float attCoef     = autoAtk ? coefMs(5.0)   : coefMs(attackMs);
            const float relCoefBase = autoRel ? coefMs(400.0) : coefMs(releaseMs);
            const float kneeDbHalf  = 3.0f;   // 6dB soft knee half-width

            // Sidechain HPF coefficient.
            const float hpfAlpha = 1.0f - std::exp(
                -2.0f * static_cast<float>(M_PI) * schpfHz / static_cast<float>(sr_));

            for (int n = 0; n < numSamples; ++n)
            {
                // Sum channels for sidechain, apply HPF.
                float sc = 0.0f;
                for (int c = 0; c < numCh; ++c)
                    sc += buffer.getReadPointer(c)[n];
                sc *= (numCh > 1) ? 0.5f : 1.0f;

                // SC HPF.
                for (int c = 0; c < std::min(numCh, 2); ++c)
                {
                    hpfZ_[c] += hpfAlpha * (buffer.getReadPointer(c)[n] - hpfZ_[c]);
                    if (c == 0) sc = buffer.getReadPointer(0)[n] - hpfZ_[0];
                }

                // RMS-ish: one-pole on squared signal (~10ms window).
                const float rmsCoef = 1.0f - std::exp(-1.0f / static_cast<float>(0.01 * sr_));
                rmsZ_ += rmsCoef * (sc * sc - rmsZ_);
                const float rmsVal = std::sqrt(std::max(rmsZ_, 1e-12f));

                // Auto-release: faster recovery when the signal sits below threshold.
                float relCoef = relCoefBase;
                if (autoRel && rmsVal < threshLin)
                    relCoef *= 4.0f;
                rmsZ_ = std::max(rmsZ_, 0.0f);  // denormal guard

                // Soft-knee gain computation.
                float gr = 1.0f;
                if (rmsVal > 0.0f)
                {
                    const float dbIn  = juce::Decibels::gainToDecibels(rmsVal);
                    const float dbOver = dbIn - threshDb;
                    if (dbOver > -kneeDbHalf)
                    {
                        const float kneeFraction = (dbOver + kneeDbHalf) / (2.0f * kneeDbHalf);
                        const float effectiveOver = dbOver > kneeDbHalf
                            ? dbOver
                            : dbOver * juce::jlimit(0.0f, 1.0f, kneeFraction);
                        gr = juce::Decibels::decibelsToGain(-effectiveOver * (1.0f - 1.0f / ratio));
                    }
                }
                // Attack while clamping to a lower gain, release while recovering —
                // this is what makes Attack/Release actually do something (the gain
                // envelope previously used a fixed smoothing and ignored both params).
                const float envCoef = (gr < gainZ_) ? attCoef : relCoef;
                gainZ_ += envCoef * (gr - gainZ_);
                const float g = gainZ_ * makeupLin;
                const float dry0 = numCh > 0 ? buffer.getReadPointer(0)[n] : 0.0f;
                const float dry1 = numCh > 1 ? buffer.getReadPointer(1)[n] : dry0;
                if (numCh > 0) buffer.getWritePointer(0)[n] = dry0 * (1.0f - mixTgt) + dry0 * g * mixTgt;
                if (numCh > 1) buffer.getWritePointer(1)[n] = dry1 * (1.0f - mixTgt) + dry1 * g * mixTgt;
                for (int c = 2; c < numCh; ++c)
                {
                    const float d = buffer.getReadPointer(c)[n];
                    buffer.getWritePointer(c)[n] = d * (1.0f - mixTgt) + d * g * mixTgt;
                }
            }
        }

        [[nodiscard]] int numParams() const override { return 7; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (index)
            {
                case 0:
                    p.id = "lockstep.buscomp.thresh";
                    p.label = "Thresh";
                    p.minValue = -36.0f; p.maxValue = 0.0f; p.defaultValue = -18.0f;
                    break;
                case 1:
                    p.id = "lockstep.buscomp.ratio";
                    p.label = "Ratio";
                    // Continuous, log-scaled: fine resolution near 1:1, coarser up high.
                    p.minValue = 1.0f; p.maxValue = 20.0f; p.defaultValue = 2.0f;
                    p.skew = 0.3f;
                    break;
                case 2:
                    p.id = "lockstep.buscomp.attack";
                    p.label = "Atk";
                    // Continuous ms; 0 = Auto. skew biases resolution to short times.
                    p.minValue = 0.0f; p.maxValue = 30.0f; p.defaultValue = 5.0f;
                    p.skew = 0.4f; p.unit = ParamSpec::Unit::Ms; p.zeroLabel = "Auto";
                    break;
                case 3:
                    p.id = "lockstep.buscomp.release";
                    p.label = "Rel";
                    // Continuous ms; 0 = Auto (program-dependent release).
                    p.minValue = 0.0f; p.maxValue = 1200.0f; p.defaultValue = 250.0f;
                    p.skew = 0.4f; p.unit = ParamSpec::Unit::Ms; p.zeroLabel = "Auto";
                    break;
                case 4:
                    p.id = "lockstep.buscomp.schpf";
                    p.label = "SC HPF";
                    p.minValue = 20.0f; p.maxValue = 300.0f; p.defaultValue = 60.0f;
                    p.skew = 0.4f;
                    break;
                case 5:
                    p.id = "lockstep.buscomp.makeup";
                    p.label = "Mkup";
                    p.minValue = 0.0f; p.maxValue = 12.0f; p.defaultValue = 0.0f;
                    break;
                default:
                    p.id = "lockstep.buscomp.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 1.0f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "BUS"; }

    private:
        static inline const std::string kId = "lockstep.buscomp.v1";

        double sr_ = 44100.0;
        float rmsZ_ = 0.0f;
        float gainZ_ = 1.0f;
        float hpfZ_[2] = { 0.0f, 0.0f };
    };
}
