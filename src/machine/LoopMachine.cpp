#include "LoopMachine.h"
#include "../deckcore/Resampler.h"
#include "../deckcore/Seam.h"
#include "ChannelPolicy.h"
#include "DeckAdapter.h"
#include "StretchMath.h"
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

        // The per-sub-track mix params, generated rather than switched (§40.3).
        if (index >= kSlotSubMixBase && index < kSlotSubSrcBase)
        {
            const int rel = index - kSlotSubMixBase;
            const int sub = rel / kSubMixFields;         // 0..3
            const int field = rel % kSubMixFields;       // 0 level,1 pan,2 mute,3 solo
            const juce::String n{ sub + 1 };             // user-facing 1-based
            switch (field)
            {
                case 0:  // level
                    s.id = "sub" + n + "_level";
                    s.label = "T" + n + " Lvl";
                    s.minValue = 0.0f; s.maxValue = 1.0f; s.defaultValue = 1.0f;
                    return s;
                case 1:  // pan (balance law: 0 = both channels unity)
                    s.id = "sub" + n + "_pan";
                    s.label = "T" + n + " Pan";
                    s.minValue = -1.0f; s.maxValue = 1.0f; s.defaultValue = 0.0f;
                    return s;
                case 2:  // mute
                    s.id = "sub" + n + "_mute";
                    s.label = "T" + n + " Mute";
                    s.minValue = 0.0f; s.maxValue = 1.0f; s.defaultValue = 0.0f;
                    s.isStepped = true;
                    return s;
                default: // solo
                    s.id = "sub" + n + "_solo";
                    s.label = "T" + n + " Solo";
                    s.minValue = 0.0f; s.maxValue = 1.0f; s.defaultValue = 0.0f;
                    s.isStepped = true;
                    return s;
            }
        }

        // Per-sub input source for subs 1..3 (S2, §40.3). Sub 0 uses
        // kSlotInputSource (id "input_source"); these carry input_source_2/3/4.
        // Default None so an extra sub is unassigned (and, under the S4 arming
        // law, disarmed) until the user picks a source for it.
        if (index >= kSlotSubSrcBase && index < kNumSlots)
        {
            const int sub = index - kSlotSubSrcBase + 1;  // 1..3
            s.id = inputSourceSlotId(sub);                 // input_source_2/3/4
            s.label = "T" + juce::String(sub + 1) + " Src";
            s.minValue = 0.0f;
            s.maxValue = kInputSourceMaxValue;
            s.defaultValue = 0.0f;  // None
            s.isStepped = true;
            s.valueLabels = std::span<const char* const>(kInputSourceLabels.data(),
                                                         kInputSourceLabels.size());
            return s;
        }

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
            case kSlotSubTrackCount:
                s.id = "subtrack_count";
                s.label = "Tracks";
                s.minValue = 1.0f;
                s.maxValue = static_cast<float>(kMaxInputSubTracks);  // 1..4 (§40.3)
                s.defaultValue = 1.0f;   // a single stereo sub-track — today's Loop
                s.isStepped = true;
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

    void LoopMachine::prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        const int cap = static_cast<int>(sampleRate_ * kLoopMaxSeconds);
        // §40.3: the undo backup and the overdub layer are deck-medium-wide, not
        // pair-0 — a 4-sub-track overdub folds into all armed channel-pairs, and
        // whole-deck undo restores all of them. Sized to the widest possible deck
        // (kMaxDeckChannels = kDeckChans); a single-track loop only ever touches
        // pair 0.
        backup_.setSize(kDeckChans, cap, false, true, false);
        backup_.clear();
        overdubLayer_.setSize(kDeckChans, cap, false, true, false);  // R4 overdub layer B
        overdubLayer_.clear();
        // C6 + §40.13: the pre-roll ring is deck-wide and retro-length (long enough
        // to cover a double-tap window), so a retro record-start can backfill every
        // sub-track's channel-pair. The seam splice length (`seamLen_`) is kept
        // short and independent — spliceSeam takes only that off the ring's tail.
        seamLen_ = std::max(1, static_cast<int>(kSeamSpliceSec * sampleRate_));
        preLen_ = std::max(seamLen_, static_cast<int>(kRetroPreRollSec * sampleRate_));
        preRing_.setSize(kDeckChans, preLen_, false, true, false);
        preRing_.clear();
        preSnap_.setSize(kDeckChans, preLen_, false, true, false);
        preSnap_.clear();
        const int maxBlock = std::max(1, maxBlockSize);
        for (auto& b : subInput_) { b.setSize(2, maxBlock, false, true, false); b.clear(); }
        // S7: the FreeLen fit's realtime engine + its per-block output scratch. The
        // engine is prepared at the deck width (equal source/output rate — a pure
        // time stretch, no resampling); all allocation happens here, so start()/
        // process() are audio-thread safe.
        fitEngine_.prepare(sampleRate_, sampleRate_, kDeckChans, maxBlock);
        fitScratch_.setSize(kDeckChans, maxBlock, false, true, false);
        fitScratch_.clear();
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
        tap1Valid_ = false;        // §40.13 retro double-tap
        tap1Pos_ = 0.0;
        recStartPos_ = 0.0;
        retroBackfill_ = 0;
        retroCloseLen_ = -1;
        brActive_ = false;
        brCaptured_ = false;
        tapeAction_ = Cmd::None;
        tapeResync_ = false;
        tapeMult_ = 1.0;
        wowPhase_ = 0.0;
        wowDepth_ = 0.0;
        replacing_ = false;
        // S7: drop any fit and its engine voice.
        if (fitState_ == FitState::Streaming) fitEngine_.reset();
        fitState_ = FitState::Off;
        fitScope_ = FitScope::WholeDeck;
        fitSub_ = -1;
        fitSrcLen_ = 0;
        fitTargetLen_ = 0;
        fitScratchFilled_ = false;
        fitSrcBuf_.setSize(0, 0);
        fitStateMirror_.store(0, std::memory_order_release);
        fitTargetLenMirror_.store(0, std::memory_order_relaxed);
        fitSrcLenMirror_.store(0, std::memory_order_relaxed);
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
        const int ringCh = preRing_.getNumChannels();
        // Sub 0 (the track buffer) → channel-pair 0.
        for (int ch = 0; ch < std::min({ chans, 2, ringCh }); ++ch)
            preRing_.setSample(ch, preWrite_, in.getSample(ch, sample));
        // §40.13: subs 1..3 → their channel-pairs, so a deck-wide retro backfill
        // reaches every armed sub-track. The ring always runs, in every state.
        for (int sub = 1; sub < kMaxInputSubTracks; ++sub)
        {
            const auto& si = subInput_[static_cast<std::size_t>(sub)];
            for (int ch = 0; ch < 2; ++ch)
            {
                const int rc = 2 * sub + ch;
                if (rc < ringCh && ch < si.getNumChannels() && sample < si.getNumSamples())
                    preRing_.setSample(rc, preWrite_, si.getSample(ch, sample));
            }
        }
        if (++preWrite_ >= preLen_) preWrite_ = 0;
    }

    void LoopMachine::spliceSeam()
    {
        if (! preSnapped_ || target_ == nullptr || loopLen_ <= 0) return;

        // Never take more than a quarter of the loop, and never more than the seam
        // length (`seamLen_`, a few ms — decoupled from the retro-length ring,
        // §40.13). A loop shorter than the splice keeps its seam, honestly.
        const int len = std::min(seamLen_, loopLen_ / 4);
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

    float LoopMachine::mixSubTracks(int outCh, double pos, double readRate,
                                    int subCount) const
    {
        if (target_ == nullptr || loopLen_ <= 0) return 0.0f;
        // Solo is subtractive: if any sub-track is soloed, only soloed ones play.
        bool anySolo = false;
        for (int sub = 0; sub < subCount; ++sub)
            if (deck_.subTrack(sub).soloed) { anySolo = true; break; }

        float acc = 0.0f;
        for (int sub = 0; sub < subCount; ++sub)
        {
            const auto& st = deck_.subTrack(sub);
            if (st.muted || (anySolo && !st.soloed)) continue;
            const int rdCh = 2 * sub + outCh;   // pair `sub`, L or R
            if (rdCh >= target_->getNumChannels()) continue;
            // Center-unity balance: pan 0 leaves both channels at level; panning
            // toward one side attenuates the opposite channel (so a lone centered
            // sub-track reads exactly as loopSample).
            const float panGain = (outCh == 0)
                ? (st.pan <= 0.0f ? 1.0f : 1.0f - st.pan)
                : (st.pan >= 0.0f ? 1.0f : 1.0f + st.pan);
            acc += loopSample(rdCh, pos, readRate) * st.level * panGain;
        }
        return acc;
    }

    // -- S7 FreeLen pitch-preserved fit ------------------------------------------

    int LoopMachine::LoopFitSource::read(float* dest, int ch, juce::int64 srcPos, int n)
    {
        // WholeDeck reads the loop's own recorded PCM (target_); SingleSub (the FIT
        // verb) reads its untruncated source copy (fitSrcBuf_).
        const bool sub = owner != nullptr && owner->fitScope_ == FitScope::SingleSub;
        const auto* buf = owner == nullptr ? nullptr
                                           : (sub ? &owner->fitSrcBuf_ : owner->target_);
        const int len = owner != nullptr ? owner->fitSrcLen_ : 0;
        const int chans = buf != nullptr ? buf->getNumChannels() : 0;
        const int useCh = std::min(ch, chans - 1);
        for (int i = 0; i < n; ++i)
        {
            const juce::int64 sp = srcPos + i;
            dest[i] = (buf != nullptr && useCh >= 0 && sp >= 0 && sp < len)
                          ? buf->getSample(useCh, static_cast<int>(sp)) : 0.0f;
        }
        return n;
    }

    juce::int64 LoopMachine::LoopFitSource::length() const
    {
        return owner != nullptr ? owner->fitSrcLen_ : 0;
    }

    int LoopMachine::LoopFitSource::numChannels() const
    {
        // WholeDeck reads the recorded deck width (2 per sub-track); SingleSub reads a
        // stereo source. engineChannels() clamps to the policy width.
        if (owner == nullptr) return 2;
        const auto* buf = owner->fitScope_ == FitScope::SingleSub
                              ? &owner->fitSrcBuf_ : owner->target_;
        return buf != nullptr ? engineChannels(buf->getNumChannels()) : 2;
    }

    double LoopMachine::LoopFitSource::sampleRate() const
    {
        return owner != nullptr ? owner->sampleRate_ : 44100.0;
    }

    void LoopMachine::engageFreeLenFit()
    {
        // FreeLen only (Free = native, Sync = varispeed grid-lock). The recorded
        // length rounds UP to the next launch-quant multiple, extend-only, and the
        // window is filled pitch-preserved.
        fitState_ = FitState::Off;
        fitScope_ = FitScope::WholeDeck;
        fitSub_ = -1;
        if (syncMode_ != 1 || loopLen_ <= 0 || target_ == nullptr) return;

        const double tol = kFitTolSec * sampleRate_;
        const juce::int64 target = stretchmath::fitTargetLength(
            loopLen_, transport_.launchQuantPeriodSamples, tol, transport_.samplesPerBar);
        fitSrcLen_ = loopLen_;
        fitTargetLen_ = static_cast<int>(target);

        // Already on-grid (target == recorded), or the fitted window would overrun
        // the slot capacity → adopt as-is (no stretch). Publishing Baked lets the
        // processor skip a pointless bake.
        if (fitTargetLen_ <= loopLen_ || (capacity_ > 0 && fitTargetLen_ > capacity_))
        {
            fitTargetLen_ = loopLen_;
            fitState_ = FitState::Baked;
        }
        else
        {
            const double ratio = static_cast<double>(fitTargetLen_)
                                 / static_cast<double>(loopLen_);
            loopFitSource_.owner = this;
            fitEngine_.start(&loopFitSource_, 0.0, ratio, 1.0);
            fitEngine_.setLoop(0, fitSrcLen_);  // seamless looped stretched read
            fitState_ = FitState::Streaming;
            playPos_ = 0.0;
            lastPos_ = 0.0;
        }

        // Publish the lengths BEFORE the generation bump (release), so a processor
        // that observes the new generation reads consistent target/source lengths.
        fitTargetLenMirror_.store(fitTargetLen_, std::memory_order_relaxed);
        fitSrcLenMirror_.store(fitSrcLen_, std::memory_order_relaxed);
        fitBakeGen_.fetch_add(1, std::memory_order_release);
    }

    void LoopMachine::engageSubFit(int sub, const juce::AudioBuffer<float>& src,
                                   int srcLen, int window)
    {
        cancelFit();  // clear any prior fit (also bumps the generation)
        if (srcLen <= 0 || window <= 0) return;
        fitScope_ = FitScope::SingleSub;
        fitSub_ = clampSub(sub);
        fitSrcLen_ = srcLen;
        fitTargetLen_ = window;
        // Keep an untruncated stereo copy of the source: target_'s pair holds it only
        // up to the window, but the stretch reads the whole source.
        fitSrcBuf_.setSize(2, srcLen, false, false, true);
        fitSrcBuf_.clear();
        const int sc = std::max(1, engineChannels(src.getNumChannels()));
        for (int c = 0; c < 2; ++c)
            fitSrcBuf_.copyFrom(c, 0, src, std::min(c, sc - 1), 0,
                                std::min(srcLen, src.getNumSamples()));

        const double ratio = static_cast<double>(window) / static_cast<double>(srcLen);
        loopFitSource_.owner = this;
        fitEngine_.start(&loopFitSource_, 0.0, ratio, 1.0);
        fitEngine_.setLoop(0, srcLen);       // seamless looped stretched read
        fitState_ = FitState::Streaming;
        playPos_ = 0.0;
        lastPos_ = 0.0;

        fitTargetLenMirror_.store(fitTargetLen_, std::memory_order_relaxed);
        fitSrcLenMirror_.store(fitSrcLen_, std::memory_order_relaxed);
        fitStateMirror_.store(static_cast<int>(fitState_), std::memory_order_release);
        // Bump AFTER the lengths so a processor observing the new generation reads
        // consistent target/source lengths, then queues the bake.
        fitBakeGen_.fetch_add(1, std::memory_order_release);
    }

    void LoopMachine::cancelFit()
    {
        if (fitState_ == FitState::Streaming) fitEngine_.reset();
        fitState_ = FitState::Off;
        fitScope_ = FitScope::WholeDeck;
        fitSub_ = -1;
        fitSrcLen_ = 0;
        fitTargetLen_ = 0;
        fitScratchFilled_ = false;
        fitSrcBuf_.setSize(0, 0);
        fitTargetLenMirror_.store(0, std::memory_order_relaxed);
        fitSrcLenMirror_.store(0, std::memory_order_relaxed);
        // Bump the generation so an in-flight background bake is superseded.
        fitBakeGen_.fetch_add(1, std::memory_order_release);
    }

    bool LoopMachine::snapshotFitSource(juce::AudioBuffer<float>& dst) const
    {
        const int srcLen = fitSrcLenMirror_.load(std::memory_order_acquire);
        if (srcLen <= 0) return false;
        if (fitScope_ == FitScope::SingleSub)
        {
            // The FIT verb stretches its own untruncated source copy (fitSrcBuf_),
            // not target_ (that holds the loaded source truncated to the window).
            const int nch = fitSrcBuf_.getNumChannels();
            const int copyLen = std::min(srcLen, fitSrcBuf_.getNumSamples());
            if (nch <= 0 || copyLen <= 0) return false;
            dst.setSize(nch, srcLen, false, false, true);
            dst.clear();
            for (int ch = 0; ch < nch; ++ch)
                dst.copyFrom(ch, 0, fitSrcBuf_, ch, 0, copyLen);
            return true;
        }
        // WholeDeck: the recorded PCM behind the mirror is stable while the fit
        // streams (the take closed, and the streaming read touches fitScratch_, not
        // target_). Snapshot the whole recorded deck width.
        if (target_ == nullptr) return false;
        const int nch = target_->getNumChannels();
        const int copyLen = std::min(srcLen, target_->getNumSamples());
        if (nch <= 0 || copyLen <= 0) return false;
        dst.setSize(nch, srcLen, false, false, true);
        dst.clear();
        for (int ch = 0; ch < nch; ++ch)
            dst.copyFrom(ch, 0, *target_, ch, 0, copyLen);
        return true;
    }

    bool LoopMachine::adoptBakedFit(const juce::AudioBuffer<float>& baked, int targetLen,
                                    std::uint32_t forGeneration)
    {
        // Drop a stale/superseded bake: a new take, Clear, or a manual length edit
        // has bumped the generation (and moved fitState_ off Streaming).
        if (forGeneration != fitBakeGen_.load(std::memory_order_acquire)) return false;
        if (fitState_ != FitState::Streaming) return false;
        if (targetLen <= 0) return false;
        // Resolve the slot buffer from the pool: the FIT verb can engage a fit via
        // loadSubTrack before the machine has ever process()'d, so the `target_`
        // member may be stale/null. We run inside withQuiescedEngine, so reseating it
        // is safe.
        target_ = pool_.mutableVolatilePcm(targetSlot_);
        if (target_ == nullptr) return false;

        if (fitScope_ == FitScope::SingleSub)
        {
            // Write the stretched source into the fitted sub-track's channel-pair
            // only — the window (loopLen_) and every other sub-track are unchanged.
            const int firstCh = 2 * fitSub_;
            const int nch = target_->getNumChannels();
            if (firstCh + 1 >= nch) return false;
            const int bakedCh = std::max(1, baked.getNumChannels());
            const int copyLen = std::min({ targetLen, loopLen_, baked.getNumSamples() });
            for (int c = 0; c < 2; ++c)
            {
                target_->clear(firstCh + c, 0, loopLen_);
                target_->copyFrom(firstCh + c, 0, baked, std::min(c, bakedCh - 1), 0, copyLen);
            }
        }
        else
        {
            if (targetLen > capacity_) return false;
            // Grow the slot to the fitted window (within the pre-reserved capacity,
            // so no reallocation) and write the baked stretched PCM in across the
            // whole recorded deck width.
            const int nch = target_->getNumChannels();
            target_->setSize(nch, targetLen, false, false, true);
            const int copyCh = std::min(nch, baked.getNumChannels());
            const int copyLen = std::min(targetLen, baked.getNumSamples());
            for (int ch = 0; ch < nch; ++ch)
            {
                if (ch < copyCh) target_->copyFrom(ch, 0, baked, ch, 0, copyLen);
                else             target_->clear(ch, 0, targetLen);
            }
            loopLen_ = targetLen;
            // Refresh the loop's musical length now that it is grid-aligned.
            const double spb = transport_.samplesPerBar;
            pool_.setSourceBars(targetSlot_,
                                spb > 0.0 ? static_cast<double>(loopLen_) / spb : 0.0);
        }

        // The stream advanced playPos_ at unity over the fitted window, so the static
        // read continues in phase; rate returns to native. Retire the engine and mark
        // the loop plain static PCM.
        fitEngine_.reset();
        fitState_ = FitState::Baked;
        fitScope_ = FitScope::WholeDeck;
        fitSub_ = -1;
        fitScratchFilled_ = false;
        fitSrcBuf_.setSize(0, 0);

        loopLenMirror_.store(loopLen_, std::memory_order_release);
        fitStateMirror_.store(static_cast<int>(fitState_), std::memory_order_release);
        return true;
    }

    float LoopMachine::mixSubTracksStreamed(int outCh, int sample, double pos,
                                            double readRate, int subCount) const
    {
        if (! fitScratchFilled_) return 0.0f;
        // Same per-sub level/pan/mute/solo mix as mixSubTracks. A *streamed* sub reads
        // the engine's stretched output (fitScratch_); a non-streamed sub (SingleSub's
        // untouched sub-tracks) falls back to the static fractional read of target_.
        bool anySolo = false;
        for (int sub = 0; sub < subCount; ++sub)
            if (deck_.subTrack(sub).soloed) { anySolo = true; break; }

        const int scChans = fitScratch_.getNumChannels();
        const int tChans = target_ != nullptr ? target_->getNumChannels() : 0;
        float acc = 0.0f;
        for (int sub = 0; sub < subCount; ++sub)
        {
            const auto& st = deck_.subTrack(sub);
            if (st.muted || (anySolo && !st.soloed)) continue;
            const float panGain = (outCh == 0)
                ? (st.pan <= 0.0f ? 1.0f : 1.0f - st.pan)
                : (st.pan >= 0.0f ? 1.0f : 1.0f + st.pan);

            const bool streamedSub = (fitScope_ == FitScope::WholeDeck) || (sub == fitSub_);
            float raw = 0.0f;
            if (streamedSub)
            {
                // WholeDeck: each sub owns pair 2*sub of the engine output. SingleSub:
                // the engine produces one stereo stretch in pair 0.
                const int rdCh = (fitScope_ == FitScope::WholeDeck ? 2 * sub : 0) + outCh;
                if (rdCh < scChans) raw = fitScratch_.getSample(rdCh, sample);
            }
            else
            {
                const int rdCh = 2 * sub + outCh;
                if (rdCh < tChans) raw = loopSample(rdCh, pos, readRate);
            }
            acc += raw * st.level * panGain;
        }
        return acc;
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
            // §40.3 (ChannelPolicy.h): folding the overdub layer into the take is a
            // deck-medium-WIDE operation — every armed sub-track's channel-pair, not
            // just pair 0. Unarmed pairs of the layer are zero (nothing scattered
            // into them), so folding the full width is exact and needs no arm test.
            const int tch = std::min(target_->getNumChannels(),
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
        if (syncMode_ >= kSyncGrid)  // Sync — grid-locked length (varispeed)
            return syncedLengthSamples();
        // S7: Free Len no longer varispeeds. It rounds its length up to the grid and
        // fills the window pitch-preserved (streamed then baked, engageFreeLenFit), so
        // its playback rate is native (1.0) — the fit is length, not speed. Free is
        // native too.
        return 0.0;
    }

    void LoopMachine::scaleLoop(float g)
    {
        if (target_ == nullptr || loopLen_ <= 0) return;
        // Deck-medium-wide (§40.7): decay every recorded channel, so a four-track
        // loop fades all its sub-tracks — not just pair 0.
        const int tch = target_->getNumChannels();
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
        cancelFit();          // S7: a new take supersedes any active/pending fit

        // §40.13: the take's first sample is at the current transport position —
        // unless a retro double-tap backfills, in which case the take began
        // `retroBackfill_` samples earlier. recStartPos_ is the absolute position a
        // retro close measures the loop length against.
        recStartPos_ = toSnapshot(transport_).positionSamples
                       - static_cast<double>(retroBackfill_);

        // C6: freeze the pre-roll — the input immediately before this take's first
        // sample. It is what must precede loop[0] when the loop wraps, and at close
        // it is spliced into the loop's end.
        for (int ch = 0; ch < preSnap_.getNumChannels(); ++ch)
            for (int i = 0; i < preLen_; ++i)
                preSnap_.setSample(ch, i, preRing_.getSample(ch, (preWrite_ + i) % preLen_));
        preSnapped_ = true;

        // §40.13 retro record-start: prepend the pre-roll covering tap1..now into
        // the loop, so the take begins where you first pressed. Only armed sub-
        // tracks receive content (unarmed pairs stay silent); the pre-roll's LAST
        // `n` samples are tap1..now, in order. Exact — the loop records at rate 1.
        if (retroBackfill_ > 0)
        {
            const int n = std::min({ retroBackfill_, preLen_, capacity_ });
            const int tch = target_->getNumChannels();
            const int pch = preSnap_.getNumChannels();
            for (int sub = 0; sub < deck_.subTrackCount(); ++sub)
            {
                if (! deck_.subTrack(sub).armed) continue;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const int c = 2 * sub + ch;
                    if (c >= tch || c >= pch) continue;
                    for (int i = 0; i < n; ++i)
                        target_->setSample(c, i, preSnap_.getSample(c, preLen_ - n + i));
                }
            }
            recPos_ = n;
        }
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
        tap1Valid_ = false;  // §40.13: a normally-fired (non-double-tapped) edge is not retro
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
        cancelFit();  // S7: supersede any in-flight bake before the slot is emptied
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
        cancelFit();  // S7: a manual length edit overrides the grid fit
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
        cancelFit();  // S7: a manual length edit overrides the grid fit
        commitOverdubLayer();  // R4: fold B before duplicating content
        const int newLen = loopLen_ * 2;
        target_->setSize(target_->getNumChannels(), newLen, true, false, true);
        // Deck-medium-wide (§40.7): duplicate every sub-track, not just pair 0.
        const int tch = target_->getNumChannels();
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
                case Cmd::Reverse:
                case Cmd::ReplacePunch: break;  // momentary — never the discrete path
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

            // §40.13 retroactive double-tap. Snapshot pendingEdge() around the
            // command: a false->true transition is a fresh arm (tap 1) — stamp its
            // transport position. An immediate override (the double-tap "now") that
            // fires a durable edge while the stamp is live consumes it: a
            // record-start backfills from the pre-roll, a record-close trims the
            // loop to the first tap. The stamp is dropped once the arm resolves.
            const bool wasPending = deck_.pendingEdge();
            const auto edge = deck_.applyCommand(deckCmd, immediate, snapshot, haveTake);
            const bool nowPending = deck_.pendingEdge();
            if (! wasPending && nowPending)
            {
                tap1Pos_ = snapshot.positionSamples;
                tap1Valid_ = true;
            }
            if (immediate && tap1Valid_)
            {
                if (edge.startRecording)
                    retroBackfill_ = static_cast<int>(std::lround(std::clamp(
                        snapshot.positionSamples - tap1Pos_, 0.0,
                        static_cast<double>(preLen_))));
                if (edge.closeRecording)
                    retroCloseLen_ = static_cast<int>(
                        std::lround(std::max(0.0, tap1Pos_ - recStartPos_)));
            }
            applyEdge(edge);
            retroBackfill_ = 0;
            retroCloseLen_ = -1;
            if (! nowPending) tap1Valid_ = false;  // arm resolved (fired or cancelled)
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
            case Cmd::ReplacePunch:
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
            case Cmd::ReplacePunch:
                if (c.pressed) startReplacePunch();
                else stopReplacePunch();
                break;
        }
    }

    // S4: enter momentary punch-replace. Only meaningful once a loop exists; we
    // ride the ordinary overdub machinery (which snapshots for undo and drops a
    // fresh layer B) and set `replacing_` so the write erases A under the head.
    void LoopMachine::startReplacePunch()
    {
        if (loopLen_ <= 0) return;
        if (deck_.state() == State::Playing)
            applyCommand(Cmd::RecordCycle, /*immediate=*/true);  // → Overdubbing (snapshots)
        if (deck_.state() == State::Overdubbing)
            replacing_ = true;
    }

    void LoopMachine::stopReplacePunch()
    {
        if (! replacing_) return;
        replacing_ = false;
        // Fold the replacement in and return to Playing, exactly as ending a dub.
        if (deck_.state() == State::Overdubbing)
            applyCommand(Cmd::RecordCycle, /*immediate=*/true);
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

    bool LoopMachine::loadSubTrack(int sub, const juce::AudioBuffer<float>& src,
                                   int srcLen, int slot, int srcPoolIndex)
    {
        if (slot < 0 || srcLen <= 0) return false;
        const int cap = pool_.volatileCapacity(slot);
        if (cap <= 0) return false;
        sub = clampSub(sub);

        juce::AudioBuffer<float>* buf = nullptr;
        int L = loopLen_;
        if (L <= 0)
        {
            // Empty loop: the window becomes the source length. Open the slot at the
            // full deck width so every sub-track's pair exists (others stay silent).
            L = std::min(srcLen, cap);
            if (L <= 0) return false;
            buf = pool_.beginVolatileCapture(slot, L, 2 * kMaxInputSubTracks);
            loopLen_ = L;
        }
        else
        {
            // Existing window: write into the pair without disturbing the others.
            buf = pool_.mutableVolatilePcm(slot);
        }
        if (buf == nullptr) return false;
        targetSlot_ = slot;

        const int firstCh = 2 * sub;
        if (firstCh + 1 >= buf->getNumChannels()) return false;
        const int copyN = std::min(srcLen, L);
        const int srcChans = std::max(1, engineChannels(src.getNumChannels()));
        for (int c = 0; c < 2; ++c)
        {
            buf->clear(firstCh + c, 0, L);                  // replace the sub-track
            const int useCh = std::min(c, srcChans - 1);    // a mono source fills both
            buf->copyFrom(firstCh + c, 0, src, useCh, 0, copyN);
        }

        // The deck now has at least sub+1 sub-tracks; adopt the take.
        if (deck_.subTrackCount() < sub + 1) deck_.setSubTrackCount(sub + 1);
        subSrcPool_[static_cast<std::size_t>(sub)] = srcPoolIndex;
        pool_.setVolatileOrigin(slot, SampleOrigin::Loop);
        deck_.setState(State::Playing);
        playPos_ = 0.0;
        lastPos_ = 0.0;
        stateMirror_.store(static_cast<int>(deck_.state()), std::memory_order_release);
        loopLenMirror_.store(loopLen_, std::memory_order_release);  // chrome/tests + FIT
        return true;
    }

    void LoopMachine::closeRecording()
    {
        // §40.13: a retro double-tap close lands the loop length at the FIRST tap
        // (tap1Pos_ - recStartPos_, precomputed into retroCloseLen_), discarding
        // the overshoot between the two taps; playback then wraps phase-continuously
        // at the first tap. Never longer than what was actually captured. A normal
        // close uses the full recorded length.
        loopLen_ = (retroCloseLen_ > 0) ? std::clamp(retroCloseLen_, 0, recPos_)
                                        : std::max(0, recPos_);
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
            // S7: a FreeLen take fits its length to the launch-quant grid,
            // pitch-preserved — stream it live now, bake it static in the background.
            engageFreeLenFit();
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
        const int chans = engineChannels(buffer.getNumChannels());

        const int targetSlot = (params.size() > kSlotTargetBuffer)
            ? static_cast<int>(std::lround(params[kSlotTargetBuffer])) : 0;
        targetSlot_ = pool_.nthVolatileIndex(targetSlot);
        target_ = pool_.mutableVolatilePcm(targetSlot_);
        capacity_ = pool_.volatileCapacity(targetSlot_);

        // The deck's sub-track count (§40.3). Default 1 = a single stereo sub-track,
        // byte-identical to today. dc::Deck clamps to [1, 4]; a change mid-take does
        // not resize the buffer already recording (the width was fixed at capture).
        const int subCount = (params.size() > kSlotSubTrackCount)
            ? static_cast<int>(std::lround(params[kSlotSubTrackCount])) : 1;
        deck_.setSubTrackCount(subCount);

        // Pull the per-sub-track mix into the deck's sub-track table (§40.3), so
        // playback summing reads one model. Defaults (level 1 / pan 0 / mute 0)
        // make a single sub-track transparent.
        for (int sub = 0; sub < kMaxInputSubTracks; ++sub)
        {
            const int base = kSlotSubMixBase + sub * kSubMixFields;
            auto& st = deck_.subTrack(sub);
            st.level = (params.size() > static_cast<std::size_t>(base))
                ? std::clamp(params[static_cast<std::size_t>(base)], 0.0f, 1.0f) : 1.0f;
            st.pan = (params.size() > static_cast<std::size_t>(base + 1))
                ? std::clamp(params[static_cast<std::size_t>(base + 1)], -1.0f, 1.0f) : 0.0f;
            st.muted = (params.size() > static_cast<std::size_t>(base + 2))
                && params[static_cast<std::size_t>(base + 2)] >= 0.5f;
            st.soloed = (params.size() > static_cast<std::size_t>(base + 3))
                && params[static_cast<std::size_t>(base + 3)] >= 0.5f;
        }
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

        // S7: while the FreeLen fit streams, pull this block of the stretched loop
        // from the realtime engine into fitScratch_ ONCE, up front (the loop below
        // then reads it per-sample). The engine loops the recorded source internally
        // (setLoop), so successive blocks are a seamless stretched loop. Filled here,
        // at block start, so a mid-block record-close that engages streaming leaves
        // fitScratch_ unfilled for the rest of that block (a few silent loop samples
        // at the very close — the next block streams cleanly).
        const bool streaming = (fitState_ == FitState::Streaming);
        fitScratchFilled_ = false;
        if (streaming && fitTargetLen_ > 0)
        {
            fitScratch_.clear();
            fitEngine_.process(fitScratch_, 0, numSamples);
            fitScratchFilled_ = true;
        }

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
                        if (recPos_ < capacity_)
                        {
                            // S4: ARM gates every write. Sub 0 is default-armed, so a
                            // single-sub-track loop is byte-identical; a disarmed sub
                            // leaves its channel-pair silent (the capture is cleared).
                            if (tch && deck_.subTrack(0).armed)
                                target_->setSample(ch, recPos_, in);  // sub 0 → pair 0
                            // §40.3: each extra sub-track records its own input into
                            // its channel-pair. ch (0/1) selects the pair's L/R, so
                            // sub k's L → target channel 2k, its R → 2k+1.
                            for (int sub = 1; sub < subCount; ++sub)
                            {
                                if (! deck_.subTrack(sub).armed) continue;
                                const int tgtCh = 2 * sub + ch;
                                const auto& si = subInput_[static_cast<std::size_t>(sub)];
                                if (tgtCh < target_->getNumChannels() && ch < si.getNumChannels())
                                    target_->setSample(tgtCh, recPos_, si.getSample(ch, i));
                            }
                        }
                        break;
                    case State::Playing:
                        // S7: while the fit streams, the loop output is the engine's
                        // stretched block, not a fractional read of the source.
                        if (tch && streaming && transportGates)
                            loopOut = mixSubTracksStreamed(ch, i, pos, effRate_, subCount);
                        else if (tch && loopLen_ > 0 && transportGates)
                            loopOut = mixSubTracks(ch, pos, effRate_, subCount);
                        break;
                    case State::Overdubbing:
                        // S7: streaming takes precedence — read the fitted stream and
                        // skip the overdub scatter (an overdub during the brief
                        // streaming bridge is deferred; the bake lands within a few
                        // blocks and normal overdub resumes on the static PCM).
                        if (tch && streaming && transportGates)
                        {
                            loopOut = mixSubTracksStreamed(ch, i, pos, effRate_, subCount);
                        }
                        else if (tch && loopLen_ > 0 && transportGates)
                        {
                            // R4 + §40.3: monitor the committed loop A (all sub-tracks
                            // mixed) plus the in-progress overdub layer B on each ARMED
                            // sub-track, and scatter each armed sub's input into its own
                            // channel-pair of B with a bandlimited (add-only) fractional
                            // write. Sub 0's input is the primary (inScratch_); sub k>0
                            // taps subInput_[k]. Unarmed subs receive nothing. A's
                            // decay/feedback and the fold happen once per iteration at
                            // the wrap (below), decoupled from this write.
                            loopOut = mixSubTracks(ch, pos, effRate_, subCount);
                            const int layerChans = overdubLayer_.getNumChannels();
                            for (int sub = 0; sub < subCount; ++sub)
                            {
                                if (! deck_.subTrack(sub).armed) continue;
                                const int lch = 2 * sub + ch;
                                if (lch >= layerChans) continue;
                                const float sin = (sub == 0) ? in
                                    : (ch < subInput_[static_cast<std::size_t>(sub)].getNumChannels()
                                           ? subInput_[static_cast<std::size_t>(sub)].getSample(ch, i)
                                           : 0.0f);
                                loopOut += readLayer(overdubLayer_, lch, pos, effRate_);
                                sharedLoopResampler().scatterAddCircular(
                                    overdubLayer_.getWritePointer(lch), loopLen_, pos,
                                    std::abs(effRate_), sin);
                                // S4 punch-replace: erase committed A under the head
                                // so the fold nets a replacement, not an add. Exact at
                                // rate 1 (the scatter deposits at the integer
                                // position); |rate| != 1 under-covers the scatter
                                // footprint and leaves faint bleed — varispeed replace
                                // is deferred (documented, acceptable v1).
                                if (replacing_ && lch < target_->getNumChannels())
                                {
                                    int idx = static_cast<int>(std::floor(pos)) % loopLen_;
                                    if (idx < 0) idx += loopLen_;
                                    target_->setSample(lch, idx, 0.0f);
                                }
                            }
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
            if (loopLen_ > 0 && !brNow && !tapeNow && !streaming && transportGates
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
                     && (loopLen_ > 0 || streaming) && transportGates)
            {
                if (streaming)
                {
                    // S7: the engine emits the stretched loop sequentially; playPos_
                    // just tracks the fitted-window phase for chrome (the mini-seq
                    // playhead), advancing at unity over the fitted length.
                    effRate_ = 1.0;
                    playPos_ += 1.0;
                    if (fitTargetLen_ > 0 && playPos_ >= static_cast<double>(fitTargetLen_))
                        playPos_ -= static_cast<double>(fitTargetLen_);
                }
                else if (brNow)
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
        // S7: while streaming, the loop's musical length is the fitted window, not the
        // shorter recorded source — chrome/tests see the length the ear hears. Baked
        // sets loopLen_ = fitTargetLen_, so the two agree from then on.
        const int reportedLen = streaming && fitTargetLen_ > 0 ? fitTargetLen_ : loopLen_;
        loopLenMirror_.store(reportedLen, std::memory_order_release);  // S4 chrome/tests
        fitStateMirror_.store(static_cast<int>(fitState_), std::memory_order_release);
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
                             && reportedLen > 0;
        phaseMirror_.store(playing
                               ? static_cast<float>(playPos_ / static_cast<double>(reportedLen))
                               : -1.0f,
                           std::memory_order_release);
        pendingMirror_.store(deck_.pendingEdge(),
                             std::memory_order_release);
    }
}
