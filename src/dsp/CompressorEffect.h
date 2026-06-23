#pragma once

#include "../machine/IEffect.h"
#include <cmath>

namespace lockstep
{
    // Feed-forward peak compressor — stereo-linked detector, hard knee.
    // Params: thresh (dB), ratio (stepped 2:1/4:1/8:1/20:1), attack (ms),
    //         release (ms), makeup (dB).
    class CompressorEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sampleRate_ = sampleRate;
            envZ_ = 0.0f;
            gainZ_ = 1.0f;
        }

        void reset() override
        {
            envZ_ = 0.0f;
            gainZ_ = 1.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float threshDb  = params.size() > 0
                                        ? juce::jlimit(-48.0f, 0.0f, params[0])
                                        : -18.0f;
            // Ratio: continuous (log-scaled in the UI via paramSpec.skew).
            const float ratio     = params.size() > 1
                                        ? juce::jlimit(1.0f, 20.0f, params[1])
                                        : 4.0f;
            const float attackMs  = params.size() > 2
                                        ? juce::jlimit(0.1f, 100.0f, params[2])
                                        : 5.0f;
            const float releaseMs = params.size() > 3
                                        ? juce::jlimit(10.0f, 1000.0f, params[3])
                                        : 120.0f;
            const float makeupDb  = params.size() > 4
                                        ? juce::jlimit(0.0f, 24.0f, params[4])
                                        : 0.0f;

            const float threshLin   = juce::Decibels::decibelsToGain(threshDb);
            const float makeupLin   = juce::Decibels::decibelsToGain(makeupDb);
            const float attCoef     = 1.0f - std::exp(
                -1.0f / static_cast<float>(0.001 * attackMs  * sampleRate_));
            const float relCoef     = 1.0f - std::exp(
                -1.0f / static_cast<float>(0.001 * releaseMs * sampleRate_));
            const float gainSmooth  = 1.0f - std::exp(
                -1.0f / static_cast<float>(0.002 * sampleRate_));

            for (int n = 0; n < numSamples; ++n)
            {
                // Stereo-linked peak detector.
                float peak = 0.0f;
                for (int c = 0; c < numCh; ++c)
                    peak = std::max(peak, std::abs(buffer.getReadPointer(c)[n]));

                const float coef = (peak > envZ_) ? attCoef : relCoef;
                envZ_ += coef * (peak - envZ_);

                float gr = 1.0f;
                if (envZ_ > threshLin && envZ_ > 0.0f)
                {
                    const float dbOver   = juce::Decibels::gainToDecibels(envZ_) - threshDb;
                    const float reducedDb = dbOver * (1.0f - 1.0f / ratio);
                    gr = juce::Decibels::decibelsToGain(-reducedDb);
                }
                gainZ_ += gainSmooth * (gr - gainZ_);

                const float g = gainZ_ * makeupLin;
                for (int c = 0; c < numCh; ++c)
                    buffer.getWritePointer(c)[n] *= g;
            }
        }

        [[nodiscard]] int numParams() const override { return 5; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (index)
            {
                case 0:
                    p.id = "lockstep.comp.thresh";
                    p.label = "Thresh";
                    p.minValue = -48.0f; p.maxValue = 0.0f; p.defaultValue = -18.0f;
                    break;
                case 1:
                    p.id = "lockstep.comp.ratio";
                    p.label = "Ratio";
                    // Continuous, log-scaled: fine resolution near 1:1, coarser up high.
                    p.minValue = 1.0f; p.maxValue = 20.0f; p.defaultValue = 4.0f;
                    p.skew = 0.3f;
                    break;
                case 2:
                    p.id = "lockstep.comp.attack";
                    p.label = "Atk";
                    p.minValue = 0.1f; p.maxValue = 100.0f; p.defaultValue = 5.0f;
                    p.skew = 0.3f;
                    break;
                case 3:
                    p.id = "lockstep.comp.release";
                    p.label = "Rel";
                    p.minValue = 10.0f; p.maxValue = 1000.0f; p.defaultValue = 120.0f;
                    p.skew = 0.3f;
                    break;
                default:
                    p.id = "lockstep.comp.makeup";
                    p.label = "Mkup";
                    p.minValue = 0.0f; p.maxValue = 24.0f; p.defaultValue = 0.0f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "CMP"; }

    private:
        static inline const std::string kId = "lockstep.comp.v1";
        static constexpr int kFxSec = 5;

        double sampleRate_ = 44100.0;
        float envZ_ = 0.0f;
        float gainZ_ = 1.0f;
    };
}
