#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lockstep
{
    class LockstepProcessor;

    // Save/load seam. M8 expands this to carry the full sequence, P-Locks,
    // sample-pool refs, and CC mappings alongside the APVTS parameters.
    namespace PluginState
    {
        // Bump when the on-disk format changes in a breaking way.
        inline constexpr int kCurrentVersion = 1;

        void writeTo(juce::MemoryBlock& dest, LockstepProcessor& proc);
        void readFrom(const void* data, int sizeInBytes, LockstepProcessor& proc);
    }
}
