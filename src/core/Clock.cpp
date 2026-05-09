#include "Clock.h"
#include <algorithm>

namespace lockstep
{
    Clock::Clock() = default;

    void Clock::prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        samplePosition_ = 0;
        localPpq_       = 0.0;
        ppqBlockStart_  = 0.0;
        ppqBlockEnd_    = 0.0;
        ppqJumped_      = false;
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
            // ppqBlockStart_ stays at last value
        }
        else
        {
            // No playhead (standalone or no-host context).
            hostPlaying_ = false;
            bpm_ = localBpm_;
            ppqBlockStart_ = localPpq_;

            if (inPluginPlaying_ && sampleRate_ > 0.0)
                localPpq_ += static_cast<double>(blockSize) * bpm_ / (sampleRate_ * 60.0);
        }

        // End of this block (used as prevBlockEnd next call).
        ppqBlockEnd_ = ppqBlockStart_
            + static_cast<double>(blockSize) * bpm_ / (sampleRate_ * 60.0);

        // Detect backward jump (DAW loop, locator drag).
        if (ppqBlockStart_ < prevBlockEnd - 1e-6)
            ppqJumped_ = true;

        // Legacy sample counter for the StepGrid playhead display.
        samplePosition_.fetch_add(blockSize, std::memory_order_relaxed);
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
    }

    double Clock::samplesPerPpq() const
    {
        if (bpm_ <= 0.0 || sampleRate_ <= 0.0)
            return 0.0;
        return (sampleRate_ * 60.0) / bpm_;
    }

    double Clock::samplesPerStep() const
    {
        // 16th note = 0.25 PPQ = one step at the base grid.
        return samplesPerPpq() * 0.25;
    }
}
