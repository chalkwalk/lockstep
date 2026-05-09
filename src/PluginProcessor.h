#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

#include "core/Clock.h"
#include "core/Sequence.h"
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
        const Clock& clock() const { return clock_; }
        SamplePool& samplePool() { return samplePool_; }
        EditContext& editContext() { return editContext_; }

        using juce::AudioProcessor::processBlock;

    private:
        juce::AudioProcessorValueTreeState apvts_;
        SamplePool samplePool_;
        Sequence sequence_;
        Clock clock_;
        EditContext editContext_;
        MidiInput midiInput_;
        std::array<std::unique_ptr<IMachine>, kNumTracks> machines_;
        std::array<double, kNumTracks> nextTriggerPos_{};

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
