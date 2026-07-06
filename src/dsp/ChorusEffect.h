#pragma once

#include "../machine/IEffect.h"
#include "Interpolation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lockstep
{
    // Multi-voice chorus — Rate (Hz) / Depth / Mix / Feedback (9.24 S9 rebuild).
    //
    // The original was a single integer-tap voice (thin, and it zipper-stepped as
    // the modulated delay quantised to whole samples). This rebuild runs THREE
    // fractional (4-point Hermite) voices per channel, each on a phase-offset LFO
    // (0, 120, 240 deg), with the right channel's LFOs rotated a further quarter
    // cycle so the wet signal decorrelates into a wide stereo image.
    //
    // The appended `chorus_fb` param (default 0) folds the summed wet back into the
    // delay line for a flanger-leaning resonance; at its default of 0 the write is
    // exactly the dry input, so old projects (which have no stored value) load with
    // the classic no-feedback character. Param ids rate/depth/mix are unchanged.
    class ChorusEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;
        static constexpr int kVoices = 3;
        // Nominal centre delay and modulation swing (seconds). Base keeps the read
        // tap comfortably away from the write head; swing scales with Depth.
        static constexpr float kBaseSec = 0.011f;   // 11 ms centre
        static constexpr float kSwingSec = 0.008f;  // up to 8 ms of modulation

    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sampleRate_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            const int bufLen = static_cast<int>(sampleRate * 0.05) + 1;  // 50ms max
            for (auto& b : buf_) b.assign(static_cast<std::size_t>(bufLen), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& ph : phase_) ph = 0.0f;
            for (auto& z : mixZ_)   z = 0.5f;
            for (auto& z : depthZ_) z = 0.3f;
            for (auto& z : fbZ_)    z = 0.0f;
            (void)maxBlockSize;
        }

        void reset() override
        {
            for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& ph : phase_) ph = 0.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const float rate = params.size() > 0
                                   ? juce::jlimit(0.1f, 5.0f, params[0] * 5.0f)
                                   : 0.5f;
            const float depthTarget = params.size() > 1
                                          ? juce::jlimit(0.0f, 1.0f, params[1]) : 0.3f;
            const float mixTarget = params.size() > 2 ? params[2] : 0.5f;
            const float fbTarget = params.size() > 3
                                       ? juce::jlimit(0.0f, 0.95f, params[3]) : 0.0f;
            const float phInc = static_cast<float>(
                rate * juce::MathConstants<double>::twoPi / sampleRate_);

            constexpr float twoPi = juce::MathConstants<float>::twoPi;
            // Per-voice LFO phase offsets (0, 120, 240 deg).
            constexpr std::array<float, kVoices> voiceOff = {
                0.0f, twoPi / 3.0f, 2.0f * twoPi / 3.0f };

            for (int c = 0; c < std::min(ch, 2); ++c)
            {
                auto* data = buffer.getWritePointer(c);
                auto& b = buf_[static_cast<std::size_t>(c)];
                auto& wr = head_[static_cast<std::size_t>(c)];
                auto& ph = phase_[static_cast<std::size_t>(c)];
                auto& mixZ = mixZ_[static_cast<std::size_t>(c)];
                auto& depthZ = depthZ_[static_cast<std::size_t>(c)];
                auto& fbZ = fbZ_[static_cast<std::size_t>(c)];
                const int bufLen = static_cast<int>(b.size());
                // Right channel's LFOs are rotated a quarter cycle for stereo width.
                const float chanOff = (c == 1) ? (twoPi * 0.25f) : 0.0f;

                for (int i = 0; i < numSamples; ++i)
                {
                    mixZ   += smoothCoef_ * (mixTarget - mixZ);
                    depthZ += smoothCoef_ * (depthTarget - depthZ);
                    fbZ    += smoothCoef_ * (fbTarget - fbZ);

                    const float swing = kSwingSec * depthZ;
                    float wetSum = 0.0f;
                    for (int v = 0; v < kVoices; ++v)
                    {
                        const float lfo = 0.5f
                            + 0.5f * std::sin(ph + voiceOff[static_cast<std::size_t>(v)]
                                              + chanOff);
                        const float delaySec = kBaseSec + swing * lfo;
                        const float dSamp = std::clamp(
                            delaySec * static_cast<float>(sampleRate_),
                            2.0f, static_cast<float>(bufLen - 3));
                        float readPos = static_cast<float>(wr) - dSamp;
                        if (readPos < 0.0f) readPos += static_cast<float>(bufLen);
                        const int i0 = static_cast<int>(readPos);
                        const float fr = readPos - static_cast<float>(i0);
                        const auto at = [&](int k) {
                            const int idx = ((i0 + k) % bufLen + bufLen) % bufLen;
                            return b[static_cast<std::size_t>(idx)];
                        };
                        wetSum += hermite4(at(-1), at(0), at(1), at(2), fr);
                    }
                    const float wet = wetSum * (1.0f / static_cast<float>(kVoices));

                    // Feedback (default 0 -> write is exactly the dry input, the
                    // classic no-feedback chorus). Soft-limited for stability.
                    const float fed = data[i] + fbZ * wet;
                    b[static_cast<std::size_t>(wr)] = fed / (1.0f + std::abs(fed) * 0.2f);
                    wr = (wr + 1) % bufLen;

                    data[i] = data[i] * (1.0f - mixZ) + wet * mixZ;

                    ph += phInc;
                    if (ph > twoPi) ph -= twoPi;
                }
            }
            for (int c = 2; c < ch; ++c)
                buffer.copyFrom(c, 0, buffer, std::min(c, 1), 0, numSamples);
        }

        int numParams() const override { return 4; }

        ParamSpec paramSpec(int i) const override
        {
            auto mk = [](const char* id, const char* label, float def) {
                ParamSpec p;
                p.id = id;
                p.label = label;
                p.maxValue = 1.0f;
                p.defaultValue = def;
                p.sectionIndex = kFxSec;
                return p;
            };
            switch (i)
            {
                case 0: return mk("lockstep.chorus.rate",  "Rate",  0.1f);
                case 1: return mk("lockstep.chorus.depth", "Depth", 0.3f);
                case 2: return mk("lockstep.chorus.mix",   "Mix",   0.5f);
                // Appended S9. Default 0 = legacy no-feedback sound on old projects.
                case 3: return mk("lockstep.chorus.fb",    "Feedbk", 0.0f);
                default: return {};
            }
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "CHR"; }

    private:
        static inline const std::string kId = "lockstep.chorus.v1";
        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.005f;
        std::array<std::vector<float>, 2> buf_;
        std::array<int, 2> head_{};
        std::array<float, 2> phase_{};
        std::array<float, 2> mixZ_{ 0.5f, 0.5f };
        std::array<float, 2> depthZ_{ 0.3f, 0.3f };
        std::array<float, 2> fbZ_{ 0.0f, 0.0f };
    };
}
