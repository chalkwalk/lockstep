#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ParameterIDs.h"
#include "core/StateResolver.h"
#include "machine/SamplerMachine.h"
#include "state/PluginState.h"

namespace lockstep
{
    namespace
    {
        struct BusesPropertiesAccessor : juce::AudioProcessor
        {
            static BusesProperties make()
            {
                return BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true);
            }
        };
    }

    LockstepProcessor::LockstepProcessor()
        : juce::AudioProcessor(BusesPropertiesAccessor::make()),
          apvts_(*this, nullptr, "Lockstep", createParameterLayout()),
          machine_(std::make_unique<SamplerMachine>())
    {
    }

    LockstepProcessor::~LockstepProcessor() = default;

    void LockstepProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        clock_.prepare(sampleRate);
        machine_->prepare(sampleRate, samplesPerBlock);
        machine_->reset();
    }

    void LockstepProcessor::releaseResources() {}

    bool LockstepProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
    {
        const auto& mainOut = layouts.getMainOutputChannelSet();
        return mainOut == juce::AudioChannelSet::stereo()
            || mainOut == juce::AudioChannelSet::mono();
    }

    void LockstepProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto totalIn  = getTotalNumInputChannels();
        const auto totalOut = getTotalNumOutputChannels();
        for (int ch = totalIn; ch < totalOut; ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
        buffer.clear();

        midiInput_.process(midi, editContext_);

        const auto& tracks = sequence_.tracks;
        const double samplesPerStep = clock_.samplesPerStep();
        const auto pos = clock_.samplePosition();

        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const auto& track = tracks[i];
            int stepIndex = 0;
            if (samplesPerStep > 0.0 && track.length > 0)
            {
                const auto totalSteps = static_cast<std::int64_t>(
                    static_cast<double>(pos) / (samplesPerStep * static_cast<double>(track.divider <= 0 ? 1 : track.divider)));
                stepIndex = static_cast<int>(((totalSteps % track.length) + track.length) % track.length);
            }

            const auto frame = StateResolver::resolve(track, stepIndex);
            machine_->process(frame, buffer);
        }

        const float gainDb = apvts_.getRawParameterValue(ParamIDs::outputGain)->load();
        buffer.applyGain(juce::Decibels::decibelsToGain(gainDb, -60.0f));

        clock_.advance(buffer.getNumSamples());
    }

    juce::AudioProcessorEditor* LockstepProcessor::createEditor()
    {
        return new LockstepEditor(*this);
    }

    void LockstepProcessor::getStateInformation(juce::MemoryBlock& dest)
    {
        PluginState::writeTo(dest, apvts_);
    }

    void LockstepProcessor::setStateInformation(const void* data, int sizeInBytes)
    {
        PluginState::readFrom(data, sizeInBytes, apvts_);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lockstep::LockstepProcessor();
}
