#include "VoiceChoke.h"

namespace lockstep
{
    VoiceChoke::VoiceChoke() = default;
    VoiceChoke::~VoiceChoke() = default;

    void VoiceChoke::prepare(double sampleRate, float fadeMs)
    {
        fadeSamples_ = static_cast<int>(sampleRate * 0.001 * static_cast<double>(fadeMs));
        fadeRemaining_ = 0;
    }

    void VoiceChoke::trigger()
    {
        fadeRemaining_ = fadeSamples_;
    }

    float VoiceChoke::nextGain()
    {
        if (fadeRemaining_ <= 0)
            return 0.0f;
        const float gain = static_cast<float>(fadeRemaining_) / static_cast<float>(fadeSamples_);
        --fadeRemaining_;
        return gain;
    }
}
