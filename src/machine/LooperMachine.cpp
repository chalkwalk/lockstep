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
        manualLen_ = false;
        brActive_ = false;
        brCaptured_ = false;
        tapeAction_ = Cmd::None;
        tapeResync_ = false;
        tapeMult_ = 1.0;
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
        // Sync (S1): loop length = track length (steps) × step PPQ, in samples.
        // samples-per-quarter = samplesPerBar / barPpq (consistent with the rest of
        // the transport snapshot, so multi-looper grid phase-lock stays exact).
        if (syncMode_ < kSyncGrid) return 0.0;
        if (loopGridSteps_ <= 0 || loopStepPpq_ <= 0.0) return 0.0;
        const double spb = transport_.samplesPerBar;
        const double barPpq = transport_.barPpq;
        if (spb <= 0.0 || barPpq <= 0.0) return 0.0;
        const double spq = spb / barPpq;
        return static_cast<double>(loopGridSteps_) * loopStepPpq_ * spq;
    }

    double LooperMachine::targetOutputSamples() const
    {
        if (syncMode_ >= kSyncGrid)  // Sync — grid-locked length
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
        if (syncMode_ >= kSyncGrid) return syncedLengthSamples();  // Sync — period = loop length
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
        manualLen_ = false;   // a fresh take re-attaches to grid-lock (S4)
        // Grid-locked modes (N Bar / Steps) auto-close after the synced length (at
        // the record tempo); Free/Free-Len close on the gesture.
        recLenTarget_ = 0;
        if (syncMode_ >= kSyncGrid)
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
            case Cmd::Halve:
                // S4: play only the first half of the loop window — a clean cut, no
                // resample. The buffer keeps its full content (a later Double recovers
                // it). Detach from grid-lock so it plays native (no pitch change).
                if (loopLen_ >= 2 && (state_ == State::Playing
                                      || state_ == State::Overdubbing
                                      || state_ == State::Stopped))
                {
                    loopLen_ /= 2;
                    if (playPos_ >= static_cast<double>(loopLen_))
                        playPos_ = std::fmod(playPos_, static_cast<double>(loopLen_));
                    lastPos_ = playPos_;
                    manualLen_ = true;
                    haveBackup_ = false;
                    pool_.setSourceBars(targetSlot_, 0.0);
                }
                break;
            case Cmd::Double:
                // S4: double the loop window — duplicate the content into the second
                // half (no resample, no pitch change). Capped at the slot capacity.
                if (loopLen_ > 0 && target_ != nullptr
                    && (state_ == State::Playing || state_ == State::Overdubbing
                        || state_ == State::Stopped))
                {
                    const int newLen = loopLen_ * 2;
                    if (newLen <= capacity_)
                    {
                        target_->setSize(target_->getNumChannels(), newLen, true, false, true);
                        const int tch = std::min(2, target_->getNumChannels());
                        for (int ch = 0; ch < tch; ++ch)
                            target_->copyFrom(ch, loopLen_, *target_, ch, 0, loopLen_);
                        loopLen_ = newLen;
                        manualLen_ = true;
                        haveBackup_ = false;
                        pool_.setSourceBars(targetSlot_, 0.0);
                    }
                }
                break;
            case Cmd::BeatRepeat:
            case Cmd::TapeStop:
            case Cmd::Dip:
            case Cmd::HalfSpeed:
            case Cmd::Reverse:
                break;  // momentary — driven by handlePerf, never the discrete path
        }
    }

    void LooperMachine::pushPerf(const PerfCmd& c) noexcept
    {
        int s1 = 0, sz1 = 0, s2 = 0, sz2 = 0;
        perfFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
        if (sz1 > 0) perfSlots_[static_cast<std::size_t>(s1)] = c;
        else if (sz2 > 0) perfSlots_[static_cast<std::size_t>(s2)] = c;
        perfFifo_.finishedWrite(sz1 + sz2);
    }

    void LooperMachine::handlePerf(const PerfCmd& c)
    {
        switch (c.action)
        {
            case Cmd::None:
                break;
            case Cmd::RecordCycle:
            case Cmd::PlayStop:
            case Cmd::Clear:
            case Cmd::Undo:
            case Cmd::Halve:
            case Cmd::Double:
                if (c.pressed) applyCommand(c.action, c.immediate);  // discrete: press edge only
                break;
            case Cmd::BeatRepeat:
                if (c.pressed) startBeatRepeat(c.value);
                else stopBeatRepeat();
                break;
            case Cmd::TapeStop:
            case Cmd::Dip:
            case Cmd::HalfSpeed:
            case Cmd::Reverse:
                if (c.pressed) startTapeFx(c.action);
                else stopTapeFx(c.action);
                break;
        }
    }

    void LooperMachine::startBeatRepeat(int rateIdx)
    {
        if (loopLen_ <= 0) return;
        brRateIdx_ = juce::jlimit(0, 3, rateIdx);
        // Cell length = a musical fraction of the bar (1/16, 1/8, 1/4, 1/2). With no
        // known tempo (Free, no transport) the loop itself is the bar. Clamp to the
        // loop so a cell can't exceed the buffer.
        static constexpr double kFrac[4] = { 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2 };
        const double barLen = transport_.samplesPerBar > 0.0
            ? transport_.samplesPerBar : static_cast<double>(loopLen_);
        double cell = barLen * kFrac[static_cast<std::size_t>(brRateIdx_)];
        cell = juce::jlimit(std::max(4.0, 0.002 * sampleRate_),
                            static_cast<double>(loopLen_), cell);
        brCellLen_ = cell;
        brShadow_ = playPos_;
        // Capture the cell currently under the playhead (grid-aligned to loop start).
        brCellStart_ = std::floor(playPos_ / cell) * cell;
        brCaptured_ = false;   // no jump at press; run to the boundary first
        brActive_ = true;
    }

    void LooperMachine::stopBeatRepeat()
    {
        if (!brActive_) return;
        brActive_ = false;
        playPos_ = brShadow_;  // resync to the free-running position (non-phase-locked)
    }

    void LooperMachine::startTapeFx(Cmd fx)
    {
        if (loopLen_ <= 0) return;
        tapeAction_ = fx;
        tapeResync_ = false;
        tapeGridPos_ = playPos_;   // anchor the grid-truth to the current position
        // tapeMult_ keeps its current value and slews to the effect target in-DSP.
    }

    void LooperMachine::stopTapeFx(Cmd fx)
    {
        if (tapeAction_ != fx) return;  // not the held effect (e.g. tape-stop already braked)
        tapeAction_ = Cmd::None;
        tapeResync_ = true;             // accelerate back + catch the grid on release
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

        // Drain the command FIFO: apply every queued edge (discrete verbs + momentary
        // press/release) in order before the DSP runs this block.
        {
            int s1 = 0, sz1 = 0, s2 = 0, sz2 = 0;
            perfFifo_.prepareToRead(perfFifo_.getNumReady(), s1, sz1, s2, sz2);
            for (int k = 0; k < sz1; ++k)
                handlePerf(perfSlots_[static_cast<std::size_t>(s1 + k)]);
            for (int k = 0; k < sz2; ++k)
                handlePerf(perfSlots_[static_cast<std::size_t>(s2 + k)]);
            perfFifo_.finishedRead(sz1 + sz2);
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
        // S4: a manual HALF/DBL length edit plays native (tOut=0 → rate 1, no
        // phase-lock) so the cut/double has no pitch change.
        const double tOut = manualLen_ ? 0.0 : targetOutputSamples();
        const double rateTarget = (loopLen_ > 0 && tOut > 0.0)
            ? static_cast<double>(loopLen_) / tOut : 1.0;
        const double slew = 1.0 - std::exp(-1.0 / (0.02 * sampleRate_));

        // C5: loop-wrap crossfade length (≤ a quarter of the loop).
        xfadeLen_ = (loopLen_ > 0)
            ? std::min(static_cast<int>(0.005 * sampleRate_), loopLen_ / 4) : 0;

        // Sync mode phase-locks the read position to the transport grid.
        const bool phaseLock = (syncMode_ >= kSyncGrid) && transport_.running && tOut > 0.0;

        // S5: beat-repeat overrides both phase-lock and free-running advance while held
        // (the playhead loops the captured cell instead). Constant for the block — the
        // FIFO that toggles brActive_ is drained above, before the sample loop.
        const bool brNow = brActive_ && loopLen_ > 0 && brCellLen_ > 0.0
                           && (state_ == State::Playing || state_ == State::Overdubbing);

        // S6: tape FX drive the playback rate through a slewed envelope (and, after
        // release, a one-pole catch-up to the grid). Like beat-repeat they override
        // phase-lock/free-run while engaged. Slew times: fast for the FX engage/catch,
        // slower for the tape-stop deceleration ramp.
        const bool tapeNow = (tapeAction_ != Cmd::None || tapeResync_) && loopLen_ > 0
                             && (state_ == State::Playing || state_ == State::Overdubbing);
        const double tapeSlewFast = 1.0 - std::exp(-1.0 / (0.006 * sampleRate_));
        const double tapeStopSlew = 1.0 - std::exp(-1.0 / (0.080 * sampleRate_));
        const double resyncCoeff  = 1.0 - std::exp(-1.0 / (0.040 * sampleRate_));

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
            if (phaseLock && !brNow && !tapeNow && loopLen_ > 0
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
                && !brNow && !tapeNow
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
            else if ((state_ == State::Playing || state_ == State::Overdubbing) && loopLen_ > 0)
            {
                if (brNow)
                {
                    // Advance the free-running shadow in parallel (release resyncs to it),
                    // and the play position inside the captured cell. Before the first
                    // boundary the playhead runs on to it (no jump at press); after, it
                    // loops [cellStart, cellStart+cellLen).
                    brShadow_ += rate_;
                    if (brShadow_ >= static_cast<double>(loopLen_))
                        brShadow_ -= static_cast<double>(loopLen_);
                    playPos_ += rate_;
                    const double cellEnd = brCellStart_ + brCellLen_;
                    if (!brCaptured_)
                    {
                        if (playPos_ >= cellEnd)
                        {
                            playPos_ = brCellStart_ + (playPos_ - cellEnd);
                            brCaptured_ = true;
                        }
                        else if (playPos_ >= static_cast<double>(loopLen_))
                            playPos_ -= static_cast<double>(loopLen_);
                    }
                    else if (playPos_ >= cellEnd)
                    {
                        playPos_ -= brCellLen_;
                    }
                }
                else if (tapeNow)
                {
                    // Grid-truth position advances at the base rate regardless of the FX,
                    // so a release can catch up to where the loop would have been.
                    tapeGridPos_ += rate_;
                    if (tapeGridPos_ >= static_cast<double>(loopLen_))
                        tapeGridPos_ -= static_cast<double>(loopLen_);

                    if (tapeAction_ != Cmd::None)
                    {
                        double target = 1.0;
                        double tSlew = tapeSlewFast;
                        switch (tapeAction_)
                        {
                            case Cmd::TapeStop:  target = 0.0;  tSlew = tapeStopSlew; break;
                            case Cmd::Dip:       target = kDipRate;                   break;
                            case Cmd::HalfSpeed: target = 0.5;                        break;
                            case Cmd::Reverse:   target = -1.0;                       break;
                            default: break;
                        }
                        tapeMult_ += (target - tapeMult_) * tSlew;
                        playPos_ += rate_ * tapeMult_;
                        if (playPos_ >= static_cast<double>(loopLen_)) playPos_ -= static_cast<double>(loopLen_);
                        else if (playPos_ < 0.0) playPos_ += static_cast<double>(loopLen_);
                        // Tape-stop braked to a standstill → graceful Stopped (pairs with
                        // the instant STOP button). Reset the envelope for the next play.
                        if (tapeAction_ == Cmd::TapeStop && tapeMult_ < 0.01)
                        {
                            state_ = State::Stopped;
                            tapeAction_ = Cmd::None;
                            tapeResync_ = false;
                            tapeMult_ = 1.0;
                        }
                    }
                    else  // tapeResync_: slew the rate back to 1 and one-pole the gap to grid
                    {
                        tapeMult_ += (1.0 - tapeMult_) * tapeSlewFast;
                        double gap = std::fmod(tapeGridPos_ - playPos_, static_cast<double>(loopLen_));
                        if (gap > static_cast<double>(loopLen_) * 0.5) gap -= static_cast<double>(loopLen_);
                        else if (gap < static_cast<double>(loopLen_) * -0.5) gap += static_cast<double>(loopLen_);
                        playPos_ += rate_ + gap * resyncCoeff;
                        if (playPos_ >= static_cast<double>(loopLen_)) playPos_ -= static_cast<double>(loopLen_);
                        else if (playPos_ < 0.0) playPos_ += static_cast<double>(loopLen_);
                        if (std::abs(gap) < 1.0 && std::abs(tapeMult_ - 1.0) < 0.01)
                        {
                            tapeResync_ = false;      // caught up — hand back to normal/phase-lock
                            playPos_ = tapeGridPos_;
                        }
                    }
                }
                else if (!phaseLock)
                {
                    playPos_ += rate_;
                    if (playPos_ >= static_cast<double>(loopLen_))
                        playPos_ -= static_cast<double>(loopLen_);
                }
                // phase-lock (non-beat-repeat / non-tape): playPos_ set from transport above.
            }
        }

        for (int ch = chans; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        stateMirror_.store(static_cast<int>(state_), std::memory_order_release);
        loopLenMirror_.store(loopLen_, std::memory_order_release);  // S4 chrome/tests
        brRateMirror_.store(brNow ? brRateIdx_ : -1, std::memory_order_release);  // S5
        // S6: light the held tape-fx cell (TapeStop=0, Dip=1, HalfSpeed=2, Reverse=3);
        // -1 while idle or merely resyncing after release.
        int tapeCell = -1;
        switch (tapeAction_)
        {
            case Cmd::TapeStop:  tapeCell = 0; break;
            case Cmd::Dip:       tapeCell = 1; break;
            case Cmd::HalfSpeed: tapeCell = 2; break;
            case Cmd::Reverse:   tapeCell = 3; break;
            default: break;
        }
        tapeMirror_.store(tapeCell, std::memory_order_release);

        // S2: publish loop-position chrome for the mini-seq. Phase 0..1 while playing
        // (-1 otherwise) drives the continuous playhead; pendingEdge drives the
        // landing pip at the loop-start anchor (Armed or a scheduled stop/re-play).
        const bool playing = (state_ == State::Playing || state_ == State::Overdubbing)
                             && loopLen_ > 0;
        phaseMirror_.store(playing
                               ? static_cast<float>(playPos_ / static_cast<double>(loopLen_))
                               : -1.0f,
                           std::memory_order_release);
        pendingMirror_.store(state_ == State::Armed || pendingAction_ != 0,
                             std::memory_order_release);
    }
}
