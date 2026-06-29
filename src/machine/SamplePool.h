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

        // Volatile (RAM-only) entries hold captured audio written by recorder
        // trigs (DESIGN §28). They have no file backing (empty ref.path), are
        // badged "REC", and are not serialised with the project. The pcm buffer
        // is pre-sized to a capacity in prepareVolatile(); a recorder shrinks it
        // to the captured length via setSize(avoidReallocating) so playback reads
        // exactly the captured region with no sampler changes.
        bool isVolatile = false;

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

        // Append an empty volatile (RAM-only) entry; returns its pool index.
        // The pcm buffer is zero-length until prepareVolatile() sizes it.
        // Message-thread only. (DESIGN §28.)
        int addVolatile();

        // Resize every volatile entry's pcm buffer to a capacity of maxSamples
        // (the recorder later shrinks to the captured length without reallocating).
        // Call from prepareToPlay(); message/prepare thread only.
        void prepareVolatile(double sampleRate, int numChannels, int maxSamples);

        bool isVolatileIndex(int index) const;

        // Audio-thread-safe mutable handle to a pre-sized volatile buffer, for a
        // recorder/looper to write into. Returns nullptr for non-volatile or
        // out-of-range indices. The capacity is fixed by prepareVolatile(); a
        // writer may shrink via setSize(..., avoidReallocating=true) but must not
        // grow past the capacity.
        juce::AudioBuffer<float>* mutableVolatilePcm(int index);

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
