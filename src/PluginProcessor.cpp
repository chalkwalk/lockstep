#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ParameterIDs.h"
#include "core/StateResolver.h"
#include "core/TrigEvaluator.h"
#include "machine/MidiDevicePresets.h"
#include "machine/DrumSynthMachine.h"
#include "machine/FMMachine.h"
#include "machine/MidiOutMachine.h"
#include "machine/SamplerMachine.h"
#include "machine/SlicerMachine.h"
#include "machine/SamplePlayingMachineBase.h"
#include "machine/VAMachine.h"
#include "machine/StubMachine.h"
#include "state/PluginState.h"
#include <algorithm>
#include <cmath>
#include <limits>

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

        // Spread-with-bias note picker. Given ascending-sorted pitches[0..N-1]
        // and target voice count K (with K <= N), picks K indices into pitches
        // that always include the endpoints (top + bottom when K >= 2), then
        // fills the interior so the chosen indices are as evenly spread as
        // possible. When K is odd the inner "centre" is taken; when K is even
        // and the natural choice falls between two interior pitches, the bias
        // resolves the tie (TopBias rounds upward, BottomBias rounds downward).
        // For K == 1 the bias picks top (TopBias) or bottom (BottomBias).
        static void pickSpreadNotes(const std::array<int, kMaxNotesPerStep>& sortedAsc,
                                     int N, int K, NoteSelection bias,
                                     std::array<int, kMaxNotesPerStep>& out,
                                     int& outCount)
        {
            outCount = 0;
            if (N <= 0 || K <= 0) return;
            if (K >= N)
            {
                for (int i = 0; i < N; ++i) out[static_cast<std::size_t>(i)] = sortedAsc[static_cast<std::size_t>(i)];
                outCount = N;
                return;
            }

            // Pick K positions in [0, N-1]; endpoints first.
            std::array<int, kMaxNotesPerStep> idx{};
            int count = 0;
            if (K == 1)
            {
                idx[0] = (bias == NoteSelection::BottomBias) ? 0 : (N - 1);
                count = 1;
            }
            else
            {
                idx[0] = 0;
                idx[1] = N - 1;
                count = 2;
                // Fill interior positions evenly spread between 0 and N-1.
                // Position formula: p_j = j*(N-1)/(K-1) for j = 1..K-2.
                // Round toward the bias when the exact position is between two indices.
                for (int j = 1; j <= K - 2; ++j)
                {
                    const int num = j * (N - 1);
                    const int den = K - 1;
                    const int floorIdx = num / den;
                    const int rem      = num - floorIdx * den;
                    int chosen = floorIdx;
                    if (rem != 0)
                    {
                        // bias the rounding: TopBias rounds up, BottomBias rounds down.
                        if (bias == NoteSelection::TopBias) chosen = floorIdx + 1;
                        else                                 chosen = floorIdx;
                    }
                    // Deduplicate against already-picked indices (rare, defensive).
                    bool dup = false;
                    for (int k = 0; k < count; ++k) if (idx[static_cast<std::size_t>(k)] == chosen) { dup = true; break; }
                    if (!dup) idx[static_cast<std::size_t>(count++)] = chosen;
                }
            }

            // Sort the chosen indices ascending and emit pitches.
            std::sort(idx.begin(), idx.begin() + count);
            for (int j = 0; j < count; ++j)
                out[static_cast<std::size_t>(j)] = sortedAsc[static_cast<std::size_t>(idx[static_cast<std::size_t>(j)])];
            outCount = count;
        }
    }

    LockstepProcessor::LockstepProcessor()
        : juce::AudioProcessor(BusesPropertiesAccessor::make()),
          apvts_(*this, nullptr, "Lockstep", createParameterLayout())
    {
        nextTriggerPpq_.fill(0.0);
        firedStepIdx_.fill(-1);
        lastRecordedStepNum_.fill(std::numeric_limits<int64_t>::min());

        for (auto& s : mzSlots_)
            s.store(-1, std::memory_order_relaxed);

        syncModeParam_    = apvts_.getRawParameterValue(ParamIDs::syncMode);
        channelModeParam_ = apvts_.getRawParameterValue(ParamIDs::channelMode);

        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const auto ti = static_cast<std::size_t>(t);
            trackLengthParams_[ti]  = apvts_.getRawParameterValue(ParamIDs::trackLength(t));
            trackDividerParams_[ti] = apvts_.getRawParameterValue(ParamIDs::trackDivider(t));
            trackMuteParams_[ti]    = apvts_.getRawParameterValue(ParamIDs::trackMute(t));
            trackSoloParams_[ti]    = apvts_.getRawParameterValue(ParamIDs::trackSolo(t));
        }

        // T0 starts as a sampler; T1–T15 are stub (empty) until materialised.
        // machines_ entries are set correctly here; Part[0] seed below confirms.
        machines_[0] = std::make_unique<SamplerMachine>(samplePool_);
        for (std::size_t t = 1; t < kNumTracks; ++t)
            machines_[t] = std::make_unique<StubMachine>("");

        // Verify the state upgrade chain every time the plugin loads in debug mode.
       #if JUCE_DEBUG
        {
            juce::UnitTestRunner runner;
            runner.setAssertOnFailure(false);
            runner.runTestsInCategory("PluginState");
        }
       #endif

        // Seed Bank[0]/Part[0] with one active sampler track; T1-T15 are empty stub
        // tracks. All other patterns and parts start uninitialised — they are
        // "empty slots" that materialise on first use via the copy/create gesture.
        {
            auto& part0 = project_.banks[0].parts[0];
            part0.initialised = true;

            // T0: sampler with default params.
            {
                part0.tracks[0].machineId = SamplerMachine::kMachineId;
                const int np = machines_[0]->numParams();
                part0.tracks[0].baseParams.assign(static_cast<std::size_t>(np), 0.0f);
                for (int s = 0; s < np; ++s)
                    part0.tracks[0].baseParams[static_cast<std::size_t>(s)] =
                        machines_[0]->paramSpec(s).defaultValue;
                part0.tracks[0].baseParams[0] = 0.0f;  // sample slot 0
            }

            // T1–T15: empty stub tracks.
            for (std::size_t t = 1; t < kNumTracks; ++t)
            {
                part0.tracks[t].machineId = StubMachine::kMachineId;
                part0.tracks[t].baseParams.clear();
            }

            auto& pat0 = project_.banks[0].patterns[0];
            pat0.initialised = true;
            // Only T0 has real baseParams; T1-T15 stay empty.
            pat0.sequence.tracks[0].baseParams = part0.tracks[0].baseParams;
        }

        // Seed the new hierarchy (Phase 7): Song[0], SongTrack[0] kit = sampler,
        // SongTrack[1..15] kit = stub; Section[0] activeMask = all true (default).
        {
            auto& p0 = arrangement_.songs[0];

            p0.tracks[0].kit.machineId  = SamplerMachine::kMachineId;
            p0.tracks[0].kit.baseParams =
                project_.banks[0].parts[0].tracks[0].baseParams;  // reuse seed above

            for (std::size_t t = 1; t < kNumTracks; ++t)
            {
                p0.tracks[t].kit.machineId = StubMachine::kMachineId;
                p0.tracks[t].kit.baseParams.clear();
            }
            // Section[0]: all tracks active, coreTime 4/4, phrase indices = 0.
            // phraseIdx and activeMask default correctly (0s and trues).
        }

        // Project the seeded Song[0] into the working buffer. Arrangement's own
        // constructor synced from an empty kit (it ran before this seed), so the
        // working buffer must be refreshed now that Song[0]'s kit is populated.
        arrangement_.syncWorkingFromActive();
    }

    LockstepProcessor::~LockstepProcessor() = default;

    void LockstepProcessor::setActivePattern(int bankIdx, int patternIdx)
    {
        if (bankIdx    < 0 || bankIdx    >= static_cast<int>(kNumBanks))        return;
        if (patternIdx < 0 || patternIdx >= static_cast<int>(kPatternsPerBank)) return;
        if (bankIdx == activeBankIdx_ && patternIdx == activePatternIdx_)        return;

        const int oldPartRef   = activePattern().partRef;
        activeBankIdx_    = bankIdx;
        activePatternIdx_ = patternIdx;
        const int newPartRef   = activePattern().partRef;

        // Always sync Track.baseParams from the active Part. This ensures that
        // newly-materialised patterns (which have empty baseParams before their first
        // sync) are immediately safe for the audio thread, and also corrects any
        // mismatch that can arise after state load for patterns that reference a
        // Part whose machineId set has changed.
        const auto& part = activePart();
        for (std::size_t t = 0; t < kNumTracks; ++t)
            sequence().tracks[t].baseParams = part.tracks[t].baseParams;

        // Reinstall machines if the Part reference changed.
        if (newPartRef != oldPartRef)
            reinstallMachinesFromActivePart();
    }

    void LockstepProcessor::setMZSlots(int slotOffset)
    {
        for (int i = 0; i < 4; ++i)
            mzSlots_[static_cast<std::size_t>(i)].store(slotOffset + i,
                                                         std::memory_order_relaxed);
    }

    void LockstepProcessor::startLearn(CCScope scope, int trackIndex,
                                        int slot, int mzPosition)
    {
        learnRequest_.scope      = scope;
        learnRequest_.trackIndex = trackIndex;
        learnRequest_.slot       = slot;
        learnRequest_.mzPosition = mzPosition;
        learnActive_.store(true, std::memory_order_release);
    }

    void LockstepProcessor::cancelLearn()
    {
        learnActive_.store(false, std::memory_order_release);
    }

    int LockstepProcessor::activePartShareCount() const
    {
        const auto& bank    = project_.banks[static_cast<std::size_t>(activeBankIdx_)];
        const int   partRef = activePattern().partRef;
        int count = 0;
        for (const auto& pat : bank.patterns)
            if (pat.partRef == partRef) ++count;
        return count;
    }

    bool LockstepProcessor::forkActivePart()
    {
        auto& bank         = project_.banks[static_cast<std::size_t>(activeBankIdx_)];
        const int partRef  = activePattern().partRef;

        // Nothing to fork if this Part is not shared.
        if (activePartShareCount() <= 1) return false;

        // Find a Part slot that is not referenced by any pattern in this bank.
        int freeSlot = -1;
        for (int p = 0; p < kPartsPerBank; ++p)
        {
            bool used = false;
            for (const auto& pat : bank.patterns)
                if (pat.partRef == p) { used = true; break; }
            if (!used) { freeSlot = p; break; }
        }
        if (freeSlot < 0) return false;

        bank.parts[static_cast<std::size_t>(freeSlot)] =
            bank.parts[static_cast<std::size_t>(partRef)];
        activePattern().partRef = freeSlot;
        return true;
    }

    void LockstepProcessor::appendToChain(int bankIdx, int patternIdx)
    {
        if (bankIdx    < 0 || bankIdx    >= static_cast<int>(kNumBanks))        return;
        if (patternIdx < 0 || patternIdx >= static_cast<int>(kPatternsPerBank)) return;
        chain_.push_back({ bankIdx, patternIdx });
    }

    void LockstepProcessor::clearChain()
    {
        chain_.clear();
    }

    void LockstepProcessor::queueScene(int sectionIdx, bool toFloor)
    {
        if (sectionIdx < 0 || sectionIdx >= kScenesPerSong) return;
        queuedSceneToFloor_.store(toFloor, std::memory_order_release);
        queuedSceneIdx_.store(sectionIdx, std::memory_order_release);
    }

    void LockstepProcessor::cancelQueuedScene()
    {
        queuedSceneIdx_.store(-1, std::memory_order_release);
    }

    bool LockstepProcessor::hasQueuedScene() const
    {
        return queuedSceneIdx_.load(std::memory_order_acquire) >= 0;
    }

    int LockstepProcessor::queuedSectionIdx() const
    {
        return queuedSceneIdx_.load(std::memory_order_acquire);
    }

    void LockstepProcessor::queuePattern(int bankIdx, int patternIdx)
    {
        if (bankIdx    < 0 || bankIdx    >= static_cast<int>(kNumBanks))        return;
        if (patternIdx < 0 || patternIdx >= static_cast<int>(kPatternsPerBank)) return;
        queuedPatternBankIdx_.store(bankIdx,    std::memory_order_relaxed);
        queuedPatternPatIdx_ .store(patternIdx, std::memory_order_release);
    }

    void LockstepProcessor::cancelQueuedPattern()
    {
        chain_.clear();
        queuedPatternBankIdx_.store(-1, std::memory_order_relaxed);
        queuedPatternPatIdx_ .store(-1, std::memory_order_release);
    }

    bool LockstepProcessor::hasQueuedPattern() const
    {
        return queuedPatternPatIdx_.load(std::memory_order_acquire) >= 0;
    }

    int LockstepProcessor::queuedPatternBankIdx() const
    {
        return queuedPatternBankIdx_.load(std::memory_order_relaxed);
    }

    int LockstepProcessor::queuedPatternPatIdx() const
    {
        return queuedPatternPatIdx_.load(std::memory_order_acquire);
    }

    WidgetMappingInfo LockstepProcessor::queryWidgetMapping(int slot, int mzPosition) const
    {
        for (const auto& m : ccMappingTable_.mappings())
        {
            if (m.scope == CCScope::Contextual && m.mzPosition == mzPosition)
                return { true, CCScope::Contextual, -1, m.slot, m.mzPosition, m.ccNumber };
            if ((m.scope == CCScope::Track || m.scope == CCScope::SelectedTrack)
                && m.slot == slot)
                return { true, m.scope, m.trackIndex, m.slot, -1, m.ccNumber };
        }
        return {};
    }

    void LockstepProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        clock_.prepare(sampleRate);
        metronome_.prepare(sampleRate);
        midiClockReceiver_.reset();
        for (auto& m : machines_)
        {
            m->prepare(sampleRate, samplesPerBlock);
            m->reset();
        }
        // Size per-track scratch buffers to match the output bus.
        // Machines write here; blocks are summed to the main output buffer.
        {
            const int numOut = getTotalNumOutputChannels();
            for (auto& tb : trackBuffers_)
                tb.setSize(numOut, samplesPerBlock, false, true, false);
        }
        for (auto& choke : trackChokes_)
            choke.prepare(sampleRate, 1.5f);
        for (auto& fltr : trackFltrs_)
            fltr.prepare(sampleRate);
        for (auto& amp : trackAmps_)
            amp.prepare(sampleRate);
        for (auto& pnf : pendingNoteOffs_)
        {
            pnf.samplesRemaining = -1;
            pnf.openEnded        = false;
        }
        firedStepIdx_.fill(-1);
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

        // Clear per-track scratch buffers once per block.
        for (auto& tb : trackBuffers_)
            tb.clear();

        // --- MIDI clock scanning (before any other processing) ---------------
        const bool isStandalone =
            (wrapperType == juce::AudioProcessor::wrapperType_Standalone);
        const auto mcBlock = midiClockReceiver_.advance(
            midi, buffer.getNumSamples(), getSampleRate());

        // Hoist mode so both the MIDI clock handler and the sequencer gate share it.
        const auto mode = static_cast<SyncMode>(
            syncModeParam_ ? static_cast<int>(syncModeParam_->load()) : 0);

        if (isStandalone && mcBlock.hasClock)
        {
            if (mcBlock.didStart)
            {
                clock_.resetPhase();
                clock_.setInPluginPlaying(true);
                anchorPpq_ = 0.0;
                nextTriggerPpq_.fill(0.0);
                lastStepFired_.fill(false);
            }
            if (mcBlock.didStop)
                clock_.setInPluginPlaying(false);
            // In Locked mode, dropout from the external clock stops the sequencer.
            // In Auto mode, dropout freewheels at the last known BPM.
            if (mode == SyncMode::Locked && mcBlock.dropout)
                clock_.setInPluginPlaying(false);
            // Keep localBpm synced for seamless Auto freewheel.
            if (mcBlock.bpm > 0.0)
                clock_.setLocalBpm(mcBlock.bpm);
        }

        // Update clock now so PPQ is known before MIDI note routing below.
        // The JUCE AudioProcessorPlayer (standalone wrapper) always provides a
        // PlayHead, but its getPosition() sets only timeInSamples/timeInSeconds
        // with no isPlaying, no BPM, and no PPQ position. Passing it to
        // Clock::update() would lock us in the "DAW present but stopped" branch
        // and freeze PPQ forever. In standalone we synthesise PPQ locally, so
        // pass nullptr to skip the playhead entirely.
        Clock::MidiClockInput midiClockIn;
        if (isStandalone && mcBlock.hasClock && mcBlock.running && !mcBlock.dropout)
        {
            midiClockIn.active   = true;
            midiClockIn.ppqStart = mcBlock.ppqStart;
            midiClockIn.ppqEnd   = mcBlock.ppqEnd;
            midiClockIn.bpm      = mcBlock.bpm;
        }
        clock_.update(isStandalone ? nullptr : getPlayHead(),
                      buffer.getNumSamples(), midiClockIn);

        // ---- Mode-based "is the sequencer running?" gate ------------------
        bool sequencerRunning = false;
        if (mode == SyncMode::Locked)
        {
            // Locked + hosted → DAW transport controls; Locked + standalone → in-plugin Play.
            sequencerRunning = isStandalone ? clock_.inPluginPlaying() : clock_.hostPlaying();
        }
        else  // Auto
        {
            sequencerRunning = clock_.inPluginPlaying();
            // Rising edge. A fresh start (after stop/reset) re-anchors so the
            // pattern plays from step 0 at the current position. A resume from
            // pause keeps the existing anchor and pending trig schedule so the
            // pattern continues in phase with the playhead (no audible restart).
            if (sequencerRunning && !wasInPluginPlaying_ && freshStartPending_.exchange(false))
            {
                anchorPpq_ = clock_.ppqAtBlockStart();
                nextTriggerPpq_.fill(anchorPpq_);
            }
        }
        wasInPluginPlaying_ = clock_.inPluginPlaying();

        // MF.6: on transport stop (falling edge), send All-Notes-Off +
        // Reset-All-Controllers on every MIDI-out track to prevent stuck notes.
        // Also mark pending audio note-offs for immediate dispatch so audio voices
        // are released even if the gate would have fired after the stop point.
        if (wasSequencerRunning_ && !sequencerRunning)
        {
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                if (machines_[i]->isMidiOut())
                {
                    juce::MidiBuffer stopBuf;
                    static_cast<MidiOutMachine*>(machines_[i].get())->allNotesOff(stopBuf);
                    if (!isStandalone)
                        midi.addEvents(stopBuf, 0, -1, 0);
                }
                else
                {
                    // Collapse the remaining countdown to 0 so the pending note-off
                    // fires at the start of the next block via the normal dispatch path.
                    // Also schedule open-ended (gate=None) notes for immediate release.
                    auto& pnf = pendingNoteOffs_[i];
                    if (pnf.samplesRemaining > 0 || pnf.openEnded)
                        pnf.samplesRemaining = 0;
                }
            }
        }
        wasSequencerRunning_ = sequencerRunning;

        // Panic: flush voices and send All-Notes-Off without stopping the clock.
        if (panicPending_.exchange(false, std::memory_order_acq_rel))
        {
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                if (machines_[i]->isMidiOut())
                {
                    juce::MidiBuffer stopBuf;
                    static_cast<MidiOutMachine*>(machines_[i].get())->allNotesOff(stopBuf);
                    if (!isStandalone)
                        midi.addEvents(stopBuf, 0, -1, 0);
                }
                else
                {
                    auto& pnf = pendingNoteOffs_[i];
                    if (pnf.samplesRemaining > 0 || pnf.openEnded)
                        pnf.samplesRemaining = 0;
                }
            }
        }

        // ---- PPQ window for step detection --------------------------------
        // In Auto mode, offset PPQ by the anchor so step 0 aligns with Play press.
        const double ppqOffset     = (mode == SyncMode::Auto) ? anchorPpq_ : 0.0;
        const double blockStart    = clock_.ppqAtBlockStart() - ppqOffset;
        const double blockEnd      = clock_.ppqAtBlockEnd()   - ppqOffset;
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
                lastStepFired_[i] = false;
            }
            for (auto& pnf : pendingNoteOffs_)
            {
                pnf.samplesRemaining = -1;
                pnf.openEnded        = false;
            }
            firedStepIdx_.fill(-1);
            metronome_.reset();
        }

        // Snapshot MZ slot mapping for audio-thread use.
        std::array<int, 4> mzSlotSnapshot;
        for (std::size_t i = 0; i < 4; ++i)
            mzSlotSnapshot[i] = mzSlots_[i].load(std::memory_order_relaxed);

        CCMidiContext ccCtx;
        ccCtx.table       = &ccMappingTable_;
        ccCtx.focusTrack  = focusTrack_;
        ccCtx.mzSlots     = mzSlotSnapshot;
        ccCtx.channelMode = channelModeParam_
            ? static_cast<ChannelMode>(static_cast<int>(channelModeParam_->load()))
            : ChannelMode::Omni;
        ccCtx.getCurrentTrackValue = [this](int t, int s) -> float {
            const auto ti = static_cast<std::size_t>(t);
            const auto& trk = sequence().tracks[ti];
            auto* m = machines_[ti].get();
            const int mnp     = m->numParams();
            const int fltrOff = mnp;
            const int ampOff  = mnp + (m->hasInternalFilter() ? 0 : kFltrSlots);
            float base;
            if (static_cast<std::size_t>(s) < trk.baseParams.size())
                base = trk.baseParams[static_cast<std::size_t>(s)];
            else if (!m->hasInternalFilter() && s >= fltrOff && s < fltrOff + kFltrSlots)
                base = kit(static_cast<int>(ti)).fltrState.getSlot(s - fltrOff);
            else if (!m->hasInternalAmp() && s >= ampOff && s < ampOff + kAmpSlots)
                base = kit(static_cast<int>(ti)).ampState.getSlot(s - ampOff);
            else
                base = 0.0f;
            // When a step is held on this track, apply its P-Lock overlay.
            if (editContext_.isActiveForEditing()
                && editContext_.heldTrackIndex() == t)
            {
                const int step = editContext_.heldStepIndex();
                if (step >= 0 && step < kMaxStepsPerTrack)
                    return trk.steps[static_cast<std::size_t>(step)].overrides.get(s, base);
            }
            return base;
        };
        ccCtx.getMetadata = [this](int t, int s) {
            return paramSpec(t, s);
        };
        ccCtx.writeTrackParam = [this](int t, int s, float v) {
            writeParam(t, s, v);
        };
        if (learnActive_.load(std::memory_order_acquire))
        {
            ccCtx.onLearnCapture = [this](int ccNum)
            {
                CCMapping m;
                m.ccNumber   = ccNum;
                m.scope      = learnRequest_.scope;
                m.trackIndex = learnRequest_.trackIndex;
                m.slot       = learnRequest_.slot;
                m.mzPosition = learnRequest_.mzPosition;
                ccMappingTable_.addMapping(std::move(m));
                learnActive_.store(false, std::memory_order_release);
            };
        }

        // Per-track MIDI buffers populated from external MIDI and sequencer trigs.
        std::array<juce::MidiBuffer, kNumTracks> trackMidi;

        // Emit note-offs that were scheduled beyond the previous block's boundary.
        const int numBlockSamples = buffer.getNumSamples();
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            auto& pnf = pendingNoteOffs_[i];
            if (pnf.samplesRemaining < 0) continue;
            if (pnf.samplesRemaining < numBlockSamples)
            {
                for (int n = 0; n < pnf.noteCount; ++n)
                    trackMidi[i].addEvent(
                        juce::MidiMessage::noteOff(1, pnf.notes[static_cast<std::size_t>(n)]),
                        pnf.samplesRemaining);
                pnf.samplesRemaining = -1;
                pnf.openEnded        = false;
            }
            else
            {
                pnf.samplesRemaining -= numBlockSamples;
            }
        }

        // Route external note-on: record into sequencer-scope fields,
        // then inject into the track's MIDI buffer.
        const bool recArmed = clock_.isRecordArmed();
        ccCtx.onNoteOn = [this, &trackMidi, blockStart, samplesPerPpq,
                          sequencerRunning, recArmed]
                         (int track, int sampleOffset, int midiNote, int velocity)
        {
            const auto ti   = static_cast<std::size_t>(track);
            const int  note = std::clamp(midiNote, 0, 127);

            if (recArmed && editContext_.isActiveForEditing()
                && editContext_.heldTrackIndex() == track)
            {
                // M7.4: Key-as-PLock — each note key writes a distinct pool index
                // to the sample_id slot of the held step (drum play-in mode).
                const int sampleSlot = slotForId(track, "sample_id");
                if (sampleSlot >= 0 && samplePool_.size() > 0)
                {
                    const int poolIdx = std::clamp(midiNote - 60, 0,
                                                   samplePool_.size() - 1);
                    writeParam(track, sampleSlot, static_cast<float>(poolIdx));
                }
                // Enable the trig on the held step.
                const int step = editContext_.heldStepIndex();
                if (step >= 0 && step < kMaxStepsPerTrack)
                    sequence().tracks[ti].steps[static_cast<std::size_t>(step)].trig = true;
            }
            else if (recArmed && sequencerRunning
                     && !editContext_.isActiveForEditing())
            {
                // M7.2: Quantize note-on to nearest step boundary, write trig.
                // Chord aggregation: multiple notes quantising to the same absolute step
                // number accumulate (same visit). A new absolute step number = a new visit;
                // in overwrite mode the step is cleared before the first note of the visit,
                // so each pass through the pattern replaces rather than piles up.
                const int trackDiv = static_cast<int>(trackDividerParams_[ti]->load());
                const double divPpq = 0.25 * static_cast<double>(trackDiv <= 0 ? 1 : trackDiv);
                const int trackLen  = static_cast<int>(trackLengthParams_[ti]->load());
                if (divPpq > 0.0 && trackLen > 0 && samplesPerPpq > 0.0)
                {
                    const double noteOnPpq = blockStart
                        + static_cast<double>(sampleOffset) / samplesPerPpq;
                    const auto nearestNum = static_cast<std::int64_t>(
                        std::round(noteOnPpq / divPpq));
                    const int stepIdx = static_cast<int>(
                        ((nearestNum % static_cast<std::int64_t>(trackLen))
                         + trackLen) % trackLen);
                    auto& s = sequence().tracks[ti].steps[static_cast<std::size_t>(stepIdx)];
                    // New absolute step = start of a new visit.
                    // In overwrite mode (default), clear the step before its first note.
                    // In overdub mode, never clear — notes always accumulate.
                    if (nearestNum != lastRecordedStepNum_[ti])
                    {
                        lastRecordedStepNum_[ti] = nearestNum;
                        if (!clock_.isOverdubArmed())
                        {
                            s.trigOverride.noteCount         = 0;
                            s.trigOverride.hasNoteVelocities = false;
                        }
                    }
                    s.trig = true;
                    if (s.trigOverride.noteCount < kMaxNotesPerStep)
                    {
                        // De-dup: skip if note already in chord.
                        bool already = false;
                        for (int n = 0; n < s.trigOverride.noteCount; ++n)
                            if (s.trigOverride.notes[static_cast<std::size_t>(n)] == note)
                                { already = true; break; }
                        if (!already)
                        {
                            const auto idx = static_cast<std::size_t>(s.trigOverride.noteCount);
                            s.trigOverride.notes[idx]      = note;
                            s.trigOverride.velocities[idx] = static_cast<uint8_t>(velocity);
                            s.trigOverride.hasNoteVelocities = true;
                            ++s.trigOverride.noteCount;
                        }
                    }
                    // MHZ.6.1: record note-on sample for gate capture on note-off.
                    const int64_t noteOnSample = totalSamplesProcessed_
                                                 + static_cast<int64_t>(sampleOffset);
                    realtimeNotes_[ti][static_cast<std::size_t>(midiNote)] = { stepIdx, noteOnSample };
                }
            }
            else
            {
                // M5.8: Without record arm — write note override to all held steps, or
                // update the track default.
                if (editContext_.isActiveForEditing()
                    && editContext_.heldTrackIndex() == track
                    && !editContext_.heldSteps().empty())
                {
                    // Snapshot-currently-held semantics:
                    // 1. If no capture is active, this is a fresh chord: clear all held steps.
                    // 2. Mark this note as physically held; update the held-set snapshot.
                    // 3. Write the full snapshot to all held steps.
                    const bool isFresh = !chordCapture_.active;
                    if (isFresh)
                    {
                        for (int si : editContext_.heldSteps())
                        {
                            if (si >= 0 && si < kMaxStepsPerTrack)
                                sequence().tracks[ti]
                                    .steps[static_cast<std::size_t>(si)]
                                    .trigOverride.noteCount = 0;
                        }
                        chordCapture_.active          = true;
                        chordCapture_.gateStartSample = totalSamplesProcessed_ + sampleOffset;
                        chordCapture_.maxVelocity     = 0;
                        chordCapture_.totalVelocity   = 0;
                        chordCapture_.capturedCount   = 0;
                    }

                    if (!chordCapture_.heldNotes[static_cast<std::size_t>(note)])
                    {
                        chordCapture_.heldNotes[static_cast<std::size_t>(note)] = true;
                        ++chordCapture_.heldCount;
                    }
                    if (velocity > chordCapture_.maxVelocity)
                        chordCapture_.maxVelocity = velocity;
                    chordCapture_.totalVelocity += velocity;
                    ++chordCapture_.capturedCount;

                    // Build ascending snapshot of currently-held notes.
                    std::array<int, kMaxNotesPerStep> snapshot{};
                    int snapCount = 0;
                    for (int n = 0; n < 128 && snapCount < kMaxNotesPerStep; ++n)
                        if (chordCapture_.heldNotes[static_cast<std::size_t>(n)])
                            snapshot[static_cast<std::size_t>(snapCount++)] = n;

                    // Write snapshot to all held steps; force trig on.
                    for (int si : editContext_.heldSteps())
                    {
                        if (si < 0 || si >= kMaxStepsPerTrack) continue;
                        auto& step = sequence().tracks[ti].steps[static_cast<std::size_t>(si)];
                        step.trig = true;
                        step.trigOverride.noteCount = snapCount;
                        for (int n = 0; n < snapCount; ++n)
                            step.trigOverride.notes[static_cast<std::size_t>(n)] = snapshot[static_cast<std::size_t>(n)];
                    }
                    editContext_.markParamWritten();
                }
                else
                {
                    sequence().tracks[ti].trigDefaults.note = note;
                }
            }

            trackMidi[ti].addEvent(
                juce::MidiMessage::noteOn(1, midiNote,
                                          static_cast<juce::uint8>(velocity)),
                sampleOffset);
            midiPulse_[ti].store(1.0f, std::memory_order_relaxed);
        };

        // Route external note-off directly into the track's buffer;
        // also finalise chord gate when the last captured note is released.
        ccCtx.onNoteOff = [this, &trackMidi, samplesPerPpq, recArmed, sequencerRunning]
                          (int track, int sampleOffset, int midiNote)
        {
            trackMidi[static_cast<std::size_t>(track)].addEvent(
                juce::MidiMessage::noteOff(1, midiNote),
                sampleOffset);

            // MHZ.6.1: finalise gate for realtime record path.
            if (recArmed && sequencerRunning && !editContext_.isActiveForEditing()
                && midiNote >= 0 && midiNote < 128)
            {
                const auto ti = static_cast<std::size_t>(track);
                auto& entry = realtimeNotes_[ti][static_cast<std::size_t>(midiNote)];
                if (entry.stepIdx >= 0)
                {
                    const int64_t noteOffSample =
                        totalSamplesProcessed_ + static_cast<int64_t>(sampleOffset);
                    const float gateMs =
                        static_cast<float>(noteOffSample - entry.noteOnSample)
                        * 1000.0f / static_cast<float>(getSampleRate());
                    const double captureBpm = (samplesPerPpq > 0.0)
                        ? (getSampleRate() * 60.0 / samplesPerPpq) : clock_.localBpm();
                    const MusicalGate g = nearestMusicalGate(std::max(1.0f, gateMs), captureBpm);
                    auto& trig = sequence().tracks[ti]
                                     .steps[static_cast<std::size_t>(entry.stepIdx)].trigOverride;
                    // Max-gate rule: keep the longest gate among all notes in this step.
                    if (!trig.hasGate
                        || static_cast<uint8_t>(g) > static_cast<uint8_t>(trig.gateValue))
                    {
                        trig.hasGate   = true;
                        trig.gateValue = g;
                    }
                    entry.stepIdx = -1;
                }
            }

            if (!chordCapture_.active) return;
            // Only handle if this note was part of the capture.
            if (midiNote < 0 || midiNote >= 128) return;
            if (!chordCapture_.heldNotes[static_cast<std::size_t>(midiNote)]) return;

            chordCapture_.heldNotes[static_cast<std::size_t>(midiNote)] = false;
            --chordCapture_.heldCount;
            if (chordCapture_.heldCount > 0) return;

            // All chord keys released: write gate + mean velocity to every still-held step.
            const int64_t gateEndSample =
                totalSamplesProcessed_ + static_cast<int64_t>(sampleOffset);
            const int64_t gateSamples = gateEndSample - chordCapture_.gateStartSample;
            const float   gateMs = static_cast<float>(gateSamples)
                                   * 1000.0f / static_cast<float>(getSampleRate());
            const double  captureBpm = (samplesPerPpq > 0.0)
                ? (getSampleRate() * 60.0 / samplesPerPpq)
                : clock_.localBpm();

            if (editContext_.heldTrackIndex() == track)
            {
                const auto ti = static_cast<std::size_t>(track);
                const MusicalGate capturedGate =
                    nearestMusicalGate(std::max(1.0f, gateMs), captureBpm);
                const int meanVel = (chordCapture_.capturedCount > 0)
                    ? (chordCapture_.totalVelocity / chordCapture_.capturedCount)
                    : chordCapture_.maxVelocity;
                for (int si : editContext_.heldSteps())
                {
                    if (si < 0 || si >= kMaxStepsPerTrack) continue;
                    auto& trig = sequence().tracks[ti]
                                     .steps[static_cast<std::size_t>(si)].trigOverride;
                    trig.hasGate  = true;
                    trig.gateValue = capturedGate;
                    if (meanVel > 0)
                    {
                        trig.hasVelocity = true;
                        trig.velocity    = meanVel;
                    }
                }
            }
            // Capture complete — next note-on will start a fresh chord.
            chordCapture_.active = false;
        };

        midiInput_.process(midi, editContext_, ccCtx);

        // Consume any pending preview request from the UI thread.
        // Inject note-on immediately and schedule a note-off 400 ms out.
        const int newPreview = previewPoolIndex_.exchange(-1, std::memory_order_acq_rel);
        if (newPreview >= 0)
        {
            // Cancel an in-flight preview note-off before re-triggering.
            if (previewNoteOffRemaining_ >= 0)
                trackMidi[static_cast<std::size_t>(previewTrack_)].addEvent(
                    juce::MidiMessage::noteOff(1, previewNote_), 0);

            previewActive_           = true;
            previewTrack_            = previewReqTrack_.load(std::memory_order_acquire);
            previewSampleIndex_      = newPreview;
            previewNoteOffRemaining_ = static_cast<int>(getSampleRate() * 0.4);
            previewNote_             = 60;
            trackMidi[static_cast<std::size_t>(previewTrack_)].addEvent(
                juce::MidiMessage::noteOn(1, previewNote_,
                                          static_cast<juce::uint8>(100)), 0);
        }
        // Advance preview note-off countdown; fire when the window arrives.
        if (previewNoteOffRemaining_ >= 0)
        {
            if (previewNoteOffRemaining_ < numBlockSamples)
            {
                trackMidi[static_cast<std::size_t>(previewTrack_)].addEvent(
                    juce::MidiMessage::noteOff(1, previewNote_),
                    previewNoteOffRemaining_);
                previewNoteOffRemaining_ = -1;
                previewActive_           = false;
            }
            else
            {
                previewNoteOffRemaining_ -= numBlockSamples;
            }
        }

        // MG.1: consume keyboard note request from the UI thread.
        // Bit 23 = bypassEditorial flag (set by LEVELS step-held audition).
        // When clear: routes through onNoteOn so record-arm path (M7.2, MHZ.6) fires.
        // When set: raw note-on injection — no P-Lock writes, no chord capture.
        {
            const int req = kbdNoteReq_.exchange(-1, std::memory_order_acq_rel);
            if (req >= 0)
            {
                const int kbdVel    = (req >> 24) & 0x7F;
                const bool bypass   = ((req >> 23) & 1) != 0;
                const int midiNote  = (req >> 16) & 0x7F;
                const int durMs     = req & 0xFFFF;
                const int noteTrack = kbdNoteTrack_.load(std::memory_order_acquire);
                const int noteVel   = kbdVel > 0 ? kbdVel : 100;

                // Cancel any in-flight keyboard note-off.
                if (kbdNoteOffRemaining_ >= 0)
                    trackMidi[static_cast<std::size_t>(kbdNoteActiveTrack_)].addEvent(
                        juce::MidiMessage::noteOff(1, kbdNoteActive_), 0);

                kbdNoteActiveTrack_  = noteTrack;
                kbdNoteActive_       = midiNote;
                kbdNoteOffRemaining_ =
                    static_cast<int>(getSampleRate() * static_cast<double>(durMs) / 1000.0);
                if (bypass)
                    trackMidi[static_cast<std::size_t>(noteTrack)].addEvent(
                        juce::MidiMessage::noteOn(1, static_cast<juce::uint8>(midiNote),
                                                  static_cast<juce::uint8>(noteVel)), 0);
                else
                    ccCtx.onNoteOn(noteTrack, 0, midiNote, noteVel);
            }

            if (kbdNoteOffRemaining_ >= 0)
            {
                if (kbdNoteOffRemaining_ < numBlockSamples)
                {
                    trackMidi[static_cast<std::size_t>(kbdNoteActiveTrack_)].addEvent(
                        juce::MidiMessage::noteOff(1, kbdNoteActive_),
                        kbdNoteOffRemaining_);
                    kbdNoteOffRemaining_ = -1;
                }
                else
                {
                    kbdNoteOffRemaining_ -= numBlockSamples;
                }
            }
        }

        // MG.2: retrig mode — consume activation/cancel request then fire note-ons at rate.
        {
            const int req = retrigReqTrack_.exchange(-1, std::memory_order_acq_rel);
            if (req >= 0)
            {
                // Stop any previous retrig note.
                if (retrigNoteOffRemaining_ >= 0)
                    trackMidi[static_cast<std::size_t>(retrigActiveTrack_)].addEvent(
                        juce::MidiMessage::noteOff(1, 60), 0);

                retrigActiveTrack_      = req;
                retrigRatePpq_          = retrigReqRatePpq_.load(std::memory_order_relaxed);
                retrigNextFireSamples_  = 0.0;
                retrigNoteOffRemaining_ = -1;
            }
            else if (req == -2)  // cancel signal
            {
                if (retrigNoteOffRemaining_ >= 0)
                    trackMidi[static_cast<std::size_t>(retrigActiveTrack_)].addEvent(
                        juce::MidiMessage::noteOff(1, 60), 0);
                retrigActiveTrack_      = -1;
                retrigNoteOffRemaining_ = -1;
            }

            if (retrigActiveTrack_ >= 0)
            {
                // Convert rate from PPQ to samples using the current clock BPM.
                const double samplesPerRetrig = clock_.samplesPerPpq() * retrigRatePpq_;
                const double blockLen = static_cast<double>(numBlockSamples);

                // Advance the note-off countdown.
                if (retrigNoteOffRemaining_ >= 0)
                {
                    if (retrigNoteOffRemaining_ < numBlockSamples)
                    {
                        trackMidi[static_cast<std::size_t>(retrigActiveTrack_)].addEvent(
                            juce::MidiMessage::noteOff(1, 60), retrigNoteOffRemaining_);
                        retrigNoteOffRemaining_ = -1;
                    }
                    else
                    {
                        retrigNoteOffRemaining_ -= numBlockSamples;
                    }
                }

                // Fire note-ons at the retrig rate within this block.
                double firePos = retrigNextFireSamples_;
                while (firePos < blockLen)
                {
                    const int samplePos = juce::jlimit(0, numBlockSamples - 1,
                                                        static_cast<int>(firePos));
                    const auto ti = static_cast<std::size_t>(retrigActiveTrack_);
                    trackMidi[ti].addEvent(
                        juce::MidiMessage::noteOn(1, static_cast<juce::uint8>(60),
                                                  static_cast<juce::uint8>(100)),
                        samplePos);
                    // note-off ~75% through the interval
                    const int noteOffAt = samplePos + juce::jlimit(
                        1, numBlockSamples - 1,
                        static_cast<int>(samplesPerRetrig * 0.75));
                    if (noteOffAt < numBlockSamples)
                        trackMidi[ti].addEvent(juce::MidiMessage::noteOff(1, 60), noteOffAt);
                    else
                        retrigNoteOffRemaining_ = noteOffAt - numBlockSamples;

                    firePos += samplesPerRetrig;
                }
                retrigNextFireSamples_ = firePos - blockLen;
            }
        }

        // Pre-compute solo state once: if any track is soloed, non-soloed tracks
        // are silenced (even if their mute button is off).
        bool anySoloed = false;
        for (std::size_t i = 0; i < kNumTracks; ++i)
            if (trackSoloParams_[i]->load() >= 0.5f) { anySoloed = true; break; }

        if (!sequencerRunning)
        {
            // Render voice tails and any externally-triggered notes.
            // trackMidi already contains note events routed from external MIDI.
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                const bool muted  = (trackMuteParams_[i]->load() >= 0.5f)
                                   || !section().activeMask[i];
                const bool soloed = trackSoloParams_[i]->load() >= 0.5f;
                if (muted || (anySoloed && !soloed)) continue;
                // Resolve against the held step so P-Locks written by the note-on
                // are included in the frame, falling back to -1 (base only).
                int resolveStep = -1;
                if (editContext_.isActiveForEditing()
                    && editContext_.heldTrackIndex() == static_cast<int>(i))
                    resolveStep = editContext_.heldStepIndex();
                const bool fillNow = fillActiveForTrack(static_cast<int>(i));
                auto frame = StateResolver::resolve(sequence().tracks[i], resolveStep, fillNow);
                if (previewActive_ && static_cast<int>(i) == previewTrack_)
                {
                    const int ss = slotForId(static_cast<int>(i), "sample_id");
                    if (ss >= 0 && static_cast<std::size_t>(ss) < frame.size())
                        frame[static_cast<std::size_t>(ss)] = static_cast<float>(previewSampleIndex_);
                }
                auto* mi = machines_[i].get();
                if (mi->isMidiOut())
                {
                    juce::MidiBuffer midiOutBuf;
                    mi->processMidi(trackMidi[i], frame, midiOutBuf);
                    // Plugin mode: forward to host MIDI output bus.
                    if (!isStandalone)
                        midi.addEvents(midiOutBuf, 0, numBlockSamples, 0);
                }
                else
                {
                    mi->process(trackMidi[i], frame, trackBuffers_[i]);

                    const int mnp     = mi->numParams();
                    const int fltrOff = mnp;
                    const int ampOff  = mnp + (mi->hasInternalFilter() ? 0 : kFltrSlots);

                    if (!mi->hasInternalFilter())
                    {
                        TrackFltrState fltr = kit(static_cast<int>(i)).fltrState;
                        if (resolveStep >= 0 && resolveStep < kMaxStepsPerTrack)
                        {
                            const auto& step =
                                sequence().tracks[i].steps[static_cast<std::size_t>(resolveStep)];
                            for (int fs = 0; fs < TrackFltrState::kNumSlots; ++fs)
                                if (step.overrides.has(fltrOff + fs))
                                    fltr.setSlot(fs, step.overrides.get(fltrOff + fs, 0.0f));
                            if (fillNow)
                                for (int fs = 0; fs < TrackFltrState::kNumSlots; ++fs)
                                    if (step.fillOverrides.has(fltrOff + fs))
                                        fltr.setSlot(fs, step.fillOverrides.get(fltrOff + fs, 0.0f));
                        }
                        trackFltrs_[i].processBlock(trackBuffers_[i], trackMidi[i], fltr,
                                                    numBlockSamples);
                    }

                    if (!mi->hasInternalAmp())
                    {
                        TrackAmpState amp = kit(static_cast<int>(i)).ampState;
                        if (resolveStep >= 0 && resolveStep < kMaxStepsPerTrack)
                        {
                            const auto& step =
                                sequence().tracks[i].steps[static_cast<std::size_t>(resolveStep)];
                            for (int as = 0; as < TrackAmpState::kNumSlots; ++as)
                                if (step.overrides.has(ampOff + as))
                                    amp.setSlot(as, step.overrides.get(ampOff + as, 0.0f));
                            if (fillNow)
                                for (int as = 0; as < TrackAmpState::kNumSlots; ++as)
                                    if (step.fillOverrides.has(ampOff + as))
                                        amp.setSlot(as, step.fillOverrides.get(ampOff + as, 0.0f));
                        }
                        const bool ampWasIdle = trackAmps_[i].isIdle();
                        trackAmps_[i].processBlock(trackBuffers_[i], trackMidi[i], amp,
                                                   numBlockSamples);
                        if (!ampWasIdle && trackAmps_[i].isIdle())
                            mi->reset();
                    }

                    trackPeak_[i].store(trackBuffers_[i].getMagnitude(0, numBlockSamples),
                                        std::memory_order_relaxed);
                }
            }

            // Sum per-track outputs to the main bus.
            for (std::size_t ti = 0; ti < kNumTracks; ++ti)
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.addFrom(ch, 0, trackBuffers_[ti], ch, 0, numBlockSamples);

            // Keep audio path (gain smoothing, DC blocker) running so it doesn't freeze.
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
            masterPeak_.store(buffer.getMagnitude(0, numSamples),
                              std::memory_order_relaxed);
            return;
        }

        // ── Section launch engine (Phase 7 / DESIGN §4.8, §16) ───────────────
        // A queued Section fires at the next core-time bar boundary.
        {
            const int qSecIdx = queuedSceneIdx_.load(std::memory_order_acquire);
            if (qSecIdx >= 0 && samplesPerPpq > 0.0)
            {
                const auto& ct     = section().coreTime;
                const double barPpq = ct.barPpq()
                                    * static_cast<double>(project_.launchQuantizeBars);
                if (barPpq > 0.0)
                {
                    // Next bar boundary at or after blockStart.
                    const double boundary =
                        std::ceil(blockStart / barPpq) * barPpq;
                    if (boundary < blockEnd)
                    {
                        queuedSceneIdx_.store(-1, std::memory_order_release);
                        const bool toFloor =
                            queuedSceneToFloor_.load(std::memory_order_acquire);
                        juce::MessageManager::callAsync(
                            [this, qSecIdx, toFloor]
                            {
                                if (toFloor) setActiveSceneToFloor(qSecIdx);
                                else         setActiveScene(qSecIdx);
                                if (onActivePatternChanged)
                                    onActivePatternChanged();
                            });
                    }
                }
            }
        }

        // ── Legacy pattern launch engine (removed in Stage D) ─────────────────
        // Queued pattern switch fires at end of the longest playing track's cycle.
        {
            const int qBankIdx = queuedPatternBankIdx_.load(std::memory_order_relaxed);
            const int qPatIdx  = queuedPatternPatIdx_ .load(std::memory_order_acquire);
            if (qBankIdx >= 0 && qPatIdx >= 0)
            {
                std::size_t longestTrack = 0;
                double      longestCycle = 0.0;
                for (std::size_t i = 0; i < kNumTracks; ++i)
                {
                    const int    tl  = static_cast<int>(trackLengthParams_[i]->load());
                    const int    td  = static_cast<int>(trackDividerParams_[i]->load());
                    const double ppq = 0.25 * static_cast<double>(td <= 0 ? 1 : td)
                                       * static_cast<double>(tl <= 0 ? 1 : tl);
                    if (ppq > longestCycle) { longestCycle = ppq; longestTrack = i; }
                }

                const int   tl     = static_cast<int>(trackLengthParams_[longestTrack]->load());
                const int   td     = static_cast<int>(trackDividerParams_[longestTrack]->load());
                const double divPpqL = 0.25 * static_cast<double>(td <= 0 ? 1 : td);
                if (divPpqL > 0.0 && samplesPerPpq > 0.0 && tl > 0)
                {
                    double cursor = nextTriggerPpq_[longestTrack];
                    if (cursor < blockStart - divPpqL)
                        cursor = std::floor(blockStart / divPpqL) * divPpqL;
                    while (cursor < blockEnd)
                    {
                        if (cursor >= blockStart)
                        {
                            const auto stepNum = static_cast<std::int64_t>(cursor / divPpqL);
                            const int  si      = static_cast<int>(
                                stepNum % static_cast<std::int64_t>(tl));
                            if (si == 0)
                            {
                                queuedPatternBankIdx_.store(-1, std::memory_order_relaxed);
                                queuedPatternPatIdx_ .store(-1, std::memory_order_release);
                                juce::MessageManager::callAsync(
                                    [this, qBankIdx, qPatIdx]
                                    {
                                        setActivePattern(qBankIdx, qPatIdx);
                                        if (!chain_.empty())
                                        {
                                            auto [nextBank, nextPat] = chain_.front();
                                            chain_.pop_front();
                                            if (chainLoopEnabled_)
                                                chain_.push_back({ qBankIdx, qPatIdx });
                                            queuePattern(nextBank, nextPat);
                                        }
                                        if (onActivePatternChanged)
                                            onActivePatternChanged();
                                    });
                                break;
                            }
                        }
                        cursor += divPpqL;
                    }
                }
            }
        }

        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const auto& track = sequence().tracks[i];

            const int trackLen = static_cast<int>(trackLengthParams_[i]->load());
            const int trackDiv = static_cast<int>(trackDividerParams_[i]->load());
            // MD.6/MD.7: combined mute = global (APVTS) || section active-mask.
            const bool globalMuted  = trackMuteParams_[i]->load() >= 0.5f;
            const bool sectionMuted = !section().activeMask[i];
            const bool muted   = globalMuted || sectionMuted;
            const bool soloed  = trackSoloParams_[i]->load() >= 0.5f;
            const bool silent  = muted || (anySoloed && !soloed);

            // MF.7: mute rising edge — send All-Notes-Off on MIDI-out tracks to
            // prevent stuck notes when a track is muted mid-note.
            if (silent && !wasSilent_[i] && machines_[i]->isMidiOut())
            {
                juce::MidiBuffer stopBuf;
                static_cast<MidiOutMachine*>(machines_[i].get())->allNotesOff(stopBuf);
                if (!isStandalone)
                    midi.addEvents(stopBuf, 0, -1, 0);
            }
            wasSilent_[i] = silent;

            // 16th note = 0.25 PPQ; divider scales the grid coarser.
            const double divPpq = 0.25 * static_cast<double>(trackDiv <= 0 ? 1 : trackDiv);

            if (divPpq <= 0.0 || samplesPerPpq <= 0.0 || trackLen <= 0 || silent)
                continue;

            // If the cursor has fallen far behind (cold start, late join),
            // snap it to the step boundary at/before blockStart so we don't
            // burn CPU catching up sample-by-sample.
            if (nextTriggerPpq_[i] < blockStart - divPpq)
                nextTriggerPpq_[i] = std::floor(blockStart / divPpq) * divPpq;

            const bool curFillActive = fillActiveForTrack(static_cast<int>(i));
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

                    const auto& step = track.steps[static_cast<std::size_t>(stepIndex)];
                    const TrigCondition& cond = step.condition.isTrivial()
                                                    ? track.baseCond
                                                    : step.condition;
                    const bool fired =
                        TrigEvaluator::shouldFire(step, cond, i, stepNum, trackLen,
                                                   lastStepFired_[i], curFillActive);
                    if (fired)
                    {
                        const double offset =
                            (nextTriggerPpq_[i] - blockStart) * samplesPerPpq;
                        triggerAt = std::max(0, static_cast<int>(offset));
                        firedStepIdx_[i] = stepIndex;  // ME.4: track last-fired step for FLTR P-Locks
                    }
                    else if (stepIndex == firedStepIdx_[i] && firedStepIdx_[i] >= 0)
                    {
                        // The step that last fired has cycled back but doesn't fire now
                        // (user toggled it off, or condition failed). Close any open-ended
                        // note that was left sounding by that step.
                        auto& pnf = pendingNoteOffs_[i];
                        if (pnf.openEnded)
                        {
                            const double off = (nextTriggerPpq_[i] - blockStart) * samplesPerPpq;
                            const int nofAt  = std::clamp(static_cast<int>(off), 0,
                                                          numBlockSamples - 1);
                            for (int n = 0; n < pnf.noteCount; ++n)
                                trackMidi[i].addEvent(
                                    juce::MidiMessage::noteOff(
                                        1, pnf.notes[static_cast<std::size_t>(n)]),
                                    nofAt);
                            pnf.openEnded = false;
                        }
                    }
                    lastStepFired_[i] = fired;
                }
                nextTriggerPpq_[i] += divPpq;
            }

            // Inject sequencer note-on(s) with resolved trig fields (note(s)/velocity/gate).
            if (triggerAt >= 0)
            {
                trigPulse_[i].store(1.0f, std::memory_order_relaxed);
                const auto trig = StateResolver::resolveTrig(track, stepIndex, curFillActive);

                // If a previous trig's note-off is still pending (gate longer than
                // the step interval), emit it immediately at triggerAt so the
                // voice releases and then retriggers cleanly. Inserting before the
                // new note-on at the same sample preserves event order because
                // juce::MidiBuffer iterates same-position events in insertion order.
                {
                    auto& pnf = pendingNoteOffs_[i];
                    if (pnf.samplesRemaining >= 0 || pnf.openEnded)
                    {
                        for (int n = 0; n < pnf.noteCount; ++n)
                            trackMidi[i].addEvent(
                                juce::MidiMessage::noteOff(
                                    1, pnf.notes[static_cast<std::size_t>(n)]),
                                triggerAt);
                        pnf.samplesRemaining = -1;
                        pnf.openEnded        = false;
                    }
                }

                // Pick which of the step's notes to emit.
                // currentVoices()==V0 (MIDI-out) passes the chord through unchanged;
                // otherwise we sort the held notes ascending and call the
                // spread-with-bias picker so endpoints survive first.
                auto* machineForVoices = machines_[static_cast<std::size_t>(i)].get();
                const auto poly = machineForVoices
                    ? machineForVoices->currentVoices(track.baseParams)
                    : IMachine::Polyphony::V1;
                const int machineVoices = static_cast<int>(poly);

                // Sort (note, velocity) pairs together ascending by pitch so the
                // spread-with-bias picker and per-note velocity lookup stay in sync.
                struct NoteVelPair { int note; uint8_t vel; };
                std::array<NoteVelPair, kMaxNotesPerStep> nvPairs{};
                for (int n = 0; n < trig.noteCount; ++n)
                {
                    const auto ni = static_cast<std::size_t>(n);
                    nvPairs[ni].note = trig.notes[ni];
                    nvPairs[ni].vel  = trig.hasNoteVelocities
                        ? trig.velocities[ni]
                        : static_cast<uint8_t>(std::clamp(trig.velocity, 1, 127));
                }
                std::sort(nvPairs.begin(), nvPairs.begin() + trig.noteCount,
                          [](const NoteVelPair& a, const NoteVelPair& b) { return a.note < b.note; });

                std::array<int, kMaxNotesPerStep> sortedNotes{};
                std::array<uint8_t, kMaxNotesPerStep> sortedVels{};
                for (int n = 0; n < trig.noteCount; ++n)
                {
                    sortedNotes[static_cast<std::size_t>(n)] = nvPairs[static_cast<std::size_t>(n)].note;
                    sortedVels [static_cast<std::size_t>(n)] = nvPairs[static_cast<std::size_t>(n)].vel;
                }

                std::array<int, kMaxNotesPerStep> emitNotes{};
                int notesToEmit = 0;
                if (machineVoices == 0)
                {
                    for (int n = 0; n < trig.noteCount; ++n)
                    {
                        emitNotes[static_cast<std::size_t>(n)] = sortedNotes[static_cast<std::size_t>(n)];
                    }
                    notesToEmit = trig.noteCount;
                }
                else
                {
                    pickSpreadNotes(sortedNotes, trig.noteCount, machineVoices,
                                    track.noteSelection, emitNotes, notesToEmit);
                }

                // Reconstruct per-note velocities for the emitted subset.
                std::array<uint8_t, kMaxNotesPerStep> emitVels{};
                for (int j = 0; j < notesToEmit; ++j)
                {
                    for (int k = 0; k < trig.noteCount; ++k)
                    {
                        if (sortedNotes[static_cast<std::size_t>(k)] == emitNotes[static_cast<std::size_t>(j)])
                        {
                            emitVels[static_cast<std::size_t>(j)] = sortedVels[static_cast<std::size_t>(k)];
                            break;
                        }
                    }
                }

                const auto uniformVel = static_cast<juce::uint8>(std::clamp(trig.velocity, 1, 127));
                for (int n = 0; n < notesToEmit; ++n)
                {
                    const auto ni  = static_cast<std::size_t>(n);
                    const auto vel = trig.hasNoteVelocities
                        ? static_cast<juce::uint8>(std::clamp(static_cast<int>(emitVels[ni]), 1, 127))
                        : uniformVel;
                    trackMidi[i].addEvent(
                        juce::MidiMessage::noteOn(1, emitNotes[ni], vel), triggerAt);
                }

                if (trig.gateValue != MusicalGate::None)
                {
                    const double liveBpm = (samplesPerPpq > 0.0)
                        ? (getSampleRate() * 60.0 / samplesPerPpq)
                        : clock_.localBpm();
                    const int gateSamples = musicalGateToSamples(
                        trig.gateValue, liveBpm, getSampleRate());
                    const int noteOffAt = triggerAt + gateSamples;
                    if (noteOffAt < numBlockSamples)
                    {
                        for (int n = 0; n < notesToEmit; ++n)
                            trackMidi[i].addEvent(
                                juce::MidiMessage::noteOff(1, emitNotes[static_cast<std::size_t>(n)]),
                                noteOffAt);
                    }
                    else
                    {
                        auto& pnf             = pendingNoteOffs_[i];
                        pnf.samplesRemaining  = noteOffAt - numBlockSamples;
                        pnf.noteCount         = notesToEmit;
                        pnf.notes             = emitNotes;
                        pnf.openEnded         = false;
                    }
                }
                else
                {
                    // gate=None: voices play to their envelope end. Track the open notes
                    // so they can be closed if the step is toggled off or the sequencer stops.
                    auto& pnf     = pendingNoteOffs_[i];
                    pnf.openEnded = true;
                    pnf.noteCount = notesToEmit;
                    pnf.notes     = emitNotes;
                    // samplesRemaining stays -1 — no scheduled release.
                }
            }

            // Resolve ParamFrame for the machine using the last-fired step so that
            // P-Locks (including fill-layer overrides) persist for the full note
            // duration rather than only the block in which the step fires.
            // MG.5: if the fired step carries a sound_id override, use the pool
            // entry's baseParams as the base, then apply step P-Locks on top.
            // Falls back to the normal StateResolver path if the entry is missing.
            auto frame = [&]() -> ParamFrame
            {
                const int fi = firedStepIdx_[i];
                if (fi >= 0 && fi < kMaxStepsPerTrack)
                {
                    const auto& ov = track.steps[static_cast<std::size_t>(fi)].trigOverride;
                    if (ov.hasSoundId && ov.soundId >= 0)
                    {
                        const auto* e = project_.soundPool.get(ov.soundId);
                        if (e && e->baseParams.size() == track.baseParams.size())
                        {
                            ParamFrame f = e->baseParams;
                            const auto& plock =
                                track.steps[static_cast<std::size_t>(fi)].overrides;
                            for (int slot = 0; slot < static_cast<int>(f.size()); ++slot)
                            {
                                if (plock.has(slot))
                                    f[static_cast<std::size_t>(slot)] =
                                        plock.get(slot, f[static_cast<std::size_t>(slot)]);
                            }
                            if (curFillActive)
                            {
                                const auto& fp =
                                    track.steps[static_cast<std::size_t>(fi)].fillOverrides;
                                for (int slot = 0; slot < static_cast<int>(f.size()); ++slot)
                                    if (fp.has(slot))
                                        f[static_cast<std::size_t>(slot)] =
                                            fp.get(slot, f[static_cast<std::size_t>(slot)]);
                            }
                            return f;
                        }
                    }
                }
                return StateResolver::resolve(track, fi, curFillActive);
            }();
            if (previewActive_ && static_cast<int>(i) == previewTrack_)
            {
                const int ss = slotForId(static_cast<int>(i), "sample_id");
                if (ss >= 0 && static_cast<std::size_t>(ss) < frame.size())
                    frame[static_cast<std::size_t>(ss)] = static_cast<float>(previewSampleIndex_);
            }
            auto* mi = machines_[i].get();
            if (mi->isMidiOut())
            {
                juce::MidiBuffer midiOutBuf;
                mi->processMidi(trackMidi[i], frame, midiOutBuf);
                if (!isStandalone)
                    midi.addEvents(midiOutBuf, 0, numBlockSamples, 0);
            }
            else
            {
                mi->process(trackMidi[i], frame, trackBuffers_[i]);

                const int mnp     = mi->numParams();
                const int fltrOff = mnp;
                const int ampOff  = mnp + (mi->hasInternalFilter() ? 0 : kFltrSlots);
                const int fsi     = firedStepIdx_[i];

                if (!mi->hasInternalFilter())
                {
                    TrackFltrState fltr = kit(static_cast<int>(i)).fltrState;
                    if (fsi >= 0 && fsi < kMaxStepsPerTrack)
                    {
                        const auto& step = sequence().tracks[i].steps[static_cast<std::size_t>(fsi)];
                        for (int fs = 0; fs < TrackFltrState::kNumSlots; ++fs)
                            if (step.overrides.has(fltrOff + fs))
                                fltr.setSlot(fs, step.overrides.get(fltrOff + fs, 0.0f));
                        if (curFillActive)
                            for (int fs = 0; fs < TrackFltrState::kNumSlots; ++fs)
                                if (step.fillOverrides.has(fltrOff + fs))
                                    fltr.setSlot(fs, step.fillOverrides.get(fltrOff + fs, 0.0f));
                    }
                    trackFltrs_[i].processBlock(trackBuffers_[i], trackMidi[i], fltr,
                                                numBlockSamples);
                }

                if (!mi->hasInternalAmp())
                {
                    TrackAmpState amp = kit(static_cast<int>(i)).ampState;
                    if (fsi >= 0 && fsi < kMaxStepsPerTrack)
                    {
                        const auto& step = sequence().tracks[i].steps[static_cast<std::size_t>(fsi)];
                        for (int as = 0; as < TrackAmpState::kNumSlots; ++as)
                            if (step.overrides.has(ampOff + as))
                                amp.setSlot(as, step.overrides.get(ampOff + as, 0.0f));
                        if (curFillActive)
                            for (int as = 0; as < TrackAmpState::kNumSlots; ++as)
                                if (step.fillOverrides.has(ampOff + as))
                                    amp.setSlot(as, step.fillOverrides.get(ampOff + as, 0.0f));
                    }
                    const bool ampWasIdle = trackAmps_[i].isIdle();
                    trackAmps_[i].processBlock(trackBuffers_[i], trackMidi[i], amp,
                                               numBlockSamples);
                    if (!ampWasIdle && trackAmps_[i].isIdle())
                        mi->reset();
                }

                trackPeak_[i].store(trackBuffers_[i].getMagnitude(0, numBlockSamples),
                                    std::memory_order_relaxed);
            }
        }

        // Per-track soft clip — prevents one hot track from dominating the master bus.
        for (std::size_t ti = 0; ti < kNumTracks; ++ti)
            for (int ch = 0; ch < trackBuffers_[ti].getNumChannels(); ++ch)
            {
                float* d = trackBuffers_[ti].getWritePointer(ch);
                for (int n = 0; n < numBlockSamples; ++n)
                    d[n] = std::tanh(d[n]);
            }

        // Sum per-track outputs to the main bus.
        for (std::size_t ti = 0; ti < kNumTracks; ++ti)
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom(ch, 0, trackBuffers_[ti], ch, 0, numBlockSamples);

        if (clock_.isMetronomeEnabled())
            metronome_.process(blockStart, blockEnd, samplesPerPpq, buffer,
                               section().coreTime.numerator,
                               section().coreTime.denominator);

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
        masterPeak_.store(buffer.getMagnitude(0, numSamples),
                          std::memory_order_relaxed);

        totalSamplesProcessed_ += numBlockSamples;
    }

    void LockstepProcessor::writeParam(int track, int slot, float value)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;
        const int np = numParams(track);
        if (slot < 0 || slot >= np)
            return;

        // Clamp sample index to the actual pool size so a full-throw CC can
        // never select a beyond-pool entry on tracks with a sample slot.
        if (idForSlot(track, slot) == "sample_id"
            || idForSlot(track, slot) == "slicer_sample_id")
        {
            const int poolSize = samplePool_.size();
            if (poolSize > 0)
                value = std::min(value, static_cast<float>(poolSize - 1));
            else
                value = 0.0f;
        }

        // Zero-crossing snap: if the slot is marked zeroCrossingSnap and the
        // machine is a SamplePlayingMachineBase, round the position value to the
        // nearest zero-crossing in the currently-loaded sample.
        {
            const auto ti = static_cast<std::size_t>(track);
            const auto* m = machines_[ti].get();
            if (m != nullptr && slot < m->numParams()
                && m->paramSpec(slot).zeroCrossingSnap)
            {
                const auto* spm = dynamic_cast<const SamplePlayingMachineBase*>(m);
                if (spm != nullptr)
                {
                    const auto& bp = sequence().tracks[ti].baseParams;
                    value = spm->snapWrittenValue(slot, value, samplePool_, bp);
                }
            }
        }

        // MD.10 Control-All: when active and no step is held on the source track,
        // broadcast to every track whose schema has the same slot id.
        if (controlAllActive_
            && !(editContext_.isActiveForEditing()
                 && editContext_.heldTrackIndex() == track))
        {
            const juce::String srcId = idForSlot(track, slot);
            if (srcId.isEmpty()) return;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                const int dstSlot = slotForId(t, srcId);
                if (dstSlot < 0) continue;
                const auto ti = static_cast<std::size_t>(t);
                if (editContext_.isActiveForEditing()
                    && editContext_.heldTrackIndex() == t)
                {
                    const int step = editContext_.heldStepIndex();
                    if (step >= 0 && step < kMaxStepsPerTrack)
                    {
                        sequence().tracks[ti].steps[static_cast<std::size_t>(step)]
                            .overrides.set(dstSlot, value);
                        editContext_.markParamWritten();
                    }
                }
                else
                {
                    auto*     dm     = machines_[ti].get();
                    const int dstMnp = dm->numParams();
                    const int dstAmpOff = dstMnp + (dm->hasInternalFilter() ? 0 : kFltrSlots);
                    if (dstSlot < dstMnp)
                    {
                        sequence().tracks[ti].baseParams[static_cast<std::size_t>(dstSlot)] = value;
                        activePart().tracks[ti].baseParams[static_cast<std::size_t>(dstSlot)] = value;
                    }
                    else if (!dm->hasInternalFilter() && dstSlot < dstAmpOff)
                    {
                        kit(static_cast<int>(ti)).fltrState.setSlot(dstSlot - dstMnp, value);
                    }
                    else if (!dm->hasInternalAmp())
                    {
                        kit(static_cast<int>(ti)).ampState.setSlot(dstSlot - dstAmpOff, value);
                    }
                }
            }
            return;
        }

        const auto ti = static_cast<std::size_t>(track);

        if (editContext_.isActiveForEditing()
            && editContext_.heldTrackIndex() == track)
        {
            const int step = editContext_.heldStepIndex();
            if (step >= 0 && step < kMaxStepsPerTrack)
            {
                sequence().tracks[ti].steps[static_cast<std::size_t>(step)]
                    .overrides.set(slot, value);
                editContext_.markParamWritten();
            }
        }
        else
        {
            auto*     wm     = machines_[ti].get();
            const int mnp    = wm->numParams();
            const int ampOff = mnp + (wm->hasInternalFilter() ? 0 : kFltrSlots);
            if (slot < mnp)
            {
                sequence().tracks[ti].baseParams[static_cast<std::size_t>(slot)] = value;
                activePart().tracks[ti].baseParams[static_cast<std::size_t>(slot)] = value;

                // Recompute slices when a slice-governing base param changes.
                recomputeSlicesIfNeeded(static_cast<int>(ti), slot,
                                        sequence().tracks[ti].baseParams);
            }
            else if (!wm->hasInternalFilter() && slot < ampOff)
            {
                kit(static_cast<int>(ti)).fltrState.setSlot(slot - mnp, value);
            }
            else if (!wm->hasInternalAmp())
            {
                kit(static_cast<int>(ti)).ampState.setSlot(slot - ampOff, value);
            }
        }
    }

    void LockstepProcessor::clearParam(int track, int step, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step  < 0 || step  >= kMaxStepsPerTrack)             return;
        if (slot  < 0 || slot  >= numParams(track))              return;
        sequence().tracks[static_cast<std::size_t>(track)]
            .steps[static_cast<std::size_t>(step)].overrides.clear(slot);
    }

    void LockstepProcessor::clearTrigOverrideField(int track, int step, int field)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step  < 0 || step  >= kMaxStepsPerTrack)             return;
        auto& trig = sequence().tracks[static_cast<std::size_t>(track)]
                         .steps[static_cast<std::size_t>(step)].trigOverride;
        switch (field)
        {
            case 0: trig.noteCount = 0; break;
            case 1: trig.hasVelocity = false; break;
            case 2: trig.hasGate = false; break;
            default: break;
        }
    }

    void LockstepProcessor::writeFillParam(int track, int slot, float value)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (slot  < 0 || slot  >= numParams(track))             return;
        if (!editContext_.isActiveForEditing()
            || editContext_.heldTrackIndex() != track) return;
        const int step = editContext_.heldStepIndex();
        if (step < 0 || step >= kMaxStepsPerTrack) return;
        sequence().tracks[static_cast<std::size_t>(track)]
            .steps[static_cast<std::size_t>(step)].fillOverrides.set(slot, value);
        editContext_.markParamWritten();
    }

    void LockstepProcessor::clearFillParam(int track, int step, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step  < 0 || step  >= kMaxStepsPerTrack)             return;
        if (slot  < 0 || slot  >= numParams(track))              return;
        sequence().tracks[static_cast<std::size_t>(track)]
            .steps[static_cast<std::size_t>(step)].fillOverrides.clear(slot);
    }

    // -------------------------------------------------------------------------
    // MD.6/MD.7: Mute helpers

    bool LockstepProcessor::getGlobalMute(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto* p = trackMuteParams_[static_cast<std::size_t>(track)];
        return p && p->load() >= 0.5f;
    }

    void LockstepProcessor::setGlobalMute(int track, bool muted)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (auto* p = apvts_.getParameter(ParamIDs::trackMute(track)))
            p->setValueNotifyingHost(muted ? 1.0f : 0.0f);
    }

    void LockstepProcessor::toggleGlobalMute(int track)
    {
        setGlobalMute(track, !getGlobalMute(track));
    }

    void LockstepProcessor::toggleSolo(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto* p = trackSoloParams_[static_cast<std::size_t>(track)];
        if (!p) return;
        const bool nowSoloed = p->load() >= 0.5f;
        if (auto* param = apvts_.getParameter(ParamIDs::trackSolo(track)))
            param->setValueNotifyingHost(nowSoloed ? 0.0f : 1.0f);
    }

    void LockstepProcessor::deleteTrack(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto ti = static_cast<std::size_t>(track);
        // Replace machine with stub (absent) and reset base params.
        setTrackMachine(track, StubMachine::kMachineId);
        // Clear all step data.
        auto& seqTrack = sequence().tracks[ti];
        for (auto& s : seqTrack.steps)
        {
            s.trig           = false;
            s.condition      = TrigCondition{};
            s.overrides      = PLock{};
            s.trigOverride   = TrigOverride{};
            s.fillTrigState  = FillTrigState::Inherit;
            s.fillOverrides  = PLock{};
            s.fillTrigOverride = TrigOverride{};
        }
        seqTrack.trigDefaults = TrigDefaults{};
    }

    void LockstepProcessor::deletePart()
    {
        auto& part = activePart();
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            part.tracks[t].machineId  = StubMachine::kMachineId;
            part.tracks[t].baseParams.clear();
            part.tracks[t].fltrState  = TrackFltrState{};
            part.tracks[t].ampState   = TrackAmpState{};

            // Mirror into the new kit hierarchy (Phase 7).
            auto& k   = kit(static_cast<int>(t));
            k.machineId  = StubMachine::kMachineId;
            k.baseParams.clear();
            k.fltrState  = TrackFltrState{};
            k.ampState   = TrackAmpState{};
        }
        reinstallMachinesFromActivePart();
    }

    bool LockstepProcessor::getPatternMute(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        return !section().activeMask[static_cast<std::size_t>(track)];
    }

    void LockstepProcessor::setPatternMute(int track, bool muted)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        section().activeMask[static_cast<std::size_t>(track)] = !muted;
    }

    void LockstepProcessor::togglePatternMute(int track)
    {
        setPatternMute(track, !getPatternMute(track));
    }

    void LockstepProcessor::clearStepLocks(int track, int step)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step  < 0 || step  >= kMaxStepsPerTrack)            return;
        auto& s = sequence().tracks[static_cast<std::size_t>(track)]
                      .steps[static_cast<std::size_t>(step)];
        s.overrides        = PLock{};
        s.trigOverride     = TrigOverride{};
        s.fillOverrides    = PLock{};
        s.fillTrigOverride = TrigOverride{};
    }

    void LockstepProcessor::cancelChordCapture(int /*track*/, int /*step*/)
    {
        // With the snapshot-currently-held model, chordCapture_ resets automatically
        // when all MIDI notes are released (heldCount drops to 0). No explicit cancel
        // is needed on step release; the next note-on will start a fresh capture.
    }

    void LockstepProcessor::pushCheckpoint()
    {
        const int key = activeBankIdx_ * kPatternsPerBank + activePatternIdx_;
        auto& stack = checkpoints_[key];
        stack.push_back({ activePattern(), activePart() });
        if (static_cast<int>(stack.size()) > kMaxCheckpoints)
            stack.erase(stack.begin());  // evict oldest
    }

    bool LockstepProcessor::popCheckpoint()
    {
        const int key = activeBankIdx_ * kPatternsPerBank + activePatternIdx_;
        auto it = checkpoints_.find(key);
        if (it == checkpoints_.end() || it->second.empty()) return false;
        const auto& entry = it->second.back();
        activePattern() = entry.savedPattern;
        activePart()    = entry.savedPart;
        it->second.pop_back();
        return true;
    }

    void LockstepProcessor::dropCheckpoint()
    {
        const int key = activeBankIdx_ * kPatternsPerBank + activePatternIdx_;
        auto it = checkpoints_.find(key);
        if (it != checkpoints_.end() && !it->second.empty())
            it->second.pop_back();
    }

    int LockstepProcessor::checkpointDepth() const
    {
        const int key = activeBankIdx_ * kPatternsPerBank + activePatternIdx_;
        const auto it = checkpoints_.find(key);
        return (it != checkpoints_.end()) ? static_cast<int>(it->second.size()) : 0;
    }

    void LockstepProcessor::rotateTrackSteps(int track, int dir)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& trk = sequence().tracks[static_cast<std::size_t>(track)];
        const int len = std::max(1, trk.length);
        if (len <= 1) return;
        if (dir > 0)
            // Shift content toward higher index (notes move right); step[len-1] wraps to step[0].
            std::rotate(trk.steps.begin(), trk.steps.begin() + (len - 1),
                        trk.steps.begin() + len);
        else
            // Shift content toward lower index (notes move left); step[0] wraps to step[len-1].
            std::rotate(trk.steps.begin(), trk.steps.begin() + 1,
                        trk.steps.begin() + len);
    }

    void LockstepProcessor::doubleTrackLength(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& trk = sequence().tracks[static_cast<std::size_t>(track)];
        const int len = std::max(1, trk.length);
        const int newLen = std::min(len * 2, kMaxStepsPerTrack);
        if (newLen <= len) return;
        for (int i = len; i < newLen; ++i)
            trk.steps[static_cast<std::size_t>(i)] =
                trk.steps[static_cast<std::size_t>(i % len)];
        trk.length = newLen;
        if (auto* p = apvts_.getParameter(ParamIDs::trackLength(track)))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(newLen)));
    }

    void LockstepProcessor::halveTrackLength(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& trk = sequence().tracks[static_cast<std::size_t>(track)];
        const int len = std::max(1, trk.length);
        const int newLen = std::max(1, len / 2);
        if (newLen >= len) return;
        trk.length = newLen;
        if (auto* p = apvts_.getParameter(ParamIDs::trackLength(track)))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(newLen)));
    }

    bool LockstepProcessor::isTrackMidiOut(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto& m = machines_[static_cast<std::size_t>(track)];
        return m && m->isMidiOut();
    }

    int LockstepProcessor::numParams(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return 0;
        auto* m = machines_[static_cast<std::size_t>(track)].get();
        return m->numParams()
             + (m->hasInternalFilter() ? 0 : kFltrSlots)
             + (m->hasInternalAmp()    ? 0 : kAmpSlots);
    }

    // Stable string IDs for the 6 FLTR virtual slots.
    static const juce::String kFltrIds[TrackFltrState::kNumSlots] = {
        "lockstep.fltr.mode", "lockstep.fltr.slope", "lockstep.fltr.cutoff",
        "lockstep.fltr.res",  "lockstep.fltr.drive", "lockstep.fltr.env"
    };

    // Stable string IDs for the 8 AMP virtual slots.
    static const juce::String kAmpIds[TrackAmpState::kNumSlots] = {
        "lockstep.amp.level", "lockstep.amp.pan",     "lockstep.amp.gate",
        "lockstep.amp.att",   "lockstep.amp.hld",     "lockstep.amp.dec",
        "lockstep.amp.sus",   "lockstep.amp.rel"
    };

    ParamSpec LockstepProcessor::paramSpec(int track, int index) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};
        const auto ti   = static_cast<std::size_t>(track);
        auto*      m    = machines_[ti].get();
        const int  mnp  = m->numParams();
        if (index < mnp)
            return m->paramSpec(index);

        const int fltrOff = mnp;
        const int ampOff  = mnp + (m->hasInternalFilter() ? 0 : kFltrSlots);

        if (!m->hasInternalFilter() && index >= fltrOff && index < fltrOff + kFltrSlots)
        {
            const int fs = index - fltrOff;
            ParamSpec p;
            p.sectionIndex = kFltrSecIdx;
            p.id           = kFltrIds[fs];
            switch (fs)
            {
            case 0: p.label="Mode";   p.isStepped=true; p.maxValue=3.0f; break;
            case 1: p.label="Slope";  p.isStepped=true; p.maxValue=1.0f;
                    p.defaultValue=1.0f; break;
            case 2: p.label="Cutoff"; p.maxValue=1.0f; p.defaultValue=1.0f; break;
            case 3: p.label="Reson";  p.maxValue=1.0f; break;
            case 4: p.label="Drive";  p.maxValue=1.0f; break;
            case 5: p.label="Env>Ct"; p.minValue=-1.0f; p.maxValue=1.0f; break;
            default: break;
            }
            return p;
        }

        if (!m->hasInternalAmp() && index >= ampOff && index < ampOff + kAmpSlots)
        {
            const int as = index - ampOff;
            ParamSpec p;
            p.sectionIndex = kAmpSecIdx;
            p.id           = kAmpIds[as];
            switch (as)
            {
            case 0: p.label="Level";   p.maxValue=2.0f; p.defaultValue=1.0f;
                    p.role=ParamSpec::Role::Level; break;
            case 1: p.label="Pan";     p.minValue=-1.0f; p.maxValue=1.0f;
                    p.role=ParamSpec::Role::Pan; break;
            case 2: p.label="Gate";    p.isStepped=true; p.maxValue=1.0f; break;
            case 3: p.label="Attack";  p.maxValue=1000.0f; p.defaultValue=1.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Attack; break;
            case 4: p.label="Hold";    p.maxValue=1000.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Hold; break;
            case 5: p.label="Decay";   p.maxValue=2000.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Decay; break;
            case 6: p.label="Sustain"; p.maxValue=1.0f; p.defaultValue=1.0f;
                    p.role=ParamSpec::Role::Sustain; break;
            case 7: p.label="Release"; p.maxValue=2000.0f; p.defaultValue=10.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Release; break;
            default: break;
            }
            return p;
        }

        return {};
    }

    int LockstepProcessor::numSections(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return 0;
        return machines_[static_cast<std::size_t>(track)]->numSections();
    }

    // Computes SectionInfo including firstSlot and pageCount from the machine's ParamSpec list.
    SectionInfo LockstepProcessor::section(int track, int sectionIndex) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};
        auto* m = machines_[static_cast<std::size_t>(track)].get();

        const int mnp    = m->numParams();
        const int ampOff = mnp + (m->hasInternalFilter() ? 0 : kFltrSlots);

        // Post-machine FLTR block owns canonical section 2 (when machine has no internal filter).
        if (sectionIndex == kFltrSecIdx && !m->hasInternalFilter())
        {
            return { "FLTR",
                     mnp,
                     (kFltrSlots + kParamsPerPage - 1) / kParamsPerPage,
                     -1 };
        }

        // Post-machine AMP block owns canonical section 3 (when machine has no internal amp).
        if (sectionIndex == kAmpSecIdx && !m->hasInternalAmp())
        {
            return { "AMP",
                     ampOff,
                     (kAmpSlots + kParamsPerPage - 1) / kParamsPerPage,
                     -1 };
        }

        SectionInfo info = m->section(sectionIndex);
        // Compute firstSlot and slotCount from the ParamSpec list so callers
        // don't have to query paramSpec() themselves.
        info.firstSlot = -1;
        int count = 0;
        const int np = m->numParams();
        for (int i = 0; i < np; ++i)
        {
            if (m->paramSpec(i).sectionIndex == sectionIndex)
            {
                if (info.firstSlot < 0)
                    info.firstSlot = i;
                ++count;
            }
        }
        info.pageCount = (count + kParamsPerPage - 1) / kParamsPerPage;
        if (info.firstSlot >= 0 && info.pageCount < 1) info.pageCount = 1;
        return info;
    }

    juce::String LockstepProcessor::idForSlot(int track, int index) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};
        const auto ti  = static_cast<std::size_t>(track);
        auto*      m   = machines_[ti].get();
        const int  mnp = m->numParams();
        if (index < mnp)
            return m->idForSlot(index);
        const int fltrOff = mnp;
        const int ampOff  = mnp + (m->hasInternalFilter() ? 0 : kFltrSlots);
        if (!m->hasInternalFilter())
        {
            const int fs = index - fltrOff;
            if (fs >= 0 && fs < kFltrSlots) return kFltrIds[fs];
        }
        if (!m->hasInternalAmp())
        {
            const int as = index - ampOff;
            if (as >= 0 && as < kAmpSlots) return kAmpIds[as];
        }
        return {};
    }

    int LockstepProcessor::slotForId(int track, const juce::String& id) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return -1;
        const auto ti = static_cast<std::size_t>(track);
        auto*      m  = machines_[ti].get();
        if (id.startsWith("lockstep.fltr.") && !m->hasInternalFilter())
        {
            const int mnp = m->numParams();
            for (int fs = 0; fs < kFltrSlots; ++fs)
                if (id == kFltrIds[fs]) return mnp + fs;
            return -1;
        }
        if (id.startsWith("lockstep.amp.") && !m->hasInternalAmp())
        {
            const int ampOff = m->numParams() + (m->hasInternalFilter() ? 0 : kFltrSlots);
            for (int as = 0; as < kAmpSlots; ++as)
                if (id == kAmpIds[as]) return ampOff + as;
            return -1;
        }
        return m->slotForId(id);
    }

    float LockstepProcessor::baseParamValue(int track, int slot) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0.0f;
        const auto ti  = static_cast<std::size_t>(track);
        const auto& bp = sequence().tracks[ti].baseParams;
        if (static_cast<std::size_t>(slot) < bp.size())
            return bp[static_cast<std::size_t>(slot)];
        auto*      m      = machines_[ti].get();
        const int  mnp    = m->numParams();
        const int  fltrOff = mnp;
        const int  ampOff  = mnp + (m->hasInternalFilter() ? 0 : kFltrSlots);
        if (!m->hasInternalFilter() && slot >= fltrOff && slot < fltrOff + kFltrSlots)
            return kit(static_cast<int>(ti)).fltrState.getSlot(slot - fltrOff);
        if (!m->hasInternalAmp() && slot >= ampOff && slot < ampOff + kAmpSlots)
            return kit(static_cast<int>(ti)).ampState.getSlot(slot - ampOff);
        return 0.0f;
    }

    void LockstepProcessor::triggerPreview(int poolIndex, int track)
    {
        previewReqTrack_.store(track, std::memory_order_relaxed);
        previewPoolIndex_.store(poolIndex, std::memory_order_release);
    }

    void LockstepProcessor::triggerNote(int track, int midiNote, int durationMs, int velocity, bool bypassEditorial)
    {
        kbdNoteTrack_.store(juce::jlimit(0, static_cast<int>(kNumTracks) - 1, track),
                            std::memory_order_relaxed);
        // Pack: bit 31=0 (keep positive), bits 30-24 = velocity (7-bit),
        //       bit 23 = bypassEditorial, bits 22-16 = note (7-bit), bits 15-0 = durationMs.
        const int packed = (juce::jlimit(1, 127, velocity) << 24)
                           | (bypassEditorial ? (1 << 23) : 0)
                           | (juce::jlimit(0, 127, midiNote) << 16)
                           | juce::jlimit(1, 0xFFFF, durationMs);
        kbdNoteReq_.store(packed, std::memory_order_release);
    }

    bool LockstepProcessor::hasTrackSlices(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto* m = machines_[static_cast<std::size_t>(track)].get();
        if (m == nullptr) return false;
        const auto* s = dynamic_cast<const ISliceable*>(m);
        return s != nullptr && s->hasSlices();
    }

    int LockstepProcessor::trackSliceCount(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0;
        const auto* m = machines_[static_cast<std::size_t>(track)].get();
        if (m == nullptr) return 0;
        const auto* s = dynamic_cast<const ISliceable*>(m);
        return s != nullptr ? s->numSlices() : 0;
    }

    void LockstepProcessor::setTrackEqualSlices(int track, int count)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto* m = machines_[static_cast<std::size_t>(track)].get();
        if (m == nullptr) return;
        auto* s = dynamic_cast<ISliceable*>(m);
        if (s != nullptr) s->setEqualSlices(count);
    }

    void LockstepProcessor::clearTrackSlices(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto* m = machines_[static_cast<std::size_t>(track)].get();
        if (m == nullptr) return;
        auto* s = dynamic_cast<ISliceable*>(m);
        if (s != nullptr) s->clearSlices();
    }

    void LockstepProcessor::recomputeSlicesIfNeeded(int track,
                                                     int slot,
                                                     const ParamFrame& baseParams)
    {
        const auto ti = static_cast<std::size_t>(track);
        auto* m = machines_[ti].get();
        auto* sl = dynamic_cast<ISliceable*>(m);
        if (sl == nullptr) return;

        const juce::String id = idForSlot(track, slot);
        const bool isSampleId  = (id == "slicer_sample_id");
        const bool isSliceSrc  = (id == "slicer_slice_src");
        const bool isSliceCount = (id == "slicer_slice_count");

        if (!isSampleId && !isSliceSrc && !isSliceCount)
            return;

        const int srcSlot   = slotForId(track, "slicer_slice_src");
        const int countSlot = slotForId(track, "slicer_slice_count");
        if (srcSlot < 0 || countSlot < 0) return;

        const int src   = static_cast<int>(std::round(
            baseParams[static_cast<std::size_t>(srcSlot)]));
        const int count = static_cast<int>(std::round(
            baseParams[static_cast<std::size_t>(countSlot)]));

        if (src == 0)
        {
            sl->setEqualSlices(count);
        }
        else
        {
            sl->detectTransientSlices(count);
        }
    }

    int LockstepProcessor::saveTrackToSoundPool(int track, const std::string& name)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return -1;
        const auto ti = static_cast<std::size_t>(track);
        const auto& partTrack = activePart().tracks[ti];

        SoundEntry entry;
        entry.name           = name.empty() ? "Sound" : name;
        entry.machineId      = partTrack.machineId;
        entry.baseParams     = partTrack.baseParams;
        entry.destinationId  = partTrack.destinationId;

        // Extract sample pool index from the first slot of baseParams (sampler tracks).
        if (!machines_[ti]->isMidiOut() && !partTrack.baseParams.empty())
            entry.samplePoolIndex = static_cast<int>(partTrack.baseParams[0]);

        return project_.soundPool.push(std::move(entry));
    }

    void LockstepProcessor::liveSwapTrackSound(int track, int poolIndex)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto* e = project_.soundPool.get(poolIndex);
        if (e == nullptr) return;
        const auto ti = static_cast<std::size_t>(track);
        if (e->machineId != activePart().tracks[ti].machineId) return;
        // Write directly to sequence track — same pattern as writeParam().
        // Audio thread picks up the new baseParams on the next processBlock.
        sequence().tracks[ti].baseParams = e->baseParams;
    }

    void LockstepProcessor::clearLiveSwap(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto ti = static_cast<std::size_t>(track);
        sequence().tracks[ti].baseParams = activePart().tracks[ti].baseParams;
    }

    bool LockstepProcessor::recallSoundFromPool(int track, int entryIndex)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto* e = project_.soundPool.get(entryIndex);
        if (e == nullptr) return false;

        const auto ti = static_cast<std::size_t>(track);
        auto& partTrack = activePart().tracks[ti];

        // Only apply if machine types match to avoid mismatched param frames.
        if (partTrack.machineId != e->machineId) return false;

        partTrack.baseParams    = e->baseParams;
        partTrack.destinationId = e->destinationId;

        // Sync the sequence track's base params so the audio thread picks it up.
        sequence().tracks[ti].baseParams = e->baseParams;
        return true;
    }

    void LockstepProcessor::setRetrigActive(int track, bool active, double ratePpq)
    {
        if (active)
        {
            retrigReqRatePpq_.store(ratePpq, std::memory_order_relaxed);
            retrigReqTrack_.store(
                juce::jlimit(0, static_cast<int>(kNumTracks) - 1, track),
                std::memory_order_release);
        }
        else
        {
            retrigReqTrack_.store(-2, std::memory_order_release);  // -2 = cancel signal
        }
    }

    juce::String LockstepProcessor::sampleShortName(int poolIndex) const
    {
        const auto* s = samplePool_.get(poolIndex);
        if (s == nullptr) return "(none)";
        return juce::File(juce::String(s->ref.path)).getFileNameWithoutExtension();
    }

    void LockstepProcessor::removeSample(int idx)
    {
        if (idx < 0 || idx >= samplePool_.size())
            return;
        const int newMax = samplePool_.size() - 2;  // max valid index after removal

        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const int sampleSlot = slotForId(t, "sample_id");
            if (sampleSlot < 0) continue;
            const auto slotSz = static_cast<std::size_t>(sampleSlot);

            auto remapPoolIdx = [idx, newMax](float cur) -> float {
                const int c = static_cast<int>(cur);
                if (c == idx)     return static_cast<float>(std::max(0, std::min(c, newMax)));
                if (c  > idx)     return static_cast<float>(c - 1);
                return cur;
            };

            // Remap Part baseParams across all banks.
            for (auto& bank : project_.banks)
            {
                for (auto& part : bank.parts)
                {
                    auto& bp = part.tracks[static_cast<std::size_t>(t)].baseParams;
                    if (slotSz < bp.size()) bp[slotSz] = remapPoolIdx(bp[slotSz]);
                }
                // Remap Pattern tracks (baseParams + step overrides).
                for (auto& pattern : bank.patterns)
                {
                    auto& track = pattern.sequence.tracks[static_cast<std::size_t>(t)];
                    if (slotSz < track.baseParams.size())
                        track.baseParams[slotSz] = remapPoolIdx(track.baseParams[slotSz]);
                    for (auto& step : track.steps)
                    {
                        if (!step.overrides.has(sampleSlot)) continue;
                        step.overrides.set(sampleSlot,
                            remapPoolIdx(step.overrides.get(sampleSlot, 0.0f)));
                    }
                }
            }
        }
        samplePool_.remove(idx);
    }

    void LockstepProcessor::swapSamples(int a, int b)
    {
        if (a == b || a < 0 || b < 0) return;

        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const int sampleSlot = slotForId(t, "sample_id");
            if (sampleSlot < 0) continue;
            const auto slotSz = static_cast<std::size_t>(sampleSlot);

            auto swapPoolIdx = [a, b](float cur) -> float {
                const int c = static_cast<int>(cur);
                if (c == a) return static_cast<float>(b);
                if (c == b) return static_cast<float>(a);
                return cur;
            };

            for (auto& bank : project_.banks)
            {
                for (auto& part : bank.parts)
                {
                    auto& bp = part.tracks[static_cast<std::size_t>(t)].baseParams;
                    if (slotSz < bp.size()) bp[slotSz] = swapPoolIdx(bp[slotSz]);
                }
                for (auto& pattern : bank.patterns)
                {
                    auto& track = pattern.sequence.tracks[static_cast<std::size_t>(t)];
                    if (slotSz < track.baseParams.size())
                        track.baseParams[slotSz] = swapPoolIdx(track.baseParams[slotSz]);
                    for (auto& step : track.steps)
                    {
                        if (!step.overrides.has(sampleSlot)) continue;
                        step.overrides.set(sampleSlot,
                            swapPoolIdx(step.overrides.get(sampleSlot, 0.0f)));
                    }
                }
            }
        }
        samplePool_.swap(a, b);
    }

    bool LockstepProcessor::relinkSample(int index, const juce::String& newPath)
    {
        return samplePool_.relink(index, newPath);
    }

    juce::AudioProcessorEditor* LockstepProcessor::createEditor()
    {
        return new LockstepEditor(*this);
    }

    // Install machines whose IDs match the active Part's PartTrack machineIds.
    // Called from setStateInformation (sequencer is stopped during state load).
    // Unsupported IDs receive a silent StubMachine that preserves data.
    static std::unique_ptr<IMachine> makeMachineForId(const std::string& id,
                                                       SamplePool& pool)
    {
        if (id == SamplerMachine::kMachineId || id.empty())
            return std::make_unique<SamplerMachine>(pool);
        if (id == MidiOutMachine::kMachineId)
            return std::make_unique<MidiOutMachine>();
        if (id == DrumSynthMachine::kMachineId)
            return std::make_unique<DrumSynthMachine>();
        if (id == FMMachine::kMachineId)
            return std::make_unique<FMMachine>();
        if (id == VAMachine::kMachineId)
            return std::make_unique<VAMachine>();
        if (id == SlicerMachine::kMachineId)
            return std::make_unique<SlicerMachine>(pool);
        // "lockstep.stub" is an explicitly-empty track (unknownId = "").
        // Any other unrecognised ID keeps its original id as the unknownId.
        if (id == StubMachine::kMachineId)
            return std::make_unique<StubMachine>("");
        return std::make_unique<StubMachine>(id);
    }

    // -------------------------------------------------------------------------
    // MGX.6 — machine selection

    static constexpr LockstepProcessor::MachineInfo kAvailableMachines[] = {
        { SamplerMachine::kMachineId,   "Sampler"    },
        { SlicerMachine::kMachineId,    "Slicer"     },
        { FMMachine::kMachineId,        "FM Synth"   },
        { VAMachine::kMachineId,        "VA Synth"   },
        { DrumSynthMachine::kMachineId, "Drum Synth" },
        { MidiOutMachine::kMachineId,   "MIDI Out"   },
    };

    int LockstepProcessor::numAvailableMachines() const
    {
        return static_cast<int>(std::size(kAvailableMachines));
    }

    LockstepProcessor::MachineInfo LockstepProcessor::availableMachineInfo(int idx) const
    {
        if (idx < 0 || idx >= static_cast<int>(std::size(kAvailableMachines)))
            return { "", "" };
        return kAvailableMachines[idx];
    }

    juce::String LockstepProcessor::getMachineId(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return {};
        const auto* m = machines_[static_cast<std::size_t>(track)].get();
        return m ? juce::String(m->machineId()) : juce::String{};
    }

    const char* LockstepProcessor::trackBadge(int track) const noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return "";
        const auto* m = machines_[static_cast<std::size_t>(track)].get();
        return m ? m->badge() : "";
    }

    std::unique_ptr<IMachine> LockstepProcessor::createMachineForId(const std::string& id)
    {
        return makeMachineForId(id, samplePool_);
    }

    int LockstepProcessor::slotForIdWithMachine(const IMachine& m, const juce::String& id) const
    {
        const int mnp = m.numParams();
        if (id.startsWith("lockstep.fltr.") && !m.hasInternalFilter())
        {
            for (int fs = 0; fs < kFltrSlots; ++fs)
                if (id == kFltrIds[fs]) return mnp + fs;
            return -1;
        }
        if (id.startsWith("lockstep.amp.") && !m.hasInternalAmp())
        {
            const int ampOff = mnp + (m.hasInternalFilter() ? 0 : kFltrSlots);
            for (int as = 0; as < kAmpSlots; ++as)
                if (id == kAmpIds[as]) return ampOff + as;
            return -1;
        }
        return m.slotForId(id);
    }

    int LockstepProcessor::numSlotsWithMachine(const IMachine& m) const
    {
        return m.numParams()
             + (m.hasInternalFilter() ? 0 : kFltrSlots)
             + (m.hasInternalAmp()    ? 0 : kAmpSlots);
    }

    ParamSpec LockstepProcessor::paramSpecWithMachine(const IMachine& m, int index) const
    {
        const int mnp     = m.numParams();
        const int fltrOff = mnp;
        const int ampOff  = mnp + (m.hasInternalFilter() ? 0 : kFltrSlots);

        if (index < mnp)
            return m.paramSpec(index);

        if (!m.hasInternalFilter() && index >= fltrOff && index < fltrOff + kFltrSlots)
        {
            const int fs = index - fltrOff;
            ParamSpec p;
            p.sectionIndex = kFltrSecIdx;
            p.id           = kFltrIds[fs];
            switch (fs)
            {
            case 0: p.label="Mode";   p.isStepped=true; p.maxValue=3.0f; break;
            case 1: p.label="Slope";  p.isStepped=true; p.maxValue=1.0f;
                    p.defaultValue=1.0f; break;
            case 2: p.label="Cutoff"; p.maxValue=1.0f; p.defaultValue=1.0f; break;
            case 3: p.label="Reson";  p.maxValue=1.0f; break;
            case 4: p.label="Drive";  p.maxValue=1.0f; break;
            case 5: p.label="Env>Ct"; p.minValue=-1.0f; p.maxValue=1.0f; break;
            default: break;
            }
            return p;
        }

        if (!m.hasInternalAmp() && index >= ampOff && index < ampOff + kAmpSlots)
        {
            const int as = index - ampOff;
            ParamSpec p;
            p.sectionIndex = kAmpSecIdx;
            p.id           = kAmpIds[as];
            switch (as)
            {
            case 0: p.label="Level";   p.maxValue=2.0f; p.defaultValue=1.0f;
                    p.role=ParamSpec::Role::Level; break;
            case 1: p.label="Pan";     p.minValue=-1.0f; p.maxValue=1.0f;
                    p.role=ParamSpec::Role::Pan; break;
            case 2: p.label="Gate";    p.isStepped=true; p.maxValue=1.0f; break;
            case 3: p.label="Attack";  p.maxValue=1000.0f; p.defaultValue=1.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Attack; break;
            case 4: p.label="Hold";    p.maxValue=1000.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Hold; break;
            case 5: p.label="Decay";   p.maxValue=2000.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Decay; break;
            case 6: p.label="Sustain"; p.maxValue=1.0f; p.defaultValue=1.0f;
                    p.role=ParamSpec::Role::Sustain; break;
            case 7: p.label="Release"; p.maxValue=2000.0f; p.defaultValue=10.0f;
                    p.unit=ParamSpec::Unit::Ms; p.role=ParamSpec::Role::Release; break;
            default: break;
            }
            return p;
        }

        return {};
    }

    int LockstepProcessor::activePatternPartRef() const
    {
        return activePattern().partRef;
    }

    // ── Phase 7 new-hierarchy methods ────────────────────────────────────────

    Phrase& LockstepProcessor::activePhrase(int t)
    {
        return arrangement_.activePhrase(t);
    }

    const Phrase& LockstepProcessor::activePhrase(int t) const
    {
        return arrangement_.activePhrase(t);
    }

    void LockstepProcessor::syncSequenceFromCurrentScene()
    {
        // Project the active Phrase + Kit of every track into the working buffer
        // (arrangement_.working). See Arrangement / HierarchyNav for the contract.
        arrangement_.syncWorkingFromActive();
    }

    void LockstepProcessor::reinstallMachinesFromActiveKit()
    {
        bool needsSuspend = false;
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto* m = machines_[t].get();
            if (m && m->machineId() != kit(static_cast<int>(t)).machineId)
                needsSuspend = true;
        }
        if (needsSuspend) suspendProcessing(true);
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto& desired = kit(static_cast<int>(t)).machineId;
            const auto* m = machines_[t].get();
            if (!m || m->machineId() != desired)
            {
                auto nm = makeMachineForId(desired, samplePool_);
                nm->prepare(getSampleRate(), getBlockSize());
                machines_[t] = std::move(nm);
            }
        }
        if (needsSuspend) suspendProcessing(false);
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            sequence().tracks[t].baseParams = kit(static_cast<int>(t)).baseParams;
            recomputeSlicesIfNeeded(static_cast<int>(t),
                                    slotForId(static_cast<int>(t), "slicer_sample_id"),
                                    sequence().tracks[t].baseParams);
        }
    }

    // The switching + write-back logic lives in Arrangement (tested in isolation);
    // the processor wrappers add only the machine (re)install where the Kit can
    // change. A Scene switch keeps the per-Song Kit, but its machine ids may still
    // differ from what is installed, so reinstall is guarded inside.
    void LockstepProcessor::setActiveSong(int pieceIdx)
    {
        if (pieceIdx < 0 || pieceIdx >= kNumSongs || pieceIdx == arrangement_.songIdx)
            return;
        arrangement_.setActiveSong(pieceIdx);
        reinstallMachinesFromActiveKit();
    }

    void LockstepProcessor::setActiveScene(int sectionIdx)
    {
        if (sectionIdx < 0 || sectionIdx >= kScenesPerSong || sectionIdx == arrangement_.sceneIdx)
            return;
        arrangement_.setActiveScene(sectionIdx);
        reinstallMachinesFromActiveKit();
    }

    void LockstepProcessor::setActiveSceneToFloor(int sectionIdx)
    {
        if (sectionIdx < 0 || sectionIdx >= kScenesPerSong)
            return;
        arrangement_.setActiveSceneToFloor(sectionIdx);
        reinstallMachinesFromActiveKit();
    }

    void LockstepProcessor::loadActivePosition(int songIdx, int sceneIdx)
    {
        arrangement_.loadPosition(songIdx, sceneIdx);
        reinstallMachinesFromActiveKit();
    }

    void LockstepProcessor::swapPhraseForTrack(int t, int phraseIdx)
    {
        arrangement_.swapPhraseForTrack(t, phraseIdx);
    }

    void LockstepProcessor::setGlobalPhrase(int focusedTrack, int phrase)
    {
        arrangement_.setGlobalPhrase(focusedTrack, phrase);
    }

    void LockstepProcessor::forceAllToPhrase(int phrase)
    {
        arrangement_.forceAllToPhrase(phrase);
    }

    void LockstepProcessor::resyncTrackToScene(int t)
    {
        arrangement_.resyncTrackToScene(t);
    }

    void LockstepProcessor::resyncAllToScene()
    {
        arrangement_.resyncAllToScene();
    }

    bool LockstepProcessor::isTrackDeviated(int t) const
    {
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return false;
        return arrangement_.deviated[static_cast<std::size_t>(t)];
    }

    int LockstepProcessor::deviationPhraseIdxForTrack(int t) const
    {
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return 0;
        return arrangement_.deviationPhraseIdx[static_cast<std::size_t>(t)];
    }

    void LockstepProcessor::commitSceneState()
    {
        arrangement_.commitSceneState();
    }

    // ── End Phase 7 new-hierarchy methods ────────────────────────────────────

    void LockstepProcessor::reinstallMachinesFromActivePart()
    {
        const auto& part = activePart();
        bool needsSuspend = false;
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto* m = machines_[t].get();
            if (m && m->machineId() != part.tracks[t].machineId)
                needsSuspend = true;
        }

        if (needsSuspend) suspendProcessing(true);
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto& desired = part.tracks[t].machineId;
            const auto* m       = machines_[t].get();
            if (!m || m->machineId() != desired)
            {
                auto nm = makeMachineForId(desired, samplePool_);
                nm->prepare(getSampleRate(), getBlockSize());
                machines_[t] = std::move(nm);
            }
        }
        if (needsSuspend) suspendProcessing(false);

        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            sequence().tracks[t].baseParams = part.tracks[t].baseParams;
            recomputeSlicesIfNeeded(static_cast<int>(t),
                                    slotForId(static_cast<int>(t), "slicer_sample_id"),
                                    sequence().tracks[t].baseParams);
        }
    }

    void LockstepProcessor::setTrackMachine(int track, const std::string& machineId)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto ti = static_cast<std::size_t>(track);

        auto nm = makeMachineForId(machineId, samplePool_);
        nm->prepare(getSampleRate(), getBlockSize());

        suspendProcessing(true);
        machines_[ti] = std::move(nm);
        suspendProcessing(false);

        auto& partTrack    = activePart().tracks[ti];
        partTrack.machineId = machineId;

        const int np = machines_[ti]->numParams();
        partTrack.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
        for (int s = 0; s < np; ++s)
            partTrack.baseParams[static_cast<std::size_t>(s)] =
                machines_[ti]->paramSpec(s).defaultValue;

        sequence().tracks[ti].baseParams = partTrack.baseParams;

        // Mirror into the new kit hierarchy (Phase 7).
        auto& k = kit(track);
        k.machineId  = machineId;
        k.baseParams = partTrack.baseParams;

        // Seed slices for slicer machine on first install so trigs fire immediately.
        recomputeSlicesIfNeeded(track,
                                slotForId(track, "slicer_sample_id"),
                                sequence().tracks[ti].baseParams);

        // VA Machine has a non-zero default sustain (0.8), so it sustains indefinitely
        // without a gate. Seed 1/8-note gate (≈250 ms at 120 BPM) on first install.
        auto& trigDef = sequence().tracks[ti].trigDefaults;
        if (machineId == VAMachine::kMachineId && trigDef.gateValue == MusicalGate::None)
            trigDef.gateValue = MusicalGate::G1_8;
    }

    void LockstepProcessor::setActivePatternPart(int partIdx)
    {
        if (partIdx < 0 || partIdx >= static_cast<int>(kPartsPerBank)) return;
        if (activePattern().partRef == partIdx) return;
        activePattern().partRef = partIdx;
        reinstallMachinesFromActivePart();
    }

    // -------------------------------------------------------------------------
    // Empty-slot gestural archetype

    bool LockstepProcessor::isPatternInitialised(int bankIdx, int patternIdx) const
    {
        if (bankIdx    < 0 || bankIdx    >= static_cast<int>(kNumBanks))        return false;
        if (patternIdx < 0 || patternIdx >= static_cast<int>(kPatternsPerBank)) return false;
        return project_.banks[static_cast<std::size_t>(bankIdx)]
                       .patterns[static_cast<std::size_t>(patternIdx)].initialised;
    }

    bool LockstepProcessor::isPartInitialised(int bankIdx, int partIdx) const
    {
        if (bankIdx  < 0 || bankIdx  >= static_cast<int>(kNumBanks))     return false;
        if (partIdx  < 0 || partIdx  >= static_cast<int>(kPartsPerBank)) return false;
        return project_.banks[static_cast<std::size_t>(bankIdx)]
                       .parts[static_cast<std::size_t>(partIdx)].initialised;
    }

    void LockstepProcessor::materialisePattern(int bankIdx, int patternIdx, bool copy)
    {
        if (bankIdx    < 0 || bankIdx    >= static_cast<int>(kNumBanks))        return;
        if (patternIdx < 0 || patternIdx >= static_cast<int>(kPatternsPerBank)) return;

        auto& bank    = project_.banks[static_cast<std::size_t>(bankIdx)];
        auto& pattern = bank.patterns[static_cast<std::size_t>(patternIdx)];

        if (copy)
            pattern = activePattern();  // full copy: sequence + partRef + mutes
        else
        {
            // Blank pattern: clear sequence, inherit current Part reference.
            pattern.sequence = Sequence{};
            pattern.patternMutes = {};
            pattern.partRef = activePattern().partRef;
        }
        pattern.initialised = true;

        // Sync baseParams from the referenced Part so the audio thread has valid data.
        const auto& part = bank.parts[static_cast<std::size_t>(pattern.partRef)];
        for (std::size_t t = 0; t < kNumTracks; ++t)
            pattern.sequence.tracks[t].baseParams = part.tracks[t].baseParams;
    }

    void LockstepProcessor::materialisePart(int bankIdx, int partIdx, bool copy)
    {
        if (bankIdx  < 0 || bankIdx  >= static_cast<int>(kNumBanks))     return;
        if (partIdx  < 0 || partIdx  >= static_cast<int>(kPartsPerBank)) return;

        auto& part = project_.banks[static_cast<std::size_t>(bankIdx)]
                                   .parts[static_cast<std::size_t>(partIdx)];

        if (copy)
        {
            part = activePart();
        }
        else
        {
            // Default Part: T0 = sampler with defaults, T1-15 = empty stubs.
            part = Part{};
            part.tracks[0].machineId = SamplerMachine::kMachineId;
            {
                SamplerMachine tmp(samplePool_);
                const int np = tmp.numParams();
                part.tracks[0].baseParams.assign(static_cast<std::size_t>(np), 0.0f);
                for (int s = 0; s < np; ++s)
                    part.tracks[0].baseParams[static_cast<std::size_t>(s)] =
                        tmp.paramSpec(s).defaultValue;
            }
            for (std::size_t t = 1; t < kNumTracks; ++t)
            {
                part.tracks[t].machineId = StubMachine::kMachineId;
                part.tracks[t].baseParams.clear();
            }
        }
        part.initialised = true;
    }

    void LockstepProcessor::copyPartTrack(int srcTrack, int dstTrack)
    {
        if (srcTrack < 0 || srcTrack >= static_cast<int>(kNumTracks)) return;
        if (dstTrack < 0 || dstTrack >= static_cast<int>(kNumTracks)) return;
        const auto si = static_cast<std::size_t>(srcTrack);
        const auto di = static_cast<std::size_t>(dstTrack);

        activePart().tracks[di] = activePart().tracks[si];
        sequence().tracks[di].baseParams = activePart().tracks[di].baseParams;

        // Install the copied machine.
        const std::string& id = activePart().tracks[di].machineId;
        auto nm = makeMachineForId(id, samplePool_);
        nm->prepare(getSampleRate(), getBlockSize());
        suspendProcessing(true);
        machines_[di] = std::move(nm);
        suspendProcessing(false);
    }

    bool LockstepProcessor::isTrackEmpty(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto ti = static_cast<std::size_t>(track);
        return machines_[ti]
            && std::string(machines_[ti]->machineId()) == StubMachine::kMachineId;
    }

    // -------------------------------------------------------------------------

    void LockstepProcessor::getStateInformation(juce::MemoryBlock& dest)
    {
        // Flush the working buffer into the active Phrase/Kit before serializing.
        // Live edits land in arrangement_.working and are otherwise only written
        // back on a scene/song/phrase switch, so a save without an intervening
        // switch would persist stale Phrases. (Stage 3 removes the working buffer.)
        arrangement_.writeBackWorkingToActive();
        PluginState::writeTo(dest, *this);
    }

    void LockstepProcessor::setStateInformation(const void* data, int sizeInBytes)
    {
        PluginState::readFrom(data, sizeInBytes, *this);

        // Project the freshly-loaded Songs into the working buffer. The serializer's
        // setActiveSong/Scene calls early-return when the saved active indices equal
        // the defaults (the common 0/0 case), so an explicit sync is required —
        // otherwise the working buffer would keep the empty constructor state.
        syncSequenceFromCurrentScene();

        // Push all MIDI-out config from a PartTrack to an already-installed machine.
        auto pushMidiOutConfig = [](MidiOutMachine* mom, const PartTrack& pt)
        {
            mom->setDestinationId(pt.destinationId);
            for (int ci = 0; ci < MidiOutMachine::kNumCCs
                          && ci < static_cast<int>(pt.midiCCNumbers.size()); ++ci)
                mom->setCCNumber(ci, pt.midiCCNumbers[static_cast<std::size_t>(ci)]);
            for (int ci = 0; ci < MidiOutMachine::kNumCCs
                          && ci < static_cast<int>(pt.midiCCLabels.size()); ++ci)
                mom->setCCLabel(ci, juce::String(pt.midiCCLabels[static_cast<std::size_t>(ci)]));
            // MF.8: restore hardware preset CC name table.
            if (!pt.midiPresetName.empty())
                mom->setCCNameTable(MidiDevicePresets::getTable(pt.midiPresetName));
            else
                mom->clearCCNameTable();
        };

        // Reinstall machines from the active Part's stored machineIds so that
        // any Part loaded from disk with an unknown machine ID gets StubMachine.
        const auto& part = activePart();
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto& pt = part.tracks[t];
            if (machines_[t] && machines_[t]->machineId() == pt.machineId)
            {
                if (machines_[t]->isMidiOut())
                    pushMidiOutConfig(static_cast<MidiOutMachine*>(machines_[t].get()), pt);
                continue;
            }
            machines_[t] = makeMachineForId(pt.machineId, samplePool_);
            if (machines_[t]->isMidiOut())
                pushMidiOutConfig(static_cast<MidiOutMachine*>(machines_[t].get()), pt);
            if (getSampleRate() > 0.0)
                machines_[t]->prepare(getSampleRate(), getBlockSize());
        }

        // Seed a default gate for VA Machine tracks that have none, so that a
        // track saved before this default existed doesn't sustain forever.
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            // NOTE: machineId() returns const char*; compare via std::string to
            // avoid a pointer-equality check that is always false.
            if (machines_[t]
                && std::string(machines_[t]->machineId()) == VAMachine::kMachineId)
            {
                auto& trigDef = sequence().tracks[t].trigDefaults;
                if (trigDef.gateValue == MusicalGate::None)
                    trigDef.gateValue = MusicalGate::G1_8;
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lockstep::LockstepProcessor();
}
