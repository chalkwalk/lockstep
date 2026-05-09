#include "Clock.h"
#include <algorithm>
#include <cstring>

namespace lockstep
{
    Clock::Clock() = default;

    void Clock::prepare(double sampleRate)
    {
        sampleRate_    = sampleRate;
        localPpq_      = 0.0;
        ppqBlockStart_ = 0.0;
        ppqBlockEnd_   = 0.0;
        ppqJumped_     = false;
        ppqUi_.store(0, std::memory_order_relaxed);
    }

    void Clock::update(juce::AudioPlayHead* playHead, int blockSize)
    {
        ppqJumped_ = false;
        const double prevBlockEnd = ppqBlockEnd_;

        const auto info = playHead ? playHead->getPosition()
                                   : juce::Optional<juce::AudioPlayHead::PositionInfo>{};

        if (info.hasValue() && info->getIsPlaying())
        {
            hostPlaying_ = true;

            if (const auto maybeBpm = info->getBpm(); maybeBpm.hasValue())
                bpm_ = std::max(1.0, *maybeBpm);

            if (const auto maybePpq = info->getPpqPosition(); maybePpq.hasValue())
                ppqBlockStart_ = *maybePpq;
        }
        else if (info.hasValue())
        {
            // DAW present but stopped — freeze PPQ, mirror tempo.
            hostPlaying_ = false;
            if (const auto maybeBpm = info->getBpm(); maybeBpm.hasValue())
                bpm_ = std::max(1.0, *maybeBpm);
            // ppqBlockStart_ stays at last value.
        }
        else
        {
            // No playhead (standalone). Synthesise PPQ only when playing.
            hostPlaying_ = false;
            bpm_ = localBpm_;
            ppqBlockStart_ = localPpq_;

            if (inPluginPlaying_.load(std::memory_order_relaxed) && sampleRate_ > 0.0)
                localPpq_ += static_cast<double>(blockSize) * bpm_ / (sampleRate_ * 60.0);
        }

        ppqBlockEnd_ = ppqBlockStart_
            + static_cast<double>(blockSize) * bpm_ / (sampleRate_ * 60.0);

        if (ppqBlockStart_ < prevBlockEnd - 1e-6)
            ppqJumped_ = true;

        // Publish ppqBlockStart_ to the UI thread via the atomic.
        std::uint64_t bits;
        std::memcpy(&bits, &ppqBlockStart_, sizeof(bits));
        ppqUi_.store(bits, std::memory_order_relaxed);
    }

    void Clock::setLocalBpm(double bpm)
    {
        localBpm_ = std::max(1.0, bpm);
        bpm_      = localBpm_;
    }

    void Clock::resetPhase()
    {
        localPpq_      = 0.0;
        ppqBlockStart_ = 0.0;
        ppqBlockEnd_   = 0.0;
        ppqJumped_     = true;
        ppqUi_.store(0, std::memory_order_relaxed);
    }

    double Clock::samplesPerPpq() const
    {
        if (bpm_ <= 0.0 || sampleRate_ <= 0.0)
            return 0.0;
        return (sampleRate_ * 60.0) / bpm_;
    }
}
