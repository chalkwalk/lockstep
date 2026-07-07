#pragma once

#include <signalsmith-dsp/windows.h>

#include <array>
#include <cmath>
#include <vector>

namespace lockstep
{
    // Bandlimited fractional resampler (9.25 R1). A polyphase windowed-sinc
    // (Kaiser window from Signalsmith DSP) with a RATE-AWARE cutoff: reading a
    // source faster than unity (pitch-up) scales its spectrum up, folding content
    // above the destination Nyquist back as aliasing. So for a read rate > 1 the
    // sinc cutoff drops to ~Nyquist/rate, band-limiting the source before it
    // aliases. At or below unity the full-band (Nyquist) kernel is used, so a
    // band-limited signal reads back unchanged and DC always passes at unity.
    //
    // A small bank of fixed-cutoff polyphase tables is built once in the
    // constructor (off the audio thread — allocation happens here); read() selects
    // the bucket for the live rate and is allocation-free / real-time safe. Each
    // phase is normalised to unit DC gain, so a constant reads back as itself at
    // every fraction and every rate. Shared by the sample players (anti-aliased
    // pitch-up) and the looper varispeed write path.
    class Resampler
    {
    public:
        static constexpr int kHalf = 8;             // taps either side of the read point
        static constexpr int kTaps = 2 * kHalf;     // 16-tap kernel
        static constexpr int kPhases = 256;         // sub-sample table resolution

        Resampler() { buildBank(); }

        // Interpolate mono `src` (length `srcLen`) at continuous position `pos`,
        // band-limited for a read `rate` (source samples advanced per output
        // sample; pass the magnitude for reverse). Window indices are clamped at
        // the buffer edges, so a position near either end reads a held edge sample
        // rather than out of bounds.
        [[nodiscard]] float read(const float* src, int srcLen, double pos,
                                 double rate) const noexcept
        {
            const Bucket& b = bucketFor(rate);
            const double baseF = std::floor(pos);
            const int base = static_cast<int>(baseF);
            int ph = static_cast<int>((pos - baseF) * kPhases + 0.5);
            if (ph < 0) ph = 0;
            if (ph > kPhases) ph = kPhases;

            const float* tab = b.table.data() + static_cast<std::size_t>(ph) * kTaps;
            float acc = 0.0f;
            for (int i = 0; i < kTaps; ++i)
            {
                int k = base - (kHalf - 1) + i;
                k = k < 0 ? 0 : (k >= srcLen ? srcLen - 1 : k);
                acc += src[k] * tab[static_cast<std::size_t>(i)];
            }
            return acc;
        }

    private:
        struct Bucket
        {
            std::vector<float> table;  // (kPhases+1) rows of kTaps, phase-major
            double maxRate = 1.0;      // serves read rates up to this value
        };

        static double sinc(double x) noexcept
        {
            if (std::abs(x) < 1.0e-9) return 1.0;
            constexpr double kPi = 3.14159265358979323846;
            const double px = kPi * x;
            return std::sin(px) / px;
        }

        void buildBank()
        {
            // Half-octave spacing up to ~5.66x (covers ±24 semitone pitch = 4x plus
            // fine tune). Each bucket band-limits to Nyquist / maxRate.
            static constexpr std::array<double, 6> kMaxRates = {
                1.0, 1.41421356, 2.0, 2.82842712, 4.0, 5.65685425
            };
            // Kaiser shape: a ~9-wide main lobe over a 16-tap window gives a clean
            // stopband (~70 dB) with a transition narrow enough to keep the
            // passband flat. operator() is non-const, so keep a mutable instance.
            auto kaiser = signalsmith::windows::Kaiser::withBandwidth(9.0);

            bank_.reserve(kMaxRates.size());
            for (const double mr : kMaxRates)
            {
                const double fc = 0.5 / mr;  // cutoff in cycles/sample (0.5 = Nyquist)
                Bucket b;
                b.maxRate = mr;
                b.table.resize(static_cast<std::size_t>((kPhases + 1) * kTaps));

                for (int ph = 0; ph <= kPhases; ++ph)
                {
                    const double frac = static_cast<double>(ph) / kPhases;
                    double sum = 0.0;
                    std::array<double, kTaps> row{};
                    for (int i = 0; i < kTaps; ++i)
                    {
                        // Source offset of tap i from the read point (see read()).
                        const double t = frac + (kHalf - 1) - i;
                        const double unit = (t + kHalf) / (2.0 * kHalf);
                        const double win = (unit >= 0.0 && unit <= 1.0)
                                               ? kaiser(unit) : 0.0;
                        const double h = 2.0 * fc * sinc(2.0 * fc * t) * win;
                        row[static_cast<std::size_t>(i)] = h;
                        sum += h;
                    }
                    // Normalise the phase to unit DC gain.
                    const double inv = (std::abs(sum) > 1.0e-12) ? 1.0 / sum : 1.0;
                    float* dst = b.table.data() + static_cast<std::size_t>(ph) * kTaps;
                    for (int i = 0; i < kTaps; ++i)
                        dst[i] = static_cast<float>(row[static_cast<std::size_t>(i)] * inv);
                }
                bank_.push_back(std::move(b));
            }
        }

        [[nodiscard]] const Bucket& bucketFor(double rate) const noexcept
        {
            const double r = std::abs(rate);
            for (const auto& b : bank_)
                if (r <= b.maxRate)
                    return b;
            return bank_.back();  // beyond the last bucket — most band-limited kernel
        }

        std::vector<Bucket> bank_;
    };
}
