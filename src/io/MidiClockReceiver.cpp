#include "MidiClockReceiver.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    void MidiClockReceiver::reset()
    {
        ppqAccum_             = 0.0;
        smoothedBpm_          = 0.0;
        haveBpm_              = false;
        running_              = false;
        hadAnyPulse_          = false;
        samplesSinceLastPulse_ = 0;
        samplesSinceAnyPulse_  = 0;
        // hasClock_ is intentionally NOT reset — once seen, always known
    }

    MidiClockReceiver::BlockResult
    MidiClockReceiver::advance(const juce::MidiBuffer& midi,
                               int blockSize, double sampleRate)
    {
        BlockResult result;
        result.hasClock = hasClock_;
        result.running  = running_;

        int lastPulseOffsetThisBlock = -1;

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();

            if (msg.isMidiStart())
            {
                running_              = true;
                hasClock_             = true;
                ppqAccum_             = 0.0;
                hadAnyPulse_          = false;
                samplesSinceLastPulse_ = 0;
                samplesSinceAnyPulse_  = 0;
                result.didStart        = true;
                result.hasClock        = true;
                result.running         = true;
            }
            else if (msg.isMidiContinue())
            {
                running_        = true;
                hasClock_       = true;
                result.hasClock = true;
                result.running  = true;
            }
            else if (msg.isMidiStop())
            {
                running_       = false;
                result.didStop = true;
                result.running = false;
            }
            else if (msg.isMidiClock())
            {
                hasClock_ = true;
                result.hasClock = true;

                const int s = metadata.samplePosition;

                // Compute inter-pulse interval for BPM tracking.
                if (hadAnyPulse_ && sampleRate > 0.0)
                {
                    const int gap = samplesSinceLastPulse_ + s;
                    if (gap > 0)
                    {
                        const double instantBpm =
                            60.0 * sampleRate / (24.0 * static_cast<double>(gap));
                        const double clamped = std::clamp(instantBpm, 20.0, 400.0);
                        if (!haveBpm_)
                        {
                            smoothedBpm_ = clamped;
                            haveBpm_     = true;
                        }
                        else
                        {
                            smoothedBpm_ = (1.0 - kEmaAlpha) * smoothedBpm_
                                         + kEmaAlpha * clamped;
                        }
                    }
                }

                ppqAccum_ += kPpqPerPulse;
                hadAnyPulse_ = true;

                // After the block we will add blockSize; set to -s now so the
                // result is (blockSize - s) = samples remaining after this pulse.
                samplesSinceLastPulse_ = -s;
                lastPulseOffsetThisBlock = s;
            }
        }

        // Advance inter-pulse counters by the full block.
        if (hadAnyPulse_)
            samplesSinceLastPulse_ += blockSize;

        samplesSinceAnyPulse_ = (lastPulseOffsetThisBlock >= 0)
            ? (blockSize - lastPulseOffsetThisBlock)
            : (samplesSinceAnyPulse_ + blockSize);

        // Dropout: running but no pulse for > threshold.
        if (running_ && haveBpm_ && sampleRate > 0.0)
        {
            const int threshold = static_cast<int>(sampleRate * kDropoutSeconds);
            result.dropout = (samplesSinceAnyPulse_ > threshold);
        }

        // BPM-smoothed PPQ for the block.
        result.ppqStart = ppqAccum_;
        if (running_ && haveBpm_ && sampleRate > 0.0)
        {
            const double advance =
                static_cast<double>(blockSize) * smoothedBpm_ / (60.0 * sampleRate);
            ppqAccum_ += advance;
        }
        result.ppqEnd = ppqAccum_;
        result.bpm    = haveBpm_ ? smoothedBpm_ : 0.0;

        return result;
    }
}
