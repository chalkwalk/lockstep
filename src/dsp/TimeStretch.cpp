#include "TimeStretch.h"
#include <cmath>

namespace lockstep
{
    void TimeStretch::prepare(double sampleRate, int maxChannels)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxCh_ = std::max(1, maxChannels);

        win_.resize(static_cast<std::size_t>(kFrame));
        for (int k = 0; k < kFrame; ++k)
            win_[static_cast<std::size_t>(k)] =
                0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                        * static_cast<float>(k)
                                        / static_cast<float>(kFrame - 1)));

        acc_.setSize(maxCh_, kFrame, false, true, false);
        hop_.setSize(maxCh_, kHop, false, true, false);
        reset();
    }

    void TimeStretch::reset()
    {
        active_ = false;
        pPos_ = 0.0;
        hopAvail_ = 0;
        acc_.clear();
        hop_.clear();
    }

    void TimeStretch::start(const juce::AudioBuffer<float>* src, double startSample,
                            double timeRatio, double pitchRatio)
    {
        src_ = src;
        srcStart_ = startSample;
        pitchRatio_ = std::max(1.0e-4, pitchRatio);
        stretchS_ = std::max(1.0e-4, timeRatio * pitchRatio_);
        pPos_ = 0.0;
        hopAvail_ = 0;
        acc_.clear();

        const int srcLen = (src_ != nullptr) ? src_->getNumSamples() : 0;
        pLen_ = (static_cast<double>(srcLen) - srcStart_) / pitchRatio_;
        active_ = (src_ != nullptr) && srcLen > 0 && pLen_ > 0.0;
    }

    void TimeStretch::setRatios(double timeRatio, double pitchRatio) noexcept
    {
        pitchRatio_ = std::max(1.0e-4, pitchRatio);
        stretchS_ = std::max(1.0e-4, timeRatio * pitchRatio_);
        const int srcLen = (src_ != nullptr) ? src_->getNumSamples() : 0;
        pLen_ = (static_cast<double>(srcLen) - srcStart_) / pitchRatio_;
    }

    float TimeStretch::srcSample(int ch, double pIndex) const
    {
        if (src_ == nullptr) return 0.0f;
        const double s = srcStart_ + pIndex * pitchRatio_;
        if (s < 0.0) return 0.0f;
        const int i0 = static_cast<int>(s);
        const int n = src_->getNumSamples();
        if (i0 < 0 || i0 + 1 >= n) return 0.0f;
        const int sc = std::min(ch, src_->getNumChannels() - 1);
        const float a = src_->getSample(sc, i0);
        const float b = src_->getSample(sc, i0 + 1);
        const float frac = static_cast<float>(s - static_cast<double>(i0));
        return a + (b - a) * frac;
    }

    int TimeStretch::bestOffset() const
    {
        const int L = kFrame - kHop;  // overlap region matched against the tail

        // Tail energy gate: with a silent/empty accumulator (first hops) there is
        // nothing to match, so keep the natural position (offset 0).
        double eT = 0.0;
        for (int k = 0; k < L; k += 2)
        {
            const float t = acc_.getSample(0, k);
            eT += static_cast<double>(t) * t;
        }
        if (eT < 1.0e-9) return 0;

        double bestScore = -1.0e30;
        int bestD = 0;
        for (int d = -kSearch; d <= kSearch; ++d)
        {
            double dot = 0.0, eC = 0.0;
            for (int k = 0; k < L; k += 2)
            {
                const float c = srcSample(0, pPos_ + static_cast<double>(d + k));
                const float t = acc_.getSample(0, k);
                dot += static_cast<double>(c) * t;
                eC += static_cast<double>(c) * c;
            }
            const double score = (eC > 1.0e-9) ? dot / std::sqrt(eC) : 0.0;
            if (score > bestScore) { bestScore = score; bestD = d; }
        }
        return bestD;
    }

    void TimeStretch::produceHop()
    {
        if (!active_) return;

        const int d = bestOffset();

        // Overlap-add a Hann-windowed analysis frame (offset by d) into acc_.
        for (int ch = 0; ch < maxCh_; ++ch)
        {
            float* a = acc_.getWritePointer(ch);
            for (int k = 0; k < kFrame; ++k)
                a[k] += win_[static_cast<std::size_t>(k)]
                        * srcSample(ch, pPos_ + static_cast<double>(d + k));
        }

        // Emit the finalised first hop; shift the overlap tail down.
        for (int ch = 0; ch < maxCh_; ++ch)
        {
            float* a = acc_.getWritePointer(ch);
            hop_.copyFrom(ch, 0, a, kHop);
            for (int k = 0; k < kFrame - kHop; ++k)
                a[k] = a[k + kHop];
            for (int k = kFrame - kHop; k < kFrame; ++k)
                a[k] = 0.0f;
        }
        hopAvail_ = kHop;

        // Advance the analysis position: Ha = Hs / S.
        pPos_ += static_cast<double>(kHop) / stretchS_;
        if (pPos_ >= pLen_)
            active_ = false;
    }

    void TimeStretch::process(juce::AudioBuffer<float>& out, int startSample, int n)
    {
        const int outCh = out.getNumChannels();
        int produced = 0;
        while (produced < n)
        {
            if (hopAvail_ == 0)
            {
                if (!active_) break;
                produceHop();
                if (hopAvail_ == 0) break;
            }
            const int take = std::min(hopAvail_, n - produced);
            const int hopOff = kHop - hopAvail_;
            for (int ch = 0; ch < outCh; ++ch)
            {
                const int sc = std::min(ch, maxCh_ - 1);
                out.copyFrom(ch, startSample + produced, hop_, sc, hopOff, take);
            }
            hopAvail_ -= take;
            produced += take;
        }
        // Source exhausted mid-block: zero-fill the remainder.
        for (int ch = 0; ch < outCh; ++ch)
            out.clear(ch, startSample + produced, n - produced);
    }
}
