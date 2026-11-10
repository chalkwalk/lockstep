#pragma once

#include "../machine/IEffect.h"
#include <chalkwalk/dsp/TiltEq.h>
#include <cmath>

namespace lockstep
{
    // Tilt EQ — complementary low/high shelf pair pivoted at 700 Hz.
    // tilt: -1 = +6dB low / -6dB high; +1 = reverse.
    // gain: trim in dB applied after tilt.
    //
    // The filter is chalkwalk-dsp's TiltEq (libs/dsp), the one copy shared with
    // the other projects; this is its JUCE face. Its tilt is the high-minus-low
    // difference in dB, so this effect's +/-6 dB per side is 12 dB per unit.
    class TiltEQEffect final : public IEffect
    {
    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            tilt_.prepare(sampleRate, 700.0, 5.0);
            tilt_.setTiltDb(0.0);
            tilt_.reset();
            lastTilt_ = 0.0f;
            gainZ_ = 1.0f;
        }

        void reset() override { tilt_.reset(); }

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

            // Positive tilt brightens: cut lows, boost highs.
            if (tiltTarget != lastTilt_)
            {
                tilt_.setTiltDb(12.0 * tiltTarget);
                lastTilt_ = tiltTarget;
            }
            float* l = buffer.getWritePointer(0);
            float* r = numCh > 1 ? buffer.getWritePointer(1) : nullptr;
            tilt_.process(l, r, numSamples);

            for (int n = 0; n < numSamples; ++n)
            {
                gainZ_ += smoothCoef_ * (gainTarget - gainZ_);
                l[n] *= gainZ_;
                if (r != nullptr) r[n] *= gainZ_;
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

        chalkwalk::dsp::TiltEq tilt_;
        float lastTilt_ = 0.0f;
        float smoothCoef_ = 0.01f;
        float gainZ_ = 1.0f;
    };
}
