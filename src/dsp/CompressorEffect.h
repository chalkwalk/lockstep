#pragma once

#include "../machine/IEffect.h"
#include <chalkwalk/dsp/Compressor.h>
#include <algorithm>
#include <cmath>

namespace lockstep
{
    // Feed-forward peak compressor — stereo-linked detector, hard knee.
    // Params: thresh (dB), ratio (1..20, log-scaled), attack (ms, 0 = Auto),
    //         release (ms, 0 = Auto), makeup (dB).
    //
    // The compressor is chalkwalk-dsp's Compressor (libs/dsp), shared with the
    // other projects; this is its JUCE face. Attack and release act on the gain
    // in dB (they used to act on a peak envelope, with a fixed 2 ms gain
    // smoother after it). Auto attack is 5 ms; Auto release is 200 ms, four
    // times faster once the signal is back under the threshold.
    class CompressorEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sampleRate_ = sampleRate;
            have_ = false;
            comp_.reset();
        }

        void reset() override { comp_.reset(); }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float in[5] = {
                params.size() > 0 ? juce::jlimit(-48.0f, 0.0f, params[0]) : -18.0f,
                params.size() > 1 ? juce::jlimit(1.0f, 20.0f, params[1]) : 4.0f,
                params.size() > 2 ? juce::jlimit(0.0f, 100.0f, params[2]) : 5.0f,
                params.size() > 3 ? juce::jlimit(0.0f, 1000.0f, params[3]) : 120.0f,
                params.size() > 4 ? juce::jlimit(0.0f, 24.0f, params[4]) : 0.0f,
            };
            if (!have_ || !std::equal(in, in + 5, last_))
            {
                std::copy(in, in + 5, last_);
                have_ = true;
                // 0 (the floor) selects Auto; the epsilon matches
                // formatParamValue's minLabel threshold so the readout agrees.
                const bool autoAtk = in[2] <= 1.0e-3f;
                const bool autoRel = in[3] <= 1.0e-3f;
                chalkwalk::dsp::Compressor::Settings s;
                s.thresholdDb = in[0];
                s.ratio = in[1];
                s.kneeDb = 0.0;
                s.attackMs = autoAtk ? 5.0 : in[2];
                s.releaseMs = autoRel ? 200.0 : in[3];
                s.autoRelease = autoRel;
                s.detectorMs = 0.0;  // peak
                s.makeupDb = in[4];
                comp_.configure(sampleRate_, s);
            }

            comp_.process(buffer.getWritePointer(0),
                          numCh > 1 ? buffer.getWritePointer(1) : nullptr, numSamples);
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
                    // Continuous ms; 0 = Auto.
                    p.minValue = 0.0f; p.maxValue = 100.0f; p.defaultValue = 5.0f;
                    p.skew = 0.3f; p.unit = ParamSpec::Unit::Ms; p.minLabel = "Auto";
                    break;
                case 3:
                    p.id = "lockstep.comp.release";
                    p.label = "Rel";
                    // Continuous ms; 0 = Auto (program-dependent release).
                    p.minValue = 0.0f; p.maxValue = 1000.0f; p.defaultValue = 120.0f;
                    p.skew = 0.3f; p.unit = ParamSpec::Unit::Ms; p.minLabel = "Auto";
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
        chalkwalk::dsp::Compressor comp_;
        float last_[5] = {};
        bool have_ = false;
    };
}
