#pragma once

#include "../machine/IEffect.h"
#include <cmath>

namespace lockstep
{
    // Tilt EQ — complementary low/high shelf pair pivoted at ~700 Hz.
    // One-pole shelves. tilt: -1 = +6dB low / -6dB high; +1 = reverse.
    // gain: trim in dB applied after tilt.
    class TiltEQEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sampleRate_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            for (auto& z : lowZ_) z = 0.0f;
            for (auto& z : tiltZ_) z = 0.0f;
            for (auto& z : gainZ_) z = 1.0f;
        }

        void reset() override
        {
            for (auto& z : lowZ_) z = 0.0f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float tiltTarget = params.size() > 0
                                         ? juce::jlimit(-1.0f, 1.0f, params[0])
                                         : 0.0f;
            const float gainTarget = params.size() > 1
                                         ? juce::Decibels::decibelsToGain(
                                               juce::jlimit(-12.0f, 12.0f, params[1]))
                                         : 1.0f;

            // One-pole LP shelf pivot at fc = 700 Hz.
            const float alpha = static_cast<float>(
                1.0 - std::exp(-2.0 * M_PI * 700.0 / sampleRate_));

            for (int c = 0; c < std::min(numCh, 2); ++c)
            {
                auto* d = buffer.getWritePointer(c);
                auto& lz = lowZ_[static_cast<std::size_t>(c)];
                auto& tz = tiltZ_[static_cast<std::size_t>(c)];
                auto& gz = gainZ_[static_cast<std::size_t>(c)];

                for (int n = 0; n < numSamples; ++n)
                {
                    tz += smoothCoef_ * (tiltTarget - tz);
                    gz += smoothCoef_ * (gainTarget - gz);

                    lz += alpha * (d[n] - lz);   // one-pole LP
                    const float hp = d[n] - lz;   // HP = input - LP

                    // Positive tilt brightens: cut lows, boost highs (matches the
                    // header contract and user expectation that "up" = more treble).
                    const float lowGain  = juce::Decibels::decibelsToGain(-tz * 6.0f);
                    const float highGain = juce::Decibels::decibelsToGain( tz * 6.0f);
                    d[n] = (lz * lowGain + hp * highGain) * gz;
                }
            }
            for (int c = 2; c < numCh; ++c)
                buffer.copyFrom(c, 0, buffer, 0, 0, numSamples);
        }

        [[nodiscard]] int numParams() const override { return 2; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            if (index == 0)
            {
                p.id = "lockstep.tilteq.tilt";
                p.label = "Tilt";
                p.minValue = -1.0f;
                p.maxValue = 1.0f;
                p.defaultValue = 0.0f;
            }
            else
            {
                p.id = "lockstep.tilteq.gain";
                p.label = "Gain";
                p.minValue = -12.0f;
                p.maxValue = 12.0f;
                p.defaultValue = 0.0f;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "TLT"; }

    private:
        static inline const std::string kId = "lockstep.tilteq.v1";
        static constexpr int kFxSec = 5;   // canonical FX section

        double sampleRate_ = 44100.0;
        float smoothCoef_ = 0.01f;
        float lowZ_[2] = { 0.0f, 0.0f };
        float tiltZ_[2] = { 0.0f, 0.0f };
        float gainZ_[2] = { 1.0f, 1.0f };
    };
}
