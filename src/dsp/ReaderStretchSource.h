#pragma once

#include "IStretchEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace lockstep
{
    // IStretchSource over a juce::BufferingAudioReader (disk streaming, 9.23 S3).
    // The reader is borrowed; the background TimeSliceThread only fills its cache,
    // while read()/length()/etc. run on the audio thread. setReadTimeout(0) on the
    // reader keeps read() non-blocking (silence, never a stall) if a region has not
    // been prefetched yet.
    //
    // The engine fetches the same [begin, n) range once per channel per grain, so a
    // one-slot cache serves the second channel from the first channel's disk read.
    class ReaderStretchSource final : public IStretchSource
    {
    public:
        void setReader(juce::BufferingAudioReader* reader, juce::int64 length,
                       int numChannels, double sampleRate)
        {
            reader_ = reader;
            length_ = length;
            numChannels_ = juce::jmax(1, numChannels);
            sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
            cacheValid_ = false;
            cache_.setSize(numChannels_, kCacheCap, false, false, true);
        }

        int read(float* dest, int ch, juce::int64 srcPos, int n) override
        {
            if (reader_ == nullptr || n <= 0)
            {
                for (int i = 0; i < n; ++i) dest[i] = 0.0f;
                return n;
            }

            // Oversized run (larger than the cache): read directly, no caching.
            if (n > kCacheCap)
            {
                readInto(dest, ch, srcPos, n);
                return n;
            }

            // Refill the cache when the requested run differs from the last one
            // (the engine asks for the same run once per channel).
            if (!cacheValid_ || srcPos != cachePos_ || n != cacheLen_)
                refill(srcPos, n);

            const int srcCh = juce::jmin(ch, numChannels_ - 1);
            const float* row = cache_.getReadPointer(srcCh);
            for (int i = 0; i < n; ++i) dest[i] = row[i];
            return n;
        }

        [[nodiscard]] juce::int64 length() const override { return length_; }
        [[nodiscard]] int numChannels() const override { return numChannels_; }
        [[nodiscard]] double sampleRate() const override { return sampleRate_; }

    private:
        static constexpr int kCacheCap = 32768;  // comfortably > Bungee max grain

        // Precondition: n <= kCacheCap (oversized reads bypass the cache).
        void refill(juce::int64 srcPos, int n)
        {
            for (int ch = 0; ch < numChannels_; ++ch)
                readInto(cache_.getWritePointer(ch), ch, srcPos, n);
            cachePos_ = srcPos;
            cacheLen_ = n;
            cacheValid_ = true;
        }

        // Read n frames of channel ch from srcPos into dest, zero-filling OOB.
        void readInto(float* dest, int ch, juce::int64 srcPos, int n)
        {
            for (int i = 0; i < n; ++i) dest[i] = 0.0f;
            if (reader_ == nullptr) return;

            const juce::int64 begin = juce::jmax<juce::int64>(0, srcPos);
            const juce::int64 end = juce::jmin<juce::int64>(length_, srcPos + n);
            if (end <= begin) return;

            const int destOffset = static_cast<int>(begin - srcPos);
            const int count = static_cast<int>(end - begin);
            const int srcCh = juce::jmin(ch, numChannels_ - 1);

            juce::AudioBuffer<float> tmp(1, count);
            tmp.clear();
            // BufferingAudioReader reads into channel pointers; route the wanted
            // source channel into tmp channel 0.
            float* chans[2] = { nullptr, nullptr };
            chans[srcCh == 0 ? 0 : 1] = tmp.getWritePointer(0);
            // For a source channel > 1 we still map it via useRight; a mono reader
            // ignores the right request. Read only the one channel we need.
            reader_->read(chans, 2, begin, count);
            for (int i = 0; i < count; ++i)
                dest[destOffset + i] = tmp.getSample(0, i);
        }

        juce::BufferingAudioReader* reader_ = nullptr;
        juce::int64 length_ = 0;
        int numChannels_ = 1;
        double sampleRate_ = 44100.0;

        juce::AudioBuffer<float> cache_;
        juce::int64 cachePos_ = -1;
        int cacheLen_ = 0;
        bool cacheValid_ = false;
    };
}
