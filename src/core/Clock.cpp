#include "Clock.h"

namespace lockstep
{
    Clock::Clock() = default;

    void Clock::prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        samplePosition_ = 0;
    }

    void Clock::advance(int numSamples)
    {
        samplePosition_ += numSamples;
    }

    void Clock::setBpm(double bpm)
    {
        bpm_ = bpm;
    }

    double Clock::samplesPerStep() const
    {
        if (bpm_ <= 0.0)
            return 0.0;
        // 16th note = quarter / 4. samples per quarter = sampleRate * 60 / bpm.
        return (sampleRate_ * 60.0) / (bpm_ * 4.0);
    }
}
