#include "TapeMachine.h"

#include "ChannelPolicy.h"

#include <algorithm>
#include <cmath>

namespace lockstep
{
    ParamSpec TapeMachine::paramSpec(int index) const
    {
        ParamSpec s;
        s.sectionIndex = kSrcSecIdx;

        // Per-sub-track mix (Stage 6c, §40.3), generated rather than switched.
        if (index >= kSlotSubMixBase && index < kNumSlots)
        {
            const int rel = index - kSlotSubMixBase;
            const int sub = rel / kSubMixFields;         // 0..3
            const int field = rel % kSubMixFields;       // 0 level,1 pan,2 mute,3 solo
            const juce::String nm{ sub + 1 };            // user-facing 1-based
            switch (field)
            {
                case 0:  // level
                    s.id = "sub" + nm + "_level";
                    s.label = "T" + nm + " Lvl";
                    s.minValue = 0.0f; s.maxValue = 1.0f; s.defaultValue = 1.0f;
                    return s;
                case 1:  // pan (balance law: 0 = both channels unity)
                    s.id = "sub" + nm + "_pan";
                    s.label = "T" + nm + " Pan";
                    s.minValue = -1.0f; s.maxValue = 1.0f; s.defaultValue = 0.0f;
                    return s;
                case 2:  // mute
                    s.id = "sub" + nm + "_mute";
                    s.label = "T" + nm + " Mute";
                    s.minValue = 0.0f; s.maxValue = 1.0f; s.defaultValue = 0.0f;
                    s.isStepped = true;
                    return s;
                default: // solo
                    s.id = "sub" + nm + "_solo";
                    s.label = "T" + nm + " Solo";
                    s.minValue = 0.0f; s.maxValue = 1.0f; s.defaultValue = 0.0f;
                    s.isStepped = true;
                    return s;
            }
        }

        // Per-sub input source for subs 1..3 (Stage 6b, §40.3). Sub 0 uses
        // kSlotInputSource (id "input_source"); these carry input_source_2/3/4.
        // Default None so an extra sub is unassigned (and, under the 6c arming law,
        // disarmed) until the user picks a source for it.
        if (index >= kSlotSubSrcBase && index < kSlotSubMixBase)
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
                s.maxValue = kInputSourceMaxValue;
                s.defaultValue = 1.0f;   // External — record live input by default
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kInputSourceLabels.data(),
                                                             kInputSourceLabels.size());
                return s;
            case kSlotMediumLength:
                s.id = "medium_length";
                s.label = "Reel";
                s.minValue = static_cast<float>(kMinMediumSeconds);
                s.maxValue = static_cast<float>(kMaxMediumSeconds);
                s.defaultValue = static_cast<float>(kDefaultMediumSeconds);
                s.unit = ParamSpec::Unit::Seconds;  // a reel is a duration in seconds
                return s;
            case kSlotMonitor:
                s.id = "tape_monitor";
                s.label = "Mon";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kMonitorLabels.size() - 1);
                s.defaultValue = 0.0f;   // Auto
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kMonitorLabels.data(),
                                                             kMonitorLabels.size());
                return s;
            case kSlotMediumDepth:
                s.id = "medium_depth";
                s.label = "Bits";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kDepthLabels.size() - 1);
                s.defaultValue = 1.0f;   // 32-bit float (clockwise end)
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kDepthLabels.data(),
                                                             kDepthLabels.size());
                return s;
            case kSlotSubTrackCount:
                s.id = "subtrack_count";
                s.label = "Tracks";
                s.minValue = 1.0f;
                s.maxValue = static_cast<float>(kMaxInputSubTracks);  // 1..4 (§40.3)
                s.defaultValue = 1.0f;   // a single stereo sub-track — today's tape
                s.isStepped = true;
                return s;
            default:
                return {};
        }
    }

    void TapeMachine::prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        // IMultiInput (Stage 6b): the extra sub-track input buffers the processor
        // fills from input_source_2/3/4. [0] is unused (sub 0 is the track buffer).
        const int maxBlock = std::max(1, maxBlockSize);
        for (auto& b : subInput_) { b.setSize(kChannelsPerSub, maxBlock, false, true, false); b.clear(); }
        // §40.13: the deck-wide retro pre-roll ring, long enough to cover a
        // double-tap gap. Zero-filled (small) so an early backfill reads silence.
        preLen_ = std::max(1, static_cast<int>(kTapePreRollSec * sampleRate_));
        preRing_.setSize(kNumPlanes, preLen_, false, true, false);
        preRing_.clear();
        preWrite_ = 0;
        setMediumSeconds(mediumSeconds_);
        reset();
    }

    void TapeMachine::reset()
    {
        deck_.setState(dc::DeckState::Playing);  // a tape is always playing its position
        medium_.resetAllUsed();
        if (preLen_ > 0) preRing_.clear();
        preWrite_ = 0;
    }

    void TapeMachine::pushPreRoll(const juce::AudioBuffer<float>& in, int sample,
                                  int subCount) noexcept
    {
        if (preLen_ <= 0) return;
        const int ringCh = preRing_.getNumChannels();
        // Sub 0 → channel-pair 0 (from the track buffer).
        for (int ch = 0; ch < std::min({ in.getNumChannels(), kChannelsPerSub, ringCh }); ++ch)
            preRing_.setSample(ch, preWrite_, in.getSample(ch, sample));
        // Subs 1..N → their channel-pairs (from subInput_), for a deck-wide backfill.
        for (int sub = 1; sub < subCount; ++sub)
        {
            const auto& si = subInput_[static_cast<std::size_t>(sub)];
            for (int ch = 0; ch < kChannelsPerSub; ++ch)
            {
                const int rc = kChannelsPerSub * sub + ch;
                if (rc < ringCh && ch < si.getNumChannels() && sample < si.getNumSamples())
                    preRing_.setSample(rc, preWrite_, si.getSample(ch, sample));
            }
        }
        if (++preWrite_ >= preLen_) preWrite_ = 0;
    }

    void TapeMachine::retroExtend(int windowSamples) noexcept
    {
        // §40.13: backfill the run-up before a punch-in. Only meaningful once the
        // punch has written at least one sample (undoLo_ >= 0). The take's start is
        // undoLo_ (the lowest reel position written this punch); prepend the ring's
        // most-recent `n` samples into [undoLo_ - n, undoLo_) on the ARMED sub-tracks
        // that the punch is writing, saving originals so Undo restores them too.
        if (deck_.state() != dc::DeckState::Recording) return;
        if (windowSamples <= 0 || undoLo_ < 0 || preLen_ <= 0) return;
        const int r1 = undoLo_;
        const int n = std::min({ windowSamples, preLen_, r1 });
        if (n <= 0) return;

        const int subCount = deck_.subTrackCount();
        const int ringCh = preRing_.getNumChannels();
        for (int sub = 0; sub < subCount; ++sub)
        {
            if (((undoArmedMask_ >> sub) & 1) == 0) continue;  // only armed subs
            medium_.ensureCommitted(sub, r1);
            for (int ch = 0; ch < kChannelsPerSub; ++ch)
            {
                const int rc = kChannelsPerSub * sub + ch;
                if (rc >= ringCh) continue;
                auto& us = undoStore_[static_cast<std::size_t>(kChannelsPerSub * sub + ch)];
                for (int k = 1; k <= n; ++k)
                {
                    const int P = r1 - k;  // reel position, descending below the punch-in
                    // Fence #8: these positions are below undoLo_ → first-touch, save.
                    us.set(static_cast<std::size_t>(P),
                           medium_.read(sub, ch, static_cast<std::int64_t>(P)));
                    // Ring: the k-th most recent sample (newest written = preWrite_-1).
                    const int ri = ((preWrite_ - k) % preLen_ + preLen_) % preLen_;
                    medium_.write(sub, ch, static_cast<std::int64_t>(P),
                                  preRing_.getSample(rc, ri));
                }
            }
        }
        undoLo_ = std::max(0, r1 - n);
        haveUndo_ = true;
    }

    void TapeMachine::setMediumSeconds(double seconds)
    {
        mediumSeconds_ = std::clamp(seconds, kMinMediumSeconds, kMaxMediumSeconds);
        allocateReel();
    }

    void TapeMachine::setMediumDepth(int depth)
    {
        const bool i16 = depth != 0;
        if (i16 == depthI16_) return;   // no change → keep the take

        // Stage 1b: changing depth CONVERTS the reel in place — it no longer wipes.
        // Snapshot the recorded content, reallocate at the new depth, write it back
        // (32f→16i quantizes once; 16i→32f is lossless). Calibration is a musical-
        // addressing property, independent of stock, so it survives the swap.
        // Stage 6c: the convert spans every sub-track — one Bits/depth covers the
        // whole deck, so a multi-sub tape keeps all its sub-tracks across the flip.
        std::array<int, kMaxInputSubTracks> subUsed{};
        std::array<juce::AudioBuffer<float>, kMaxInputSubTracks> snapshot;
        for (int sub = 0; sub < kMaxInputSubTracks; ++sub)
        {
            const int u = medium_.used(sub);
            subUsed[static_cast<std::size_t>(sub)] = u;
            if (u <= 0) continue;
            snapshot[static_cast<std::size_t>(sub)].setSize(kChannelsPerSub, u, false, false, false);
            for (int ch = 0; ch < kChannelsPerSub; ++ch)
                for (int i = 0; i < u; ++i)
                    snapshot[static_cast<std::size_t>(sub)].setSample(ch, i, medium_.read(sub, ch, i));
        }

        depthI16_ = i16;
        allocateReel();                         // fresh stores at the new depth (used → 0)

        for (int sub = 0; sub < kMaxInputSubTracks; ++sub)
        {
            const int u = subUsed[static_cast<std::size_t>(sub)];
            if (u <= 0) continue;
            medium_.ensureCommitted(sub, u);
            for (int ch = 0; ch < kChannelsPerSub; ++ch)
                for (int i = 0; i < u; ++i)
                    medium_.write(sub, ch, i,
                                  snapshot[static_cast<std::size_t>(sub)].getSample(ch, i));
        }
    }

    void TapeMachine::allocateReel()
    {
        const int cap = std::max(1, static_cast<int>(mediumSeconds_ * sampleRate_));
        reelCap_ = cap;
        // §40.3 deck width: kNumPlanes = kMaxInputSubTracks stereo sub-tracks, allocated
        // at full width and lazily committed (a 1-sub tape never touches subs 1..3).
        const auto n = static_cast<std::size_t>(cap) * kNumPlanes;

        // Exactly one backing is live; release the other. Both allocate WITHOUT
        // zero-fill (§40.3 lazy commit): `new T[]` default-inits trivial types, so
        // pages stay unmapped until written, and AudioBuffer::setSize(...,false)
        // skips the memset. ensureCommitted zeroes the span it hands to the head,
        // and out-of-range reads return 0, so the initial garbage is never read.
        if (depthI16_)
        {
            reel_.setSize(0, 0);
            undoReel_.setSize(0, 0);
            reelI16_.reset(new std::int16_t[n]);           // NOLINT(*-avoid-c-arrays)
            undoI16_.reset(new std::int16_t[n]);           // NOLINT(*-avoid-c-arrays)
            for (int p = 0; p < kNumPlanes; ++p)
            {
                const auto off = static_cast<std::size_t>(p) * static_cast<std::size_t>(cap);
                planes_[static_cast<std::size_t>(p)] =
                    dc::Store{ reelI16_.get() + off, static_cast<std::size_t>(cap) };
                undoStore_[static_cast<std::size_t>(p)] =
                    dc::Store{ undoI16_.get() + off, static_cast<std::size_t>(cap) };
            }
        }
        else
        {
            reelI16_.reset();
            undoI16_.reset();
            reel_.setSize(kNumPlanes, cap, false, false, false);
            undoReel_.setSize(kNumPlanes, cap, false, false, false);  // fence #8 punch undo (lazy)
            for (int p = 0; p < kNumPlanes; ++p)
            {
                planes_[static_cast<std::size_t>(p)] =
                    dc::Store{ reel_.getWritePointer(p), static_cast<std::size_t>(cap) };
                undoStore_[static_cast<std::size_t>(p)] =
                    dc::Store{ undoReel_.getWritePointer(p), static_cast<std::size_t>(cap) };
            }
        }
        haveUndo_ = false;
        undoLo_ = undoHi_ = -1;
        bindReel();
    }

    void TapeMachine::bindReel() noexcept
    {
        const int cap = reelCap_;
        if (cap <= 0) { medium_.unbind(); return; }

        dc::Medium::Config cfg;
        cfg.topology = dc::Topology::Linear;   // a reel, not a loop
        cfg.mediumRate = sampleRate_;          // 1× medium (§40.10)
        cfg.numSubTracks = kMaxInputSubTracks;  // §40.3: bound at full deck width
        cfg.channelsPerSubTrack = kChannelsPerSub;
        cfg.capacitySamples = cap;
        medium_.bindPlanes(cfg, planes_.data(), kNumPlanes);
    }

    void TapeMachine::copyReelTo(juce::AudioBuffer<float>& dst, int numFrames) const noexcept
    {
        const int chans = std::min(dst.getNumChannels(), 2);
        const int len = std::min(numFrames, dst.getNumSamples());
        for (int ch = 0; ch < chans; ++ch)
            for (int i = 0; i < len; ++i)
                dst.setSample(ch, i, medium_.read(0, ch, i));  // depth-transparent
    }

    void TapeMachine::copyDeckTo(juce::AudioBuffer<float>& dst, int numFrames) const noexcept
    {
        const int subs = std::min(deck_.subTrackCount(), dst.getNumChannels() / kChannelsPerSub);
        const int len = std::min(numFrames, dst.getNumSamples());
        for (int sub = 0; sub < subs; ++sub)
            for (int ch = 0; ch < kChannelsPerSub; ++ch)
                for (int i = 0; i < len; ++i)
                    dst.setSample(sub * kChannelsPerSub + ch, i, medium_.read(sub, ch, i));
    }

    void TapeMachine::applyVerb(int verb)
    {
        switch (verb)
        {
            case 1:  // RecordCycle — punch in / punch out
                if (deck_.state() == dc::DeckState::Recording)
                {
                    // Punch out: the take is committed, the punched span is undoable.
                    deck_.setState(dc::DeckState::Playing);
                    haveUndo_ = (undoLo_ >= 0 && undoHi_ >= undoLo_);
                }
                else
                {
                    // Punch in: begin a fresh undo span (save-before-write fills it).
                    deck_.setState(dc::DeckState::Recording);
                    undoLo_ = undoHi_ = -1;
                    haveUndo_ = false;
                }
                break;
            // Stage 4: verb 2 (PlayStop) retired — the Tape follows the main
            // transport, so there is no separate tape Play/Stop. Winding is available
            // whenever the song is parked (handled in process()).
            case 3:  // Clear — wipe the reel
                medium_.resetAllUsed();
                haveUndo_ = false;
                undoLo_ = undoHi_ = -1;
                calSamplesPerPpq_ = 0.0;   // §40.2: a blank reel is uncalibrated again
                deck_.setState(dc::DeckState::Playing);
                break;
            case 4:  // Undo — restore the last punch's original content
                if (haveUndo_)
                {
                    // Stage 6c: restore exactly the sub-tracks the punch wrote (armed
                    // subs). An unarmed sub was never touched, so it is left alone.
                    for (int sub = 0; sub < kMaxInputSubTracks; ++sub)
                    {
                        if (((undoArmedMask_ >> sub) & 1) == 0) continue;
                        for (int p = undoLo_; p <= undoHi_; ++p)
                            for (int ch = 0; ch < kChannelsPerSub; ++ch)
                                medium_.write(sub, ch, p,
                                              undoStore_[static_cast<std::size_t>(sub * kChannelsPerSub + ch)]
                                                  .get(static_cast<std::size_t>(p)));
                    }
                    haveUndo_ = false;
                }
                break;
            default:
                break;
        }
    }

    int TapeMachine::dropMarkerHere(int labelId)
    {
        // Reel domain (§40.2): a marker points at a place on the reel, so it lives
        // where the head is — ppq × K on a calibrated reel, transportPhaseSamples
        // otherwise. Cue/position/promote all read the same domain.
        return markers_.drop(reelPosAtBlockStart(), labelId);
    }

    int TapeMachine::dropMarkerAt(double posSamples, int labelId)
    {
        return markers_.drop(posSamples, labelId);
    }

    int TapeMachine::dropMarkerAtPpq(double ppq, int labelId)
    {
        const double k = calSamplesPerPpq_ > 0.0 ? calSamplesPerPpq_ : currentSamplesPerPpq();
        return markers_.drop(ppq * (k > 0.0 ? k : 1.0), labelId);
    }

    double TapeMachine::cueNearest() const noexcept
    {
        const int i = markers_.nearest(reelPosAtBlockStart());
        return i < 0 ? -1.0 : markers_.at(i).positionSamples;
    }

    double TapeMachine::cueNext() const noexcept
    {
        const int i = markers_.next(reelPosAtBlockStart());
        return i < 0 ? -1.0 : markers_.at(i).positionSamples;
    }

    double TapeMachine::cuePrev() const noexcept
    {
        const int i = markers_.prev(reelPosAtBlockStart());
        return i < 0 ? -1.0 : markers_.at(i).positionSamples;
    }

    void TapeMachine::process(const juce::MidiBuffer& /*events*/,
                              const ParamFrame& params,
                              juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int outChans = engineChannels(buffer.getNumChannels());
        if (! medium_.bound() || outChans <= 0) { buffer.clear(); return; }

        const int monMode = (params.size() > kSlotMonitor)
            ? static_cast<int>(std::lround(params[kSlotMonitor])) : 0;

        // §40.3 deck width (Stage 6a): the live sub-track count follows the param.
        const int subCount = (params.size() > kSlotSubTrackCount)
            ? std::clamp(static_cast<int>(std::lround(params[kSlotSubTrackCount])), 1, kMaxInputSubTracks)
            : 1;
        deck_.setSubTrackCount(subCount);

        const bool recording = deck_.state() == dc::DeckState::Recording;

        // §40.3 mix + arming (Stage 6c). Pull each sub's level/pan/mute/solo into
        // the deck table, and resolve the arming law: a sub is ARMED when its input
        // source is not None (auto-arm on SRC). Only armed subs WRITE during a punch;
        // unarmed subs play their content back. Defaults (source External on sub 0,
        // None on 1..3; level 1 / pan 0 / unmuted) keep a single-sub tape identical.
        bool anySolo = false;
        int armedMask = 0;
        for (int sub = 0; sub < subCount; ++sub)
        {
            const auto b = static_cast<std::size_t>(kSlotSubMixBase + sub * kSubMixFields);
            auto& st = deck_.subTrack(sub);
            st.level  = (params.size() > b)     ? std::clamp(params[b], 0.0f, 1.0f)      : 1.0f;
            st.pan    = (params.size() > b + 1) ? std::clamp(params[b + 1], -1.0f, 1.0f) : 0.0f;
            st.muted  = (params.size() > b + 2) && params[b + 2] >= 0.5f;
            st.soloed = (params.size() > b + 3) && params[b + 3] >= 0.5f;
            if (st.soloed) anySolo = true;

            const auto srcSlot = static_cast<std::size_t>(
                (sub == 0) ? kSlotInputSource : (kSlotSubSrcBase + sub - 1));
            const bool hasSrc = (params.size() > srcSlot)
                && decodeInputSource(params[srcSlot]).kind != InputSourceKind::None;
            st.armed = hasSrc;
            if (hasSrc) armedMask |= (1 << sub);
        }
        if (recording) undoArmedMask_ = armedMask;

        // Playback predicate + mix gains (shared by the scrub, unity, and varispeed
        // paths). A sub plays when it is enabled by the mute/solo law; solo is
        // subtractive (any solo → only soloed subs). Center-unity balance pan:
        // pan 0 leaves both channels at level, so a lone centered sub reads exactly
        // as a raw medium read (single-sub byte-identity).
        auto subPlays = [&](int sub) noexcept
        {
            const auto& st = deck_.subTrack(sub);
            if (st.muted) return false;
            if (anySolo && ! st.soloed) return false;
            return true;
        };
        auto panGain = [&](int sub, int ch) noexcept
        {
            const float p = deck_.subTrack(sub).pan;
            return (ch == 0) ? (p <= 0.0f ? 1.0f : 1.0f - p)
                             : (p >= 0.0f ? 1.0f : 1.0f + p);
        };
        auto inputForSub = [&](int sub, int ch, int i) noexcept -> float
        {
            if (sub == 0) return buffer.getSample(ch, i);
            const auto& si = subInput_[static_cast<std::size_t>(sub)];
            const int c = std::min(ch, si.getNumChannels() - 1);
            return (c >= 0 && i < si.getNumSamples()) ? si.getSample(c, i) : 0.0f;
        };

        // Stage 4: the Tape follows the MAIN transport — there is no separate tape
        // Play/Stop. When the transport is parked (not running) and we are not
        // recording, the head detaches (§40.2): it scrubs if there is scrub/jog
        // input, else idles (Mon live-thru passes; no reel read = no frozen buzz).
        // Winding is therefore available whenever the song is stopped, not behind a
        // button. A running transport chases; the reel is truth on release.
        const bool parked = ! recording && ! transport_.running;
        if (parked)
        {
            const double target = scrubTarget_.load(std::memory_order_relaxed);
            jogVel_ += jogPending_.exchange(0.0, std::memory_order_relaxed);  // drain jog
            const bool wasScrub = scrubbing_;
            scrubbing_ = std::abs(target) > 1e-6 || std::abs(scrubSmoothed_) > 1e-4
                      || std::abs(jogVel_) > 1e-4;

            if (! scrubbing_)
            {
                if (monMode != 1) buffer.clear();  // Mon On passes live-thru; else silence
                scrubHeadReel_.store(reelPosAtBlockStart(), std::memory_order_relaxed);
                return;
            }

            if (! wasScrub)  // scrub just began → seed the head from the transport
                scrubHeadPos_ = reelPosAtBlockStart();

            const double cap = static_cast<double>(reelCap_);
            dc::ReadHead rh;
            float sframe[2] = { 0.0f, 0.0f };  // NOLINT(*-avoid-c-arrays)
            for (int i = 0; i < numSamples; ++i)
            {
                scrubSmoothed_ += (target - scrubSmoothed_) * kScrubEase;  // slewed wind
                const double rate = scrubSmoothed_ + jogVel_;             // + jog rock
                rh.setRate(rate);
                rh.setPosition(scrubHeadPos_);
                // Audition the mix while winding (Stage 6c): sum every playing sub.
                float acc[2] = { 0.0f, 0.0f };  // NOLINT(*-avoid-c-arrays)
                for (int sub = 0; sub < subCount; ++sub)
                {
                    if (! subPlays(sub)) continue;
                    rh.readFrame(medium_, sub, sframe, outChans);
                    for (int ch = 0; ch < outChans; ++ch)
                        acc[ch] += sframe[ch] * deck_.subTrack(sub).level * panGain(sub, ch);
                }
                for (int ch = 0; ch < outChans; ++ch) buffer.setSample(ch, i, acc[ch]);
                scrubHeadPos_ += rate;
                jogVel_ *= kJogDecay;                                     // jog coasts to rest
                if (scrubHeadPos_ <= 0.0) { scrubHeadPos_ = 0.0; scrubSmoothed_ = jogVel_ = 0.0; }
                if (scrubHeadPos_ >= cap) { scrubHeadPos_ = cap; scrubSmoothed_ = jogVel_ = 0.0; }
            }
            scrubHeadReel_.store(scrubHeadPos_, std::memory_order_relaxed);
            return;
        }

        // (The parked branch above owns the stopped-transport case — scrub or idle —
        // so reaching here means the transport is running or we are recording.)

        // §40.2 chase-lock. Calibration latches from the current tempo at the FIRST
        // record onto a still-uncalibrated reel; from then on the reel is addressed
        // by musical time (ppq × K) and any tempo deviation is varispeed. An
        // uncalibrated reel (fresh, or with no tempo context) chases at unity and
        // behaves exactly as the integer timeline did before.
        if (recording && calSamplesPerPpq_ <= 0.0)
        {
            const double spp = currentSamplesPerPpq();
            if (spp > 0.0) calSamplesPerPpq_ = spp;
        }
        const double r = chaseRatioNow();
        const double posStart = reelPosAtBlockStart();

        // §40.13: keep the pre-roll ring current while PLAYING (running, not
        // recording) — its newest sample then sits at the punch-in reel position, so
        // a retro punch-in double-tap backfills the run-up. Pushed BEFORE the play
        // path overwrites `buffer` with reel output, so it captures the live input.
        if (! recording && transport_.running)
            for (int i = 0; i < numSamples; ++i)
                pushPreRoll(buffer, i, subCount);

        // Fence #8 undo: save the ORIGINAL of every integer reel sample the write
        // is about to touch, once per punch (span-scoped; a re-touch keeps the
        // pre-punch value). Reel-domain, so the span logic is rate-independent.
        auto saveOriginals = [&](std::int64_t a, std::int64_t b) noexcept
        {
            const auto cap = static_cast<std::int64_t>(reelCap_);
            a = std::max<std::int64_t>(a, 0);
            b = std::min<std::int64_t>(b, cap - 1);
            for (std::int64_t p = a; p <= b; ++p)
            {
                const int ip = static_cast<int>(p);
                const bool firstTouch = (undoLo_ < 0) || ip < undoLo_ || ip > undoHi_;
                if (firstTouch)
                {
                    // Save every ARMED sub's channel-pair (they share the span).
                    for (int sub = 0; sub < subCount; ++sub)
                    {
                        if (((armedMask >> sub) & 1) == 0) continue;
                        for (int c = 0; c < outChans; ++c)
                            undoStore_[static_cast<std::size_t>(sub * kChannelsPerSub + c)]
                                .set(static_cast<std::size_t>(ip), medium_.read(sub, c, p));
                    }
                    undoLo_ = (undoLo_ < 0) ? ip : std::min(undoLo_, ip);
                    undoHi_ = (undoHi_ < 0) ? ip : std::max(undoHi_, ip);
                }
            }
        };

        const bool unity = std::abs(r - 1.0) < 1e-9;

        if (unity)
        {
            // Unity fast path: integer read/write, bit-exact with the pre-chase-lock
            // record. The head index equals the absolute reel position; a locate
            // jumps the transport, so the head follows for free.
            const auto posInt = static_cast<std::int64_t>(std::llround(posStart));
            for (int i = 0; i < numSamples; ++i)
            {
                const std::int64_t pos = posInt + i;
                float out[2] = { 0.0f, 0.0f };  // NOLINT(*-avoid-c-arrays)
                if (recording)
                {
                    saveOriginals(pos, pos);
                    for (int sub = 0; sub < subCount; ++sub)
                    {
                        if (((armedMask >> sub) & 1) != 0)
                        {
                            medium_.ensureCommitted(sub, static_cast<int>(pos) + 1);
                            for (int ch = 0; ch < outChans; ++ch)
                            {
                                const float in = inputForSub(sub, ch, i);
                                medium_.write(sub, ch, pos, in);  // replace what was there
                                out[ch] += in;                    // monitor what we lay down
                            }
                        }
                        else if (subPlays(sub))  // unarmed subs keep playing back
                        {
                            for (int ch = 0; ch < outChans; ++ch)
                                out[ch] += medium_.read(sub, ch, pos)
                                         * deck_.subTrack(sub).level * panGain(sub, ch);
                        }
                    }
                    for (int ch = 0; ch < outChans; ++ch) buffer.setSample(ch, i, out[ch]);
                }
                else
                {
                    for (int sub = 0; sub < subCount; ++sub)
                    {
                        if (! subPlays(sub)) continue;
                        for (int ch = 0; ch < outChans; ++ch)
                            out[ch] += medium_.read(sub, ch, pos)
                                     * deck_.subTrack(sub).level * panGain(sub, ch);
                    }
                    for (int ch = 0; ch < outChans; ++ch)
                    {
                        float o = out[ch];
                        if (monMode == 1) o += buffer.getSample(ch, i);  // live-thru
                        buffer.setSample(ch, i, o);
                    }
                }
            }
            return;
        }

        // Varispeed path (r != 1, §40.10 head law): bandlimited fractional read and
        // erase-ahead + |rate|-scaled scatter write. Heads reseed from the musical
        // position each block, so they never drift from chase-lock. Stage 6c: each
        // ARMED sub owns its own write/erase head (they share the position but must
        // advance once per engine sample, not once per sub), and one read head
        // serves every unarmed/playing sub at the shared position.
        float frame[2] = { 0.0f, 0.0f };  // NOLINT(*-avoid-c-arrays) — head float* API
        dc::ReadHead rh;
        rh.setRate(r);
        rh.setPosition(posStart);
        if (recording)
        {
            std::array<dc::WriteHead, kMaxInputSubTracks> wh{};
            std::array<dc::EraseHead, kMaxInputSubTracks> eh{};
            for (int sub = 0; sub < subCount; ++sub)
            {
                if (((armedMask >> sub) & 1) == 0) continue;
                auto& w = wh[static_cast<std::size_t>(sub)];
                auto& e = eh[static_cast<std::size_t>(sub)];
                w.setRate(r);
                w.setPosition(posStart);
                e.setErasure(1.0f);
                e.setRate(r);
                e.setPosition(dc::EraseHead::leadFor(w, dc::EraseHead::kMinGap));
            }
            // A generous, bounded window over the kernel + erase-lead extent: any
            // sample the erase/write can touch is saved before either runs. Extra
            // saved originals just restore to themselves (harmless).
            const auto reach = static_cast<std::int64_t>(
                dc::Resampler::kHalf + dc::EraseHead::kMinGap + std::ceil(std::abs(r)) + 2.0);
            double wpos = posStart;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto base = static_cast<std::int64_t>(std::floor(wpos));
                saveOriginals(base - reach, base + reach);
                float out[2] = { 0.0f, 0.0f };  // NOLINT(*-avoid-c-arrays)
                for (int sub = 0; sub < subCount; ++sub)
                {
                    if (((armedMask >> sub) & 1) != 0)
                    {
                        for (int ch = 0; ch < outChans; ++ch) frame[ch] = inputForSub(sub, ch, i);
                        eh[static_cast<std::size_t>(sub)].sweep(medium_, sub);   // clear ahead
                        wh[static_cast<std::size_t>(sub)].writeFrame(medium_, sub, frame, outChans);
                        wh[static_cast<std::size_t>(sub)].step(medium_);
                        for (int ch = 0; ch < outChans; ++ch) out[ch] += frame[ch];  // monitor
                    }
                    else if (subPlays(sub))  // unarmed subs keep playing back
                    {
                        rh.readFrame(medium_, sub, frame, outChans);
                        for (int ch = 0; ch < outChans; ++ch)
                            out[ch] += frame[ch] * deck_.subTrack(sub).level * panGain(sub, ch);
                    }
                }
                rh.step(medium_);
                for (int ch = 0; ch < outChans; ++ch) buffer.setSample(ch, i, out[ch]);
                wpos += r;
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float out[2] = { 0.0f, 0.0f };  // NOLINT(*-avoid-c-arrays)
                for (int sub = 0; sub < subCount; ++sub)
                {
                    if (! subPlays(sub)) continue;
                    rh.readFrame(medium_, sub, frame, outChans);
                    for (int ch = 0; ch < outChans; ++ch)
                        out[ch] += frame[ch] * deck_.subTrack(sub).level * panGain(sub, ch);
                }
                for (int ch = 0; ch < outChans; ++ch)
                {
                    float o = out[ch];
                    if (monMode == 1) o += buffer.getSample(ch, i);  // live-thru
                    buffer.setSample(ch, i, o);
                }
                rh.step(medium_);
            }
        }
    }
}
