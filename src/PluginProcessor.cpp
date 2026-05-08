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
          apvts_(*this, nullptr, "Lockstep", createParameterLayout())
    {
        nextTriggerPos_.fill(0.0);

        for (auto& m : machines_)
            m = std::make_unique<SamplerMachine>(samplePool_);

        // Seed each track's base params from the machine's declared defaults.
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            for (int s = 0; s < kNumParamSlots; ++s)
            {
                sequence_.tracks[t].baseParams[static_cast<std::size_t>(s)] =
                    machines_[t]->getParamMetadata(s).defaultValue;
            }
        }
    }

    LockstepProcessor::~LockstepProcessor() = default;

    void LockstepProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        clock_.prepare(sampleRate);
        for (auto& m : machines_)
        {
            m->prepare(sampleRate, samplesPerBlock);
            m->reset();
        }
        nextTriggerPos_.fill(0.0);
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

        const double samplesPerStep = clock_.samplesPerStep();
        const auto   pos            = clock_.samplePosition();

        const double blockStart = static_cast<double>(pos);
        const double blockEnd   = blockStart + static_cast<double>(buffer.getNumSamples());

        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const auto& track = sequence_.tracks[i];

            if (samplesPerStep <= 0.0 || track.length <= 0)
                continue;

            const double effectiveSPS =
                samplesPerStep * static_cast<double>(track.divider <= 0 ? 1 : track.divider);

            // Walk nextTriggerPos_[i] forward through this block, firing any
            // step boundaries that fall within [blockStart, blockEnd).
            int triggerAt = -1;
            int stepIndex = static_cast<int>(
                static_cast<std::int64_t>(blockStart / effectiveSPS) % track.length);

            while (nextTriggerPos_[i] < blockEnd)
            {
                if (nextTriggerPos_[i] >= blockStart)
                {
                    const auto stepNum = static_cast<std::int64_t>(
                        nextTriggerPos_[i] / effectiveSPS);
                    stepIndex = static_cast<int>(stepNum % track.length);

                    if (track.steps[static_cast<std::size_t>(stepIndex)].trig)
                        triggerAt = static_cast<int>(nextTriggerPos_[i] - blockStart);
                }
                nextTriggerPos_[i] += effectiveSPS;
            }

            const auto frame = StateResolver::resolve(track, stepIndex);
            machines_[i]->process(triggerAt, frame, buffer);
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
