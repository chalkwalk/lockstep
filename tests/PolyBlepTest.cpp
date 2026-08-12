// PolyBlepTest — the band-limited oscillators alias less than naive ones.
//
// Regression for a sign inversion that had been in the Analog machine's saw,
// pulse and sub oscillators from the start. The polyBLEP polynomial is itself a
// downward step, so it must be SUBTRACTED from a saw (which also steps down)
// and ADDED where a waveform steps up. Both were the wrong way round, which
// does not merely fail to correct the discontinuity — it doubles it, and the
// oscillators aliased audibly worse than if the correction had been deleted.
//
// The measurement below is of the defect itself rather than of a reference
// waveform, which matters: the first version of this test compared against an
// additive saw truncated at Nyquist and reported the CORRECT implementation as
// twice as bad, because at 5 kHz that sum has four terms and is mostly Gibbs
// ringing.
//
// A saw at 5 kHz sampled at 48 kHz has partials at 5, 10, 15 and 20 kHz and
// then runs out of room. Everything above Nyquist folds back, and some of it
// lands underneath the fundamental: the 9th partial at 45 kHz arrives at 3 kHz,
// the 10th at 2 kHz. So energy below 4 kHz is fold-back and nothing else.

#include "TestHarness.h"
#include "../src/dsp/PolyBlep.h"
#include "../src/machine/SvfFilter.h"

#include <vector>

namespace lockstep
{
    namespace
    {
        constexpr double kSr = 48000.0;
        constexpr double kF0 = 5000.0;

        // RMS of what is left after three cascaded lowpasses well below the
        // fundamental. For a correctly band-limited saw this is close to
        // nothing; for an aliasing one it is the fold-back.
        [[nodiscard]] float energyBelowFundamental(const std::vector<float>& v)
        {
            SvfFilter a;
            SvfFilter b;
            SvfFilter c;
            // g = tan(pi * fc / sr), k = 1/Q, matching SvfFilter::setCoeffs.
            const auto g = static_cast<float>(std::tan(3.14159265358979323846 * 3500.0 / kSr));
            const float k = 1.0f / 0.7f;
            a.setCoeffs(g, k);
            b.setCoeffs(g, k);
            c.setCoeffs(g, k);

            const auto skip = static_cast<size_t>(0.05 * kSr);
            double sum = 0.0;
            size_t counted = 0;
            for (size_t i = 0; i < v.size(); ++i)
            {
                const float y = c.process(b.process(a.process(v[i], 0), 0), 0);
                if (i >= skip)
                {
                    sum += static_cast<double>(y) * static_cast<double>(y);
                    ++counted;
                }
            }
            if (counted == 0)
            {
                return 0.0f;
            }
            return static_cast<float>(std::sqrt(sum / static_cast<double>(counted)));
        }

        void testSawAliasesLessThanNaive()
        {
            const double inc = kF0 / kSr;
            const auto n = static_cast<size_t>(0.5 * kSr);

            std::vector<float> naive(n);
            std::vector<float> blep(n);

            double phase = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                naive[i] = static_cast<float>((2.0 * phase) - 1.0);
                blep[i] = dsp::polyBlepSaw(phase, inc);
                phase += inc;
                if (phase >= 1.0)
                {
                    phase -= 1.0;
                }
            }

            const float naiveAlias = energyBelowFundamental(naive);
            const float blepAlias = energyBelowFundamental(blep);

            // Measured: naive 0.071, corrected 0.033, inverted 0.129. Half the
            // naive figure is a wide margin around the right answer and is
            // nowhere near the inverted one, which is the case this exists to
            // catch.
            CHECK(blepAlias < naiveAlias * 0.5f,
                  juce::String("saw aliasing below the fundamental: naive ")
                      + juce::String(naiveAlias, 5) + ", polyBLEP " + juce::String(blepAlias, 5)
                      + " (inverted signs give roughly 0.129 — worse than no correction)");
        }

        void testPulseAliasesLessThanNaive()
        {
            const double inc = kF0 / kSr;
            const auto n = static_cast<size_t>(0.5 * kSr);
            const double width = 0.5;

            std::vector<float> naive(n);
            std::vector<float> blep(n);

            double phase = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                naive[i] = phase < width ? 1.0f : -1.0f;
                blep[i] = dsp::polyBlepPulse(phase, inc, width);
                phase += inc;
                if (phase >= 1.0)
                {
                    phase -= 1.0;
                }
            }

            const float naiveAlias = energyBelowFundamental(naive);
            const float blepAlias = energyBelowFundamental(blep);

            // Measured: naive 0.107, corrected 0.066, inverted 0.179. The
            // margin is smaller than the saw's because a pulse has two edges
            // whose corrections partly overlap at this frequency, but the
            // inverted case is still on the far side of the naive one.
            CHECK(blepAlias < naiveAlias * 0.8f,
                  juce::String("pulse aliasing below the fundamental: naive ")
                      + juce::String(naiveAlias, 5) + ", polyBLEP "
                      + juce::String(blepAlias, 5)
                      + " (inverted signs give roughly 0.179)");
        }

        void testPulseKeepsItsWidth()
        {
            // The correction must not move the duty cycle: a 25% pulse is still
            // high for a quarter of the cycle.
            for (const double width : { 0.25, 0.5, 0.75 })
            {
                const double inc = 100.0 / kSr;
                const int n = 48000;
                double phase = 0.0;
                int high = 0;
                for (int i = 0; i < n; ++i)
                {
                    if (dsp::polyBlepPulse(phase, inc, width) > 0.0f)
                    {
                        ++high;
                    }
                    phase += inc;
                    if (phase >= 1.0)
                    {
                        phase -= 1.0;
                    }
                }
                const double duty = static_cast<double>(high) / static_cast<double>(n);
                CHECK(std::abs(duty - width) < 0.03,
                      juce::String("pulse width ") + juce::String(width) + " measured duty "
                          + juce::String(duty, 4));
            }
        }

        void testOscillatorsStayInBounds()
        {
            // A correction that overshoots is a correction that clips. The
            // inverted version reaches beyond 2, which is the other half of why
            // it sounded wrong.
            for (const double hz : { 20.0, 440.0, 5000.0, 15000.0, 23000.0 })
            {
                const double inc = hz / kSr;
                double phase = 0.0;
                float worst = 0.0f;
                for (int i = 0; i < 20000; ++i)
                {
                    worst = std::max(worst, std::abs(dsp::polyBlepSaw(phase, inc)));
                    worst = std::max(worst, std::abs(dsp::polyBlepPulse(phase, inc, 0.5)));
                    phase += inc;
                    if (phase >= 1.0)
                    {
                        phase -= 1.0;
                    }
                }
                CHECK(worst < 2.0f,
                      juce::String("oscillator at ") + juce::String(hz) + " Hz reached "
                          + juce::String(worst));
            }
        }
    } // namespace

    void runPolyBlepTests()
    {
        testSawAliasesLessThanNaive();
        testPulseAliasesLessThanNaive();
        testPulseKeepsItsWidth();
        testOscillatorsStayInBounds();
    }
} // namespace lockstep
