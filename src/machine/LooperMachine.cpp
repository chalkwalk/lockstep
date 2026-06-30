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
            case kSlotMonitor:
                s.id = "loop_monitor";
                s.label = "Mon";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kMonitorLabels.size() - 1);
                s.defaultValue = 0.0f;  // Auto
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kMonitorLabels.data(),
                                                             kMonitorLabels.size());
                return s;
            case kSlotDecay:
                s.id = "loop_decay";
                s.label = "Decay";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;  // no decay (loop holds indefinitely)
                return s;
            case kSlotDecayMode:
                s.id = "loop_decay_mode";
                s.label = "Decay Md";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kDecayModeLabels.size() - 1);
                s.defaultValue = 0.0f;  // Overdub
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kDecayModeLabels.data(),
                                                             kDecayModeLabels.size());
                return s;
            case kSlotDiv:
                s.id = "loop_div";
                s.label = "Div";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kLoopDivLabels.size() - 1);
                s.defaultValue = static_cast<float>(kDefaultDivIdx);  // 1/16
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kLoopDivLabels.data(),
                                                             kLoopDivLabels.size());
                return s;
            case kSlotSteps:
                s.id = "loop_steps";
                s.label = "Steps";
                s.minValue = 1.0f;
                s.maxValue = static_cast<float>(kMaxLoopSteps);
                s.defaultValue = 16.0f;
                s.isStepped = true;
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
            case State::Armed:       return "ARM";
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
        lastPos_ = 0.0;
        recPos_ = 0;
        recLenTarget_ = 0;
        pendingAction_ = 0;
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

        const auto interp = [&](double x) -> float {
            double q = std::fmod(x, static_cast<double>(loopLen_));
            if (q < 0.0) q += static_cast<double>(loopLen_);
            const int i0 = static_cast<int>(q);
            const int i1 = (i0 + 1) % loopLen_;
            const float a = target_->getSample(ch, i0);
            const float b = target_->getSample(ch, i1);
            return a + (b - a) * static_cast<float>(q - static_cast<double>(i0));
        };

        const float base = interp(p);

        // C5: equal-power crossfade across the loop wrap. In the last xfadeLen_
        // samples, fade the tail out and the head (position `into`) in, so loop end
        // meets loop start without a click.
        if (xfadeLen_ > 0 && p >= static_cast<double>(loopLen_ - xfadeLen_))
        {
            const double into = p - static_cast<double>(loopLen_ - xfadeLen_);
            const float t = static_cast<float>(into / static_cast<double>(xfadeLen_));
            const float gOut = std::cos(t * juce::MathConstants<float>::halfPi);
            const float gIn = std::sin(t * juce::MathConstants<float>::halfPi);
            return base * gOut + interp(into) * gIn;
        }
        return base;
    }

    double LooperMachine::syncedLengthSamples() const
    {
        const double spb = transport_.samplesPerBar;
        if (spb <= 0.0) return 0.0;
        if (syncMode_ == kSyncSteps)  // Steps — loop_steps × clock division
        {
            const int spbar = std::max(1, loopDivStepsPerBar_);
            return static_cast<double>(loopSteps_) * (spb / static_cast<double>(spbar));
        }
        if (syncMode_ >= 2)  // 1/2/4 Bar
            return static_cast<double>(1 << (syncMode_ - 2)) * spb;
        return 0.0;
    }

    double LooperMachine::targetOutputSamples() const
    {
        if (syncMode_ >= 2)  // 1/2/4 Bar and Steps — grid-locked length
            return syncedLengthSamples();
        if (syncMode_ == 1)  // Free Len — the recorded musical duration
        {
            const double spb = transport_.samplesPerBar;
            const double bars = pool_.sourceBars(targetSlot_);
            return (spb > 0.0 && bars > 0.0) ? bars * spb : 0.0;
        }
        return 0.0;  // Free — native
    }

    double LooperMachine::quantPeriodSamples() const
    {
        const double spb = transport_.samplesPerBar;
        if (spb <= 0.0) return 0.0;
        if (syncMode_ >= 2) return syncedLengthSamples();  // N Bar / Steps — period = loop length
        if (syncMode_ == 1) return spb;  // Free Len → quantise the start to the bar grid
        return 0.0;  // Free — no quantise
    }

    void LooperMachine::scaleLoop(float g)
    {
        if (target_ == nullptr || loopLen_ <= 0) return;
        const int tch = std::min(2, target_->getNumChannels());
        for (int ch = 0; ch < tch; ++ch)
            juce::FloatVectorOperations::multiply(target_->getWritePointer(ch), g, loopLen_);
    }

    void LooperMachine::startRecording()
    {
        if (target_ == nullptr || capacity_ <= 0)
        {
            state_ = State::Idle;
            return;
        }
        target_->setSize(target_->getNumChannels(), capacity_, false, false, true);
        target_->clear();
        loopLen_ = 0;
        recPos_ = 0;
        haveBackup_ = false;
        // Grid-locked modes (N Bar / Steps) auto-close after the synced length (at
        // the record tempo); Free/Free-Len close on the gesture.
        recLenTarget_ = 0;
        if (syncMode_ >= 2)
        {
            const double len = syncedLengthSamples();
            if (len > 0.0) recLenTarget_ = static_cast<int>(std::lround(len));
        }
        state_ = State::Recording;
    }

    void LooperMachine::firePending()
    {
        switch (pendingAction_)
        {
            case 1: startRecording(); break;                              // Armed → Recording
            case 2: state_ = State::Stopped; break;                       // quantized stop
            case 3: playPos_ = 0.0; lastPos_ = 0.0; state_ = State::Playing; break;  // quantized re-play
            default: break;
        }
        pendingAction_ = 0;
    }

    void LooperMachine::applyCommand(Cmd c, bool immediate)
    {
        // Quantize is implied by the sync mode (#2): Free = instant; Free Len /
        // N Bar = snap the edge to the bar grid. A double-tap (immediate) forces the
        // edge now, overriding quantize and cancelling any pending action.
        const double period = quantPeriodSamples();
        const bool quantStart = !immediate && period > 0.0;                  // can arm even when stopped (fires on transport roll/boundary)
        const bool quantPlay  = !immediate && period > 0.0 && transport_.running;

        switch (c)
        {
            case Cmd::None:
                break;
            case Cmd::RecordCycle:
                switch (state_)
                {
                    case State::Idle:
                    case State::Stopped:
                        if (quantStart) { state_ = State::Armed; pendingAction_ = 1; }
                        else startRecording();
                        break;
                    case State::Armed:
                        if (immediate) startRecording();                     // double-tap: start now
                        else { state_ = (loopLen_ > 0) ? State::Stopped : State::Idle;
                               pendingAction_ = 0; }                          // single tap: cancel arm
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
                        if (quantPlay) pendingAction_ = 2;                    // quantized stop on the bar
                        else state_ = State::Stopped;
                        break;
                    case State::Stopped:
                        if (loopLen_ > 0)
                        {
                            if (quantPlay) pendingAction_ = 3;               // quantized re-play on the bar
                            else { playPos_ = 0.0; lastPos_ = 0.0; state_ = State::Playing; }
                        }
                        break;
                    case State::Armed:
                        state_ = (loopLen_ > 0) ? State::Stopped : State::Idle;
                        pendingAction_ = 0;                                   // cancel arm
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
        lastPos_ = 0.0;
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
        // Steps-mode length controls: clock division → steps/bar, and step count.
        const int divIdx = (params.size() > kSlotDiv)
            ? std::clamp(static_cast<int>(std::lround(params[kSlotDiv])), 0,
                         static_cast<int>(kDivStepsPerBar.size()) - 1)
            : kDefaultDivIdx;
        loopDivStepsPerBar_ = kDivStepsPerBar[static_cast<std::size_t>(divIdx)];
        loopSteps_ = (params.size() > kSlotSteps)
            ? std::max(1, static_cast<int>(std::lround(params[kSlotSteps]))) : 16;

        // #4: resolve live-thru. Auto monitors only when the looper is the source's
        // sole path out (None/External insert); a Track/Master tap is loop-only
        // (the tapped source is already audible — passing it through double-monitors).
        const int monMode = (params.size() > kSlotMonitor)
            ? static_cast<int>(std::lround(params[kSlotMonitor])) : 0;
        const InputSourceKind srcKind = (params.size() > kSlotInputSource)
            ? decodeInputSource(params[kSlotInputSource]).kind : InputSourceKind::None;
        const bool monitorOn = resolveMonitor(monMode, srcKind);

        // #4: decay. `decay` 0 = hold forever … 1 = full fade. Overdub mode applies
        // it only at the overdub write (a feedback knob); Always mode fades the whole
        // loop once per iteration (tape echo). decayGain is the per-application gain.
        const float decayAmt = (params.size() > kSlotDecay)
            ? juce::jlimit(0.0f, 1.0f, params[kSlotDecay]) : 0.0f;
        const float decayGain = 1.0f - decayAmt;
        const int decayMode = (params.size() > kSlotDecayMode)
            ? static_cast<int>(std::lround(params[kSlotDecayMode])) : kDecayOverdub;

        const int raw = pendingCmd_.exchange(0, std::memory_order_acq_rel);
        if (raw != 0)
        {
            const bool immediate = (raw & kImmediateBit) != 0;   // double-tap override (#2)
            applyCommand(static_cast<Cmd>(raw & ~kImmediateBit), immediate);
        }

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

        // C5: loop-wrap crossfade length (≤ a quarter of the loop).
        xfadeLen_ = (loopLen_ > 0)
            ? std::min(static_cast<int>(0.005 * sampleRate_), loopLen_ / 4) : 0;

        // Bar-quantized modes phase-lock the read position to the transport grid.
        const bool phaseLock = (syncMode_ >= 2) && transport_.running && tOut > 0.0;

        // #2: quantize period for a pending edge (record-start / stop / re-play).
        const double quantPeriod = quantPeriodSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            rate_ += (rateTarget - rate_) * slew;

            // #2: a pending quantized edge fires when the transport phase crosses a
            // bar-grid boundary within this block (sample-accurate). N-Bar modes use
            // an N-bar period so multiple loopers land on the same grid line.
            if (pendingAction_ != 0 && transport_.running && quantPeriod > 0.0)
            {
                const double phaseI = transport_.transportPhaseSamples + static_cast<double>(i);
                if (std::floor(phaseI / quantPeriod) != std::floor((phaseI - 1.0) / quantPeriod))
                    firePending();
            }

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
                const bool tch = ch < tchans;
                // Loop contribution to the output (separate from the live-thru so
                // monitor can gate the live signal without touching recording).
                float loopOut = 0.0f;
                switch (state_)
                {
                    case State::Recording:
                        if (tch && recPos_ < capacity_)
                            target_->setSample(ch, recPos_, in);  // record regardless of monitor
                        break;
                    case State::Playing:
                        if (tch && loopLen_ > 0) loopOut = loopSample(ch, pos);
                        break;
                    case State::Overdubbing:
                        if (tch && loopLen_ > 0)
                        {
                            // Overdub at the nearest integer position (varispeed
                            // write) so the new layer sums coherently into the loop.
                            // The write always includes `in` (it IS the overdub);
                            // monitor only governs whether we ALSO hear it live.
                            int wi = static_cast<int>(std::llround(pos)) % loopLen_;
                            if (wi < 0) wi += loopLen_;
                            const float oldLoop = target_->getSample(ch, wi);
                            // #4 Overdub decay fades the old layer at the write (the
                            // classic feedback knob); Always decay leaves the write
                            // alone and fades the whole loop per iteration (below).
                            const float kept = (decayMode == kDecayOverdub)
                                ? oldLoop * decayGain : oldLoop;
                            target_->setSample(ch, wi, kept + in);
                            loopOut = oldLoop;
                        }
                        break;
                    case State::Idle:
                    case State::Armed:
                    case State::Stopped:
                        break;  // no loop output (silent loop; live-thru still governed below)
                }
                // #1: live-thru is governed by monitor alone, in EVERY state — an
                // insert looper (Auto→On) must pass input through even while Idle/
                // Armed/Stopped (you hear what you're about to record). A parallel
                // tap (Auto→Off) stays loop-only. Monitor never touches recording.
                const float live = monitorOn ? in : 0.0f;
                buffer.setSample(ch, i, loopOut + live);
            }

            // #4 Always-decay: the read position wrapping (pos drops below the last)
            // marks one completed iteration — fade the whole stored loop by decayGain.
            if (decayMode == kDecayAlways && decayAmt > 0.0f && loopLen_ > 0
                && (state_ == State::Playing || state_ == State::Overdubbing)
                && pos < lastPos_)
                scaleLoop(decayGain);
            lastPos_ = pos;

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

        // #26: publish loop-phase chrome for the step-grid view. Phase 0..1 while
        // playing (-1 otherwise); grid cell count is quantize-aware (Steps → step
        // count, N Bar → bars×4 beats, Free/Free-Len → 16 continuous segments).
        const bool playing = (state_ == State::Playing || state_ == State::Overdubbing)
                             && loopLen_ > 0;
        phaseMirror_.store(playing
                               ? static_cast<float>(playPos_ / static_cast<double>(loopLen_))
                               : -1.0f,
                           std::memory_order_release);
        int gc = 16;
        if (syncMode_ == kSyncSteps)
            gc = std::clamp(loopSteps_, 1, 16);
        else if (syncMode_ >= 2)
            gc = std::clamp((1 << (syncMode_ - 2)) * 4, 1, 16);  // bars × 4 beats
        gridMirror_.store(gc, std::memory_order_release);
    }
}
