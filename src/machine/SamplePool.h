#pragma once

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
        int load(const juce::String& path);

        int size() const { return static_cast<int>(samples_.size()); }
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
