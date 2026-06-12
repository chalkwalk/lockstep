#pragma once

#include "../machine/IEffect.h"
#include <cmath>
#include <array>
#include <vector>

namespace lockstep
{
    // HQ Reverb — 8-line FDN with Hadamard feedback matrix.
    // Per-line modulated delay (slow LFO), one-pole damping, 2-allpass diffuser.
    // Pre-delay buffer. Mix defaults to 1.0 (wet-only for send slot use).
    // masterOnly = true.
    class HQReverbEffect final : public IEffect
    {
        static constexpr int kLines = 8;
        static constexpr int kFxSec = 5;

    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.01 * sampleRate));

            // Pre-delay: up to 200ms
            preDelayBuf_.assign(static_cast<std::size_t>(sampleRate * 0.201), 0.0f);
            preHead_ = 0;

            // FDN delay lines — prime-number-like lengths for density.
            static constexpr double kLengthsMs[] = {
                29.7, 37.1, 43.1, 53.3, 61.7, 71.9, 79.3, 89.7
            };
            for (int l = 0; l < kLines; ++l)
            {
                const int len = static_cast<int>(sampleRate * kLengthsMs[l] * 0.001) + 1;
                lines_[static_cast<std::size_t>(l)].assign(static_cast<std::size_t>(len), 0.0f);
                heads_[static_cast<std::size_t>(l)] = 0;
                dampZ_[static_cast<std::size_t>(l)] = 0.0f;
                modPhase_[static_cast<std::size_t>(l)] =
                    static_cast<float>(l) * static_cast<float>(juce::MathConstants<double>::twoPi) / kLines;
            }

            // Allpass diffusers (2 stages per channel).
            const int apLen = static_cast<int>(sampleRate * 0.005);  // ~5ms
            for (auto& ap : apBuf_) ap.assign(static_cast<std::size_t>(apLen), 0.0f);
            for (auto& h : apHead_) h = 0;

            for (auto& z : mixZ_) z = 1.0f;
        }

        void reset() override
        {
            std::fill(preDelayBuf_.begin(), preDelayBuf_.end(), 0.0f);
            preHead_ = 0;
            for (int l = 0; l < kLines; ++l)
            {
                std::fill(lines_[static_cast<std::size_t>(l)].begin(),
                          lines_[static_cast<std::size_t>(l)].end(), 0.0f);
                heads_[static_cast<std::size_t>(l)] = 0;
                dampZ_[static_cast<std::size_t>(l)] = 0.0f;
            }
            for (auto& ap : apBuf_) std::fill(ap.begin(), ap.end(), 0.0f);
            for (auto& h : apHead_) h = 0;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float predelayMs = params.size() > 0
                                         ? juce::jlimit(0.0f, 200.0f, params[0])
                                         : 0.0f;
            const float size  = params.size() > 1 ? juce::jlimit(0.0f, 1.0f, params[1]) : 0.5f;
            const float decay = params.size() > 2 ? juce::jlimit(0.2f, 20.0f, params[2]) : 3.0f;
            const float damp  = params.size() > 3 ? juce::jlimit(0.0f, 1.0f, params[3]) : 0.5f;
            const float lowcut = params.size() > 4 ? juce::jlimit(20.0f, 500.0f, params[4]) : 80.0f;
            const float mod   = params.size() > 5 ? juce::jlimit(0.0f, 1.0f, params[5]) : 0.3f;
            const float mixTgt = params.size() > 6 ? juce::jlimit(0.0f, 1.0f, params[6]) : 1.0f;

            // Per-line feedback gain from decay time and length.
            // g = 10^(-3 * len_s / decay)
            const float pdSamples = juce::jlimit(0.0f, static_cast<float>(preDelayBuf_.size() - 1),
                                                  predelayMs * 0.001f * static_cast<float>(sr_));
            const float dampCoef  = 1.0f - std::exp(-1.0f / static_cast<float>(0.001 * sr_));
            const float dampTarget = damp * 0.85f;
            const float lcAlpha   = 1.0f - std::exp(
                -2.0f * static_cast<float>(M_PI) * lowcut / static_cast<float>(sr_));
            const float modDepth  = mod * 4.0f;  // ±4 samples max modulation
            const float modRate   = 0.5f;
            const float modPhInc  = static_cast<float>(
                modRate * juce::MathConstants<double>::twoPi / sr_);

            for (int n = 0; n < numSamples; ++n)
            {
                // Sum both input channels as mono input.
                float input = 0.0f;
                for (int c = 0; c < numCh; ++c)
                    input += buffer.getReadPointer(c)[n];
                input *= (numCh > 1) ? 0.5f : 1.0f;

                // Low-cut on input.
                lcZ_ += lcAlpha * (input - lcZ_);
                const float filtered = input - lcZ_;

                // Pre-delay. Clamp to at least 1 sample so the read position never
                // equals the write position (read-before-write in ring buffer); at
                // predelay=0ms this gives ~0.02ms of pre-delay, which is inaudible.
                const int pdLen = static_cast<int>(preDelayBuf_.size());
                const int pdSamplesInt = std::max(1, static_cast<int>(pdSamples));
                const int pdRd = ((preHead_ - pdSamplesInt + pdLen) % pdLen);
                const float preOut = preDelayBuf_[static_cast<std::size_t>(pdRd)];
                preDelayBuf_[static_cast<std::size_t>(preHead_)] = filtered;
                preHead_ = (preHead_ + 1) % pdLen;

                // Input diffusion through 2 allpass stages.
                float diff = preOut;
                for (int s = 0; s < 2; ++s)
                {
                    const int apLen = static_cast<int>(apBuf_[static_cast<std::size_t>(s)].size());
                    const int rd = (apHead_[static_cast<std::size_t>(s)] + apLen - apLen / 2) % apLen;
                    const float apy = apBuf_[static_cast<std::size_t>(s)][static_cast<std::size_t>(rd)];
                    const float apIn = diff + 0.5f * apy;
                    apBuf_[static_cast<std::size_t>(s)][static_cast<std::size_t>(apHead_[static_cast<std::size_t>(s)])] = apIn;
                    apHead_[static_cast<std::size_t>(s)] = (apHead_[static_cast<std::size_t>(s)] + 1) % apLen;
                    diff = apy - 0.5f * apIn;
                }

                // Read from FDN lines, sum as input vector.
                float lineVals[kLines];
                for (int l = 0; l < kLines; ++l)
                {
                    auto& line = lines_[static_cast<std::size_t>(l)];
                    const int len = static_cast<int>(line.size());

                    // Modulated read offset.
                    const float lfoVal = std::sin(modPhase_[static_cast<std::size_t>(l)]);
                    modPhase_[static_cast<std::size_t>(l)] += modPhInc;
                    if (modPhase_[static_cast<std::size_t>(l)] >= static_cast<float>(juce::MathConstants<double>::twoPi))
                        modPhase_[static_cast<std::size_t>(l)] -= static_cast<float>(juce::MathConstants<double>::twoPi);

                    const float sizeOffset = size * static_cast<float>(len) * 0.5f;
                    const float rd = static_cast<float>(heads_[static_cast<std::size_t>(l)]) - sizeOffset
                                     - modDepth * (0.5f + 0.5f * lfoVal);
                    const int ri = ((static_cast<int>(rd) % len) + len) % len;
                    lineVals[l] = line[static_cast<std::size_t>(ri)];
                }

                // Hadamard mix (unnormalized, scaled by 1/sqrt(8) ≈ 0.354).
                float had[kLines];
                const float hScale = 0.354f;
                had[0] = (lineVals[0] + lineVals[1] + lineVals[2] + lineVals[3] + lineVals[4] + lineVals[5] + lineVals[6] + lineVals[7]) * hScale;
                had[1] = (lineVals[0] - lineVals[1] + lineVals[2] - lineVals[3] + lineVals[4] - lineVals[5] + lineVals[6] - lineVals[7]) * hScale;
                had[2] = (lineVals[0] + lineVals[1] - lineVals[2] - lineVals[3] + lineVals[4] + lineVals[5] - lineVals[6] - lineVals[7]) * hScale;
                had[3] = (lineVals[0] - lineVals[1] - lineVals[2] + lineVals[3] + lineVals[4] - lineVals[5] - lineVals[6] + lineVals[7]) * hScale;
                had[4] = (lineVals[0] + lineVals[1] + lineVals[2] + lineVals[3] - lineVals[4] - lineVals[5] - lineVals[6] - lineVals[7]) * hScale;
                had[5] = (lineVals[0] - lineVals[1] + lineVals[2] - lineVals[3] - lineVals[4] + lineVals[5] - lineVals[6] + lineVals[7]) * hScale;
                had[6] = (lineVals[0] + lineVals[1] - lineVals[2] - lineVals[3] - lineVals[4] - lineVals[5] + lineVals[6] + lineVals[7]) * hScale;
                had[7] = (lineVals[0] - lineVals[1] - lineVals[2] + lineVals[3] - lineVals[4] + lineVals[5] + lineVals[6] - lineVals[7]) * hScale;

                // Write back with feedback + input injection.
                float reverbOut = 0.0f;
                for (int l = 0; l < kLines; ++l)
                {
                    auto& line = lines_[static_cast<std::size_t>(l)];
                    const int len = static_cast<int>(line.size());

                    // Per-line gain from decay.
                    const float lenS = static_cast<float>(len) / static_cast<float>(sr_);
                    const float g = std::pow(10.0f, -3.0f * lenS / decay);

                    // Per-line damping (one-pole LP in feedback path).
                    dampZ_[static_cast<std::size_t>(l)] +=
                        dampCoef * (dampTarget - dampZ_[static_cast<std::size_t>(l)]);
                    had[l] = had[l] * g;
                    dampFbk_[static_cast<std::size_t>(l)] +=
                        (1.0f - dampZ_[static_cast<std::size_t>(l)]) * (had[l] - dampFbk_[static_cast<std::size_t>(l)]);

                    line[static_cast<std::size_t>(heads_[static_cast<std::size_t>(l)])] =
                        dampFbk_[static_cast<std::size_t>(l)] + diff * (1.0f / kLines);
                    heads_[static_cast<std::size_t>(l)] = (heads_[static_cast<std::size_t>(l)] + 1) % len;

                    reverbOut += lineVals[l];
                }
                reverbOut *= (1.0f / kLines);

                // Mix and write to output channels.
                for (int c = 0; c < numCh; ++c)
                {
                    auto& mz = mixZ_[static_cast<std::size_t>(std::min(c, 1))];
                    mz += smoothCoef_ * (mixTgt - mz);
                    const float dry = buffer.getReadPointer(c)[n];
                    buffer.getWritePointer(c)[n] = dry * (1.0f - mz) + reverbOut * mz;
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
                    p.id = "lockstep.verbhq.predelay";
                    p.label = "PreDly";
                    p.minValue = 0.0f; p.maxValue = 200.0f; p.defaultValue = 0.0f;
                    p.skew = 0.4f;
                    break;
                case 1:
                    p.id = "lockstep.verbhq.size";
                    p.label = "Size";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.5f;
                    break;
                case 2:
                    p.id = "lockstep.verbhq.decay";
                    p.label = "Decay";
                    p.minValue = 0.2f; p.maxValue = 20.0f; p.defaultValue = 3.0f;
                    p.skew = 0.3f;
                    break;
                case 3:
                    p.id = "lockstep.verbhq.damp";
                    p.label = "Damp";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.5f;
                    break;
                case 4:
                    p.id = "lockstep.verbhq.lowcut";
                    p.label = "LoCut";
                    p.minValue = 20.0f; p.maxValue = 500.0f; p.defaultValue = 80.0f;
                    p.skew = 0.4f;
                    break;
                case 5:
                    p.id = "lockstep.verbhq.mod";
                    p.label = "Mod";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.3f;
                    break;
                default:
                    p.id = "lockstep.verbhq.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 1.0f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "RVH"; }

    private:
        static inline const std::string kId = "lockstep.verbhq.v1";

        double sr_ = 44100.0;
        float smoothCoef_ = 0.01f;

        std::vector<float> preDelayBuf_;
        int preHead_ = 0;
        float lcZ_ = 0.0f;

        std::array<std::vector<float>, kLines> lines_;
        int  heads_[kLines] = {};
        float modPhase_[kLines] = {};
        float dampZ_[kLines] = {};
        float dampFbk_[kLines] = {};

        std::vector<float> apBuf_[2];
        int apHead_[2] = { 0, 0 };

        float mixZ_[2] = { 1.0f, 1.0f };
    };
}
