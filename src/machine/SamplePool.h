#pragma once

#include "TransientDetector.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lockstep
{
    struct SampleRef
    {
        std::string path;
        std::uint32_t hashXX32 = 0;
    };

    struct Sample
    {
        SampleRef ref;
        juce::AudioBuffer<float> pcm;
        double sampleRate = 0.0;
        bool missing = false;  // true when the file could not be found on load

        // Cached per-block analysis for transient detection (message thread only).
        // Populated by SamplePool::load(); empty for missing entries.
        BlockAnalysis analysis;
    };

    // Holds decoded PCM for every sample loaded into the session.
    // load() must be called on the message thread only.
    // get() is safe to call from the audio thread for already-loaded samples.
    class SamplePool
    {
    public:
        SamplePool();
        ~SamplePool();

        // Decode the file at path and append it to the pool.
        // Returns the index of the new entry, or -1 on failure.
        // Message-thread only.
        int load(const juce::String& path);

        // Append a placeholder entry for a file that could not be found.
        // Preserves the pool index so P-Lock references remain valid.
        // Message-thread only.
        int addMissing(const SampleRef& ref);

        // Replace a missing (or any) entry in-place with the decoded file at newPath.
        // Does not shift indices; call when the sequencer is stopped to avoid races.
        // Returns false if the file cannot be read.
        // Message-thread only.
        bool relink(int index, const juce::String& newPath);

        int size() const { return static_cast<int>(samples_.size()); }
        bool isMissing(int index) const;
        const Sample* get(int index) const;

        // Remove the entry at index, shifting higher entries down.
        // Callers must remap all references before calling. Message-thread only.
        bool remove(int index);

        // Swap two entries. Callers must remap all references before calling.
        bool swap(int a, int b);

    private:
        juce::AudioFormatManager formatManager_;
        std::vector<std::unique_ptr<Sample>> samples_;
    };
}
