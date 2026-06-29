#include "LooperMachine.h"
#include <algorithm>

namespace lockstep
{
    ParamSpec LooperMachine::paramSpec(int index) const
    {
        ParamSpec s;
        s.sectionIndex = kSrcSecIdx;
        switch (index)
        {
            case kSlotInputSource:
                s.id = kInputSourceSlotId;
                s.label = "Source";
                s.minValue = 0.0f;
                s.maxValue = kInputSourceMaxValue;  // None/Ext/Master/Track N (DESIGN §27)
                s.defaultValue = 1.0f;  // External — record live input by default
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kInputSourceLabels.data(),
                                                             kInputSourceLabels.size());
                return s;
            case kSlotTargetBuffer:
                s.id = "target_buffer";
                s.label = "Buffer";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kVolatileBufferLabels.size() - 1);
                s.defaultValue = 0.0f;
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kVolatileBufferLabels.data(),
                                                             kVolatileBufferLabels.size());
                return s;
            default:
                return {};
        }
    }

    const char* LooperMachine::stateLabel(State s) noexcept
    {
        switch (s)
        {
            case State::Idle:        return "--";
            case State::Recording:   return "REC";
            case State::Playing:     return "PLAY";
            case State::Overdubbing: return "OD";
            case State::Stopped:     return "STOP";
        }
        return "--";
    }

    void LooperMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        const int cap = static_cast<int>(sampleRate_ * kLoopMaxSeconds);
        backup_.setSize(2, cap, false, true, false);
        backup_.clear();
        reset();
    }

    void LooperMachine::reset()
    {
        state_ = State::Idle;
        loopLen_ = 0;
        playhead_ = 0;
        recPos_ = 0;
        haveBackup_ = false;
        stateMirror_.store(static_cast<int>(state_), std::memory_order_release);
    }

    void LooperMachine::snapshotForUndo()
    {
        if (target_ == nullptr || loopLen_ <= 0)
        {
            haveBackup_ = false;
            return;
        }
        const int chans = std::min(target_->getNumChannels(), backup_.getNumChannels());
        const int n = std::min(loopLen_, backup_.getNumSamples());
        for (int ch = 0; ch < chans; ++ch)
            backup_.copyFrom(ch, 0, *target_, ch, 0, n);
        haveBackup_ = true;
    }

    void LooperMachine::applyCommand(Cmd c)
    {
        switch (c)
        {
            case Cmd::None:
                break;
            case Cmd::RecordCycle:
                switch (state_)
                {
                    case State::Idle:
                    case State::Stopped:
                        // Start a fresh recording from zero. Grow the pool buffer
                        // back to full capacity (no realloc — pre-sized) and clear.
                        if (target_ != nullptr && capacity_ > 0)
                        {
                            target_->setSize(target_->getNumChannels(), capacity_,
                                             false, false, true);
                            target_->clear();
                            loopLen_ = 0;
                            recPos_ = 0;
                            haveBackup_ = false;
                            state_ = State::Recording;
                        }
                        break;
                    case State::Recording:
                        closeRecording();
                        break;
                    case State::Playing:
                        // Begin overdubbing: snapshot for one-level undo.
                        if (loopLen_ > 0)
                        {
                            snapshotForUndo();
                            state_ = State::Overdubbing;
                        }
                        break;
                    case State::Overdubbing:
                        // Toggle overdub off, keep playing.
                        state_ = State::Playing;
                        break;
                }
                break;
            case Cmd::PlayStop:
                switch (state_)
                {
                    case State::Recording:
                        closeRecording();
                        break;
                    case State::Playing:
                    case State::Overdubbing:
                        state_ = State::Stopped;
                        break;
                    case State::Stopped:
                        if (loopLen_ > 0) { playhead_ = 0; state_ = State::Playing; }
                        break;
                    case State::Idle:
                        break;  // nothing to play
                }
                break;
            case Cmd::Clear:
                // Empty the loop and the pool slot so a Sampler/Player reading it is
                // silent; back to Idle.
                if (target_ != nullptr)
                {
                    target_->setSize(target_->getNumChannels(), 0, false, false, true);
                    pool_.setSourceBars(targetSlot_, 0.0);
                }
                reset();
                break;
            case Cmd::Undo:
                if (haveBackup_ && loopLen_ > 0 && target_ != nullptr)
                {
                    const int chans = std::min(target_->getNumChannels(),
                                               backup_.getNumChannels());
                    const int n = std::min(loopLen_, backup_.getNumSamples());
                    for (int ch = 0; ch < chans; ++ch)
                        target_->copyFrom(ch, 0, backup_, ch, 0, n);
                    haveBackup_ = false;
                    state_ = State::Playing;
                }
                break;
        }
    }

    void LooperMachine::closeRecording()
    {
        loopLen_ = std::max(0, recPos_);
        playhead_ = 0;
        if (loopLen_ > 0 && target_ != nullptr)
        {
            // Shrink the reported length to the loop so a Sampler/Player reading the
            // slot plays exactly the captured region (no realloc — within capacity).
            target_->setSize(target_->getNumChannels(), loopLen_, true, false, true);
            // Stamp the captured musical length (bars) for tempo-tracking playback.
            const double spb = transport_.samplesPerBar;
            pool_.setSourceBars(targetSlot_,
                                spb > 0.0 ? static_cast<double>(loopLen_) / spb : 0.0);
            state_ = State::Playing;
        }
        else
        {
            state_ = State::Idle;
        }
    }

    void LooperMachine::process(const juce::MidiBuffer& /*events*/,
                                const ParamFrame& params,
                                juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = std::min(2, buffer.getNumChannels());

        // Resolve the target volatile pool slot for this block (B3). The loop lives
        // here so a Sampler/Player pointed at the same slot can also play it.
        const int targetSlot = (params.size() > kSlotTargetBuffer)
            ? static_cast<int>(std::lround(params[kSlotTargetBuffer])) : 0;
        targetSlot_ = pool_.nthVolatileIndex(targetSlot);
        target_ = pool_.mutableVolatilePcm(targetSlot_);
        capacity_ = pool_.volatileCapacity(targetSlot_);

        // Drain one queued command (slow control rate; single-slot is enough).
        const int raw = pendingCmd_.exchange(0, std::memory_order_acq_rel);
        if (raw != 0) applyCommand(static_cast<Cmd>(raw));

        // No valid slot → silence.
        if (target_ == nullptr)
        {
            buffer.clear();
            return;
        }

        // `buffer` arrives holding the input_source audio (fillTrackInput). Copy it
        // out before we overwrite the buffer with loop playback.
        inScratch_.setSize(chans, numSamples, false, false, true);
        for (int ch = 0; ch < chans; ++ch)
            inScratch_.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        const int tchans = std::min(chans, target_->getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                const float in = inScratch_.getSample(ch, i);
                float out = 0.0f;
                const bool tch = ch < tchans;
                switch (state_)
                {
                    case State::Recording:
                        if (tch && recPos_ < capacity_)
                            target_->setSample(ch, recPos_, in);
                        out = in;  // monitor while recording
                        break;
                    case State::Playing:
                        if (tch && loopLen_ > 0) out = target_->getSample(ch, playhead_);
                        break;
                    case State::Overdubbing:
                        if (tch && loopLen_ > 0)
                        {
                            const float mixed = target_->getSample(ch, playhead_) + in;
                            target_->setSample(ch, playhead_, mixed);
                            out = mixed;
                        }
                        break;
                    case State::Idle:
                    case State::Stopped:
                        out = 0.0f;
                        break;
                }
                buffer.setSample(ch, i, out);
            }

            // Advance the single shared position once per sample.
            if (state_ == State::Recording)
            {
                if (++recPos_ >= capacity_)
                {
                    // Hit capacity: auto-close into playback.
                    closeRecording();
                }
            }
            else if ((state_ == State::Playing || state_ == State::Overdubbing) && loopLen_ > 0)
            {
                if (++playhead_ >= loopLen_) playhead_ = 0;
            }
        }

        // Clear any extra output channels the loop did not write.
        for (int ch = chans; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        stateMirror_.store(static_cast<int>(state_), std::memory_order_release);
    }
}
