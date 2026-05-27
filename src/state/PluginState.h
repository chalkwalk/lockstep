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
        // v1: flat Sequence + SamplePool + CCMappings + Misc
        // v2: full Project/Bank/Pattern/Part hierarchy; Sequence/BaseParams
        //     moved into Project node; Misc gains activeBankIdx/activePatternIdx
        inline constexpr int kCurrentVersion = 3;

        void writeTo(juce::MemoryBlock& dest, LockstepProcessor& proc);
        void readFrom(const void* data, int sizeInBytes, LockstepProcessor& proc);

        // Exposed for testing: normalises any historical state tree to the
        // current version by applying each upgrade function in sequence.
        juce::ValueTree applyUpgrades(juce::ValueTree tree);
    }
}
