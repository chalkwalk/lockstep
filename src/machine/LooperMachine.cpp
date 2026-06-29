#include "LooperMachine.h"
#include <algorithm>
#include <cmath>

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
            case kSlotLoopSync:
                s.id = "loop_sync";
                s.label = "Sync";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kLoopSyncLabels.size() - 1);
                s.defaultValue = 0.0f;  // Free — native, ignores tempo
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kLoopSyncLabels.data(),
                                                             kLoopSyncLabels.size());
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
        playPos_ = 0.0;
        recPos_ = 0;
        recLenTarget_ = 0;
        rate_ = 1.0;
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

    float LooperMachine::loopSample(int ch, double pos) const
    {
        if (target_ == nullptr || loopLen_ <= 0) return 0.0f;
        double p = std::fmod(pos, static_cast<double>(loopLen_));
        if (p < 0.0) p += static_cast<double>(loopLen_);
        const int i0 = static_cast<int>(p);
        const int i1 = (i0 + 1) % loopLen_;
        const float a = target_->getSample(ch, i0);
        const float b = target_->getSample(ch, i1);
        return a + (b - a) * static_cast<float>(p - static_cast<double>(i0));
    }

    double LooperMachine::targetOutputSamples() const
    {
        const double spb = transport_.samplesPerBar;
        if (syncMode_ >= 2)  // 1/2/4 Bar
        {
            const double bars = static_cast<double>(1 << (syncMode_ - 2));
            return (spb > 0.0) ? bars * spb : 0.0;
        }
        if (syncMode_ == 1)  // Free Len — the recorded musical duration
        {
            const double bars = pool_.sourceBars(targetSlot_);
            return (spb > 0.0 && bars > 0.0) ? bars * spb : 0.0;
        }
        return 0.0;  // Free — native
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
                        if (target_ != nullptr && capacity_ > 0)
                        {
                            target_->setSize(target_->getNumChannels(), capacity_,
                                             false, false, true);
                            target_->clear();
                            loopLen_ = 0;
                            recPos_ = 0;
                            haveBackup_ = false;
                            // Bar-quantized modes auto-close after N bars (at the
                            // record tempo); Free/Free-Len close on the gesture.
                            recLenTarget_ = 0;
                            if (syncMode_ >= 2 && transport_.samplesPerBar > 0.0)
                            {
                                const double bars = static_cast<double>(1 << (syncMode_ - 2));
                                recLenTarget_ = static_cast<int>(
                                    std::lround(bars * transport_.samplesPerBar));
                            }
                            state_ = State::Recording;
                        }
                        break;
                    case State::Recording:
                        closeRecording();
                        break;
                    case State::Playing:
                        if (loopLen_ > 0)
                        {
                            snapshotForUndo();
                            state_ = State::Overdubbing;
                        }
                        break;
                    case State::Overdubbing:
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
                        if (loopLen_ > 0) { playPos_ = 0.0; state_ = State::Playing; }
                        break;
                    case State::Idle:
                        break;
                }
                break;
            case Cmd::Clear:
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
        playPos_ = 0.0;
        if (loopLen_ > 0 && target_ != nullptr)
        {
            target_->setSize(target_->getNumChannels(), loopLen_, true, false, true);
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

        const int targetSlot = (params.size() > kSlotTargetBuffer)
            ? static_cast<int>(std::lround(params[kSlotTargetBuffer])) : 0;
        targetSlot_ = pool_.nthVolatileIndex(targetSlot);
        target_ = pool_.mutableVolatilePcm(targetSlot_);
        capacity_ = pool_.volatileCapacity(targetSlot_);
        syncMode_ = (params.size() > kSlotLoopSync)
            ? static_cast<int>(std::lround(params[kSlotLoopSync])) : 0;

        const int raw = pendingCmd_.exchange(0, std::memory_order_acq_rel);
        if (raw != 0) applyCommand(static_cast<Cmd>(raw));

        if (target_ == nullptr)
        {
            buffer.clear();
            return;
        }

        inScratch_.setSize(chans, numSamples, false, false, true);
        for (int ch = 0; ch < chans; ++ch)
            inScratch_.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        const int tchans = std::min(chans, target_->getNumChannels());

        // Varispeed: target playback rate = loopLen / target output duration. Free
        // (or unknown tempo) → 1.0. Continuously tracked, one-pole slewed (~20 ms).
        const double tOut = targetOutputSamples();
        const double rateTarget = (loopLen_ > 0 && tOut > 0.0)
            ? static_cast<double>(loopLen_) / tOut : 1.0;
        const double slew = 1.0 - std::exp(-1.0 / (0.02 * sampleRate_));

        // Bar-quantized modes phase-lock the read position to the transport grid.
        const bool phaseLock = (syncMode_ >= 2) && transport_.running && tOut > 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            rate_ += (rateTarget - rate_) * slew;

            double pos = playPos_;
            if (phaseLock && loopLen_ > 0
                && (state_ == State::Playing || state_ == State::Overdubbing))
            {
                double frac = (transport_.transportPhaseSamples + static_cast<double>(i)) / tOut;
                frac -= std::floor(frac);
                pos = frac * static_cast<double>(loopLen_);
                playPos_ = pos;
            }

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
                        if (tch && loopLen_ > 0) out = loopSample(ch, pos);
                        break;
                    case State::Overdubbing:
                        if (tch && loopLen_ > 0)
                        {
                            // Overdub at the nearest integer position (varispeed
                            // write) so the new layer sums coherently into the loop.
                            int wi = static_cast<int>(std::llround(pos)) % loopLen_;
                            if (wi < 0) wi += loopLen_;
                            const float mixed = target_->getSample(ch, wi) + in;
                            target_->setSample(ch, wi, mixed);
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

            if (state_ == State::Recording)
            {
                if (++recPos_ >= capacity_
                    || (recLenTarget_ > 0 && recPos_ >= recLenTarget_))
                    closeRecording();
            }
            else if (!phaseLock && (state_ == State::Playing || state_ == State::Overdubbing)
                     && loopLen_ > 0)
            {
                playPos_ += rate_;
                if (playPos_ >= static_cast<double>(loopLen_))
                    playPos_ -= static_cast<double>(loopLen_);
            }
        }

        for (int ch = chans; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        stateMirror_.store(static_cast<int>(state_), std::memory_order_release);
    }
}
