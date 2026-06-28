#pragma once

#include "../machine/IEffect.h"
#include "Oversampler2x.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace lockstep
{
    // Tape-style saturation — gentler and gluier than DistortionEffect's harsher
    // drive. A single class presents two placement-aware faces (DESIGN, effect
    // quality tiers):
    //
    //   Track (LQ, 4 params): Drive / Tone / Mix / Output. No oversampling.
    //   Master (HQ, 8 params): adds Bias (even-harmonic warmth), Comp (program
    //     compression), Crisp (pre-emphasis), Low (low shelf), and runs the
    //     saturating nonlinearity at 2x via a halfband oversampler for a clean,
    //     alias-free bus/master glue.
    //
    // Tier is fixed at construction (makeEffectForId picks it from slot placement);
    // it never changes for a given slot, so the param schema is stable per slot.
    class SaturationEffect final : public IEffect
    {
    public:
        explicit SaturationEffect(EffectTier tier) : hq_(tier == EffectTier::Master) {}

        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            // Program-compression envelope follower: ~30ms.
            envCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.030 * sampleRate));
            reset();
        }

        void reset() override
        {
            for (auto& z : toneZ_) z = 0.0f;
            for (auto& z : lowZ_)  z = 0.0f;
            for (auto& z : preZ_)  z = 0.0f;
            for (auto& z : env_)   z = 0.0f;
            for (auto& os : os_)   os.reset();
            driveZ_ = 1.0f;
            mixZ_ = 1.0f;
            outZ_ = 1.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const auto p = [&](int i, float def) {
                return params.size() > static_cast<std::size_t>(i)
                           ? params[static_cast<std::size_t>(i)] : def;
            };

            // Shared four (both tiers).
            const float drive = p(0, 0.35f);
            const float driveTarget = 1.0f + drive * 8.0f;       // 1..9x into the curve
            float tone, mixTarget, outTarget;
            float bias = 0.0f, comp = 0.0f, crisp = 0.0f, lowAmt = 0.0f;
            if (hq_)
            {
                bias      = p(1, 0.0f) * 0.6f;   // asymmetry
                comp      = p(2, 0.25f);          // program compression amount
                crisp     = p(3, 0.0f);           // pre-emphasis (highs into the curve)
                tone      = p(4, 0.5f);           // HF shelf (post)
                lowAmt    = p(5, 0.5f) - 0.5f;    // low shelf, centred (0.5 = flat)
                mixTarget = p(6, 1.0f);
                outTarget = p(7, 0.5f);
            }
            else
            {
                tone      = p(1, 0.5f);
                mixTarget = p(2, 1.0f);
                outTarget = p(3, 0.5f);
            }
            const float toneCoef = toneCoefFor(tone);
            const float outGain = outputGainFor(outTarget);
            const float biasOffset = std::tanh(bias);  // DC the bias introduces

            for (int c = 0; c < ch; ++c)
            {
                auto* data = buffer.getWritePointer(c);
                const int ci = std::min(c, 1);
                const auto si = static_cast<std::size_t>(ci);
                for (int i = 0; i < numSamples; ++i)
                {
                    driveZ_ += smoothCoef_ * (driveTarget - driveZ_);
                    mixZ_   += smoothCoef_ * (mixTarget - mixZ_);
                    outZ_   += smoothCoef_ * (outGain - outZ_);

                    const float dry = data[i];
                    float in = dry;

                    // HQ pre-emphasis: lift highs into the saturator (tape pre/de).
                    if (hq_ && crisp > 0.0f)
                    {
                        preZ_[si] += 0.5f * (in - preZ_[si]);          // one-pole LP
                        in = in + crisp * (in - preZ_[si]);            // + HF emphasis
                    }

                    // Program compression: reduce drive as the signal gets hot.
                    float g = driveZ_;
                    if (hq_ && comp > 0.0f)
                    {
                        env_[si] += envCoef_ * (std::abs(in) - env_[si]);
                        g *= 1.0f / (1.0f + comp * 2.0f * env_[si]);
                    }

                    float wet;
                    if (hq_)
                    {
                        float a, b;
                        os_[si].upsample(in, a, b);
                        a = shape(g * a + bias) - biasOffset;
                        b = shape(g * b + bias) - biasOffset;
                        wet = os_[si].decimate(a, b);
                    }
                    else
                    {
                        wet = shape(g * in);
                    }

                    // HQ low shelf (tape low-end character).
                    if (hq_ && lowAmt != 0.0f)
                    {
                        lowZ_[si] += 0.08f * (wet - lowZ_[si]);        // one-pole LP (lows)
                        wet += lowAmt * lowZ_[si];
                    }

                    // HF softening (post), both tiers.
                    toneZ_[si] += toneCoef * (wet - toneZ_[si]);
                    wet = toneZ_[si];

                    data[i] = (dry * (1.0f - mixZ_) + wet * mixZ_) * outZ_;
                }
            }
        }

        int numParams() const override { return hq_ ? 8 : 4; }

        ParamSpec paramSpec(int i) const override
        {
            static constexpr int kFxSec = 5;
            auto mk = [](const char* id, const char* label, float def) {
                ParamSpec s;
                s.id = id;
                s.label = label;
                s.maxValue = 1.0f;
                s.defaultValue = def;
                s.sectionIndex = kFxSec;
                return s;
            };
            if (!hq_)
            {
                switch (i)
                {
                    case 0: return mk("lockstep.sat.drive", "Drive",  0.35f);
                    case 1: return mk("lockstep.sat.tone",  "Tone",   0.5f);
                    case 2: return mk("lockstep.sat.mix",   "Mix",    1.0f);
                    case 3: return mk("lockstep.sat.out",   "Output", 0.5f);
                    default: return {};
                }
            }
            switch (i)
            {
                case 0: return mk("lockstep.sat.drive", "Drive",  0.35f);
                case 1: return mk("lockstep.sat.bias",  "Bias",   0.0f);
                case 2: return mk("lockstep.sat.comp",  "Comp",   0.25f);
                case 3: return mk("lockstep.sat.crisp", "Crisp",  0.0f);
                case 4: return mk("lockstep.sat.tone",  "Tone",   0.5f);
                case 5: return mk("lockstep.sat.low",   "Low",    0.5f);
                case 6: return mk("lockstep.sat.mix",   "Mix",    1.0f);
                case 7: return mk("lockstep.sat.out",   "Output", 0.5f);
                default: return {};
            }
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return hq_ ? juce::String("SATH") : juce::String("SAT"); }

    private:
        // Tape-style soft saturation: smooth, gentle, gluey. Unity slope at 0.
        static float shape(float u) noexcept { return std::tanh(u); }

        // Tone 0 -> dark (slow LP), 1 -> open (near bypass).
        float toneCoefFor(float tone) const noexcept
        {
            const float t = std::clamp(tone, 0.0f, 1.0f);
            return 0.02f + t * t * 0.98f;
        }

        // Output 0.5 -> unity; range roughly -12..+6 dB.
        static float outputGainFor(float v) noexcept
        {
            const float db = (std::clamp(v, 0.0f, 1.0f) - 0.5f) * 24.0f;  // -12..+12
            return std::pow(10.0f, db / 20.0f);
        }

        static inline const std::string kId = "lockstep.saturation.v1";
        bool hq_ = false;
        double sr_ = 44100.0;
        float smoothCoef_ = 0.005f;
        float envCoef_ = 0.001f;
        std::array<float, 2> toneZ_{};
        std::array<float, 2> lowZ_{};
        std::array<float, 2> preZ_{};
        std::array<float, 2> env_{};
        std::array<dsp::Oversampler2x, 2> os_{};
        float driveZ_ = 1.0f;
        float mixZ_ = 1.0f;
        float outZ_ = 1.0f;
    };
}
