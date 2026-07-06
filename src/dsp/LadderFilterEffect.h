#pragma once

#include "../machine/IEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace lockstep
{
    // Moog-style ladder filter (9.24 S13) wrapping juce::dsp::LadderFilter — a
    // zero-delay-feedback nonlinear ladder with self-oscillating resonance and a
    // built-in drive stage. Cutoff / Reso / Drive / Mode (LP/BP/HP at 12 or 24
    // dB/oct). Same face on track and master (no tier split). id lockstep.ladder.v1.
    class LadderFilterEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;
        static constexpr const char* const kModeLabels[] = {
            "LP12", "LP24", "BP12", "BP24", "HP12", "HP24"
        };
        static constexpr int kNumModes = 6;

    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sr_ = sampleRate;
            juce::dsp::ProcessSpec spec{ sampleRate,
                static_cast<juce::uint32>(std::max(1, maxBlockSize)), 2 };
            ladder_.prepare(spec);
            ladder_.setEnabled(true);
            reset();
        }

        void reset() override { ladder_.reset(); }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const float cutoff = params.size() > 0
                                     ? juce::jlimit(20.0f, 20000.0f, params[0]) : 1000.0f;
            const float reso = params.size() > 1 ? juce::jlimit(0.0f, 1.0f, params[1]) : 0.3f;
            const float drive = params.size() > 2
                                    ? 1.0f + juce::jlimit(0.0f, 1.0f, params[2]) * 9.0f : 1.0f;
            const int mode = params.size() > 3
                                 ? juce::jlimit(0, kNumModes - 1, static_cast<int>(params[3])) : 0;

            // Nyquist-guard the cutoff so LadderFilter never sees an unstable target.
            ladder_.setCutoffFrequencyHz(std::min(cutoff, static_cast<float>(sr_ * 0.49)));
            ladder_.setResonance(std::min(reso, 0.98f));  // < 1 to stay just below self-osc
            ladder_.setDrive(drive);
            ladder_.setMode(kModeFor[static_cast<std::size_t>(mode)]);

            juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                static_cast<std::size_t>(std::min(ch, 2)),
                static_cast<std::size_t>(numSamples));
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            ladder_.process(ctx);

            for (int c = 2; c < ch; ++c)
                buffer.copyFrom(c, 0, buffer, std::min(c, 1), 0, numSamples);
        }

        int numParams() const override { return 4; }

        ParamSpec paramSpec(int i) const override
        {
            static const std::span<const char* const> kModeSpan{ kModeLabels,
                static_cast<std::size_t>(kNumModes) };
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (i)
            {
                case 0:
                    p.id = "lockstep.ladder.cutoff";
                    p.label = "Cutoff";
                    p.minValue = 20.0f; p.maxValue = 20000.0f; p.defaultValue = 1000.0f;
                    p.skew = 0.3f;
                    break;
                case 1:
                    p.id = "lockstep.ladder.reso";
                    p.label = "Reso";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.3f;
                    break;
                case 2:
                    p.id = "lockstep.ladder.drive";
                    p.label = "Drive";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.0f;
                    break;
                default:
                    p.id = "lockstep.ladder.mode";
                    p.label = "Mode";
                    p.minValue = 0.0f; p.maxValue = static_cast<float>(kNumModes - 1);
                    p.defaultValue = 0.0f; p.isStepped = true;
                    p.valueLabels = kModeSpan;
                    break;
            }
            return p;
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "LDR"; }

    private:
        using LMode = juce::dsp::LadderFilterMode;
        static constexpr std::array<LMode, kNumModes> kModeFor = {
            LMode::LPF12, LMode::LPF24, LMode::BPF12,
            LMode::BPF24, LMode::HPF12, LMode::HPF24
        };
        static inline const std::string kId = "lockstep.ladder.v1";
        double sr_ = 44100.0;
        juce::dsp::LadderFilter<float> ladder_;
    };
}
