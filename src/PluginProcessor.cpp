#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ParameterIDs.h"
#include "core/StateResolver.h"
#include "core/TrigEvaluator.h"
#include "machine/MidiOutMachine.h"
#include "machine/SamplerMachine.h"
#include "machine/StubMachine.h"
#include "state/PluginState.h"
#include <algorithm>
#include <cmath>

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
    }

    LockstepProcessor::LockstepProcessor()
        : juce::AudioProcessor(BusesPropertiesAccessor::make()),
          apvts_(*this, nullptr, "Lockstep", createParameterLayout())
    {
        nextTriggerPpq_.fill(0.0);
        firedStepIdx_.fill(-1);

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

        for (auto& m : machines_)
            m = std::make_unique<SamplerMachine>(samplePool_);

        // Verify the state upgrade chain every time the plugin loads in debug mode.
       #if JUCE_DEBUG
        {
            juce::UnitTestRunner runner;
            runner.setAssertOnFailure(false);
            runner.runTestsInCategory("PluginState");
        }
       #endif

        // Seed every Part in every Bank with machine defaults, then sync to Patterns.
        for (auto& bank : project_.banks)
        {
            for (auto& part : bank.parts)
            {
                for (std::size_t t = 0; t < kNumTracks; ++t)
                {
                    const int np = machines_[t]->numParams();
                    part.tracks[t].baseParams.assign(static_cast<std::size_t>(np), 0.0f);
                    for (int s = 0; s < np; ++s)
                        part.tracks[t].baseParams[static_cast<std::size_t>(s)] =
                            machines_[t]->paramSpec(s).defaultValue;
                    // Track N defaults to sample index N so each track sounds distinct.
                    part.tracks[t].baseParams[0] = static_cast<float>(t);
                }
            }
            // Sync all patterns' Track.baseParams from their referenced Part.
            for (auto& pattern : bank.patterns)
            {
                const auto& part = bank.parts[static_cast<std::size_t>(pattern.partRef)];
                for (std::size_t t = 0; t < kNumTracks; ++t)
                    pattern.sequence.tracks[t].baseParams = part.tracks[t].baseParams;
            }
        }
    }

    LockstepProcessor::~LockstepProcessor() = default;

    void LockstepProcessor::setActivePattern(int bankIdx, int patternIdx)
    {
        if (bankIdx    < 0 || bankIdx    >= static_cast<int>(kNumBanks))        return;
        if (patternIdx < 0 || patternIdx >= static_cast<int>(kPatternsPerBank)) return;
        if (bankIdx == activeBankIdx_ && patternIdx == activePatternIdx_)        return;

        const int oldBankIdx   = activeBankIdx_;
        const int oldPartRef   = activePattern().partRef;
        activeBankIdx_    = bankIdx;
        activePatternIdx_ = patternIdx;
        const int newPartRef   = activePattern().partRef;

        // If the Part changed, sync Track.baseParams from the new Part so the
        // audio thread sees the new machine configuration immediately.
        if (bankIdx != oldBankIdx || newPartRef != oldPartRef)
        {
            const auto& part = activePart();
            for (std::size_t t = 0; t < kNumTracks; ++t)
                sequence().tracks[t].baseParams = part.tracks[t].baseParams;
        }
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
            pnf.samplesRemaining = -1;
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
            // Rising edge: record anchor PPQ so Auto mode starts from step 0.
            if (sequencerRunning && !wasInPluginPlaying_)
            {
                anchorPpq_ = clock_.ppqAtBlockStart();
                nextTriggerPpq_.fill(anchorPpq_);
            }
        }
        wasInPluginPlaying_ = clock_.inPluginPlaying();

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
                pnf.samplesRemaining = -1;
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
                base = activePart().tracks[ti].fltrState.getSlot(s - fltrOff);
            else if (!m->hasInternalAmp() && s >= ampOff && s < ampOff + kAmpSlots)
                base = activePart().tracks[ti].ampState.getSlot(s - ampOff);
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
                trackMidi[i].addEvent(
                    juce::MidiMessage::noteOff(1, pnf.noteNumber),
                    pnf.samplesRemaining);
                pnf.samplesRemaining = -1;
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
                    s.trig = true;
                    s.trigOverride.hasNote = true;
                    s.trigOverride.note    = note;
                }
            }
            else
            {
                // M5.8: Without record arm — write note override to held step, or
                // update the track default.
                if (editContext_.isActiveForEditing()
                    && editContext_.heldTrackIndex() == track)
                {
                    const int step = editContext_.heldStepIndex();
                    if (step >= 0 && step < kMaxStepsPerTrack)
                    {
                        auto& trig = sequence().tracks[ti]
                            .steps[static_cast<std::size_t>(step)].trigOverride;
                        trig.hasNote = true;
                        trig.note    = note;
                        editContext_.markParamWritten();
                    }
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

        // Route external note-off directly into the track's buffer.
        ccCtx.onNoteOff = [&trackMidi](int track, int sampleOffset, int midiNote)
        {
            trackMidi[static_cast<std::size_t>(track)].addEvent(
                juce::MidiMessage::noteOff(1, midiNote),
                sampleOffset);
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
                                   || activePattern().patternMutes[i];
                const bool soloed = trackSoloParams_[i]->load() >= 0.5f;
                if (muted || (anySoloed && !soloed)) continue;
                // Resolve against the held step so P-Locks written by the note-on
                // are included in the frame, falling back to -1 (base only).
                int resolveStep = -1;
                if (editContext_.isActiveForEditing()
                    && editContext_.heldTrackIndex() == static_cast<int>(i))
                    resolveStep = editContext_.heldStepIndex();
                auto frame = StateResolver::resolve(sequence().tracks[i], resolveStep);
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
                        TrackFltrState fltr = activePart().tracks[i].fltrState;
                        if (resolveStep >= 0 && resolveStep < kMaxStepsPerTrack)
                        {
                            const auto& ovr =
                                sequence().tracks[i].steps[static_cast<std::size_t>(resolveStep)].overrides;
                            for (int fs = 0; fs < TrackFltrState::kNumSlots; ++fs)
                                if (ovr.has(fltrOff + fs))
                                    fltr.setSlot(fs, ovr.get(fltrOff + fs, 0.0f));
                        }
                        trackFltrs_[i].processBlock(trackBuffers_[i], trackMidi[i], fltr,
                                                    numBlockSamples);
                    }

                    if (!mi->hasInternalAmp())
                    {
                        TrackAmpState amp = activePart().tracks[i].ampState;
                        if (resolveStep >= 0 && resolveStep < kMaxStepsPerTrack)
                        {
                            const auto& ovr =
                                sequence().tracks[i].steps[static_cast<std::size_t>(resolveStep)].overrides;
                            for (int as = 0; as < TrackAmpState::kNumSlots; ++as)
                                if (ovr.has(ampOff + as))
                                    amp.setSlot(as, ovr.get(ampOff + as, 0.0f));
                        }
                        trackAmps_[i].processBlock(trackBuffers_[i], trackMidi[i], amp,
                                                   numBlockSamples);
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

        // Determine the "grid boundary" track: the one with the longest cycle
        // in PPQ (trackLen * divPpq). The queued pattern switch fires when that
        // track wraps back to step 0.
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

                // Check whether the longest track fires step 0 in this block.
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
                                // Fire on message thread so setActivePattern() can safely
                                // update project data structures.
                                queuedPatternBankIdx_.store(-1, std::memory_order_relaxed);
                                queuedPatternPatIdx_ .store(-1, std::memory_order_release);
                                juce::MessageManager::callAsync(
                                    [this, qBankIdx, qPatIdx]
                                    {
                                        setActivePattern(qBankIdx, qPatIdx);
                                        // Advance chain: pop the next entry and queue it.
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
            // MD.6/MD.7: combined mute = global (APVTS) || pattern mute.
            const bool globalMuted  = trackMuteParams_[i]->load() >= 0.5f;
            const bool patternMuted = activePattern().patternMutes[i];
            const bool muted   = globalMuted || patternMuted;
            const bool soloed  = trackSoloParams_[i]->load() >= 0.5f;
            const bool silent  = muted || (anySoloed && !soloed);

            // 16th note = 0.25 PPQ; divider scales the grid coarser.
            const double divPpq = 0.25 * static_cast<double>(trackDiv <= 0 ? 1 : trackDiv);

            if (divPpq <= 0.0 || samplesPerPpq <= 0.0 || trackLen <= 0 || silent)
                continue;

            // If the cursor has fallen far behind (cold start, late join),
            // snap it to the step boundary at/before blockStart so we don't
            // burn CPU catching up sample-by-sample.
            if (nextTriggerPpq_[i] < blockStart - divPpq)
                nextTriggerPpq_[i] = std::floor(blockStart / divPpq) * divPpq;

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
                        step.trig
                        && TrigEvaluator::shouldFire(cond, i, stepNum, trackLen,
                                                      lastStepFired_[i],
                                                      fillActive_.load(std::memory_order_relaxed));
                    if (fired)
                    {
                        const double offset =
                            (nextTriggerPpq_[i] - blockStart) * samplesPerPpq;
                        triggerAt = std::max(0, static_cast<int>(offset));
                        firedStepIdx_[i] = stepIndex;  // ME.4: track last-fired step for FLTR P-Locks
                    }
                    lastStepFired_[i] = fired;
                }
                nextTriggerPpq_[i] += divPpq;
            }

            // Inject sequencer note-on with resolved trig fields (note/velocity/gate).
            if (triggerAt >= 0)
            {
                trigPulse_[i].store(1.0f, std::memory_order_relaxed);
                const auto trig = StateResolver::resolveTrig(track, stepIndex);

                // Cancel any stale pending note-off; the new note-on supersedes it
                // (the internal choke handles the audio fade).
                pendingNoteOffs_[i].samplesRemaining = -1;

                const auto vel = static_cast<juce::uint8>(
                    std::clamp(trig.velocity, 1, 127));
                trackMidi[i].addEvent(
                    juce::MidiMessage::noteOn(1, trig.note, vel),
                    triggerAt);

                if (trig.gateMs > 0.0f)
                {
                    const int gateSamples = static_cast<int>(
                        trig.gateMs * 0.001f * static_cast<float>(getSampleRate()));
                    const int noteOffAt = triggerAt + gateSamples;
                    if (noteOffAt < numBlockSamples)
                    {
                        trackMidi[i].addEvent(
                            juce::MidiMessage::noteOff(1, trig.note),
                            noteOffAt);
                    }
                    else
                    {
                        pendingNoteOffs_[i] = { noteOffAt - numBlockSamples, trig.note };
                    }
                }
            }

            auto frame = StateResolver::resolve(track, stepIndex);
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
                    TrackFltrState fltr = activePart().tracks[i].fltrState;
                    if (fsi >= 0 && fsi < kMaxStepsPerTrack)
                    {
                        const auto& ovr =
                            sequence().tracks[i].steps[static_cast<std::size_t>(fsi)].overrides;
                        for (int fs = 0; fs < TrackFltrState::kNumSlots; ++fs)
                            if (ovr.has(fltrOff + fs))
                                fltr.setSlot(fs, ovr.get(fltrOff + fs, 0.0f));
                    }
                    trackFltrs_[i].processBlock(trackBuffers_[i], trackMidi[i], fltr,
                                                numBlockSamples);
                }

                if (!mi->hasInternalAmp())
                {
                    TrackAmpState amp = activePart().tracks[i].ampState;
                    if (fsi >= 0 && fsi < kMaxStepsPerTrack)
                    {
                        const auto& ovr =
                            sequence().tracks[i].steps[static_cast<std::size_t>(fsi)].overrides;
                        for (int as = 0; as < TrackAmpState::kNumSlots; ++as)
                            if (ovr.has(ampOff + as))
                                amp.setSlot(as, ovr.get(ampOff + as, 0.0f));
                    }
                    trackAmps_[i].processBlock(trackBuffers_[i], trackMidi[i], amp,
                                               numBlockSamples);
                }

                trackPeak_[i].store(trackBuffers_[i].getMagnitude(0, numBlockSamples),
                                    std::memory_order_relaxed);
            }
        }

        // Sum per-track outputs to the main bus.
        for (std::size_t ti = 0; ti < kNumTracks; ++ti)
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom(ch, 0, trackBuffers_[ti], ch, 0, numBlockSamples);

        if (clock_.isMetronomeEnabled())
            metronome_.process(blockStart, blockEnd, samplesPerPpq, buffer);

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
        if (idForSlot(track, slot) == "sample_id")
        {
            const int poolSize = samplePool_.size();
            if (poolSize > 0)
                value = std::min(value, static_cast<float>(poolSize - 1));
            else
                value = 0.0f;
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
                        activePart().tracks[ti].fltrState.setSlot(dstSlot - dstMnp, value);
                    }
                    else if (!dm->hasInternalAmp())
                    {
                        activePart().tracks[ti].ampState.setSlot(dstSlot - dstAmpOff, value);
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
            }
            else if (!wm->hasInternalFilter() && slot < ampOff)
            {
                activePart().tracks[ti].fltrState.setSlot(slot - mnp, value);
            }
            else if (!wm->hasInternalAmp())
            {
                activePart().tracks[ti].ampState.setSlot(slot - ampOff, value);
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

    bool LockstepProcessor::getPatternMute(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        return activePattern().patternMutes[static_cast<std::size_t>(track)];
    }

    void LockstepProcessor::setPatternMute(int track, bool muted)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        activePattern().patternMutes[static_cast<std::size_t>(track)] = muted;
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
        s.overrides   = PLock{};
        s.trigOverride = TrigOverride{};
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

    int LockstepProcessor::checkpointDepth() const
    {
        const int key = activeBankIdx_ * kPatternsPerBank + activePatternIdx_;
        const auto it = checkpoints_.find(key);
        return (it != checkpoints_.end()) ? static_cast<int>(it->second.size()) : 0;
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
            return activePart().tracks[ti].fltrState.getSlot(slot - fltrOff);
        if (!m->hasInternalAmp() && slot >= ampOff && slot < ampOff + kAmpSlots)
            return activePart().tracks[ti].ampState.getSlot(slot - ampOff);
        return 0.0f;
    }

    void LockstepProcessor::triggerPreview(int poolIndex, int track)
    {
        previewReqTrack_.store(track, std::memory_order_relaxed);
        previewPoolIndex_.store(poolIndex, std::memory_order_release);
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
        return std::make_unique<StubMachine>(id);
    }

    void LockstepProcessor::getStateInformation(juce::MemoryBlock& dest)
    {
        PluginState::writeTo(dest, *this);
    }

    void LockstepProcessor::setStateInformation(const void* data, int sizeInBytes)
    {
        PluginState::readFrom(data, sizeInBytes, *this);

        // Reinstall machines from the active Part's stored machineIds so that
        // any Part loaded from disk with an unknown machine ID gets StubMachine.
        const auto& part = activePart();
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto& pt  = part.tracks[t];
            if (machines_[t] && machines_[t]->machineId() == pt.machineId)
            {
                // Machine type unchanged — still push MIDI-out config in case it changed.
                if (machines_[t]->isMidiOut())
                {
                    auto* mom = static_cast<MidiOutMachine*>(machines_[t].get());
                    mom->setDestinationId(pt.destinationId);
                    for (int ci = 0; ci < MidiOutMachine::kNumCCs
                                  && ci < static_cast<int>(pt.midiCCNumbers.size()); ++ci)
                        mom->setCCNumber(ci, pt.midiCCNumbers[static_cast<std::size_t>(ci)]);
                    for (int ci = 0; ci < MidiOutMachine::kNumCCs
                                  && ci < static_cast<int>(pt.midiCCLabels.size()); ++ci)
                        mom->setCCLabel(ci, juce::String(pt.midiCCLabels[static_cast<std::size_t>(ci)]));
                }
                continue;
            }
            machines_[t] = makeMachineForId(pt.machineId, samplePool_);
            if (machines_[t]->isMidiOut())
            {
                auto* mom = static_cast<MidiOutMachine*>(machines_[t].get());
                mom->setDestinationId(pt.destinationId);
                for (int ci = 0; ci < MidiOutMachine::kNumCCs
                              && ci < static_cast<int>(pt.midiCCNumbers.size()); ++ci)
                    mom->setCCNumber(ci, pt.midiCCNumbers[static_cast<std::size_t>(ci)]);
                for (int ci = 0; ci < MidiOutMachine::kNumCCs
                              && ci < static_cast<int>(pt.midiCCLabels.size()); ++ci)
                    mom->setCCLabel(ci, juce::String(pt.midiCCLabels[static_cast<std::size_t>(ci)]));
            }
            if (getSampleRate() > 0.0)
                machines_[t]->prepare(getSampleRate(), getBlockSize());
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lockstep::LockstepProcessor();
}
