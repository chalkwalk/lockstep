#include "LoopMachine.h"
#include "../deckcore/Resampler.h"
#include "../deckcore/Seam.h"
#include "DeckAdapter.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    namespace
    {
        // Shared bandlimited resampler for the R4 varispeed overdub scatter. const,
        // stateless, allocation-free; the kernel bank is built once at static init.
        const dc::Resampler& sharedLoopResampler()
        {
            static const dc::Resampler r;
            return r;
        }
    }

    ParamSpec LoopMachine::paramSpec(int index) const
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
                s.defaultValue = 2.0f;  // Sync — grid-locked; the sane default (Free,
                                        // which ignores tempo, is the hardest mode to
                                        // reason about). New tracks only; existing
                                        // projects keep their serialized value.
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
            case kSlotFreewheel:
                s.id = "loop_freewheel";
                s.label = "Freewhl";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;  // follow the main transport (subordinate)
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kFreewheelLabels.data(),
                                                             kFreewheelLabels.size());
                return s;
            default:
                return {};
        }
    }

    const char* LoopMachine::stateLabel(State s) noexcept
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

    void LoopMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        const int cap = static_cast<int>(sampleRate_ * kLoopMaxSeconds);
        backup_.setSize(2, cap, false, true, false);
        backup_.clear();
        overdubLayer_.setSize(2, cap, false, true, false);  // R4 overdub layer B
        overdubLayer_.clear();
        // C6: the seam splice's pre-roll. Small (a few ms) and always running.
        preLen_ = std::max(1, static_cast<int>(kSeamSpliceSec * sampleRate_));
        preRing_.setSize(2, preLen_, false, true, false);
        preRing_.clear();
        preSnap_.setSize(2, preLen_, false, true, false);
        preSnap_.clear();
        reset();
    }

    void LoopMachine::reset()
    {
        deck_.setState(State::Idle);
        loopLen_ = 0;
        playPos_ = 0.0;
        lastPos_ = 0.0;
        recPos_ = 0;
        recLenTarget_ = 0;
        deck_.cancelPending();
        rate_ = 1.0;
        haveBackup_ = false;
        manualLen_ = false;
        preSnapped_ = false;
        brActive_ = false;
        brCaptured_ = false;
        tapeAction_ = Cmd::None;
        tapeResync_ = false;
        tapeMult_ = 1.0;
        wowPhase_ = 0.0;
        wowDepth_ = 0.0;
        dropOverdubLayer();
        stateMirror_.store(static_cast<int>(deck_.state()), std::memory_order_release);
    }

    void LoopMachine::snapshotForUndo()
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

    void LoopMachine::pushPreRoll(const juce::AudioBuffer<float>& in, int sample, int chans)
    {
        if (preLen_ <= 0) return;
        for (int ch = 0; ch < std::min(chans, preRing_.getNumChannels()); ++ch)
            preRing_.setSample(ch, preWrite_, in.getSample(ch, sample));
        if (++preWrite_ >= preLen_) preWrite_ = 0;
    }

    void LoopMachine::spliceSeam()
    {
        if (! preSnapped_ || target_ == nullptr || loopLen_ <= 0) return;

        // Never take more than a quarter of the loop, and never more pre-roll than
        // we captured. A loop shorter than the splice keeps its seam, honestly.
        const int len = std::min(preLen_, loopLen_ / 4);
        if (len <= 0) return;

        // Through deck_core: the pool slot and the pre-roll snapshot each become a
        // medium by lending their channels, and the same splice the Tape face will
        // use runs over them. Splicing the CONTENT (not the playback) is what makes
        // every other reader of this slot — a Player, a promoted WAV — get a clean
        // take too.
        dc::Medium loopMed;
        dc::Medium leadMed;
        bindBuffer(loopMed, *target_, loopLen_, dc::Topology::Circular, sampleRate_);
        bindBuffer(leadMed, preSnap_, preLen_, dc::Topology::Linear, sampleRate_);
        loopMed.adoptUsed(0, loopLen_);   // both buffers hold real audio
        leadMed.adoptUsed(0, preLen_);

        // The pre-roll's LAST `len` samples are the ones that immediately precede
        // the take's first sample.
        dc::spliceLoopEnd(loopMed, 0, loopLen_, leadMed, 0, preLen_ - len, len);
    }

    float LoopMachine::loopSample(int ch, double pos, double readRate) const
    {
        if (target_ == nullptr || loopLen_ <= 0) return 0.0f;
        double p = std::fmod(pos, static_cast<double>(loopLen_));
        if (p < 0.0) p += static_cast<double>(loopLen_);

        // The read head's law, at every rate (DESIGN §40.10): one bandlimited
        // circular read with a rate-aware cutoff, direction-agnostic, holding a
        // sample at rate 0. The loop material is periodic, so the kernel wraps mod
        // loopLen_ and the window is well-defined across the seam.
        //
        // This used to branch — Hermite at or below unity, polyphase above, where
        // the aliasing was audible. The branch was an optimisation posing as a
        // law. Below unity the full-band kernel is a delta at integer positions
        // (rate 1 stays bit-exact) and strictly better at fractional ones, and one
        // path is one thing to reason about when varispeed sweeps through it.
        const auto interp = [&](double x) -> float {
            return sharedLoopResampler().readCircular(
                target_->getReadPointer(ch), loopLen_, x, readRate);
        };

        // C6: no crossfade here. The seam is spliced into the CONTENT at close, and
        // that is the only place it can be fixed: this read is circular, so just
        // after the wrap the kernel's taps reach backwards across the seam into the
        // tail, and no gain applied on the way out of [L-X, L) can undo a
        // discontinuity that lands inside the read window at [0, X). The old wrap
        // crossfade faded the head in early, played it again after the wrap, and
        // left the ringing step untouched — it was the click, not the cure.
        return interp(p);
    }

    float LoopMachine::readLayer(const juce::AudioBuffer<float>& buf, int ch,
                                 double pos, double readRate) const
    {
        if (loopLen_ <= 0 || ch < 0 || ch >= buf.getNumChannels()) return 0.0f;
        double p = std::fmod(pos, static_cast<double>(loopLen_));
        if (p < 0.0) p += static_cast<double>(loopLen_);
        // The same read law as loopSample, and for the same reason: layer B is read
        // by the same head that reads A, at the same rate.
        return sharedLoopResampler().readCircular(buf.getReadPointer(ch),
                                                  loopLen_, p, readRate);
    }

    void LoopMachine::commitOverdubLayer()
    {
        // Fold the fresh overdub layer B into the committed loop A (add-only), then
        // clear B. A's decay/feedback is applied separately (scaleLoop), so this
        // fold never multiplies existing content — the whole point of layering.
        if (!overdubPending_) return;
        if (target_ != nullptr && loopLen_ > 0)
        {
            const int tch = std::min(std::min(2, target_->getNumChannels()),
                                     overdubLayer_.getNumChannels());
            const int n = std::min(loopLen_, overdubLayer_.getNumSamples());
            for (int ch = 0; ch < tch; ++ch)
            {
                target_->addFrom(ch, 0, overdubLayer_, ch, 0, n);
                overdubLayer_.clear(ch, 0, n);
            }
        }
        overdubPending_ = false;
    }

    void LoopMachine::dropOverdubLayer()
    {
        if (overdubLayer_.getNumSamples() > 0) overdubLayer_.clear();
        overdubPending_ = false;
    }

    double LoopMachine::syncedLengthSamples() const
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

    double LoopMachine::targetOutputSamples() const
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

    void LoopMachine::scaleLoop(float g)
    {
        if (target_ == nullptr || loopLen_ <= 0) return;
        const int tch = std::min(2, target_->getNumChannels());
        for (int ch = 0; ch < tch; ++ch)
            juce::FloatVectorOperations::multiply(target_->getWritePointer(ch), g, loopLen_);
    }

    void LoopMachine::startRecording()
    {
        if (target_ == nullptr || capacity_ <= 0)
        {
            deck_.setState(State::Idle);
            return;
        }
        // A5: claim the whole capacity as the used length and clear it — a looper
        // does not know how long its take will be until the gesture closes, and it
        // shrinks to loopLen_ then.
        // Width = two channels per sub-track (§40.3). A single-sub-track loop takes
        // two, exactly as before; a four-sub-track deck takes eight, in one slot.
        target_ = pool_.beginVolatileCapture(targetSlot_, capacity_,
                                             2 * deck_.subTrackCount());
        if (target_ == nullptr)
        {
            deck_.setState(State::Idle);
            return;
        }
        loopLen_ = 0;
        recPos_ = 0;
        haveBackup_ = false;
        manualLen_ = false;   // a fresh take re-attaches to grid-lock (S4)

        // C6: freeze the pre-roll — the input immediately before this take's first
        // sample. It is what must precede loop[0] when the loop wraps, and at close
        // it is spliced into the loop's end.
        for (int ch = 0; ch < preSnap_.getNumChannels(); ++ch)
            for (int i = 0; i < preLen_; ++i)
                preSnap_.setSample(ch, i, preRing_.getSample(ch, (preWrite_ + i) % preLen_));
        preSnapped_ = true;
        // Grid-locked modes (N Bar / Steps) auto-close after the synced length (at
        // the record tempo); Free/Free-Len close on the gesture.
        recLenTarget_ = 0;
        if (syncMode_ >= kSyncGrid)
        {
            const double len = syncedLengthSamples();
            if (len > 0.0) recLenTarget_ = static_cast<int>(std::lround(len));
        }
        deck_.setState(State::Recording);
    }

    void LoopMachine::firePending()
    {
        applyEdge(deck_.firePending());
    }

    // One place turns a deck decision into looper work. dc::Deck owns the state
    // machine and the quantized edge; the medium, the pool slot and the overdub
    // layer are ours, so the doing stays here (DESIGN §40.11).
    void LoopMachine::applyEdge(const dc::DeckEdge& e)
    {
        if (e.startRecording) startRecording();      // may fall back to Idle
        if (e.closeRecording) closeRecording();      // may fall back to Idle
        if (e.beginOverdub)
        {
            snapshotForUndo();
            dropOverdubLayer();  // R4: start a fresh overdub layer B
        }
        // endOverdub needs no work: layer B is folded by the block-start commit.
        if (e.restartPlayback)
        {
            playPos_ = 0.0;
            lastPos_ = 0.0;
        }
        if (e.clear) doClear();
        if (e.undo) doUndo();
        if (e.halve) doHalve();
        if (e.doubleLen) doDouble();
    }

    void LoopMachine::doClear()
    {
        if (target_ != nullptr)
        {
            target_->setSize(target_->getNumChannels(), 0, false, false, true);
            pool_.setSourceBars(targetSlot_, 0.0);
            pool_.setVolatileOrigin(targetSlot_, SampleOrigin::Empty);  // W3a
        }
        reset();
    }

    void LoopMachine::doUndo()
    {
        dropOverdubLayer();  // R4: discard the in-progress overdub layer
        const int chans = std::min(target_->getNumChannels(), backup_.getNumChannels());
        const int n = std::min(loopLen_, backup_.getNumSamples());
        for (int ch = 0; ch < chans; ++ch)
            target_->copyFrom(ch, 0, backup_, ch, 0, n);
        haveBackup_ = false;
    }

    void LoopMachine::doHalve()
    {
        // S4: play only the first half of the loop window — a clean cut, no
        // resample. The buffer keeps its full content (a later Double recovers
        // it). Detach from grid-lock so it plays native (no pitch change).
        commitOverdubLayer();  // R4: fold B at the current length first
        loopLen_ /= 2;
        if (playPos_ >= static_cast<double>(loopLen_))
            playPos_ = std::fmod(playPos_, static_cast<double>(loopLen_));
        lastPos_ = playPos_;
        manualLen_ = true;
        haveBackup_ = false;
        pool_.setSourceBars(targetSlot_, 0.0);
    }

    void LoopMachine::doDouble()
    {
        // S4: double the loop window — duplicate the content into the second half
        // (no resample, no pitch change). Capped at the slot capacity.
        commitOverdubLayer();  // R4: fold B before duplicating content
        const int newLen = loopLen_ * 2;
        target_->setSize(target_->getNumChannels(), newLen, true, false, true);
        const int tch = std::min(2, target_->getNumChannels());
        for (int ch = 0; ch < tch; ++ch)
            target_->copyFrom(ch, loopLen_, *target_, ch, 0, loopLen_);
        loopLen_ = newLen;
        manualLen_ = true;
        haveBackup_ = false;
        pool_.setSourceBars(targetSlot_, 0.0);
    }

    void LoopMachine::applyCommand(Cmd c, bool immediate)
    {
        // 11.2: the discrete verbs ARE the deck's (dc::DeckCmd). Edge timing,
        // arming, punch-out and the double-tap instant override live in dc::Deck,
        // resolved against the one shared launch grid delivered by the processor
        // (9.17; loop_sync now selects loop *length* only). The momentary
        // performance actions below are the looper's own and stay here.
        //
        // `haveTake` answers "is there something to act on", and each verb asks a
        // different question of the buffer — which is why it is computed per verb
        // rather than once. dc::Deck does not know what a pool slot is.
        const auto snapshot = toSnapshot(transport_);

        const auto deckCmd = [c]() -> dc::DeckCmd {
            switch (c)
            {
                case Cmd::RecordCycle: return dc::DeckCmd::RecordCycle;
                case Cmd::PlayStop:    return dc::DeckCmd::PlayStop;
                case Cmd::Clear:       return dc::DeckCmd::Clear;
                case Cmd::Undo:        return dc::DeckCmd::Undo;
                case Cmd::Halve:       return dc::DeckCmd::Halve;
                case Cmd::Double:      return dc::DeckCmd::Double;
                case Cmd::None:
                case Cmd::BeatRepeat:
                case Cmd::TapeStop:
                case Cmd::Dip:
                case Cmd::HalfSpeed:
                case Cmd::Reverse:     break;
            }
            return dc::DeckCmd::None;
        }();

        if (deckCmd != dc::DeckCmd::None)
        {
            bool haveTake = loopLen_ > 0;
            switch (c)
            {
                case Cmd::Undo:
                    haveTake = haveBackup_ && loopLen_ > 0 && target_ != nullptr;
                    break;
                case Cmd::Halve:
                    haveTake = loopLen_ >= 2;
                    break;
                case Cmd::Double:
                    haveTake = loopLen_ > 0 && target_ != nullptr && loopLen_ * 2 <= capacity_;
                    break;
                case Cmd::Clear:
                    haveTake = true;  // clearing an empty deck is a no-op, not a refusal
                    break;
                default:
                    break;
            }

            applyEdge(deck_.applyCommand(deckCmd, immediate, snapshot, haveTake));
            return;
        }

        switch (c)
        {
            // Returned above, via the deck. Named here so -Wswitch keeps guarding
            // the enum: a new verb must be routed somewhere on purpose.
            case Cmd::None:
            case Cmd::RecordCycle:
            case Cmd::PlayStop:
            case Cmd::Clear:
            case Cmd::Undo:
            case Cmd::Halve:
            case Cmd::Double:
                break;

            case Cmd::BeatRepeat:
            case Cmd::TapeStop:
            case Cmd::Dip:
            case Cmd::HalfSpeed:
            case Cmd::Reverse:
                break;  // momentary — driven by handlePerf, never the discrete path
        }
    }

    void LoopMachine::pushPerf(const PerfCmd& c) noexcept
    {
        int s1 = 0, sz1 = 0, s2 = 0, sz2 = 0;
        perfFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
        if (sz1 > 0) perfSlots_[static_cast<std::size_t>(s1)] = c;
        else if (sz2 > 0) perfSlots_[static_cast<std::size_t>(s2)] = c;
        perfFifo_.finishedWrite(sz1 + sz2);
    }

    void LoopMachine::handlePerf(const PerfCmd& c)
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

    void LoopMachine::startBeatRepeat(int rateIdx)
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

    void LoopMachine::stopBeatRepeat()
    {
        if (!brActive_) return;
        brActive_ = false;
        playPos_ = brShadow_;  // resync to the free-running position (non-phase-locked)
    }

    void LoopMachine::startTapeFx(Cmd fx)
    {
        if (loopLen_ <= 0) return;
        tapeAction_ = fx;
        tapeResync_ = false;
        tapeGridPos_ = playPos_;   // anchor the grid-truth to the current position
        // tapeMult_ keeps its current value and slews to the effect target in-DSP.
    }

    void LoopMachine::stopTapeFx(Cmd fx)
    {
        if (tapeAction_ != fx) return;  // not the held effect (e.g. tape-stop already braked)
        tapeAction_ = Cmd::None;
        tapeResync_ = true;             // accelerate back + catch the grid on release
        wowDepth_ = 0.0;                // S6: the wobble stops; tapeMult_ glides to 1
    }

    void LoopMachine::closeRecording()
    {
        loopLen_ = std::max(0, recPos_);
        playPos_ = 0.0;
        lastPos_ = 0.0;
        if (loopLen_ > 0 && target_ != nullptr)
        {
            spliceSeam();  // C6: make the wrap continuous, in the content, once
            target_->setSize(target_->getNumChannels(), loopLen_, true, false, true);
            const double spb = transport_.samplesPerBar;
            pool_.setSourceBars(targetSlot_,
                                spb > 0.0 ? static_cast<double>(loopLen_) / spb : 0.0);
            pool_.setVolatileOrigin(targetSlot_, SampleOrigin::Loop);  // W3a: tag origin
            deck_.setState(State::Playing);
        }
        else
        {
            deck_.setState(State::Idle);
        }
    }

    void LoopMachine::process(const juce::MidiBuffer& /*events*/,
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

        // #4 / S7: resolve live-thru. Auto monitors an insert source (None/External)
        // in every state except while the captured loop plays back (state-aware —
        // computed per-sample below, since the record→play transition can happen
        // mid-block); a Track/Master tap is loop-only. On/Off are absolute.
        const int monMode = (params.size() > kSlotMonitor)
            ? static_cast<int>(std::lround(params[kSlotMonitor])) : 0;
        const InputSourceKind srcKind = (params.size() > kSlotInputSource)
            ? decodeInputSource(params[kSlotInputSource]).kind : InputSourceKind::None;

        // #4: decay. `decay` 0 = hold forever … 1 = full fade. Overdub mode applies
        // it only at the overdub write (a feedback knob); Always mode fades the whole
        // loop once per iteration (tape echo). decayGain is the per-application gain.
        const float decayAmt = (params.size() > kSlotDecay)
            ? juce::jlimit(0.0f, 1.0f, params[kSlotDecay]) : 0.0f;
        const float decayGain = 1.0f - decayAmt;
        const int decayMode = (params.size() > kSlotDecayMode)
            ? static_cast<int>(std::lround(params[kSlotDecayMode])) : kDecayOverdub;

        // Layered stop / DESIGN: a Sync (grid-locked) loop is subordinate to the
        // main transport — its playback holds when the transport stops and re-derives
        // its position from the transport phase on resume (automatic phase-correct).
        // A Free/Free-Len loop is not transport-locked, so it freewheels by nature
        // (plays transport-stopped — the pedal workflow); the explicit per-track
        // `loop_freewheel` param lets a Sync loop opt out of the subordination too.
        // Recording/monitoring are unaffected here.
        const bool freewheel = (params.size() > kSlotFreewheel)
            && std::lround(params[kSlotFreewheel]) >= 1;
        const bool transportGates =
            freewheel || (syncMode_ < kSyncGrid) || transport_.running;

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

        // R4: a drained command may have left Overdubbing (RecordCycle, quantized
        // stop, etc.) with an uncommitted overdub layer — fold it into the loop now
        // so a partial final pass isn't lost or read while stale (Undo/Clear drop it
        // instead, clearing overdubPending_ first).
        if (deck_.state() != State::Overdubbing && overdubPending_)
            commitOverdubLayer();

        // W1: keep the grid-locked record length in step with the LIVE tempo/grid.
        // recLenTarget_ was fixed once at startRecording(); if the BPM or the pushed
        // loop grid changes mid-take — or wasn't yet valid at record-start — the
        // captured length would no longer match the loop the sequencer plays back
        // (targetOutputSamples() is recomputed live every block), yielding a take
        // that's short (or long) relative to the musical loop. Re-derive it here so
        // record length and playback length stay the same musical duration.
        if (deck_.state() == State::Recording && syncMode_ >= kSyncGrid)
        {
            const double len = syncedLengthSamples();
            if (len > 0.0)
                recLenTarget_ = static_cast<int>(std::lround(len));
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

        // Sync mode phase-locks the read position to the transport grid.
        const bool phaseLock = (syncMode_ >= kSyncGrid) && transport_.running && tOut > 0.0;

        // S5: beat-repeat overrides both phase-lock and free-running advance while held
        // (the playhead loops the captured cell instead). Constant for the block — the
        // FIFO that toggles brActive_ is drained above, before the sample loop.
        const bool brNow = brActive_ && loopLen_ > 0 && brCellLen_ > 0.0
                           && (deck_.state() == State::Playing || deck_.state() == State::Overdubbing);

        // S6: tape FX drive the playback rate through a slewed envelope (and, after
        // release, a one-pole catch-up to the grid). Like beat-repeat they override
        // phase-lock/free-run while engaged. Slew times: fast for the FX engage/catch,
        // slower for the tape-stop deceleration ramp.
        const bool tapeNow = (tapeAction_ != Cmd::None || tapeResync_) && loopLen_ > 0
                             && (deck_.state() == State::Playing || deck_.state() == State::Overdubbing);
        // W4: tape-like glide — see kTape*Sec in the header. The engage/return glide
        // (tapeSlewFast) was ~6 ms and snapped; it now sweeps audibly on half/reverse.
        const double tapeSlewFast = 1.0 - std::exp(-1.0 / (kTapeGlideSec  * sampleRate_));
        const double tapeStopSlew = 1.0 - std::exp(-1.0 / (kTapeStopSec   * sampleRate_));
        const double resyncCoeff  = 1.0 - std::exp(-1.0 / (kTapeResyncSec * sampleRate_));

        // #2/9.17: quantize period for a pending edge (record-start / stop /
        // re-play) comes from the shared launch-quantize authority; the phase
        // offset aligns PhraseEnd edges to the track anchor after a relaunch.
        const double quantPeriod = transport_.launchQuantPeriodSamples;
        const double quantPhaseOffset = transport_.launchQuantPhaseOffsetSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            rate_ += (rateTarget - rate_) * slew;

            // #2: a pending quantized edge fires when the transport phase crosses a
            // bar-grid boundary within this block (sample-accurate). N-Bar modes use
            // an N-bar period so multiple loopers land on the same grid line.
            if (deck_.pendingEdge() && transport_.running && quantPeriod > 0.0)
            {
                const double phaseI = transport_.transportPhaseSamples
                                      + static_cast<double>(i) - quantPhaseOffset;
                if (std::floor(phaseI / quantPeriod) != std::floor((phaseI - 1.0) / quantPeriod))
                    firePending();
            }

            double pos = playPos_;
            if (phaseLock && !brNow && !tapeNow && loopLen_ > 0
                && (deck_.state() == State::Playing || deck_.state() == State::Overdubbing))
            {
                double frac = (transport_.transportPhaseSamples + static_cast<double>(i)) / tOut;
                frac -= std::floor(frac);
                pos = frac * static_cast<double>(loopLen_);
                playPos_ = pos;
                effRate_ = rateTarget;  // phase-lock advance = loopLen/tOut exactly
            }

            // C6: the pre-roll ring runs in every state — a take can begin on any
            // sample, and when it does, this is what preceded it.
            pushPreRoll(inScratch_, i, chans);

            for (int ch = 0; ch < chans; ++ch)
            {
                const float in = inScratch_.getSample(ch, i);
                const bool tch = ch < tchans;
                // Loop contribution to the output (separate from the live-thru so
                // monitor can gate the live signal without touching recording).
                float loopOut = 0.0f;
                switch (deck_.state())
                {
                    case State::Recording:
                        if (tch && recPos_ < capacity_)
                            target_->setSample(ch, recPos_, in);  // record regardless of monitor
                        break;
                    case State::Playing:
                        if (tch && loopLen_ > 0 && transportGates)
                            loopOut = loopSample(ch, pos, effRate_);
                        break;
                    case State::Overdubbing:
                        if (tch && loopLen_ > 0 && transportGates)
                        {
                            // R4: monitor the committed loop A plus the in-progress
                            // overdub layer B, and scatter the input into B with a
                            // bandlimited (add-only) fractional write — no integer
                            // quantisation on a varispeed write. A's decay/feedback
                            // and the fold of B into A happen once per iteration at
                            // the wrap (below), decoupled from this write, so the
                            // windowed spread never multi-decays overlapping slots.
                            loopOut = loopSample(ch, pos, effRate_)
                                    + readLayer(overdubLayer_, ch, pos, effRate_);
                            sharedLoopResampler().scatterAddCircular(
                                overdubLayer_.getWritePointer(ch), loopLen_, pos,
                                std::abs(effRate_), in);
                            overdubPending_ = true;
                        }
                        break;
                    case State::Idle:
                    case State::Armed:
                    case State::Stopped:
                        break;  // no loop output (silent loop; live-thru still governed below)
                }
                // #1 / S7: live-thru is governed by monitor alone (never touches
                // recording), resolved per-sample from the current state — an insert
                // looper monitors while Idle/Armed/Recording/Overdubbing/Stopped and
                // drops to loop-only once the take is Playing back. A parallel tap
                // stays loop-only. On/Off are absolute.
                const float live = resolveMonitor(monMode, srcKind, deck_.state()) ? in : 0.0f;
                buffer.setSample(ch, i, loopOut + live);
            }

            // One loop iteration completes when the read position wraps (drops below
            // the last). At that seam do the per-iteration bookkeeping: (#4) decay
            // the committed loop A, and (R4) commit the fresh overdub layer B into A.
            //   • Always decay fades A every iteration (tape echo, playing or
            //     overdubbing); Overdub decay fades A only while overdubbing (the
            //     feedback knob — was a per-sample write multiply, now an equivalent
            //     once-per-iteration scale since the overdub touches every slot once).
            //   • Committing B (add-only) folds the pass in AFTER A's decay, so k
            //     passes give A = Σ gᵏ⁻ʲ·Bⱼ (the classic feedback-looper sum).
            if (loopLen_ > 0 && !brNow && !tapeNow && transportGates
                && (deck_.state() == State::Playing || deck_.state() == State::Overdubbing)
                && pos < lastPos_)
            {
                const bool decayNow = decayAmt > 0.0f
                    && (decayMode == kDecayAlways
                        || (decayMode == kDecayOverdub && deck_.state() == State::Overdubbing));
                if (decayNow) scaleLoop(decayGain);
                if (deck_.state() == State::Overdubbing) commitOverdubLayer();
            }
            lastPos_ = pos;

            if (deck_.state() == State::Recording)
            {
                if (++recPos_ >= capacity_
                    || (recLenTarget_ > 0 && recPos_ >= recLenTarget_))
                    closeRecording();
            }
            else if ((deck_.state() == State::Playing || deck_.state() == State::Overdubbing)
                     && loopLen_ > 0 && transportGates)
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
                    effRate_ = rate_;
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
                            case Cmd::HalfSpeed: target = 0.5;                        break;
                            case Cmd::Reverse:   target = -1.0;                       break;
                            case Cmd::Dip:
                            {
                                // Wow: the head speed wobbles around unity for as long
                                // as the cell is held. The depth eases in through the
                                // ordinary glide, then the rate follows the wobble
                                // exactly — slewing the wobble itself would just be a
                                // lowpass on it, and at 5 Hz the 0.1 s glide would eat
                                // most of the depth.
                                wowDepth_ += (kWowDepth - wowDepth_) * tapeSlewFast;
                                wowPhase_ += 2.0 * juce::MathConstants<double>::pi
                                             * kWowRateHz / sampleRate_;
                                if (wowPhase_ >= 2.0 * juce::MathConstants<double>::pi)
                                    wowPhase_ -= 2.0 * juce::MathConstants<double>::pi;
                                target = 1.0 + wowDepth_ * std::sin(wowPhase_);
                                tSlew = 1.0;  // follow it; the depth envelope smooths entry
                                break;
                            }
                            default: break;
                        }
                        tapeMult_ += (target - tapeMult_) * tSlew;
                        effRate_ = rate_ * tapeMult_;
                        playPos_ += effRate_;
                        if (playPos_ >= static_cast<double>(loopLen_)) playPos_ -= static_cast<double>(loopLen_);
                        else if (playPos_ < 0.0) playPos_ += static_cast<double>(loopLen_);
                        // Tape-stop braked to a standstill → graceful Stopped (pairs with
                        // the instant STOP button). Reset the envelope for the next play.
                        if (tapeAction_ == Cmd::TapeStop && tapeMult_ < 0.01)
                        {
                            deck_.setState(State::Stopped);
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
                        effRate_ = rate_ + gap * resyncCoeff;  // catch-up can exceed 1
                        playPos_ += effRate_;
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
                    effRate_ = rate_;
                    playPos_ += rate_;
                    if (playPos_ >= static_cast<double>(loopLen_))
                        playPos_ -= static_cast<double>(loopLen_);
                }
                // phase-lock (non-beat-repeat / non-tape): playPos_ set from transport above.
            }
        }

        for (int ch = chans; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        stateMirror_.store(static_cast<int>(deck_.state()), std::memory_order_release);
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
        const bool playing = (deck_.state() == State::Playing || deck_.state() == State::Overdubbing)
                             && loopLen_ > 0;
        phaseMirror_.store(playing
                               ? static_cast<float>(playPos_ / static_cast<double>(loopLen_))
                               : -1.0f,
                           std::memory_order_release);
        pendingMirror_.store(deck_.pendingEdge(),
                             std::memory_order_release);
    }
}
