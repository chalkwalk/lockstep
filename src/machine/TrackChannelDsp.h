#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../core/TrackChannelState.h"

namespace lockstep
{
    // Apply CHANNEL block (level, pan) to buf in-place.
    // Called on every audio track after FILTER + [ENVELOPE].
    // Send A/B taps are handled separately in the processBlock loop (accumulated
    // into the send buses after this call).
    inline void applyChannel(juce::AudioBuffer<float>& buf,
                             const TrackChannelState& ch,
                             int numSamples) noexcept
    {
        const float level = juce::jlimit(0.0f, 2.0f, ch.level);
        const float pan   = juce::jlimit(-1.0f, 1.0f, ch.pan);
        const float panL  = level * (1.0f - std::max(pan, 0.0f));
        const float panR  = level * (1.0f + std::min(pan, 0.0f));

        const int numCh = buf.getNumChannels();
        if (numCh >= 1)
        {
            float* data = buf.getWritePointer(0);
            for (int n = 0; n < numSamples; ++n)
                data[n] *= panL;
        }
        if (numCh >= 2)
        {
            float* data = buf.getWritePointer(1);
            for (int n = 0; n < numSamples; ++n)
                data[n] *= panR;
        }
    }
}
