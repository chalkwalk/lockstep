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
        nextTriggerPpq_.fill(0.0);

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
            // Track N defaults to sample index N so each track sounds distinct.
            sequence_.tracks[t].baseParams[0] = static_cast<float>(t);
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
        nextTriggerPpq_.fill(0.0);

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

        clock_.update(getPlayHead(), buffer.getNumSamples());

        const double blockStart    = clock_.ppqAtBlockStart();
        const double blockEnd      = clock_.ppqAtBlockEnd();
        const double samplesPerPpq = clock_.samplesPerPpq();

        // If the DAW looped or the user hit Reset, snap all per-track cursors
        // to the step boundary just at/before the new block start.
        if (clock_.ppqJumped())
        {
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                const int div = static_cast<int>(trackDividerParams_[i]->load());
                const double divPpq = 0.25 * static_cast<double>(div <= 0 ? 1 : div);
                if (divPpq > 0.0)
                    nextTriggerPpq_[i] = std::floor(blockStart / divPpq) * divPpq;
            }
        }

        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const auto& track = sequence_.tracks[i];

            const int trackLen = static_cast<int>(trackLengthParams_[i]->load());
            const int trackDiv = static_cast<int>(trackDividerParams_[i]->load());
            const bool muted   = trackMuteParams_[i]->load() >= 0.5f;

            // 16th note = 0.25 PPQ; divider scales the grid coarser.
            const double divPpq = 0.25 * static_cast<double>(trackDiv <= 0 ? 1 : trackDiv);

            if (divPpq <= 0.0 || samplesPerPpq <= 0.0 || trackLen <= 0 || muted)
                continue;

            // If the cursor has fallen far behind (cold start, late join),
            // snap it to the step boundary at/before blockStart so we don't
            // burn CPU catching up sample-by-sample.
            if (nextTriggerPpq_[i] < blockStart - divPpq)
                nextTriggerPpq_[i] = std::floor(blockStart / divPpq) * divPpq;

            int triggerAt = -1;
            int stepIndex = 0;

            while (nextTriggerPpq_[i] < blockEnd)
            {
                if (nextTriggerPpq_[i] >= blockStart)
                {
                    const auto stepNum = static_cast<std::int64_t>(
                        nextTriggerPpq_[i] / divPpq);
                    stepIndex = static_cast<int>(
                        stepNum % static_cast<std::int64_t>(trackLen));

                    if (track.steps[static_cast<std::size_t>(stepIndex)].trig)
                    {
                        const double offset =
                            (nextTriggerPpq_[i] - blockStart) * samplesPerPpq;
                        triggerAt = std::max(0, static_cast<int>(offset));
                    }
                }
                nextTriggerPpq_[i] += divPpq;
            }

            const auto frame = StateResolver::resolve(track, stepIndex);
            machines_[i]->process(triggerAt, frame, buffer);
        }

        // Output stage: smoothed gain → DC blocker → soft-clip
        const float targetGainDb = apvts_.getRawParameterValue(ParamIDs::outputGain)->load();
        gainSmoothed_.setTargetValue(
            juce::Decibels::decibelsToGain(targetGainDb, -60.0f));

        const int numOut     = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const int numDcChans = std::min(numOut, static_cast<int>(dcX1_.size()));

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
    }

    void LockstepProcessor::writeParam(int track, int slot, float value)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;
        if (slot < 0 || slot >= kNumParamSlots)
            return;

        const auto ti = static_cast<std::size_t>(track);

        if (editContext_.isActiveForEditing()
            && editContext_.heldTrackIndex() == track)
        {
            const int step = editContext_.heldStepIndex();
            if (step >= 0 && step < kMaxStepsPerTrack)
            {
                sequence_.tracks[ti].steps[static_cast<std::size_t>(step)]
                    .overrides.set(slot, value);
                editContext_.markParamWritten();
            }
        }
        else
        {
            sequence_.tracks[ti].baseParams[static_cast<std::size_t>(slot)] = value;
        }
    }

    void LockstepProcessor::clearParam(int track, int step, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step  < 0 || step  >= kMaxStepsPerTrack)             return;
        if (slot  < 0 || slot  >= kNumParamSlots)                return;
        sequence_.tracks[static_cast<std::size_t>(track)]
            .steps[static_cast<std::size_t>(step)].overrides.clear(slot);
    }

    ParamMetadata LockstepProcessor::paramMetadata(int track, int slot) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};
        return machines_[static_cast<std::size_t>(track)]->getParamMetadata(slot);
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
