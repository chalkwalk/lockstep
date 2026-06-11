#pragma once
// EngineHarness -- helpers for headless LockstepProcessor tests.
//
// Usage:
//   EngineHarness h;               // constructs + prepareToPlay at 48 kHz / 256 samples
//   h.renderBlocks(10);            // drive 10 processBlocks with advancing playhead
//   h.processor().someMethod();    // direct access to the processor
//
// The harness provides a stub AudioPlayHead that reports isPlaying=true and
// advances ppqPosition by blockSize / (sampleRate * 60 / bpm) per block.

#include "../src/PluginProcessor.h"
#include <cmath>
#include <memory>

namespace lockstep
{
    // Stub playhead: reports playing=true at a configurable BPM with position
    // advancing by exactly one block's worth of PPQ per renderBlocks() call.
    class StubPlayHead : public juce::AudioPlayHead
    {
    public:
        explicit StubPlayHead(double bpm = 120.0,
                              double sampleRate = 48000.0,
                              int    blockSize  = 256)
            : bpm_(bpm), sampleRate_(sampleRate), blockSize_(blockSize)
        {}

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(playing_);
            info.setBpm(bpm_);
            info.setPpqPosition(ppq_);
            info.setTimeInSamples(static_cast<juce::int64>(sampleOffset_));
            info.setTimeInSeconds(static_cast<double>(sampleOffset_) / sampleRate_);
            return info;
        }

        void advance()
        {
            if (playing_)
            {
                ppq_          += ppqPerBlock();
                sampleOffset_ += blockSize_;
            }
        }

        void setPlaying(bool p) { playing_ = p; }
        bool isPlaying()  const { return playing_; }
        double ppqPosition() const { return ppq_; }
        void   resetPosition() { ppq_ = 0.0; sampleOffset_ = 0; }

        double ppqPerBlock() const
        {
            return static_cast<double>(blockSize_) * bpm_ / (sampleRate_ * 60.0);
        }

    private:
        double bpm_;
        double sampleRate_;
        int    blockSize_;
        bool   playing_      = true;
        double ppq_          = 0.0;
        int64_t sampleOffset_ = 0;
    };

    // Assembles everything needed for a headless processBlock run.
    class EngineHarness
    {
    public:
        static constexpr double kSampleRate = 48000.0;
        static constexpr int    kBlockSize  = 256;
        static constexpr double kBpm        = 120.0;

        EngineHarness()
            : playHead_(kBpm, kSampleRate, kBlockSize)
        {
            processor_ = std::make_unique<LockstepProcessor>();
            processor_->setPlayHead(&playHead_);
            processor_->prepareToPlay(kSampleRate, kBlockSize);
            // Start the in-plugin transport so sequencerRunning=true regardless
            // of which SyncMode the APVTS defaults to (Locked mode uses
            // hostPlaying(), other modes use inPluginPlaying()).
            processor_->clock().setInPluginPlaying(true);
            buffer_.setSize(2, kBlockSize, false, true, false);
        }

        ~EngineHarness()
        {
            processor_->releaseResources();
        }

        LockstepProcessor& processor() { return *processor_; }

        // Run N blocks. Advances the playhead after each block.
        void renderBlocks(int n)
        {
            for (int i = 0; i < n; ++i)
            {
                midi_.clear();
                buffer_.clear();
                processor_->processBlock(buffer_, midi_);
                playHead_.advance();
            }
        }

        // Check for NaN/Inf in the last rendered buffer.
        bool lastBufferHasNaN() const
        {
            for (int ch = 0; ch < buffer_.getNumChannels(); ++ch)
            {
                for (int i = 0; i < buffer_.getNumSamples(); ++i)
                {
                    if (!std::isfinite(buffer_.getSample(ch, i)))
                        return true;
                }
            }
            return false;
        }

        float lastBufferRms() const
        {
            double sum = 0.0;
            int    n   = 0;
            for (int ch = 0; ch < buffer_.getNumChannels(); ++ch)
            {
                for (int i = 0; i < buffer_.getNumSamples(); ++i)
                {
                    const float v = buffer_.getSample(ch, i);
                    sum += static_cast<double>(v) * static_cast<double>(v);
                }
                n += buffer_.getNumSamples();
            }
            return (n > 0) ? static_cast<float>(std::sqrt(sum / static_cast<double>(n))) : 0.0f;
        }

        const juce::AudioBuffer<float>& buffer()  const { return buffer_; }
        const juce::MidiBuffer&         midiOut() const { return midi_; }
        StubPlayHead&                   playHead()      { return playHead_; }

    private:
        StubPlayHead                        playHead_;
        std::unique_ptr<LockstepProcessor>  processor_;
        juce::AudioBuffer<float>            buffer_;
        juce::MidiBuffer                    midi_;
    };

} // namespace lockstep
