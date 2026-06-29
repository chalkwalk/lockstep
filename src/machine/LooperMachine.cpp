#include "LooperMachine.h"
#include <algorithm>

namespace lockstep
{
    ParamSpec LooperMachine::paramSpec(int index) const
    {
        if (index != kSlotInputSource) return {};
        ParamSpec s;
        s.id = kInputSourceSlotId;
        s.label = "Source";
        s.minValue = 0.0f;
        s.maxValue = kInputSourceMaxValue;  // None/Ext/Master/Track N (DESIGN §27)
        s.defaultValue = 1.0f;  // External — record live input by default
        s.isStepped = true;
        s.sectionIndex = kSrcSecIdx;
        s.valueLabels = std::span<const char* const>(kInputSourceLabels.data(),
                                                     kInputSourceLabels.size());
        return s;
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
        capacity_ = static_cast<int>(sampleRate_ * kLoopMaxSeconds);
        loop_.setSize(2, capacity_, false, true, false);
        backup_.setSize(2, capacity_, false, true, false);
        loop_.clear();
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
        loop_.clear();
        stateMirror_.store(static_cast<int>(state_), std::memory_order_release);
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
                        // Start a fresh recording from zero.
                        loop_.clear();
                        loopLen_ = 0;
                        recPos_ = 0;
                        haveBackup_ = false;
                        state_ = State::Recording;
                        break;
                    case State::Recording:
                        // Close the loop and start playing.
                        loopLen_ = std::max(0, recPos_);
                        playhead_ = 0;
                        state_ = (loopLen_ > 0) ? State::Playing : State::Idle;
                        break;
                    case State::Playing:
                        // Begin overdubbing: snapshot for one-level undo.
                        if (loopLen_ > 0)
                        {
                            backup_.makeCopyOf(loop_, true);
                            haveBackup_ = true;
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
                        // Play ends an in-progress recording too.
                        loopLen_ = std::max(0, recPos_);
                        playhead_ = 0;
                        state_ = (loopLen_ > 0) ? State::Playing : State::Idle;
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
                reset();
                break;
            case Cmd::Undo:
                if (haveBackup_ && loopLen_ > 0)
                {
                    loop_.makeCopyOf(backup_, true);
                    haveBackup_ = false;
                    state_ = State::Playing;
                }
                break;
        }
    }

    void LooperMachine::process(const juce::MidiBuffer& /*events*/,
                                const ParamFrame& /*params*/,
                                juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = std::min(2, buffer.getNumChannels());

        // Drain one queued command (slow control rate; single-slot is enough).
        const int raw = pendingCmd_.exchange(0, std::memory_order_acq_rel);
        if (raw != 0) applyCommand(static_cast<Cmd>(raw));

        // `buffer` arrives holding the input_source audio (fillTrackInput). Copy it
        // out before we overwrite the buffer with loop playback.
        inScratch_.setSize(chans, numSamples, false, false, true);
        for (int ch = 0; ch < chans; ++ch)
            inScratch_.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                const float in = inScratch_.getSample(ch, i);
                float out = 0.0f;
                switch (state_)
                {
                    case State::Recording:
                        if (recPos_ < capacity_)
                            loop_.setSample(ch, recPos_, in);
                        out = in;  // monitor while recording
                        break;
                    case State::Playing:
                        if (loopLen_ > 0) out = loop_.getSample(ch, playhead_);
                        break;
                    case State::Overdubbing:
                        if (loopLen_ > 0)
                        {
                            const float mixed = loop_.getSample(ch, playhead_) + in;
                            loop_.setSample(ch, playhead_, mixed);
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
                    loopLen_ = capacity_;
                    playhead_ = 0;
                    state_ = State::Playing;
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
