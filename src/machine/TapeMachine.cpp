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
                s.maxValue = static_cast<float>(dc::kMaxSubTracks);  // 1..4 (§40.3)
                s.defaultValue = 1.0f;   // a single stereo sub-track — today's tape
                s.isStepped = true;
                return s;
            default:
                return {};
        }
    }

    void TapeMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setMediumSeconds(mediumSeconds_);
        reset();
    }

    void TapeMachine::reset()
    {
        deck_.setState(dc::DeckState::Playing);  // a tape is always playing its position
        medium_.resetAllUsed();
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
        const int recorded = recordedSamples();
        juce::AudioBuffer<float> snapshot;
        if (recorded > 0)
        {
            snapshot.setSize(2, recorded, false, false, false);
            copyReelTo(snapshot, recorded);     // depth-transparent read of the old reel
        }

        depthI16_ = i16;
        allocateReel();                         // fresh stores at the new depth (used → 0)

        if (recorded > 0)
        {
            medium_.ensureCommitted(0, recorded);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < recorded; ++i)
                    medium_.write(0, ch, i, snapshot.getSample(ch, i));  // depth-transparent
        }
    }

    void TapeMachine::allocateReel()
    {
        const int cap = std::max(1, static_cast<int>(mediumSeconds_ * sampleRate_));
        reelCap_ = cap;
        // §40.3 deck width: kNumPlanes = kMaxSubTracks stereo sub-tracks, allocated
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
        cfg.numSubTracks = dc::kMaxSubTracks;  // §40.3: bound at full deck width
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
                    for (int p = undoLo_; p <= undoHi_; ++p)
                        for (int ch = 0; ch < 2; ++ch)
                            medium_.write(0, ch, p,
                                          undoStore_[static_cast<std::size_t>(ch)]
                                              .get(static_cast<std::size_t>(p)));
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
        // Stored on the deck so subTrackCount()/IMultiInput report it; the record
        // and mix paths still touch only sub 0 until 6b/6c bring the higher subs in.
        const int subCount = (params.size() > kSlotSubTrackCount)
            ? std::clamp(static_cast<int>(std::lround(params[kSlotSubTrackCount])), 1, dc::kMaxSubTracks)
            : 1;
        deck_.setSubTrackCount(subCount);

        const bool recording = deck_.state() == dc::DeckState::Recording;

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
                rh.readFrame(medium_, 0, sframe, outChans);
                for (int ch = 0; ch < outChans; ++ch) buffer.setSample(ch, i, sframe[ch]);
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
                    for (int c = 0; c < outChans; ++c)
                        undoStore_[static_cast<std::size_t>(c)]
                            .set(static_cast<std::size_t>(ip), medium_.read(0, c, p));
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
                if (recording)
                {
                    saveOriginals(pos, pos);
                    medium_.ensureCommitted(0, static_cast<int>(pos) + 1);
                    for (int ch = 0; ch < outChans; ++ch)
                    {
                        const float in = buffer.getSample(ch, i);
                        medium_.write(0, ch, pos, in);      // replace what was there
                        buffer.setSample(ch, i, in);        // monitor what we lay down
                    }
                }
                else
                {
                    for (int ch = 0; ch < outChans; ++ch)
                    {
                        float out = medium_.read(0, ch, pos);
                        if (monMode == 1) out += buffer.getSample(ch, i);  // live-thru
                        buffer.setSample(ch, i, out);
                    }
                }
            }
            return;
        }

        // Varispeed path (r != 1, §40.10 head law): bandlimited fractional read and
        // erase-ahead + |rate|-scaled scatter write. Heads reseed from the musical
        // position each block, so they never drift from chase-lock.
        float frame[2] = { 0.0f, 0.0f };  // NOLINT(*-avoid-c-arrays) — head float* API
        if (recording)
        {
            dc::WriteHead wh;
            wh.setRate(r);
            wh.setPosition(posStart);
            dc::EraseHead eh;                       // replace = erase + write
            eh.setErasure(1.0f);
            eh.setRate(r);
            eh.setPosition(dc::EraseHead::leadFor(wh, dc::EraseHead::kMinGap));
            // A generous, bounded window over the kernel + erase-lead extent: any
            // sample the erase/write can touch is saved before either runs. Extra
            // saved originals just restore to themselves (harmless).
            const auto reach = static_cast<std::int64_t>(
                dc::Resampler::kHalf + dc::EraseHead::kMinGap + std::ceil(std::abs(r)) + 2.0);
            for (int i = 0; i < numSamples; ++i)
            {
                for (int ch = 0; ch < outChans; ++ch) frame[ch] = buffer.getSample(ch, i);
                const auto base = static_cast<std::int64_t>(std::floor(wh.position()));
                saveOriginals(base - reach, base + reach);
                eh.sweep(medium_, 0);               // clear tape ahead of the deposit
                wh.writeFrame(medium_, 0, frame, outChans);
                wh.step(medium_);
                for (int ch = 0; ch < outChans; ++ch) buffer.setSample(ch, i, frame[ch]);
            }
        }
        else
        {
            dc::ReadHead rh;
            rh.setRate(r);
            rh.setPosition(posStart);
            for (int i = 0; i < numSamples; ++i)
            {
                rh.readFrame(medium_, 0, frame, outChans);
                for (int ch = 0; ch < outChans; ++ch)
                {
                    float out = frame[ch];
                    if (monMode == 1) out += buffer.getSample(ch, i);  // live-thru
                    buffer.setSample(ch, i, out);
                }
                rh.step(medium_);
            }
        }
    }
}
