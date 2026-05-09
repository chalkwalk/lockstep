#pragma once

#include <atomic>
#include <cstdint>

namespace lockstep
{
    // Polymetric clock. Tracks query the clock for "what step am I on right
    // now?" — each track has its own length and divider, so step indices are
    // computed via modulo against a shared sample-position counter.
    class Clock
    {
    public:
        Clock();

        void prepare(double sampleRate);

        // Advance by `numSamples` of host time. The processor calls this once
        // per processBlock.
        void advance(int numSamples);

        // Set the master tempo (BPM). Polymetric track behaviour layers on
        // top via per-track step length and divider; the clock itself is
        // tempo-relative, not bar-relative.
        void setBpm(double bpm);

        double bpm() const { return bpm_; }
        std::int64_t samplePosition() const
        {
            return samplePosition_.load(std::memory_order_relaxed);
        }
        double sampleRate() const { return sampleRate_; }

        // Samples per 16th note at the current tempo. The base step grid is
        // 16ths; per-track dividers scale this.
        double samplesPerStep() const;

    private:
        double sampleRate_ = 0.0;
        double bpm_ = 120.0;
        std::atomic<std::int64_t> samplePosition_{0};
    };
}
