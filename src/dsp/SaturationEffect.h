#pragma once

#include "../machine/IEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace lockstep
{
    // Tape-style saturation — gentler and gluier than DistortionEffect's harsher
    // drive. A single class presents two placement-aware faces (DESIGN, effect
    // quality tiers):
    //
    //   Track (LQ, 4 params): Drive / Tone / Mix / Output.
    //   Master (HQ, 8 params): adds Bias (even-harmonic warmth), Comp (program
    //     compression), Crisp (pre-emphasis), Low (low shelf).
    //
    // Both faces run the saturating nonlinearity through 4x oversampling
    // (juce::dsp::Oversampling, minimum-phase IIR polyphase — near-zero latency,
    // no PDC) so the tanh curve stays alias-suppressed (9.24 S5). One mono
    // oversampler per channel matches the per-channel filter state. 4x (not 2x)
    // because a hot high-frequency tanh is near-square: at 2x its 5th harmonic
    // still folds below Nyquist. 4x drops mid/high-band aliasing ~26 dB vs no OS.
    //
    // Tier is fixed at construction (makeEffectForId picks it from slot placement);
    // it never changes for a given slot, so the param schema is stable per slot.
    class SaturationEffect final : public IEffect
    {
    public:
        explicit SaturationEffect(EffectTier tier) : hq_(tier == EffectTier::Master) {}

        void prepare(double sampleRate, int maxBlockSize) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            // Program-compression envelope follower: ~30ms.
            envCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.030 * sampleRate));
            const int maxB = std::max(1, maxBlockSize);
            for (auto& os : os_)
            {
                os = std::make_unique<juce::dsp::Oversampling<float>>(
                    1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
                os->initProcessing(static_cast<std::size_t>(maxB));
            }
            dryScratch_.assign(static_cast<std::size_t>(maxB), 0.0f);
            reset();
        }

        void reset() override
        {
            for (auto& z : toneZ_) z = 0.0f;
            for (auto& z : lowZ_)  z = 0.0f;
            for (auto& z : preZ_)  z = 0.0f;
            for (auto& z : env_)   z = 0.0f;
            for (auto& os : os_)   if (os) os->reset();
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

            const int nOs = std::min(numSamples, static_cast<int>(dryScratch_.size()));

            for (int c = 0; c < ch; ++c)
            {
                auto* data = buffer.getWritePointer(c);
                const int ci = std::min(c, 1);
                const auto si = static_cast<std::size_t>(ci);

                // Pass 1 (base rate): compute the pre-nonlinearity signal
                // (g*in + bias) in place, stashing the dry input for the final mix.
                // Drive smoothing + HQ pre-emphasis and program compression all
                // live at the base rate; only the memoryless tanh needs 2x.
                for (int i = 0; i < nOs; ++i)
                {
                    driveZ_ += smoothCoef_ * (driveTarget - driveZ_);

                    const float dry = data[i];
                    dryScratch_[static_cast<std::size_t>(i)] = dry;
                    float in = dry;

                    if (hq_ && crisp > 0.0f)
                    {
                        preZ_[si] += 0.5f * (in - preZ_[si]);          // one-pole LP
                        in = in + crisp * (in - preZ_[si]);            // + HF emphasis
                    }

                    float g = driveZ_;
                    if (hq_ && comp > 0.0f)
                    {
                        env_[si] += envCoef_ * (std::abs(in) - env_[si]);
                        g *= 1.0f / (1.0f + comp * 2.0f * env_[si]);
                    }

                    data[i] = g * in + bias;
                }

                // Oversample the pre-shape signal, apply the tanh at 4x, decimate.
                float* ptr = data;
                juce::dsp::AudioBlock<float> block(&ptr, 1, static_cast<std::size_t>(nOs));
                auto up = os_[si]->processSamplesUp(block);
                float* upd = up.getChannelPointer(0);
                const int un = static_cast<int>(up.getNumSamples());
                for (int k = 0; k < un; ++k)
                    upd[k] = shape(upd[k]) - biasOffset;
                os_[si]->processSamplesDown(block);   // data[] now holds the wet signal

                // Pass 3 (base rate): HQ low shelf + HF softening + dry/wet mix.
                for (int i = 0; i < nOs; ++i)
                {
                    mixZ_ += smoothCoef_ * (mixTarget - mixZ_);
                    outZ_ += smoothCoef_ * (outGain - outZ_);

                    float wet = data[i];
                    if (hq_ && lowAmt != 0.0f)
                    {
                        lowZ_[si] += 0.08f * (wet - lowZ_[si]);        // one-pole LP (lows)
                        wet += lowAmt * lowZ_[si];
                    }

                    toneZ_[si] += toneCoef * (wet - toneZ_[si]);
                    wet = toneZ_[si];

                    data[i] = (dryScratch_[static_cast<std::size_t>(i)]
                               * (1.0f - mixZ_) + wet * mixZ_) * outZ_;
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
        // One mono 2x oversampler per channel (built in prepare, so process() is
        // allocation-free). dryScratch_ holds one channel's dry input across the
        // oversampled shaping for the final dry/wet mix.
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> os_{};
        std::vector<float> dryScratch_;
        float driveZ_ = 1.0f;
        float mixZ_ = 1.0f;
        float outZ_ = 1.0f;
    };
}
