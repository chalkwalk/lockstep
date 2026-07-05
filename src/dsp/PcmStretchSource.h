#pragma once

#include "IStretchEngine.h"

namespace lockstep
{
    // IStretchSource over an in-RAM juce::AudioBuffer (a SamplePool entry's PCM).
    // Borrows the buffer — it must outlive playback. Reads outside the buffer are
    // zero-filled. Rebind (setSource) on the message thread / withQuiescedEngine;
    // read() is audio-thread safe against a stable binding.
    class PcmStretchSource final : public IStretchSource
    {
    public:
        PcmStretchSource() = default;

        PcmStretchSource(const juce::AudioBuffer<float>* pcm, double sampleRate)
        {
            setSource(pcm, sampleRate);
        }

        void setSource(const juce::AudioBuffer<float>* pcm, double sampleRate)
        {
            pcm_ = pcm;
            sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        }

        int read(float* dest, int ch, juce::int64 srcPos, int n) override
        {
            const int len = pcm_ != nullptr ? pcm_->getNumSamples() : 0;
            const int nch = pcm_ != nullptr ? pcm_->getNumChannels() : 0;
            const int srcCh = (nch > 0) ? juce::jmin(ch, nch - 1) : 0;
            const float* src = (len > 0) ? pcm_->getReadPointer(srcCh) : nullptr;

            for (int i = 0; i < n; ++i)
            {
                const juce::int64 p = srcPos + i;
                dest[i] = (src != nullptr && p >= 0 && p < len)
                              ? src[p] : 0.0f;
            }
            return n;
        }

        [[nodiscard]] juce::int64 length() const override
        {
            return pcm_ != nullptr ? pcm_->getNumSamples() : 0;
        }

        [[nodiscard]] int numChannels() const override
        {
            return pcm_ != nullptr ? juce::jmax(1, pcm_->getNumChannels()) : 1;
        }

        [[nodiscard]] double sampleRate() const override { return sampleRate_; }

    private:
        const juce::AudioBuffer<float>* pcm_ = nullptr;
        double sampleRate_ = 44100.0;
    };
}
