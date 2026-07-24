#pragma once
// EngineHarness -- helpers for headless LockstepProcessor tests.
//
// Usage:
//   EngineHarness h;               // constructs + prepareToPlay at 48 kHz / 256 samples
//   h.renderBlocks(10);            // drive 10 processBlocks with advancing playhead
//   h.processor().someMethod();    // direct access to the processor
//
// The processor standup (rate/buffer/channel/playhead) lives in AudioRig -- the
// single owner shared with UiDriver's live-audio bridge, so the two rigs cannot
// drift. EngineHarness owns the processor and delegates the driving to AudioRig;
// StubPlayHead now lives in AudioRig.h too.

#include "AudioRig.h"

#include <memory>

namespace lockstep
{
    // Assembles everything needed for a headless processBlock run.
    class EngineHarness
    {
    public:
        static constexpr double kSampleRate = AudioRig::kSampleRate;
        static constexpr int kBlockSize = AudioRig::kBlockSize;
        static constexpr double kBpm = AudioRig::kBpm;

        EngineHarness()
            : processor_(std::make_unique<LockstepProcessor>()),
              audio_(*processor_, kBpm, kSampleRate, kBlockSize)
        {}

        // The channel count a host would allocate for processBlock.
        [[nodiscard]] int numHostChannels() const { return audio_.numHostChannels(); }

        LockstepProcessor& processor() { return *processor_; }

        // Run N blocks. Advances the playhead after each block.
        void renderBlocks(int n) { audio_.renderBlocks(n); }

        bool lastBufferHasNaN() const { return audio_.lastBufferHasNaN(); }
        float lastBufferRms() const { return audio_.lastBufferRms(); }

        const juce::AudioBuffer<float>& buffer() const { return audio_.buffer(); }
        const juce::MidiBuffer& midiOut() const { return audio_.midiOut(); }
        StubPlayHead& playHead() { return audio_.playHead(); }

    private:
        // processor_ before audio_: audio_ holds a reference to it and must be
        // destroyed (releaseResources) while the processor is still alive.
        std::unique_ptr<LockstepProcessor> processor_;
        AudioRig audio_;
    };

} // namespace lockstep
