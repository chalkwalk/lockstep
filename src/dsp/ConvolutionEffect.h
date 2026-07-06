#pragma once

#include "../machine/IEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace lockstep
{
    // Convolution reverb (9.24 S16) wrapping juce::dsp::Convolution in its default
    // zero-latency configuration (uniform-partitioned — no PDC, drops onto the
    // zero-latency chain). Params: IR-select (Pool | Bundled 1..N), Pre-delay,
    // Damp (wet LP), Mix. id lockstep.conv.v1.
    //
    // The IR comes from one of two sources:
    //   - Pool (select 0): a pool sample handed in via setImpulseResponse() — the
    //     processor resolves the slot's irRef (v31, S15) and pushes the PCM on the
    //     message thread. loadImpulseResponse() is wait-free; the swap completes on
    //     the Convolution's background thread and goes live on a later block.
    //   - Bundled (select 1..N): a built-in IR (S17 ships baked WAVs via BinaryData;
    //     until then loadBundledIr synthesises a deterministic decay so the effect
    //     is never silent).
    // Bypass cuts the tail (like every insert here — consistent, documented).
    class ConvolutionEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;
        static constexpr int kNumBundled = 4;
        static constexpr const char* const kSelLabels[] = {
            "Pool", "Bndl1", "Bndl2", "Bndl3", "Bndl4"
        };
        static constexpr int kNumSel = 1 + kNumBundled;

    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sr_ = sampleRate;
            const int maxB = std::max(1, maxBlockSize);
            juce::dsp::ProcessSpec spec{ sampleRate,
                static_cast<juce::uint32>(maxB), 2 };
            conv_.prepare(spec);
            wet_.setSize(2, maxB);
            const int pdLen = static_cast<int>(sampleRate * 0.201) + 1;  // 200 ms max
            for (auto& b : pd_) b.assign(static_cast<std::size_t>(pdLen), 0.0f);
            for (auto& h : pdHead_) h = 0;
            for (auto& z : dampZ_) z = 0.0f;
            prepared_ = true;
            lastSel_ = -1;   // force (re)load on the next process
            if (havePoolIr_) poolDirty_ = true;
        }

        void reset() override
        {
            conv_.reset();
            for (auto& b : pd_) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : pdHead_) h = 0;
            for (auto& z : dampZ_) z = 0.0f;
        }

        void setImpulseResponse(const juce::AudioBuffer<float>& ir,
                                double irSampleRate) override
        {
            poolIr_.makeCopyOf(ir);
            poolSr_ = irSampleRate > 0.0 ? irSampleRate : sr_;
            havePoolIr_ = ir.getNumSamples() > 0;
            poolDirty_ = true;   // process() loads it once when Pool is selected
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const int sel = params.size() > 0
                                ? juce::jlimit(0, kNumSel - 1, static_cast<int>(params[0])) : 0;
            const float predelayMs = params.size() > 1
                                         ? juce::jlimit(0.0f, 200.0f, params[1]) : 0.0f;
            const float damp = params.size() > 2 ? juce::jlimit(0.0f, 1.0f, params[2]) : 0.0f;
            const float mix  = params.size() > 3 ? juce::jlimit(0.0f, 1.0f, params[3]) : 0.3f;

            if (sel != lastSel_)
            {
                lastSel_ = sel;
                if (sel == 0) poolDirty_ = true;   // switch to the pool IR
                else          loadBundledIr(sel - 1);
            }
            if (sel == 0 && poolDirty_)
            {
                poolDirty_ = false;
                loadPool();
            }

            const int nCh = std::min(ch, 2);
            const int pdSamp = std::min(static_cast<int>(predelayMs * 0.001f
                                                         * static_cast<float>(sr_)),
                                        static_cast<int>(pd_[0].size()) - 1);

            // Wet path: predelayed copy of the input, convolved, damped.
            for (int c = 0; c < nCh; ++c)
            {
                const float* in = buffer.getReadPointer(c);
                float* w = wet_.getWritePointer(c);
                auto& line = pd_[static_cast<std::size_t>(c)];
                const int len = static_cast<int>(line.size());
                int& wr = pdHead_[static_cast<std::size_t>(c)];
                for (int i = 0; i < numSamples; ++i)
                {
                    // Write current, then read `pdSamp` back — at pdSamp=0 this is a
                    // passthrough (reading the just-written sample), not a full-buffer
                    // delay.
                    line[static_cast<std::size_t>(wr)] = in[i];
                    const int rd = (wr - pdSamp + len) % len;
                    w[i] = line[static_cast<std::size_t>(rd)];
                    wr = (wr + 1) % len;
                }
            }

            std::array<float*, 2> chans{ wet_.getWritePointer(0),
                                         nCh > 1 ? wet_.getWritePointer(1) : nullptr };
            juce::dsp::AudioBlock<float> block(chans.data(),
                static_cast<std::size_t>(nCh), static_cast<std::size_t>(numSamples));
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            conv_.process(ctx);

            const float dampCoef = 1.0f - damp * 0.98f;  // 1 = open, small = dark
            for (int c = 0; c < nCh; ++c)
            {
                float* data = buffer.getWritePointer(c);
                const float* w = wet_.getReadPointer(c);
                auto& dz = dampZ_[static_cast<std::size_t>(c)];
                for (int i = 0; i < numSamples; ++i)
                {
                    dz += dampCoef * (w[i] - dz);
                    data[i] = data[i] * (1.0f - mix) + dz * mix;
                }
            }
        }

        int numParams() const override { return 4; }

        ParamSpec paramSpec(int i) const override
        {
            static const std::span<const char* const> kSelSpan{ kSelLabels,
                static_cast<std::size_t>(kNumSel) };
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (i)
            {
                case 0:
                    p.id = "lockstep.conv.ir";
                    p.label = "IR";
                    p.minValue = 0.0f; p.maxValue = static_cast<float>(kNumSel - 1);
                    // Default = Pool: a picked pool IR (S16 gesture) takes effect with
                    // no extra param move; a fresh insert with no IR is simply inert
                    // (you pick an IR, as you must for any convolver). Bundled slots
                    // (S17) are reachable by advancing the param.
                    p.defaultValue = 0.0f;
                    p.isStepped = true; p.valueLabels = kSelSpan;
                    break;
                case 1:
                    p.id = "lockstep.conv.predelay";
                    p.label = "PreDly";
                    p.minValue = 0.0f; p.maxValue = 200.0f; p.defaultValue = 0.0f;
                    p.skew = 0.4f;
                    break;
                case 2:
                    p.id = "lockstep.conv.damp";
                    p.label = "Damp";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.3f;
                    break;
                default:
                    p.id = "lockstep.conv.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.3f;
                    break;
            }
            return p;
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "CNV"; }

        // Test/UI introspection (juce::dsp::Convolution loads on a background
        // thread, so callers spin until the IR is live).
        [[nodiscard]] int currentIrSize() const { return conv_.getCurrentIRSize(); }
        [[nodiscard]] int reportedLatency() const { return conv_.getLatency(); }

    private:
        void loadPool()
        {
            if (!havePoolIr_ || poolIr_.getNumSamples() == 0) return;
            juce::AudioBuffer<float> copy;
            copy.makeCopyOf(poolIr_);
            conv_.loadImpulseResponse(std::move(copy), poolSr_,
                juce::dsp::Convolution::Stereo::no,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);
        }

        // S17 replaces this body with a BinaryData read of a baked WAV. Until then
        // it synthesises a deterministic exponential-decay noise IR so the Bundled
        // slots are never silent. `n` in [0, kNumBundled).
        void loadBundledIr(int n)
        {
            const int len = static_cast<int>(sr_ * (0.6 + 0.4 * n));  // 0.6..1.8 s
            juce::AudioBuffer<float> ir(1, std::max(1, len));
            float* d = ir.getWritePointer(0);
            std::uint32_t rng = 0x1234567u + static_cast<std::uint32_t>(n) * 2654435761u;
            const float tau = static_cast<float>(sr_) * (0.15f + 0.1f * static_cast<float>(n));
            for (int i = 0; i < len; ++i)
            {
                rng = rng * 1664525u + 1013904223u;
                const float noise = static_cast<float>(rng >> 9) * (1.0f / 8388608.0f) - 1.0f;
                d[i] = noise * std::exp(-static_cast<float>(i) / tau);
            }
            conv_.loadImpulseResponse(std::move(ir), sr_,
                juce::dsp::Convolution::Stereo::no,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::yes);
        }

        static inline const std::string kId = "lockstep.conv.v1";
        double sr_ = 44100.0;
        bool prepared_ = false;
        int lastSel_ = -1;
        juce::dsp::Convolution conv_;
        juce::AudioBuffer<float> wet_;
        std::array<std::vector<float>, 2> pd_;
        std::array<int, 2> pdHead_{};
        std::array<float, 2> dampZ_{};
        juce::AudioBuffer<float> poolIr_;
        double poolSr_ = 44100.0;
        bool havePoolIr_ = false;
        bool poolDirty_ = false;
    };
}
