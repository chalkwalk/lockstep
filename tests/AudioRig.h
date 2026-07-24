#pragma once
// AudioRig -- the single owner of "how a live LockstepProcessor is stood up".
//
// Two test rigs need a processor pumping real audio: the headless-engine tests
// (EngineHarness) and the bridged UI-interaction tests (UiDriver's live-audio
// mode). Before this, EngineHarness owned that logic alone; when UiDriver grew a
// bridge, the rate/buffer/channel/playhead assumptions had to match EXACTLY or the
// two rigs would quietly disagree about what "running" means -- and the one nobody
// audited would be the one telling the truth. So there is one owner, and both call
// it.
//
// AudioRig does NOT own the processor -- the caller does (EngineHarness owns one
// outright; UiDriver's processor is owned by its editor rig). AudioRig owns the
// playhead, the scratch buffer, the MIDI in/out buffer, and the lifecycle
// (prepareToPlay in the ctor, releaseResources in the dtor). Construct it AFTER the
// processor and destroy it BEFORE -- it holds a reference.
//
//   AudioRig audio(proc);          // prepared, transport running
//   audio.injectInput(midi);       // optional: MIDI for the next block
//   audio.renderBlocks(4);         // pump processBlock, advance the playhead
//   CHECK(audio.lastBufferRms() > 0.0f, "it made a sound");

#include "../src/PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace lockstep
{
    // Stub playhead: reports playing=true at a configurable BPM with position
    // advancing by exactly one block's worth of PPQ per renderBlocks() call.
    class StubPlayHead : public juce::AudioPlayHead
    {
    public:
        explicit StubPlayHead(double bpm = 120.0,
                              double sampleRate = 48000.0,
                              int blockSize = 256)
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
                ppq_ += ppqPerBlock();
                sampleOffset_ += blockSize_;
            }
        }

        void setPlaying(bool p) { playing_ = p; }
        bool isPlaying() const { return playing_; }
        double ppqPosition() const { return ppq_; }
        void resetPosition()
        {
            ppq_ = 0.0;
            sampleOffset_ = 0;
        }
        // Jump the transport to an absolute PPQ (host loop, locate, or a
        // stop → relocate → restart). sampleOffset_ tracks the position so
        // getPosition() stays self-consistent.
        void setPpq(double p)
        {
            ppq_ = p;
            sampleOffset_ = static_cast<int64_t>(p * sampleRate_ * 60.0 / bpm_);
        }

        double ppqPerBlock() const
        {
            return static_cast<double>(blockSize_) * bpm_ / (sampleRate_ * 60.0);
        }

    private:
        double bpm_;
        double sampleRate_;
        int blockSize_;
        bool playing_ = true;
        double ppq_ = 0.0;
        int64_t sampleOffset_ = 0;
    };

    // Prepares an externally-owned processor and drives it block by block.
    class AudioRig
    {
    public:
        static constexpr double kSampleRate = 48000.0;
        static constexpr int kBlockSize = 256;
        static constexpr double kBpm = 120.0;

        explicit AudioRig(LockstepProcessor& proc,
                          double bpm = kBpm,
                          double sampleRate = kSampleRate,
                          int blockSize = kBlockSize)
            : proc_(proc),
              playHead_(bpm, sampleRate, blockSize),
              blockSize_(blockSize)
        {
            proc_.setPlayHead(&playHead_);
            // A real host always calls setRateAndBufferSizeDetails() before
            // prepareToPlay(); without it getSampleRate() returns 0, which zeroes
            // every getSampleRate()-based calc in the audio path (e.g. musical-gate
            // length → instant note-off → silent envelope machines). Mirror the host.
            proc_.setRateAndBufferSizeDetails(sampleRate, blockSize);
            proc_.prepareToPlay(sampleRate, blockSize);
            // Start the in-plugin transport so sequencerRunning=true regardless of
            // which SyncMode the APVTS defaults to (Locked mode uses hostPlaying(),
            // other modes use inPluginPlaying()). Between this and a playing
            // StubPlayHead, both branches are covered.
            proc_.clock().setInPluginPlaying(true);
            // A host hands processBlock a buffer with max(totalIn, totalOut)
            // channels — every bus, not just the main pair. Since 11.12 enabled the
            // Cue/Aux/Send outputs and Ext2-4 inputs by default, that is ~20
            // channels, and a 2-channel buffer would send getBusBuffer() walking off
            // the end. Size it from the processor so the rig tracks the bus layout
            // instead of assuming one.
            buffer_.setSize(numHostChannels(), blockSize, false, true, false);
        }

        ~AudioRig() { proc_.releaseResources(); }

        AudioRig(const AudioRig&) = delete;
        AudioRig& operator=(const AudioRig&) = delete;

        // The channel count a host would allocate for processBlock.
        [[nodiscard]] int numHostChannels() const
        {
            return std::max(2, std::max(proc_.getTotalNumInputChannels(),
                                        proc_.getTotalNumOutputChannels()));
        }

        // Queue MIDI to be handed to the NEXT processBlock (the io/MidiInput
        // ingestion seam a hardware controller or the host feeds). Consumed by the
        // first renderBlocks() block, then cleared -- one block's worth, like a host.
        void injectInput(const juce::MidiBuffer& in) { pendingInput_.addEvents(in, 0, -1, 0); }

        // Run N blocks. Feeds any queued input on the first block, captures the
        // block's MIDI output, and advances the playhead after each block.
        void renderBlocks(int n)
        {
            for (int i = 0; i < n; ++i)
            {
                midi_.clear();
                if (i == 0 && ! pendingInput_.isEmpty())
                {
                    midi_.addEvents(pendingInput_, 0, -1, 0);
                    pendingInput_.clear();
                }
                buffer_.clear();
                proc_.processBlock(buffer_, midi_);   // midi_ carries in, returns out
                playHead_.advance();
            }
        }

        [[nodiscard]] bool lastBufferHasNaN() const
        {
            for (int ch = 0; ch < buffer_.getNumChannels(); ++ch)
                for (int i = 0; i < buffer_.getNumSamples(); ++i)
                    if (! std::isfinite(buffer_.getSample(ch, i)))
                        return true;
            return false;
        }

        [[nodiscard]] float lastBufferRms() const
        {
            double sum = 0.0;
            int n = 0;
            for (int ch = 0; ch < buffer_.getNumChannels(); ++ch)
            {
                sum += channelSumSq(ch);
                n += buffer_.getNumSamples();
            }
            return (n > 0) ? static_cast<float>(std::sqrt(sum / static_cast<double>(n))) : 0.0f;
        }

        // RMS of one channel -- so a journey can prove sound landed on the routed
        // output and nowhere spurious (a muted track's channel should go quiet).
        [[nodiscard]] float lastChannelRms(int ch) const
        {
            if (ch < 0 || ch >= buffer_.getNumChannels() || buffer_.getNumSamples() == 0)
                return 0.0f;
            return static_cast<float>(
                std::sqrt(channelSumSq(ch) / static_cast<double>(buffer_.getNumSamples())));
        }

        [[nodiscard]] const juce::AudioBuffer<float>& buffer() const { return buffer_; }
        [[nodiscard]] const juce::MidiBuffer& midiOut() const { return midi_; }
        [[nodiscard]] StubPlayHead& playHead() { return playHead_; }
        [[nodiscard]] int blockSize() const { return blockSize_; }

    private:
        [[nodiscard]] double channelSumSq(int ch) const
        {
            double sum = 0.0;
            for (int i = 0; i < buffer_.getNumSamples(); ++i)
            {
                const double v = static_cast<double>(buffer_.getSample(ch, i));
                sum += v * v;
            }
            return sum;
        }

        LockstepProcessor& proc_;
        StubPlayHead playHead_;
        int blockSize_;
        juce::AudioBuffer<float> buffer_;
        juce::MidiBuffer midi_;
        juce::MidiBuffer pendingInput_;
    };
} // namespace lockstep
