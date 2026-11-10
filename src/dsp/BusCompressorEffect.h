#pragma once

#include "../machine/IEffect.h"
#include <chalkwalk/dsp/Compressor.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lockstep
{
    // Bus Compressor — feed-forward, 6 dB soft knee, 10 ms RMS detector on both
    // sides, sidechain HPF, program-dependent release, parallel mix.
    // masterOnly = true.
    //
    // The compressor is chalkwalk-dsp's Compressor (libs/dsp), shared with the
    // other projects; this is its JUCE face plus the dry/wet mix. Moving it
    // there fixed two faults this effect had: its detector heard only the left
    // side's high-passed signal, and its knee added up to ~0.2 dB just below
    // the threshold.
    class BusCompressorEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;

    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sr_ = sampleRate;
            have_ = false;
            comp_.reset();
            const auto n = static_cast<std::size_t>(std::max(maxBlockSize, 1));
            dry_[0].assign(n, 0.0f);
            dry_[1].assign(n, 0.0f);
        }

        void reset() override { comp_.reset(); }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float in[6] = {
                params.size() > 0 ? juce::jlimit(-36.0f, 0.0f, params[0]) : -18.0f,
                params.size() > 1 ? juce::jlimit(1.0f, 20.0f, params[1]) : 2.0f,
                params.size() > 2 ? juce::jlimit(0.0f, 30.0f, params[2]) : 5.0f,
                params.size() > 3 ? juce::jlimit(0.0f, 1200.0f, params[3]) : 250.0f,
                params.size() > 4 ? juce::jlimit(20.0f, 300.0f, params[4]) : 60.0f,
                params.size() > 5 ? juce::jlimit(0.0f, 12.0f, params[5]) : 0.0f,
            };
            const float mix = params.size() > 6 ? juce::jlimit(0.0f, 1.0f, params[6]) : 1.0f;
            if (!have_ || !std::equal(in, in + 6, last_))
            {
                std::copy(in, in + 6, last_);
                have_ = true;
                // 0 (the floor) selects Auto; the epsilon matches
                // formatParamValue's minLabel threshold so the readout agrees.
                const bool autoAtk = in[2] <= 1.0e-3f;
                const bool autoRel = in[3] <= 1.0e-3f;
                chalkwalk::dsp::Compressor::Settings s;
                s.thresholdDb = in[0];
                s.ratio = in[1];
                s.kneeDb = 6.0;
                s.attackMs = autoAtk ? 5.0 : in[2];
                s.releaseMs = autoRel ? 400.0 : in[3];
                s.autoRelease = autoRel;
                s.detectorMs = 10.0;
                s.sidechainHighPassHz = in[4];
                s.makeupDb = in[5];
                comp_.configure(sr_, s);
            }

            float* l = buffer.getWritePointer(0);
            float* r = numCh > 1 ? buffer.getWritePointer(1) : nullptr;
            if (mix >= 1.0f)
            {
                comp_.process(l, r, numSamples);
                return;
            }
            // Parallel: keep the dry signal, in pieces no longer than the
            // buffer prepare() made, so nothing allocates here.
            const int chunk = static_cast<int>(dry_[0].size());
            for (int off = 0; off < numSamples; off += chunk)
            {
                const int m = std::min(chunk, numSamples - off);
                float* dl = dry_[0].data();
                float* dr = dry_[1].data();
                std::copy(l + off, l + off + m, dl);
                if (r != nullptr) std::copy(r + off, r + off + m, dr);
                comp_.process(l + off, r != nullptr ? r + off : nullptr, m);
                for (int i = 0; i < m; ++i)
                {
                    l[off + i] = dl[i] + mix * (l[off + i] - dl[i]);
                    if (r != nullptr)
                        r[off + i] = dr[i] + mix * (r[off + i] - dr[i]);
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
                    p.skew = 0.4f; p.unit = ParamSpec::Unit::Ms; p.minLabel = "Auto";
                    break;
                case 3:
                    p.id = "lockstep.buscomp.release";
                    p.label = "Rel";
                    // Continuous ms; 0 = Auto (program-dependent release).
                    p.minValue = 0.0f; p.maxValue = 1200.0f; p.defaultValue = 250.0f;
                    p.skew = 0.4f; p.unit = ParamSpec::Unit::Ms; p.minLabel = "Auto";
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
        chalkwalk::dsp::Compressor comp_;
        float last_[6] = {};
        bool have_ = false;
        std::vector<float> dry_[2];
    };
}
