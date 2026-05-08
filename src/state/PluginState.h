#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lockstep
{
    class Sequence;

    // Save/load seam. Today this just round-trips APVTS XML. P-Lock data and
    // sample-pool references (with xxHash32) land in M7 — at which point the
    // payload grows beyond raw APVTS but never carries PCM bytes.
    namespace PluginState
    {
        inline constexpr int kCurrentVersion = 1;

        void writeTo(juce::MemoryBlock& dest, juce::AudioProcessorValueTreeState& apvts);
        void readFrom(const void* data, int sizeInBytes, juce::AudioProcessorValueTreeState& apvts);
    }
}
