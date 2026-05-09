#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ParameterIDs.h"
#include "core/StateResolver.h"
#include "machine/SamplerMachine.h"
#include "state/PluginState.h"
#include <algorithm>
#include <cmath>

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

        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const auto ti = static_cast<std::size_t>(t);
            trackLengthParams_[ti]  = apvts_.getRawParameterValue(ParamIDs::trackLength(t));
            trackDividerParams_[ti] = apvts_.getRawParameterValue(ParamIDs::trackDivider(t));
            trackMuteParams_[ti]    = apvts_.getRawParameterValue(ParamIDs::trackMute(t));
        }

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

        gainSmoothed_.reset(sampleRate, 0.05);  // 50 ms ramp
        const float initGainDb = apvts_.getRawParameterValue(ParamIDs::outputGain)->load();
        gainSmoothed_.setCurrentAndTargetValue(
            juce::Decibels::decibelsToGain(initGainDb, -60.0f));

        dcX1_.fill(0.0f);
        dcY1_.fill(0.0f);
    }

    void LockstepProcessor::releaseResources()
    {
        dcX1_.fill(0.0f);
        dcY1_.fill(0.0f);
    }

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

            const int trackLen =
                static_cast<int>(trackLengthParams_[i]->load());
            const int trackDiv =
                static_cast<int>(trackDividerParams_[i]->load());

            const bool muted = trackMuteParams_[i]->load() >= 0.5f;
            if (samplesPerStep <= 0.0 || trackLen <= 0 || muted)
                continue;

            const double effectiveSPS =
                samplesPerStep * static_cast<double>(trackDiv <= 0 ? 1 : trackDiv);

            // Walk nextTriggerPos_[i] forward through this block, firing any
            // step boundaries that fall within [blockStart, blockEnd).
            int triggerAt = -1;
            int stepIndex = static_cast<int>(
                static_cast<std::int64_t>(blockStart / effectiveSPS) % trackLen);

            while (nextTriggerPos_[i] < blockEnd)
            {
                if (nextTriggerPos_[i] >= blockStart)
                {
                    const auto stepNum = static_cast<std::int64_t>(
                        nextTriggerPos_[i] / effectiveSPS);
                    stepIndex = static_cast<int>(stepNum % trackLen);

                    if (track.steps[static_cast<std::size_t>(stepIndex)].trig)
                        triggerAt = static_cast<int>(nextTriggerPos_[i] - blockStart);
                }
                nextTriggerPos_[i] += effectiveSPS;
            }

            const auto frame = StateResolver::resolve(track, stepIndex);
            machines_[i]->process(triggerAt, frame, buffer);
        }

        // Output stage: smoothed gain → DC blocker → soft-clip
        const float targetGainDb = apvts_.getRawParameterValue(ParamIDs::outputGain)->load();
        gainSmoothed_.setTargetValue(
            juce::Decibels::decibelsToGain(targetGainDb, -60.0f));

        const int numOut      = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();
        const int numDcChans  = std::min(numOut, static_cast<int>(dcX1_.size()));

        for (int i = 0; i < numSamples; ++i)
        {
            const float gain = gainSmoothed_.getNextValue();
            for (int ch = 0; ch < numOut; ++ch)
            {
                float s = buffer.getSample(ch, i) * gain;

                if (ch < numDcChans)
                {
                    const float x1 = dcX1_[static_cast<std::size_t>(ch)];
                    const float y1 = dcY1_[static_cast<std::size_t>(ch)];
                    const float y  = s - x1 + 0.999f * y1;
                    dcX1_[static_cast<std::size_t>(ch)] = s;
                    dcY1_[static_cast<std::size_t>(ch)] = y;
                    s = y;
                }

                buffer.setSample(ch, i, std::tanh(s));
            }
        }

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
