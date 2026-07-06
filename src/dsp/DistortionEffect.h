#pragma once

#include "../machine/IEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace lockstep
{
    // Soft-clip distortion — Drive / Tone (post-LPF) / Mix.
    // Per-sample smoothing on drive and mix (~5ms) to prevent zipper noise.
    // The tanh runs through 8x oversampling (juce::dsp::Oversampling, min-phase
    // IIR polyphase — near-zero latency, no PDC) so the hard clip stays
    // alias-suppressed (9.24 S7). 8x, not the 4x used by Saturation, because
    // drive reaches 20x here (vs 9x) — a far wider harmonic series; at 4x the
    // residual alias was only ~-24 dB. It's a per-insert effect (not per-voice),
    // so the extra stages are affordable. One mono oversampler per channel.
    class DistortionEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sampleRate_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            for (auto& z : lpfZ_) z = 0.0f;
            for (auto& z : driveZ_) z = 1.0f;
            for (auto& z : mixZ_) z = 0.5f;
            const int maxB = std::max(1, maxBlockSize);
            for (auto& os : os_)
            {
                os = std::make_unique<juce::dsp::Oversampling<float>>(
                    1, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
                os->initProcessing(static_cast<std::size_t>(maxB));
            }
            dryScratch_.assign(static_cast<std::size_t>(maxB), 0.0f);
        }

        void reset() override
        {
            for (auto& z : lpfZ_) z = 0.0f;
            for (auto& os : os_) if (os) os->reset();
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const float driveTarget = 1.0f + (params.size() > 0 ? params[0] : 0.3f) * 19.0f;
            const float tone = params.size() > 1 ? params[1] : 1.0f;
            const float mixTarget = params.size() > 2 ? params[2] : 0.5f;

            const int nOs = std::min(numSamples, static_cast<int>(dryScratch_.size()));

            for (int c = 0; c < ch; ++c)
            {
                auto* data = buffer.getWritePointer(c);
                const int ci = std::min(c, 1);
                const auto si = static_cast<std::size_t>(ci);
                auto& lpZ = lpfZ_[si];
                auto& driveZ = driveZ_[si];
                auto& mixZ = mixZ_[si];

                // Pass 1: base-rate drive smoothing -> pre-clip signal (dry*drive),
                // stash dry for the final mix. Only the memoryless tanh needs 8x.
                for (int i = 0; i < nOs; ++i)
                {
                    driveZ += smoothCoef_ * (driveTarget - driveZ);
                    const float dry = data[i];
                    dryScratch_[static_cast<std::size_t>(i)] = dry;
                    data[i] = dry * driveZ;
                }

                // Oversample -> tanh -> decimate.
                float* ptr = data;
                juce::dsp::AudioBlock<float> block(&ptr, 1, static_cast<std::size_t>(nOs));
                auto up = os_[si]->processSamplesUp(block);
                float* upd = up.getChannelPointer(0);
                const int un = static_cast<int>(up.getNumSamples());
                for (int k = 0; k < un; ++k)
                    upd[k] = std::tanh(upd[k]);
                os_[si]->processSamplesDown(block);

                // Pass 3: base-rate tone LP + dry/wet mix.
                for (int i = 0; i < nOs; ++i)
                {
                    mixZ += smoothCoef_ * (mixTarget - mixZ);
                    lpZ += tone * (data[i] - lpZ);
                    data[i] = dryScratch_[static_cast<std::size_t>(i)] * (1.0f - mixZ)
                              + lpZ * mixZ;
                }
            }
        }

        int numParams() const override { return 3; }

        ParamSpec paramSpec(int i) const override
        {
            static constexpr int kFxSec = 5;
            switch (i)
            {
                case 0: {
                    ParamSpec p;
                    p.id = "lockstep.dist.drive";
                    p.label = "Drive";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.3f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 1: {
                    ParamSpec p;
                    p.id = "lockstep.dist.tone";
                    p.label = "Tone";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                case 2: {
                    ParamSpec p;
                    p.id = "lockstep.dist.mix";
                    p.label = "Mix";
                    p.maxValue = 1.0f;
                    p.defaultValue = 0.5f;
                    p.sectionIndex = kFxSec;
                    return p;
                }
                default: return {};
            }
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "DRV"; }

    private:
        static inline const std::string kId = "lockstep.distortion.v1";
        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.005f;
        std::array<float, 2> lpfZ_{};
        std::array<float, 2> driveZ_{ 1.0f, 1.0f };
        std::array<float, 2> mixZ_{ 0.5f, 0.5f };
        // One mono 8x oversampler per channel (built in prepare, allocation-free
        // process). dryScratch_ holds one channel's dry across the oversampled clip.
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> os_{};
        std::vector<float> dryScratch_;
    };
}
