#pragma once

#include "../machine/IEffect.h"
#include <array>
#include <cmath>

namespace lockstep
{
    // Single-sideband frequency shifter (9.24 S13). Unlike a pitch shifter this
    // adds a fixed number of Hz to every partial (inharmonic — metallic, barber-
    // pole, through-zero flanging with feedback). Shift / Mix / Feedback. Same
    // face on track and master. id lockstep.freqshift.v1.
    //
    // Built from an FIR Hilbert transformer (Signalsmith ships no Hilbert; an FIR
    // design is deterministic, sample-rate-independent in normalised terms, and
    // directly testable for sideband rejection). The quadrature (imag) branch is a
    // Type-III antisymmetric Hilbert FIR; the real branch is a matched delay of M
    // samples. The analytic pair is rotated by a phasor accumulating at the shift
    // frequency: out = re*cos θ − im*sin θ. Latency = M samples (~0.7 ms @ 48 k);
    // no host PDC (kept small and internal, like the codebase's other effects).
    class FreqShifterEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;
        // FIR length (odd). Long enough that the lower sideband is rejected > 30 dB
        // even for low probe tones (~1 kHz), which is the hard case for a Hilbert
        // FIR — the 2/(πk) impulse decays slowly, so low-frequency accuracy needs
        // length. Latency = M = 100 samples (~2 ms @ 48 k); no host PDC.
        static constexpr int kN = 201;
        static constexpr int kM = (kN - 1) / 2;  // centre tap / real-branch delay

    public:
        FreqShifterEffect() { buildKernel(); }

        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            reset();
        }

        void reset() override
        {
            for (auto& st : chan_)
            {
                st.buf.fill(0.0f);
                st.pos = 0;
                st.lastOut = 0.0f;
            }
            phase_ = 0.0f;
            mixZ_ = 0.5f;
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            const float shiftHz = params.size() > 0
                                      ? juce::jlimit(-2000.0f, 2000.0f, params[0]) : 0.0f;
            const float mixTgt = params.size() > 1 ? juce::jlimit(0.0f, 1.0f, params[1]) : 0.5f;
            const float fb = params.size() > 2 ? juce::jlimit(0.0f, 0.95f, params[2]) : 0.0f;

            const float phInc = static_cast<float>(
                shiftHz * juce::MathConstants<double>::twoPi / sr_);
            const int nCh = std::min(ch, 2);

            for (int n = 0; n < numSamples; ++n)
            {
                mixZ_ += smoothCoef_ * (mixTgt - mixZ_);
                const float cs = std::cos(phase_);
                const float sn = std::sin(phase_);

                for (int c = 0; c < nCh; ++c)
                {
                    auto& st = chan_[static_cast<std::size_t>(c)];
                    auto* data = buffer.getWritePointer(c);
                    const float in = data[n] + fb * st.lastOut;

                    st.buf[static_cast<std::size_t>(st.pos)] = in;

                    // Real branch: matched delay of M samples.
                    const int reIdx = (st.pos - kM + kN) % kN;
                    const float re = st.buf[static_cast<std::size_t>(reIdx)];

                    // Imag branch: Hilbert FIR over the ring (age 0 = newest).
                    float im = 0.0f;
                    for (int j = 0; j < kN; ++j)
                    {
                        const float hj = h_[static_cast<std::size_t>(j)];
                        if (hj == 0.0f) continue;
                        const int idx = (st.pos - j + kN) % kN;
                        im += hj * st.buf[static_cast<std::size_t>(idx)];
                    }

                    const float wet = re * cs - im * sn;
                    st.lastOut = wet;
                    // Mix against the *delayed* dry (re) so wet/dry stay time-aligned.
                    data[n] = re * (1.0f - mixZ_) + wet * mixZ_;
                    st.pos = (st.pos + 1) % kN;
                }

                phase_ += phInc;
                if (phase_ > static_cast<float>(juce::MathConstants<double>::twoPi))
                    phase_ -= static_cast<float>(juce::MathConstants<double>::twoPi);
                else if (phase_ < 0.0f)
                    phase_ += static_cast<float>(juce::MathConstants<double>::twoPi);
            }
            for (int c = 2; c < ch; ++c)
                buffer.copyFrom(c, 0, buffer, std::min(c, 1), 0, numSamples);
        }

        int numParams() const override { return 3; }

        ParamSpec paramSpec(int i) const override
        {
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (i)
            {
                case 0:
                    p.id = "lockstep.freqshift.shift";
                    p.label = "Shift";
                    p.minValue = -2000.0f; p.maxValue = 2000.0f; p.defaultValue = 0.0f;
                    break;
                case 1:
                    p.id = "lockstep.freqshift.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 0.5f;
                    break;
                default:
                    p.id = "lockstep.freqshift.fb";
                    p.label = "Feedbk";
                    p.minValue = 0.0f; p.maxValue = 0.95f; p.defaultValue = 0.0f;
                    break;
            }
            return p;
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "FSH"; }

    private:
        // Type-III antisymmetric Hilbert FIR, Blackman-windowed. h[j] indexes taps
        // by age (0 = newest); k = j - M is the offset from the centre tap.
        void buildKernel() noexcept
        {
            constexpr double pi = juce::MathConstants<double>::pi;
            for (int j = 0; j < kN; ++j)
            {
                const int k = j - kM;
                double v = 0.0;
                if (k != 0 && (k % 2 != 0))
                    v = 2.0 / (pi * static_cast<double>(k));
                const double w = 0.42 - 0.5 * std::cos(2.0 * pi * j / (kN - 1))
                                      + 0.08 * std::cos(4.0 * pi * j / (kN - 1));
                h_[static_cast<std::size_t>(j)] = static_cast<float>(v * w);
            }
        }

        struct ChanState
        {
            std::array<float, kN> buf{};
            int pos = 0;
            float lastOut = 0.0f;
        };

        static inline const std::string kId = "lockstep.freqshift.v1";
        double sr_ = 44100.0;
        float smoothCoef_ = 0.005f;
        std::array<float, kN> h_{};
        std::array<ChanState, 2> chan_{};
        float phase_ = 0.0f;
        float mixZ_ = 0.5f;
    };
}
