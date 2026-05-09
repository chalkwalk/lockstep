#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

#include "core/Clock.h"
#include "core/Sequence.h"
#include "core/SyncMode.h"
#include "io/EditContext.h"
#include "io/MidiInput.h"
#include "machine/IMachine.h"
#include "machine/SamplePool.h"

namespace lockstep
{
    class LockstepProcessor : public juce::AudioProcessor
    {
    public:
        LockstepProcessor();
        ~LockstepProcessor() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "Lockstep"; }
        bool acceptsMidi() const override                    { return true; }
        bool producesMidi() const override                   { return false; }
        bool isMidiEffect() const override                   { return false; }
        double getTailLengthSeconds() const override         { return 0.0; }

        int getNumPrograms() override                        { return 1; }
        int getCurrentProgram() override                     { return 0; }
        void setCurrentProgram(int) override                 {}
        const juce::String getProgramName(int) override      { return {}; }
        void changeProgramName(int, const juce::String&) override {}

        void getStateInformation(juce::MemoryBlock& dest) override;
        void setStateInformation(const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
        Sequence& sequence() { return sequence_; }
        const Sequence& sequence() const { return sequence_; }
        Clock&       clock()       { return clock_; }
        const Clock& clock() const { return clock_; }
        SamplePool& samplePool() { return samplePool_; }
        EditContext& editContext() { return editContext_; }

        // Route a parameter write to the correct layer. If EditContext is
        // active for the given track, the value lands in the held step's
        // P-Lock; otherwise it updates the track's base params.
        void writeParam(int track, int slot, float value);

        // Remove the P-Lock override for one slot on a specific step.
        void clearParam(int track, int step, int slot);

        // Metadata for a slot from the machine on the given track.
        ParamMetadata paramMetadata(int track, int slot) const;

        using juce::AudioProcessor::processBlock;

    private:
        juce::AudioProcessorValueTreeState apvts_;
        SamplePool samplePool_;
        Sequence sequence_;
        Clock clock_;
        EditContext editContext_;
        MidiInput midiInput_;
        std::array<std::unique_ptr<IMachine>, kNumTracks> machines_;
        std::array<double, kNumTracks> nextTriggerPpq_{};
        std::array<bool, kNumTracks>   lastStepFired_{};   // prev-dependency state
        double anchorPpq_ = 0.0;         // Auto mode: PPQ at last in-plugin Play press
        bool   wasInPluginPlaying_ = false;  // Auto mode: rising-edge detection

        // Raw pointer to the syncMode choice parameter, cached in ctor.
        std::atomic<float>* syncModeParam_ = nullptr;

        // Cached APVTS raw-value pointers for per-track structural params (audio-thread safe).
        std::array<std::atomic<float>*, kNumTracks> trackLengthParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackDividerParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackMuteParams_{};

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed_;
        std::array<float, 2> dcX1_{};  // per-channel DC blocker: previous input
        std::array<float, 2> dcY1_{};  // per-channel DC blocker: previous output

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepProcessor)
    };
}
