#include "PluginProcessor.h"
#include "Parameters.h"
#include "ParameterIDs.h"
#include "core/AccentVel.h"
#include "core/Subdivision.h"
#include "core/SoundPoolOps.h"
#include "core/StateResolver.h"
#include "core/Swing.h"
#include "core/TrigEvaluator.h"
#include "core/OutputDest.h"
#include "core/RoutingGraph.h"
#include "dsp/SoftClip.h"
#include "machine/InputSource.h"
#include "machine/ThruMachine.h"
#include "machine/RecorderMachine.h"
#include "machine/LooperMachine.h"
#include "machine/StaticMachine.h"
#include "machine/PlayerMachine.h"
#include "machine/MidiDevicePresets.h"
#include "machine/DrumSynthMachine.h"
#include "machine/FMMachine.h"
#include "machine/MidiOutMachine.h"
#include "machine/SamplerMachine.h"
#include "machine/SlicerMachine.h"
#include "machine/SamplePlayingMachineBase.h"
#include "machine/VAMachine.h"
#include "machine/StubMachine.h"
#include "state/Hash.h"
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
                // 6.1: a stereo audio input on the main bus feeds the External
                // source (Thru / resampling, DESIGN §27). In standalone JUCE wires
                // the device input here; in a host it is the plugin's audio input.
                return BusesProperties()
                    .withInput("In", juce::AudioChannelSet::stereo(), true)
                    .withOutput("Out", juce::AudioChannelSet::stereo(), true);
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
                    const int rem = num - floorIdx * den;
                    int chosen = floorIdx;
                    if (rem != 0)
                    {
                        // bias the rounding: TopBias rounds up, BottomBias rounds down.
                        if (bias == NoteSelection::TopBias) chosen = floorIdx + 1;
                        else chosen = floorIdx;
                    }
                    // Deduplicate against already-picked indices (rare, defensive).
                    bool dup = false;
                    for (int k = 0; k < count; ++k)
                        if (idx[static_cast<std::size_t>(k)] == chosen)
                        {
                            dup = true;
                            break;
                        }
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
        for (auto& d : trackDensity_) d.store(1.0f, std::memory_order_relaxed);
        masterDensity_.store(0.0f, std::memory_order_relaxed);

        for (auto& s : mzSlots_)
            s.store(-1, std::memory_order_relaxed);

        syncModeParam_ = apvts_.getRawParameterValue(ParamIDs::syncMode);
        channelModeParam_ = apvts_.getRawParameterValue(ParamIDs::channelMode);

        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const auto ti = static_cast<std::size_t>(t);
            trackLengthParams_[ti] = apvts_.getRawParameterValue(ParamIDs::trackLength(t));
            trackDividerParams_[ti] = apvts_.getRawParameterValue(ParamIDs::trackDivider(t));
            trackMuteParams_[ti] = apvts_.getRawParameterValue(ParamIDs::trackMute(t));
            trackSoloParams_[ti] = apvts_.getRawParameterValue(ParamIDs::trackSolo(t));
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

        // Seed Song[0]: Track[0] kit = sampler with default params, T1-T15 = stub.
        {
            auto& p0 = arrangement_.songs[0];

            p0.tracks[0].kit.machineId = SamplerMachine::kMachineId;
            {
                const int np = machines_[0]->numParams();
                auto& kbp = p0.tracks[0].kit.baseParams;
                kbp.assign(static_cast<std::size_t>(np), 0.0f);
                for (int s = 0; s < np; ++s)
                    kbp[static_cast<std::size_t>(s)] = machines_[0]->paramSpec(s).defaultValue;
                kbp[0] = 0.0f;  // sample slot 0
            }

            for (std::size_t t = 1; t < kNumTracks; ++t)
            {
                p0.tracks[t].kit.machineId = StubMachine::kMachineId;
                p0.tracks[t].kit.baseParams.clear();
            }
            // Section[0]: all tracks active, coreTime 4/4, phrase indices = 0.
            // phraseIdx and activeMask default correctly (0s and trues).
            p0.scenes[0].initialised = true;  // only scene 1 populated by default
        }

        // Project the seeded Song[0] into the working buffer. Arrangement's own
        // constructor synced from an empty kit (it ran before this seed), so the
        // working buffer must be refreshed now that Song[0]'s kit is populated.
        arrangement_.syncWorkingFromActive();

        // Reserve the volatile REC buffers (DESIGN §28). Done before capturing the
        // default blob; writeSamplePool skips volatile entries, so the blob carries
        // none and newProject re-seeds them via finishStateLoad.
        seedVolatileSlots();

        // Capture the pristine default state so newProject() can reset to it later.
        PluginState::writeTo(defaultStateBlob_, *this);
        savedStateHash_ = stateHash();
    }

    LockstepProcessor::~LockstepProcessor() = default;

    void LockstepProcessor::setMZSlots(int slotOffset)
    {
        for (int i = 0; i < 4; ++i)
            mzSlots_[static_cast<std::size_t>(i)].store(slotOffset + i,
                                                        std::memory_order_relaxed);
    }

    void LockstepProcessor::startLearn(CCScope scope, int trackIndex,
                                       int slot, int mzPosition)
    {
        learnRequest_.scope = scope;
        learnRequest_.trackIndex = trackIndex;
        learnRequest_.slot = slot;
        learnRequest_.mzPosition = mzPosition;
        learnActive_.store(true, std::memory_order_release);
    }

    void LockstepProcessor::cancelLearn()
    {
        learnActive_.store(false, std::memory_order_release);
    }


    void LockstepProcessor::queueScene(int sectionIdx, bool toFloor)
    {
        if (sectionIdx < 0 || sectionIdx >= kScenesPerSong) return;
        // Pre-stage the scene switch on the message thread so the audio thread can
        // apply it at the bar boundary without allocation (DESIGN §38.4 / 8.17).
        stagedSwapReady_.store(false, std::memory_order_release);
        arrangement_.prepareSceneLaunch(sectionIdx, toFloor,
                                        stagedSwap_.working,
                                        stagedSwap_.deviated,
                                        stagedSwap_.deviationPhraseIdx,
                                        stagedSwap_.density,
                                        stagedSwap_.masterDensity);
        stagedSwap_.sceneIdx = sectionIdx;
        stagedSwap_.toFloor = toFloor;
        stagedSwapReady_.store(true, std::memory_order_release);
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


    WidgetMappingInfo LockstepProcessor::queryWidgetMapping(int slot, int mzPosition) const
    {
        for (const auto& m : ccMappingTable_.mappings())
        {
            if (m.scope == CCScope::Contextual && m.mzPosition == mzPosition)
                return { true, CCScope::Contextual, -1, m.slot, m.mzPosition, m.ccNumber };
            if ((m.scope == CCScope::Track || m.scope == CCScope::SelectedTrack) && m.slot == slot)
                return { true, m.scope, m.trackIndex, m.slot, -1, m.ccNumber };
        }
        return {};
    }

    void LockstepProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        preparedSampleRate_ = sampleRate;
        preparedBlockSize_ = samplesPerBlock;

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
            for (auto& bb : busInputBufs_)   // A2: inbound-bus accumulators
                bb.setSize(numOut, samplesPerBlock, false, true, false);
            for (auto& sb : sendBusBufs_)
                sb.setSize(numOut, samplesPerBlock, false, true, false);
            // 6.1: input capture + prior-block master tap (DESIGN §27).
            inputCapture_.setSize(numOut, samplesPerBlock, false, true, false);
            inputCapture_.clear();
            prevMasterBuf_.setSize(numOut, samplesPerBlock, false, true, false);
            prevMasterBuf_.clear();
            // 6.2: pre-size the reserved volatile REC buffers to their capacity so
            // a recorder can shrink-to-length on the audio thread without
            // reallocating (DESIGN §28). Capacity = kVolatileMaxSeconds at the
            // prepared rate.
            const int volatileCap =
                static_cast<int>(sampleRate * kVolatileMaxSeconds);
            samplePool_.prepareVolatile(sampleRate, numOut, volatileCap);
        }
        for (auto& choke : trackChokes_)
            choke.prepare(sampleRate, 1.5f);
        for (auto& fltr : trackFltrs_)
            fltr.prepare(sampleRate);
        for (auto& env : trackEnvs_)
            env.prepare(sampleRate);
        for (auto& ins : trackInserts_)
            for (auto& eff : ins)
                if (eff)
                {
                    eff->prepare(sampleRate, samplesPerBlock);
                    eff->reset();
                }
        for (auto& eff : masterInserts_)
            if (eff)
            {
                eff->prepare(sampleRate, samplesPerBlock);
                eff->reset();
            }
        for (auto& pnf : pendingNoteOffs_)
        {
            pnf.samplesRemaining = -1;
            pnf.openEnded = false;
        }
        firedStepIdx_.fill(-1);
        lastScheduledStepNum_.fill(-1);
        nextTriggerPpq_.fill(0.0);
        for (auto& pt : pendingTrigs_) pt.pending = false;

        morphFaderSmoothed_.reset(sampleRate, 0.05);  // 50 ms ramp
        morphFaderSmoothed_.setCurrentAndTargetValue(
            morphFaderTarget_.load(std::memory_order_relaxed));

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

    // 6.1: fill a track's scratch buffer from its machine's resolved input_source
    // before process() (DESIGN §27). trackBuffers_[track] is already cleared at
    // block start, so None — and, until A2, Track-N — simply leave it silent.
    void LockstepProcessor::fillTrackInput(int track, const ParamFrame& frame,
                                           int numSamples)
    {
        const auto t = static_cast<std::size_t>(track);
        auto* m = machines_[t].get();
        if (m == nullptr || m->isMidiOut()) return;  // MIDI-out has no audio input

        const int slot = m->slotForId(kInputSourceSlotId);
        if (slot < 0 || slot >= static_cast<int>(frame.size())) return;

        const InputSourceSel sel = decodeInputSource(frame[static_cast<std::size_t>(slot)]);
        auto& dst = trackBuffers_[t];

        const auto copyInto = [&](const juce::AudioBuffer<float>& src) {
            const int chans = std::min(dst.getNumChannels(), src.getNumChannels());
            const int n = std::min(numSamples, src.getNumSamples());
            for (int ch = 0; ch < chans; ++ch)
                dst.copyFrom(ch, 0, src, ch, 0, n);
        };

        switch (sel.kind)
        {
            case InputSourceKind::None:
                break;  // leave the cleared buffer
            case InputSourceKind::External:
                copyInto(inputCapture_);
                break;
            case InputSourceKind::Master:
                // #3 run-time feedback guard (second layer, mirroring routeForTrack's
                // dormancy): even though validInputSources() omits Master from the
                // rotary when this track's output reaches Master, a loaded/stale
                // project could still hold that selection. Leave the buffer silent
                // rather than close the Master → tap → output loop.
                if (!outputReachesMaster(track))
                    copyInto(prevMasterBuf_);  // one-block tap (DESIGN §27)
                break;
            case InputSourceKind::Track:
            {
                // Tap-fork (DESIGN §27): a read-only copy of track N's post-chain
                // output. computeOrder(routingEdges(), tapEdges()) guarantees N has
                // already run this block, so this is same-block / zero-latency. The
                // tap never sums into N's own output (no duplication). Self-tap and
                // MIDI-out sources resolve to silence (the cleared buffer).
                const int n = sel.track;
                if (n >= 0 && n < static_cast<int>(kNumTracks)
                    && n != track && !machines_[static_cast<std::size_t>(n)]->isMidiOut())
                    copyInto(trackBuffers_[static_cast<std::size_t>(n)]);
                break;
            }
        }

        // A2: mix in any tracks routed to this one as a bus (DESIGN §27). Topo
        // order guarantees those feeders have already deposited here. Silent for
        // a track that is no one's destination, so the all-Master case is a no-op.
        {
            const auto& bus = busInputBufs_[t];
            const int chans = std::min(dst.getNumChannels(), bus.getNumChannels());
            const int n = std::min(numSamples, bus.getNumSamples());
            for (int ch = 0; ch < chans; ++ch)
                dst.addFrom(ch, 0, bus, ch, 0, n);
        }
    }

    // A2: render one track's full audio chain into trackBuffers_[i]. Extracted
    // verbatim from the two transport paths so both share one implementation and
    // can be driven in routing (topological) order. resolveStep selects the step
    // whose FLTR/CHANNEL/ENV overrides apply; insert overrides always resolve
    // against firedStepIdx_ (matching the prior inline behaviour).
    void LockstepProcessor::processTrackChain(std::size_t i, const ParamFrame& frame,
                                              int resolveStep, bool fillActive,
                                              float faderNow, int numBlockSamples,
                                              juce::MidiBuffer& trackMidiI)
    {
        auto* mi = machines_[i].get();

        fillTrackInput(static_cast<int>(i), frame, numBlockSamples);
        // C2: deliver the block transport to tempo-aware machines before process().
        if (auto* ta = dynamic_cast<ITempoAware*>(mi))
            ta->setTransport(blockTransport_);
        mi->process(trackMidiI, frame, trackBuffers_[i]);

        const int mnp = mi->numParams();
        const int fltrOff = mnp;
        const int chanOff = mnp + kFltrSlots;
        const int envOff  = chanOff + kChannelSlots;

        // FILTER: always present on audio tracks (OFF mode = passthrough).
        {
            TrackFltrState fltr = kit(static_cast<int>(i)).fltrState;
            for (int fs = 0; fs < kFltrSlots; ++fs)
                fltr.setSlot(fs, morphBlend(section(), static_cast<int>(i),
                                            fltrOff + fs, fltr.getSlot(fs), faderNow));
            if (resolveStep >= 0 && resolveStep < kMaxStepsPerTrack)
            {
                const auto& step =
                    sequence().tracks[i].steps[static_cast<std::size_t>(resolveStep)];
                for (int fs = 0; fs < kFltrSlots; ++fs)
                    if (step.overrides.has(fltrOff + fs))
                        fltr.setSlot(fs, step.overrides.get(fltrOff + fs, 0.0f));
                if (fillActive)
                    for (int fs = 0; fs < kFltrSlots; ++fs)
                        if (step.fillOverrides.has(fltrOff + fs))
                            fltr.setSlot(fs, step.fillOverrides.get(fltrOff + fs, 0.0f));
            }
            trackFltrs_[i].processBlock(trackBuffers_[i], trackMidiI, fltr,
                                        numBlockSamples);
        }

        // CHANNEL: always present; resolve sends before applying level/pan.
        TrackChannelState ch = kit(static_cast<int>(i)).channelState;
        for (int cs = 0; cs < kChannelSlots; ++cs)
            ch.setSlot(cs, morphBlend(section(), static_cast<int>(i),
                                      chanOff + cs, ch.getSlot(cs), faderNow));
        if (resolveStep >= 0 && resolveStep < kMaxStepsPerTrack)
        {
            const auto& step =
                sequence().tracks[i].steps[static_cast<std::size_t>(resolveStep)];
            for (int cs = 0; cs < kChannelSlots; ++cs)
                if (step.overrides.has(chanOff + cs))
                    ch.setSlot(cs, step.overrides.get(chanOff + cs, 0.0f));
            if (fillActive)
                for (int cs = 0; cs < kChannelSlots; ++cs)
                    if (step.fillOverrides.has(chanOff + cs))
                        ch.setSlot(cs, step.fillOverrides.get(chanOff + cs, 0.0f));
        }
        const float trackSendA = ch.sendA;
        const float trackSendB = ch.sendB;

        // ENVELOPE: only for machines without internal amp.
        if (!mi->hasInternalAmp())
        {
            TrackEnvState env = kit(static_cast<int>(i)).envState;
            for (int es = 0; es < kEnvSlots; ++es)
                env.setSlot(es, morphBlend(section(), static_cast<int>(i),
                                           envOff + es, env.getSlot(es), faderNow));
            if (resolveStep >= 0 && resolveStep < kMaxStepsPerTrack)
            {
                const auto& step =
                    sequence().tracks[i].steps[static_cast<std::size_t>(resolveStep)];
                for (int es = 0; es < kEnvSlots; ++es)
                    if (step.overrides.has(envOff + es))
                        env.setSlot(es, step.overrides.get(envOff + es, 0.0f));
                if (fillActive)
                    for (int es = 0; es < kEnvSlots; ++es)
                        if (step.fillOverrides.has(envOff + es))
                            env.setSlot(es, step.fillOverrides.get(envOff + es, 0.0f));
            }
            const bool envWasIdle = trackEnvs_[i].isIdle();
            trackEnvs_[i].processBlock(trackBuffers_[i], trackMidiI, env,
                                       numBlockSamples);
            if (!envWasIdle && trackEnvs_[i].isIdle())
                mi->reset();
        }

        // Apply CHANNEL level/pan to the post-filter/envelope signal.
        applyChannel(trackBuffers_[i], ch, numBlockSamples);

        // 6.5: post-machine insert chain — runs for all machines.
        for (int ins = 0; ins < 2; ++ins)
        {
            auto* eff = trackInserts_[i][static_cast<std::size_t>(ins)].get();
            if (!eff) continue;
            const auto& kitIns = kit(static_cast<int>(i)).inserts[static_cast<std::size_t>(ins)];
            if (kitIns.bypass) continue;
            const int insOff = insertParamOffset(static_cast<int>(i), ins);
            const int insnp = eff->numParams();
            ParamFrame fxFrame(static_cast<std::size_t>(insnp));
            for (int p = 0; p < insnp; ++p)
            {
                const float base = static_cast<std::size_t>(p) < kitIns.baseParams.size()
                                       ? kitIns.baseParams[static_cast<std::size_t>(p)]
                                       : eff->paramSpec(p).defaultValue;
                float resolved = morphBlend(section(), static_cast<int>(i),
                                            insOff + p, base, faderNow);
                if (firedStepIdx_[i] >= 0)
                {
                    const auto& st = sequence().tracks[i].steps[static_cast<std::size_t>(firedStepIdx_[i])];
                    resolved = st.overrides.get(insOff + p, resolved);
                }
                fxFrame[static_cast<std::size_t>(p)] = resolved;
            }
            eff->process(trackBuffers_[i], numBlockSamples, fxFrame);
        }

        // 8.26: post-insert, post-level send taps. MIDI-out tracks skipped by caller.
        {
            const int numTrCh = trackBuffers_[i].getNumChannels();
            if (trackSendA > 0.0f)
            {
                const int numCh = std::min(sendBusBufs_[0].getNumChannels(), numTrCh);
                for (int bch = 0; bch < numCh; ++bch)
                    sendBusBufs_[0].addFrom(bch, 0, trackBuffers_[i], bch, 0,
                                            numBlockSamples, trackSendA);
            }
            if (trackSendB > 0.0f)
            {
                const int numCh = std::min(sendBusBufs_[1].getNumChannels(), numTrCh);
                for (int bch = 0; bch < numCh; ++bch)
                    sendBusBufs_[1].addFrom(bch, 0, trackBuffers_[i], bch, 0,
                                            numBlockSamples, trackSendB);
            }
        }

        // No per-track soft clip: the only structural clip is the master output
        // stage (gain-staging is master-only, DESIGN). Tracks and buses stay
        // linear with float headroom so a clean machine (e.g. FM) reaches the
        // master uncoloured; machine-internal character saturation is unaffected.

        trackPeak_[i].store(trackBuffers_[i].getMagnitude(0, numBlockSamples),
                            std::memory_order_relaxed);

        // D: per-track stem tap — post-fader, post-FX (un-clipped); before the
        // track is summed/routed onward. No-op unless this stem is armed.
        stemRecorders_[i].writeBlock(trackBuffers_[i], numBlockSamples);
    }

    // A2: validated routing decision for a track from its CHANNEL "Out" base
    // value (DESIGN §27). Per-step P-locks of Out are intentionally ignored so
    // the routing graph is stable across the block.
    LockstepProcessor::TrackRoute LockstepProcessor::routeForTrack(int track) const
    {
        const auto t = static_cast<std::size_t>(track);
        if (machines_[t]->isMidiOut())
            return { Route::Off, -1 };  // no audio to route
        const auto sel = decodeOutputDest(kit(track).channelState.out);
        switch (sel.kind)
        {
            case OutputDestKind::Off:    return { Route::Off, -1 };
            case OutputDestKind::Master: return { Route::Master, -1 };
            case OutputDestKind::Track:
                // Bus target must currently be an input-aware pass-through machine
                // (declares input_source). If not — e.g. the target was swapped to
                // a synth/stub after the edge was made — the edge goes DORMANT:
                // fall back to Master (audio-safe, no black hole) and revive
                // automatically if the target becomes a bus again. The stored Out
                // value is left untouched.
                if (sel.track >= 0 && sel.track < static_cast<int>(kNumTracks)
                    && sel.track != track
                    && !machines_[static_cast<std::size_t>(sel.track)]->isMidiOut()
                    && slotForId(sel.track, kInputSourceSlotId) >= 0)
                    return { Route::Bus, sel.track };
                return { Route::Master, -1 };
        }
        return { Route::Master, -1 };
    }

    LockstepProcessor::RouteReject
    LockstepProcessor::validateOutEdit(int from, float value) const
    {
        const auto sel = decodeOutputDest(value);
        if (sel.kind != OutputDestKind::Track) return RouteReject::None;  // Master/Off ok
        const int to = sel.track;
        if (to == from) return RouteReject::Self;
        if (to < 0 || to >= static_cast<int>(kNumTracks)) return RouteReject::None;
        // The target must be a bus-capable machine: input-aware + has audio.
        auto* tm = machines_[static_cast<std::size_t>(to)].get();
        if (tm == nullptr || tm->isMidiOut() || slotForId(to, kInputSourceSlotId) < 0)
            return RouteReject::NoAudioInput;
        if (wouldRoutingCycle(from, to)) return RouteReject::Cycle;
        return RouteReject::None;
    }

    std::vector<float> LockstepProcessor::validOutTargets(int fromTrack) const
    {
        std::vector<float> targets;
        targets.push_back(encodeOutputDest(OutputDestKind::Off));
        targets.push_back(encodeOutputDest(OutputDestKind::Master));
        for (int k = 0; k < static_cast<int>(kNumTracks); ++k)
        {
            const float enc = encodeOutputDest(OutputDestKind::Track, k);
            if (validateOutEdit(fromTrack, enc) == RouteReject::None)
                targets.push_back(enc);
        }
        // Keep the current stored dest representable even if it is no longer a
        // valid target (dormant after a machine swap).
        if (fromTrack >= 0 && fromTrack < static_cast<int>(kNumTracks))
        {
            const float cur = kit(fromTrack).channelState.out;
            if (std::find(targets.begin(), targets.end(), cur) == targets.end())
                targets.push_back(cur);
        }
        return targets;
    }

    // #3 feedback guard: walk track `from`'s functional CHANNEL-Out chain and
    // report whether it reaches the Master sum. Master and Off both collapse to
    // -1 in routingEdges(), so we use routeForTrack() per hop to tell them apart.
    // The chain is a single-out functional graph (out-degree ≤ 1), so a guard
    // bound of kNumTracks hops is sufficient even if a stale cycle exists.
    bool LockstepProcessor::outputReachesMaster(int from) const
    {
        int cur = from;
        for (std::size_t guard = 0; guard <= kNumTracks; ++guard)
        {
            if (cur < 0 || cur >= static_cast<int>(kNumTracks)) return false;
            const auto r = routeForTrack(cur);
            switch (r.route)
            {
                case Route::Master: return true;
                case Route::Off:    return false;
                case Route::Bus:    cur = r.busTrack; break;
            }
        }
        return false;  // cycle without reaching Master — treat as not-Master
    }

    std::vector<float> LockstepProcessor::validInputSources(int track) const
    {
        std::vector<float> out;
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return out;
        // None and External never feed back (External is outside audio).
        out.push_back(encodeInputSource(InputSourceKind::None));
        out.push_back(encodeInputSource(InputSourceKind::External));
        // Master tap is safe only if this track's own output does NOT reach
        // Master — otherwise output → Master → tap → output is a feedback loop.
        if (!outputReachesMaster(track))
            out.push_back(encodeInputSource(InputSourceKind::Master));
        // Track taps: any audio track but self that does not close a cycle in the
        // mix+tap union (reuses the same hasCycle the topo-sort/refusal uses).
        for (int k = 0; k < static_cast<int>(kNumTracks); ++k)
        {
            if (k == track) continue;
            if (machines_[static_cast<std::size_t>(k)]->isMidiOut()) continue;
            auto tap = tapEdges();
            tap[static_cast<std::size_t>(track)] = k;
            if (routing::hasCycle(routingEdges(), tap)) continue;
            out.push_back(encodeInputSource(InputSourceKind::Track, k));
        }
        // Keep the current stored selection representable even if now unsafe
        // (e.g. loaded from disk, or made stale by a later routing change).
        const int slot = slotForId(track, kInputSourceSlotId);
        if (slot >= 0)
        {
            const auto& bp = kit(track).baseParams;
            const float cur = (static_cast<std::size_t>(slot) < bp.size())
                                  ? bp[static_cast<std::size_t>(slot)] : 0.0f;
            if (std::find(out.begin(), out.end(), cur) == out.end())
                out.push_back(cur);
        }
        return out;
    }

    // A2: the block's bus-edge array for the topological sort — dest[i] is the
    // audio track i feeds, or -1 for Master / Off.
    std::array<int, kNumTracks> LockstepProcessor::routingEdges() const
    {
        std::array<int, kNumTracks> dest{};
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const auto r = routeForTrack(static_cast<int>(i));
            dest[i] = (r.route == Route::Bus) ? r.busTrack : -1;
        }
        return dest;
    }

    int LockstepProcessor::tapSourceForTrack(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return -1;
        const auto* m = machines_[static_cast<std::size_t>(track)].get();
        if (m == nullptr || m->isMidiOut()) return -1;
        const int slot = m->slotForId(kInputSourceSlotId);
        if (slot < 0) return -1;
        const auto& bp = sequence().tracks[static_cast<std::size_t>(track)].baseParams;
        if (slot >= static_cast<int>(bp.size())) return -1;
        const InputSourceSel sel = decodeInputSource(bp[static_cast<std::size_t>(slot)]);
        if (sel.kind != InputSourceKind::Track) return -1;
        if (sel.track < 0 || sel.track >= static_cast<int>(kNumTracks) || sel.track == track)
            return -1;
        return sel.track;
    }

    std::array<int, kNumTracks> LockstepProcessor::tapEdges() const
    {
        std::array<int, kNumTracks> tapSrc{};
        for (std::size_t i = 0; i < kNumTracks; ++i)
            tapSrc[i] = tapSourceForTrack(static_cast<int>(i));
        return tapSrc;
    }

    bool LockstepProcessor::wouldRoutingCycle(int from, int toTrack) const
    {
        const auto edges = routingEdges();
        return routing::wouldCreateCycle(edges, from, toTrack);
    }

    void LockstepProcessor::depositToBus(std::size_t track, int numBlockSamples)
    {
        const auto r = routeForTrack(static_cast<int>(track));
        if (r.route != Route::Bus) return;
        auto& bus = busInputBufs_[static_cast<std::size_t>(r.busTrack)];
        const auto& src = trackBuffers_[track];
        const int chans = std::min(bus.getNumChannels(), src.getNumChannels());
        for (int ch = 0; ch < chans; ++ch)
            bus.addFrom(ch, 0, src, ch, 0, numBlockSamples);
    }

    void LockstepProcessor::sumRoutedToMaster(juce::AudioBuffer<float>& buffer,
                                              int numBlockSamples)
    {
        for (std::size_t ti = 0; ti < kNumTracks; ++ti)
        {
            if (routeForTrack(static_cast<int>(ti)).route != Route::Master) continue;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom(ch, 0, trackBuffers_[ti], ch, 0, numBlockSamples);
        }
    }

    // 6.1: cache the final master output so a track sourcing Master reads the
    // prior block (the one sanctioned feedback-free tap, DESIGN §27).
    void LockstepProcessor::cachePrevMaster(const juce::AudioBuffer<float>& buf,
                                            int numSamples)
    {
        const int chans = std::min(prevMasterBuf_.getNumChannels(), buf.getNumChannels());
        const int n = std::min(numSamples, prevMasterBuf_.getNumSamples());
        prevMasterBuf_.clear();
        for (int ch = 0; ch < chans; ++ch)
            prevMasterBuf_.copyFrom(ch, 0, buf, ch, 0, n);
    }

    // 5.6: clear one-shot spent state so spent steps fire again (DESIGN §30).
    void LockstepProcessor::rearmOneShots(int track)
    {
        if (track < 0)
        {
            for (auto& t : oneShotSpent_)
                t.fill(false);
        }
        else if (track < static_cast<int>(kNumTracks))
        {
            oneShotSpent_[static_cast<std::size_t>(track)].fill(false);
        }
    }

    bool LockstepProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
    {
        const auto& mainOut = layouts.getMainOutputChannelSet();
        if (mainOut != juce::AudioChannelSet::stereo() && mainOut != juce::AudioChannelSet::mono())
            return false;
        // 6.1: the main input may be stereo, mono, or disabled (no input host).
        const auto& mainIn = layouts.getMainInputChannelSet();
        return mainIn == juce::AudioChannelSet::stereo() || mainIn == juce::AudioChannelSet::mono()
               || mainIn == juce::AudioChannelSet::disabled();
    }

    void LockstepProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto totalIn = getTotalNumInputChannels();
        const auto totalOut = getTotalNumOutputChannels();

        // 6.1: snapshot the plugin audio input before we overwrite the shared
        // in/out buffer, so tracks with input_source = External can read it
        // (DESIGN §27). Buses with no input leave inputCapture_ silent.
        {
            const int numSamples = buffer.getNumSamples();
            const int capCh = std::min(inputCapture_.getNumChannels(), totalIn);
            inputCapture_.clear();
            for (int ch = 0; ch < capCh; ++ch)
                inputCapture_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        }

        for (int ch = totalIn; ch < totalOut; ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
        buffer.clear();

        // Clear per-track scratch buffers once per block.
        for (auto& tb : trackBuffers_)
            tb.clear();
        // A2: clear inbound-bus accumulators; tracks routed to a bus fill these.
        for (auto& bb : busInputBufs_)
            bb.clear();
        // 8.26: clear send buses at block start; they accumulate per-track taps below.
        for (auto& sb : sendBusBufs_)
            sb.clear();
        // 8.26: broadcast tempo to all installed effects (tempo-synced effects use this).
        {
            const double blockBpm = clock_.bpm();
            for (auto& ins : trackInserts_)
                for (auto& eff : ins)
                    if (eff) eff->setTimeInfo(blockBpm);
            for (auto& eff : masterInserts_)
                if (eff) eff->setTimeInfo(blockBpm);
            for (auto& eff : masterSends_)
                if (eff) eff->setTimeInfo(blockBpm);
        }

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
            midiClockIn.active = true;
            midiClockIn.ppqStart = mcBlock.ppqStart;
            midiClockIn.ppqEnd = mcBlock.ppqEnd;
            midiClockIn.bpm = mcBlock.bpm;
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
                // The step-detection window below is anchor-RELATIVE in Auto mode
                // (blockStart = ppqAtBlockStart - anchorPpq_), and the whole grid is
                // consumed in that frame (main scan, ppqJumped re-floor). So the
                // fresh cursors must be anchor-relative too: step 0 sits at relative
                // PPQ 0. Filling with the absolute anchorPpq_ put the grid in the
                // wrong frame, so a fresh start at a non-zero position (e.g. stop →
                // replay without a phase reset) left every cursor past blockEnd and
                // dropped trigs entirely.
                nextTriggerPpq_.fill(0.0);
            }
        }
        wasInPluginPlaying_ = clock_.inPluginPlaying();

        // 9.15: publish the focused track's current playhead step so the editor
        // repaints the sequencer grid exactly when it advances — driven by this
        // loop, the same one that fires the notes. Computed with the display's
        // musical step grid (subdivisionPpqFromIndex) and raw cumulative PPQ, so
        // the published step matches the one buildSurfaceModel renders.
        {
            // Fall back to track 0 when no track is focused. focusStepUi drives the
            // display vblank's playhead repaint (its only consumer); leaving it at
            // -1 when focusTrack_ < 0 froze the on-screen playhead until the user
            // touched a track. A new project loads focusTrack = -1 (the serialized
            // default), so a fresh project's playhead never advanced without input.
            const int ft = (focusTrack_ >= 0 && focusTrack_ < static_cast<int>(kNumTracks))
                               ? focusTrack_ : 0;
            int focusStep = -1;
            {
                const auto fi = static_cast<std::size_t>(ft);
                const int len = std::max(1, static_cast<int>(trackLengthParams_[fi]->load()));
                const int divIdx = std::clamp(static_cast<int>(trackDividerParams_[fi]->load()),
                                              kSubdivMin, kSubdivMax);
                const double divPpq = subdivisionPpqFromIndex(divIdx);
                if (divPpq > 0.0)
                {
                    const double cumPpq = clock_.cumulativePpq();
                    focusStep = static_cast<int>(
                        static_cast<std::int64_t>(cumPpq / divPpq) % len);
                }
            }
            focusStepUi_.store(focusStep, std::memory_order_relaxed);
        }

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
        // 5.6: re-arm one-shot trigs on transport (re)start — DESIGN §30.
        if (sequencerRunning && !wasSequencerRunning_)
            rearmOneShots(-1);
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
        const double ppqOffset = (mode == SyncMode::Auto) ? anchorPpq_ : 0.0;
        const double blockStart = clock_.ppqAtBlockStart() - ppqOffset;
        const double blockEnd = clock_.ppqAtBlockEnd() - ppqOffset;
        const double samplesPerPpq = clock_.samplesPerPpq();

        // C2: snapshot the transport for ITempoAware machines (Player/Looper/
        // Recorder). samplesPerBar = barPpq × samplesPerPpq; phase = absolute song
        // position in samples at block start (for looper grid phase-lock).
        blockTransport_.bpm = clock_.bpm();
        blockTransport_.sampleRate = getSampleRate();
        blockTransport_.samplesPerBar = effectiveTimeSig().barPpq() * samplesPerPpq;
        blockTransport_.transportPhaseSamples = clock_.ppqAtBlockStart() * samplesPerPpq;
        blockTransport_.running = sequencerRunning;

        // If the DAW looped or the user hit Reset, snap all per-track cursors
        // to the step boundary just at/before the new block start.
        if (clock_.ppqJumped())
        {
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                const int subdivIdx = std::clamp(static_cast<int>(trackDividerParams_[i]->load()),
                                                  kSubdivMin, kSubdivMax);
                const double divPpq = subdivisionPpqFromIndex(subdivIdx);
                if (divPpq > 0.0)
                    nextTriggerPpq_[i] = std::floor(blockStart / divPpq) * divPpq;
                lastStepFired_[i] = false;
            }
            for (auto& pnf : pendingNoteOffs_)
            {
                pnf.samplesRemaining = -1;
                pnf.openEnded = false;
            }
            for (auto& pt : pendingTrigs_) pt.pending = false;
            firedStepIdx_.fill(-1);
            lastScheduledStepNum_.fill(-1);
            metronome_.reset();
        }

        // Snapshot MZ slot mapping for audio-thread use.
        std::array<int, 4> mzSlotSnapshot;
        for (std::size_t i = 0; i < 4; ++i)
            mzSlotSnapshot[i] = mzSlots_[i].load(std::memory_order_relaxed);

        CCMidiContext ccCtx;
        ccCtx.table = &ccMappingTable_;
        ccCtx.focusTrack = focusTrack_;
        ccCtx.mzSlots = mzSlotSnapshot;
        ccCtx.channelMode = channelModeParam_
                                ? static_cast<ChannelMode>(static_cast<int>(channelModeParam_->load()))
                                : ChannelMode::Omni;
        ccCtx.getCurrentTrackValue = [this](int t, int s) -> float {
            const auto ti = static_cast<std::size_t>(t);
            const auto& trk = sequence().tracks[ti];
            auto* m = machines_[ti].get();
            const int mnp = m->numParams();
            const int fltrOff = mnp;
            const int chanOff = mnp + kFltrSlots;
            const int envOff  = chanOff + kChannelSlots;
            float base;
            if (static_cast<std::size_t>(s) < trk.baseParams.size())
                base = trk.baseParams[static_cast<std::size_t>(s)];
            else if (!m->isMidiOut() && s >= fltrOff && s < fltrOff + kFltrSlots)
                base = kit(static_cast<int>(ti)).fltrState.getSlot(s - fltrOff);
            else if (!m->isMidiOut() && s >= chanOff && s < chanOff + kChannelSlots)
                base = kit(static_cast<int>(ti)).channelState.getSlot(s - chanOff);
            else if (!m->isMidiOut() && !m->hasInternalAmp() && s >= envOff && s < envOff + kEnvSlots)
                base = kit(static_cast<int>(ti)).envState.getSlot(s - envOff);
            else
            {
                // 6.5: insert base params.
                int insOff = envOff + (m->hasInternalAmp() ? 0 : kEnvSlots);
                base = 0.0f;
                for (int ins = 0; ins < 2; ++ins)
                {
                    auto* eff = trackInserts_[ti][static_cast<std::size_t>(ins)].get();
                    if (!eff) continue;
                    const int np2 = eff->numParams();
                    if (s >= insOff && s < insOff + np2)
                    {
                        const auto& bp = kit(static_cast<int>(ti)).inserts[static_cast<std::size_t>(ins)].baseParams;
                        base = (static_cast<std::size_t>(s - insOff) < bp.size()) ? bp[static_cast<std::size_t>(s - insOff)] : 0.0f;
                        break;
                    }
                    insOff += np2;
                }
            }
            // When a step is held on this track, apply its P-Lock overlay.
            if (editContext_.isActiveForEditing() && editContext_.heldTrackIndex() == t)
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
        ccCtx.setCrossfaderValue = [this](float v) {
            // CC is calibrated top=A: invert before storing so f=0=A remains canonical.
            setMorphFader(1.0f - v);
        };
        if (learnActive_.load(std::memory_order_acquire))
        {
            ccCtx.onLearnCapture = [this](int ccNum) {
                CCMapping m;
                m.ccNumber = ccNum;
                m.scope = learnRequest_.scope;
                m.trackIndex = learnRequest_.trackIndex;
                m.slot = learnRequest_.slot;
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
                pnf.openEnded = false;
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
                          sequencerRunning, recArmed](int track, int sampleOffset, int midiNote, int velocity) {
            const auto ti = static_cast<std::size_t>(track);
            const int note = std::clamp(midiNote, 0, 127);

            if (recArmed && editContext_.isActiveForEditing() && editContext_.heldTrackIndex() == track)
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
            else if (recArmed && sequencerRunning && !editContext_.isActiveForEditing())
            {
                // M7.2: Quantize note-on to nearest step boundary, write trig.
                // Chord aggregation: multiple notes quantising to the same absolute step
                // number accumulate (same visit). A new absolute step number = a new visit;
                // in overwrite mode the step is cleared before the first note of the visit,
                // so each pass through the pattern replaces rather than piles up.
                const int subdivIdx2 = std::clamp(static_cast<int>(trackDividerParams_[ti]->load()),
                                                   kSubdivMin, kSubdivMax);
                const double divPpq = subdivisionPpqFromIndex(subdivIdx2);
                const int trackLen = static_cast<int>(trackLengthParams_[ti]->load());
                if (divPpq > 0.0 && trackLen > 0 && samplesPerPpq > 0.0)
                {
                    const double noteOnPpq = blockStart + static_cast<double>(sampleOffset) / samplesPerPpq;
                    const auto nearestNum = static_cast<std::int64_t>(
                        std::round(noteOnPpq / divPpq));
                    const int stepIdx = static_cast<int>(
                        ((nearestNum % static_cast<std::int64_t>(trackLen)) + trackLen) % trackLen);
                    auto& s = sequence().tracks[ti].steps[static_cast<std::size_t>(stepIdx)];
                    // New absolute step = start of a new visit.
                    // In overwrite mode (default), clear the step before its first note.
                    // In overdub mode, never clear — notes always accumulate.
                    if (nearestNum != lastRecordedStepNum_[ti])
                    {
                        lastRecordedStepNum_[ti] = nearestNum;
                        if (!clock_.isOverdubArmed())
                        {
                            s.trigOverride.noteCount = 0;
                            s.trigOverride.hasNoteVelocities = false;
                            s.microOffset = 0.0f;  // clear before residual capture below
                            s.condition = TrigCondition{};  // stale condition must not gate the new trig
                        }
                        // Capture sub-step timing residual (DESIGN §19.1). Measure the
                        // note-on position against the swung step location so that a
                        // consistently swung performance records near-zero residuals.
                        const bool isOdd = (nearestNum % 2) == 1;
                        const float rSongAll = song().swing;
                        const float rSongTrk = song().tracks[ti].swing;
                        const float rSceneAll = section().swing;
                        const float effSwg = effectiveSwing(rSongAll, rSongTrk, rSceneAll);
                        const float swingDelta = isOdd ? effSwg : 0.0f;
                        const float residual = static_cast<float>(
                                                   noteOnPpq / divPpq - static_cast<double>(nearestNum)) -
                                               swingDelta;
                        s.microOffset = std::clamp(residual, -0.5f, 0.5f);
                    }
                    s.trig = true;
                    if (s.trigOverride.noteCount < kMaxNotesPerStep)
                    {
                        // De-dup: skip if note already in chord.
                        bool already = false;
                        for (int n = 0; n < s.trigOverride.noteCount; ++n)
                            if (s.trigOverride.notes[static_cast<std::size_t>(n)] == note)
                            {
                                already = true;
                                break;
                            }
                        if (!already)
                        {
                            const auto idx = static_cast<std::size_t>(s.trigOverride.noteCount);
                            s.trigOverride.notes[idx] = note;
                            s.trigOverride.velocities[idx] = static_cast<uint8_t>(velocity);
                            s.trigOverride.hasNoteVelocities = true;
                            ++s.trigOverride.noteCount;
                        }
                    }
                    // MHZ.6.1: record note-on sample for gate capture on note-off.
                    const int64_t noteOnSample = totalSamplesProcessed_ + static_cast<int64_t>(sampleOffset);
                    realtimeNotes_[ti][static_cast<std::size_t>(midiNote)] = { stepIdx, noteOnSample };
                }
            }
            else
            {
                // M5.8: Without record arm — write note override to all held steps, or
                // update the track default.
                if (editContext_.isActiveForEditing() && editContext_.heldTrackIndex() == track && !editContext_.heldSteps().empty())
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
                                sequence().tracks[ti].steps[static_cast<std::size_t>(si)].trigOverride.noteCount = 0;
                        }
                        chordCapture_.active = true;
                        chordCapture_.gateStartSample = totalSamplesProcessed_ + sampleOffset;
                        chordCapture_.maxVelocity = 0;
                        chordCapture_.totalVelocity = 0;
                        chordCapture_.capturedCount = 0;
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
        ccCtx.onNoteOff = [this, &trackMidi, samplesPerPpq, recArmed, sequencerRunning](int track, int sampleOffset, int midiNote) {
            trackMidi[static_cast<std::size_t>(track)].addEvent(
                juce::MidiMessage::noteOff(1, midiNote),
                sampleOffset);

            // MHZ.6.1: finalise gate for realtime record path.
            if (recArmed && sequencerRunning && !editContext_.isActiveForEditing() && midiNote >= 0 && midiNote < 128)
            {
                const auto ti = static_cast<std::size_t>(track);
                auto& entry = realtimeNotes_[ti][static_cast<std::size_t>(midiNote)];
                if (entry.stepIdx >= 0)
                {
                    const int64_t noteOffSample =
                        totalSamplesProcessed_ + static_cast<int64_t>(sampleOffset);
                    const float gateMs =
                        static_cast<float>(noteOffSample - entry.noteOnSample) * 1000.0f / static_cast<float>(getSampleRate());
                    const double captureBpm = (samplesPerPpq > 0.0)
                                                  ? (getSampleRate() * 60.0 / samplesPerPpq)
                                                  : clock_.localBpm();
                    const MusicalGate g = nearestMusicalGate(std::max(1.0f, gateMs), captureBpm);
                    auto& trig = sequence().tracks[ti].steps[static_cast<std::size_t>(entry.stepIdx)].trigOverride;
                    // Max-gate rule: keep the longest gate among all notes in this step.
                    if (!trig.hasGate || static_cast<uint8_t>(g) > static_cast<uint8_t>(trig.gateValue))
                    {
                        trig.hasGate = true;
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
            const float gateMs = static_cast<float>(gateSamples) * 1000.0f / static_cast<float>(getSampleRate());
            const double captureBpm = (samplesPerPpq > 0.0)
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
                    auto& trig = sequence().tracks[ti].steps[static_cast<std::size_t>(si)].trigOverride;
                    trig.hasGate = true;
                    trig.gateValue = capturedGate;
                    if (meanVel > 0)
                    {
                        trig.hasVelocity = true;
                        trig.velocity = meanVel;
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

            previewActive_ = true;
            previewTrack_ = previewReqTrack_.load(std::memory_order_acquire);
            previewSampleIndex_ = newPreview;
            previewNoteOffRemaining_ = static_cast<int>(getSampleRate() * 0.4);
            previewNote_ = 60;
            trackMidi[static_cast<std::size_t>(previewTrack_)].addEvent(
                juce::MidiMessage::noteOn(1, previewNote_,
                                          static_cast<juce::uint8>(100)),
                0);
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
                previewActive_ = false;
            }
            else
            {
                previewNoteOffRemaining_ -= numBlockSamples;
            }
        }

        // MG.1 / poly: drain keyboard note commands from the UI thread.
        // Each note-on takes a live voice; gate notes (durationMs == 0) ring until
        // a matching note-off (pad release), fixed-duration notes auto-off after
        // their countdown. Non-bypass notes route through onNoteOn/onNoteOff so a
        // held chord both sounds and is captured (record + step-held chord paths);
        // bypass notes inject raw (LEVELS audition — no chord capture).
        {
            auto emitVoiceOff = [&](LiveVoice& v, int off) {
                if (v.bypass)
                    trackMidi[static_cast<std::size_t>(v.track)].addEvent(
                        juce::MidiMessage::noteOff(1, v.note), off);
                else
                    ccCtx.onNoteOff(v.track, off, v.note);
            };

            auto allocVoice = [&]() -> LiveVoice& {
                for (auto& v : liveVoices_)
                    if (!v.active) return v;
                // All voices busy → steal the first, releasing its note so the
                // synth does not leave it hanging.
                emitVoiceOff(liveVoices_[0], 0);
                liveVoices_[0].active = false;
                return liveVoices_[0];
            };

            int s1, n1, s2, n2;
            kbdFifo_.prepareToRead(kbdFifo_.getNumReady(), s1, n1, s2, n2);
            auto handle = [&](const KbdNoteCmd& c) {
                const int track = juce::jlimit(0, static_cast<int>(kNumTracks) - 1,
                                               static_cast<int>(c.track));
                // Per-track Scale stage (DESIGN §4.10): conform live play-in to the
                // key. applyScaleMode is deterministic, so a note-off recomputes the
                // same conformed pitch and matches the held voice. Filter → nullopt
                // drops the live note (no voice). bypassEditorial notes are raw.
                const ScaleMode sm = song().tracks[static_cast<std::size_t>(track)].kit.scaleMode;
                const auto conformed = (sm == ScaleMode::Off || c.bypassEditorial)
                    ? std::optional<int>(static_cast<int>(c.note))
                    : applyScaleMode(effectiveKeySig(), sm, static_cast<int>(c.note));
                const int playNote = conformed.value_or(static_cast<int>(c.note));
                // Release any voice already holding this (track, conformed note) —
                // both for a note-off and to retrigger a repeated note-on cleanly.
                for (auto& v : liveVoices_)
                    if (v.active && v.track == track && v.note == playNote)
                    {
                        emitVoiceOff(v, 0);
                        v.active = false;
                    }
                if (c.noteOff)
                    return;
                if (!conformed)
                    return;   // Filter dropped this live note — nothing to play.

                const int vel = c.velocity > 0 ? c.velocity : 100;
                if (c.bypassEditorial)
                    trackMidi[static_cast<std::size_t>(track)].addEvent(
                        juce::MidiMessage::noteOn(1, static_cast<juce::uint8>(playNote),
                                                  static_cast<juce::uint8>(vel)),
                        0);
                else
                    ccCtx.onNoteOn(track, 0, playNote, vel);

                LiveVoice& v = allocVoice();
                v.track = track;
                v.note = playNote;
                v.bypass = c.bypassEditorial;
                // Gate notes (durationMs == 0) ring until note-off, but get a
                // generous safety cap so a lost note-off (focus change, dropped
                // MIDI) can never hang a note forever.
                constexpr double kMaxGateSeconds = 30.0;
                const double secs = (c.durationMs > 0)
                                        ? static_cast<double>(c.durationMs) / 1000.0
                                        : kMaxGateSeconds;
                v.samplesRemaining = static_cast<int>(getSampleRate() * secs);
                v.active = true;
            };
            for (int i = 0; i < n1; ++i) handle(kbdQueue_[static_cast<std::size_t>(s1 + i)]);
            for (int i = 0; i < n2; ++i) handle(kbdQueue_[static_cast<std::size_t>(s2 + i)]);
            kbdFifo_.finishedRead(n1 + n2);

            // Drain engine parameter commands (writeParam, P-Lock writes).
            drainEngineCmds();

            // Apply a pending pre-staged scene switch at the top of the block
            // (DESIGN §38.4 / 8.17). The message thread pre-built the new working
            // Sequence in queueScene; we swap here so the sequencer reads the new
            // scene starting from this block.
            if (pendingSceneApply_.load(std::memory_order_acquire) && stagedSwapReady_.load(std::memory_order_acquire))
            {
                pendingSceneApply_.store(false, std::memory_order_relaxed);
                arrangement_.applySceneLaunch(stagedSwap_.sceneIdx,
                                              stagedSwap_.working,
                                              stagedSwap_.deviated,
                                              stagedSwap_.deviationPhraseIdx);
                // Sync density atomics from the staged overlay values.
                for (std::size_t t = 0; t < kNumTracks; ++t)
                    trackDensity_[t].store(stagedSwap_.density[t], std::memory_order_relaxed);
                masterDensity_.store(stagedSwap_.masterDensity, std::memory_order_relaxed);
                sceneSwitchApplied_.store(true, std::memory_order_release);
                // 5.6: a scene launch is pattern (re)entry — re-arm one-shots so the
                // arriving scene's accents fire (spent state is per stepIdx, DESIGN §30).
                rearmOneShots(-1);
            }

            // Advance fixed-duration voices; emit their note-off when expired.
            for (auto& v : liveVoices_)
            {
                if (!v.active || v.samplesRemaining < 0) continue;
                if (v.samplesRemaining < numBlockSamples)
                {
                    emitVoiceOff(v, v.samplesRemaining);
                    v.active = false;
                }
                else
                {
                    v.samplesRemaining -= numBlockSamples;
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
                        juce::MidiMessage::noteOff(1, retrigNote_), 0);

                retrigActiveTrack_ = req;
                retrigRatePpq_ = retrigReqRatePpq_.load(std::memory_order_relaxed);
                retrigNote_ = retrigReqNote_.load(std::memory_order_relaxed);
                retrigNextFireSamples_ = 0.0;
                retrigNoteOffRemaining_ = -1;
            }
            else if (req == -2)  // cancel signal
            {
                if (retrigNoteOffRemaining_ >= 0)
                    trackMidi[static_cast<std::size_t>(retrigActiveTrack_)].addEvent(
                        juce::MidiMessage::noteOff(1, retrigNote_), 0);
                retrigActiveTrack_ = -1;
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
                            juce::MidiMessage::noteOff(1, retrigNote_), retrigNoteOffRemaining_);
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
                        juce::MidiMessage::noteOn(1,
                                                  static_cast<juce::uint8>(retrigNote_),
                                                  static_cast<juce::uint8>(100)),
                        samplePos);
                    // note-off ~75% through the interval; allow multi-block deferral
                    // via retrigNoteOffRemaining_ — do not clamp to block boundary.
                    const int noteOffDelta = std::max(1, static_cast<int>(samplesPerRetrig * 0.75));
                    const int noteOffAt = samplePos + noteOffDelta;
                    if (noteOffAt < numBlockSamples)
                        trackMidi[ti].addEvent(
                            juce::MidiMessage::noteOff(1, retrigNote_), noteOffAt);
                    else
                        retrigNoteOffRemaining_ = noteOffAt - numBlockSamples;

                    firePos += samplesPerRetrig;
                }
                retrigNextFireSamples_ = firePos - blockLen;
            }
        }

        // Pre-compute solo state once: if any track is soloed, non-soloed tracks
        // are silenced (even if their mute button is off). A2 (DESIGN §27): solo
        // is routing-aware — soloing a bus keeps its feeders audible (so you hear
        // what flows in), and soloing a feeder keeps its downstream bus chain
        // audible (so it still reaches master). The audibleMask encodes both.
        std::array<bool, kNumTracks> soloedFlags{};
        bool anySoloed = false;
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            soloedFlags[i] = trackSoloParams_[i]->load() >= 0.5f;
            anySoloed = anySoloed || soloedFlags[i];
        }
        const auto soloAudible = routing::soloAudibleMask(routingEdges(), soloedFlags);

        // Advance morph fader smoother once per block (DESIGN §17.2).
        morphFaderSmoothed_.setTargetValue(
            morphFaderTarget_.load(std::memory_order_relaxed));
        if (numBlockSamples > 1)
            morphFaderSmoothed_.skip(numBlockSamples - 1);
        const float faderNow = morphFaderSmoothed_.getNextValue();
        const bool faderSideNow = (faderNow >= 0.5f);  // false=A, true=B

        // Stepped-snap parity: when the fader crosses 0.5 on a MIDI-out track that has
        // morphed params, emit All-Notes-Off on the old channel before the snap takes
        // effect (DESIGN §17.2). Prevents hung notes on channel change.
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            if (faderSideNow != morphLastSide_[i] && machines_[i]->isMidiOut())
            {
                const Scene& sc = section();
                const bool hasMorph = [&]() noexcept {
                    for (const auto& kv : sc.morphA)
                        if (kv.first.first == static_cast<int>(i)) return true;
                    for (const auto& kv : sc.morphB)
                        if (kv.first.first == static_cast<int>(i)) return true;
                    return false;
                }();
                if (hasMorph)
                {
                    auto* mom = static_cast<MidiOutMachine*>(machines_[i].get());
                    juce::MidiBuffer aonBuf;
                    mom->allNotesOff(aonBuf);
                    if (!isStandalone)
                        midi.addEvents(aonBuf, 0, numBlockSamples, 0);
                }
            }
            morphLastSide_[i] = faderSideNow;
        }

        if (!sequencerRunning)
        {
            // Render voice tails and any externally-triggered notes.
            // trackMidi already contains note events routed from external MIDI.
            // A2: drive tracks in routing order so a bus's inbound audio is ready.
            const auto routeOrderIdle = routing::computeOrder(routingEdges(), tapEdges());
            for (std::size_t oi = 0; oi < kNumTracks; ++oi)
            {
                const std::size_t i = static_cast<std::size_t>(routeOrderIdle[oi]);
                const bool muted = (trackMuteParams_[i]->load() >= 0.5f) || !section().activeMask[i];
                if (muted || (anySoloed && !soloAudible[i])) continue;
                // Resolve against the held step so P-Locks written by the note-on
                // are included in the frame, falling back to -1 (base only).
                int resolveStep = -1;
                if (editContext_.isActiveForEditing() && editContext_.heldTrackIndex() == static_cast<int>(i))
                    resolveStep = editContext_.heldStepIndex();
                const bool fillNow = fillActiveForTrack(static_cast<int>(i));
                const MorphContext mc0{ &section(), static_cast<int>(i), faderNow, machines_[i].get() };
                auto frame = StateResolver::resolve(sequence().tracks[i], resolveStep, fillNow, &mc0);
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
                    processTrackChain(i, frame, resolveStep, fillNow, faderNow,
                                      numBlockSamples, trackMidi[i]);
                    depositToBus(i, numBlockSamples);
                }
            }

            // A2: sum only Master-routed tracks (bus-routed audio reaches master
            // through its bus track's chain).
            sumRoutedToMaster(buffer, numBlockSamples);

            // Master insert chain — shared helper used by both transport paths.
            processMasterChain(buffer, numBlockSamples);

            // Keep audio path (gain smoothing, DC blocker) running so it doesn't freeze.
            const float targetGainDb = apvts_.getRawParameterValue(ParamIDs::outputGain)->load();
            gainSmoothed_.setTargetValue(
                juce::Decibels::decibelsToGain(targetGainDb, -60.0f));

            const int numOut = buffer.getNumChannels();
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
                        const float y = s - x1 + 0.999f * y1;
                        dcX1_[static_cast<std::size_t>(ch)] = s;
                        dcY1_[static_cast<std::size_t>(ch)] = y;
                        s = y;
                    }
                    buffer.setSample(ch, i, dsp::softClip(s));
                }
            }
            masterPeak_.store(buffer.getMagnitude(0, 0, numSamples),
                              std::memory_order_relaxed);
            masterPeakR_.store(buffer.getNumChannels() > 1
                                   ? buffer.getMagnitude(1, 0, numSamples)
                                   : masterPeak_.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
            captureRecorder_.writeBlock(buffer, numBlockSamples);
            cachePrevMaster(buffer, numBlockSamples);
            return;
        }

        // ── Section launch engine (Phase 7 / DESIGN §4.8, §16) ───────────────
        // A queued Section fires at the next core-time bar boundary.
        {
            const int qSecIdx = queuedSceneIdx_.load(std::memory_order_acquire);
            if (qSecIdx >= 0 && samplesPerPpq > 0.0)
            {
                const auto ct = effectiveTimeSig();
                const double barPpq = ct.barPpq() * static_cast<double>(project_.launchQuantizeBars);
                if (barPpq > 0.0)
                {
                    // Next bar boundary at or after blockStart.
                    const double boundary =
                        std::ceil(blockStart / barPpq) * barPpq;
                    if (boundary < blockEnd)
                    {
                        queuedSceneIdx_.store(-1, std::memory_order_release);
                        // Signal top-of-next-block to apply the pre-staged swap.
                        // The message thread prepared the new Sequence in queueScene;
                        // the audio thread swaps it in at the next block boundary
                        // without allocation (DESIGN §38.4 / 8.17).
                        pendingSceneApply_.store(true, std::memory_order_release);
                        // Reinstall machines on the message thread. queueScene already
                        // flushed kit data via writeBackWorkingToActive, so this is safe
                        // to run before the audio-thread swap commits.
                        juce::MessageManager::callAsync(
                            [this] {
                                reinstallMachinesFromActiveKit();
                            });
                    }
                }
            }
        }


        // A2: process tracks in routing (topological) order so a bus's inbound
        // audio is deposited before the bus runs. Scheduling is per-track
        // independent (probability/density are deterministic by track+step), so
        // reordering does not affect trig decisions.
        const auto routeOrderRun = routing::computeOrder(routingEdges(), tapEdges());
        for (std::size_t oi = 0; oi < kNumTracks; ++oi)
        {
            const std::size_t i = static_cast<std::size_t>(routeOrderRun[oi]);
            const auto& track = sequence().tracks[i];

            const int trackLen = static_cast<int>(trackLengthParams_[i]->load());
            const int trackSubdiv = std::clamp(static_cast<int>(trackDividerParams_[i]->load()),
                                               kSubdivMin, kSubdivMax);
            // MD.6/MD.7: combined mute = global (APVTS) || section active-mask.
            const bool globalMuted = trackMuteParams_[i]->load() >= 0.5f;
            const bool sectionMuted = !section().activeMask[i];
            const bool muted = globalMuted || sectionMuted;
            const bool silent = muted || (anySoloed && !soloAudible[i]);

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

            const double divPpqMusical = subdivisionPpqFromIndex(trackSubdiv);
            // DESIGN §4.9: scale step grid by the Song × Scene tempo ratio.
            const double tempoRatio = effectiveTempoRatio();
            const double divPpq = (tempoRatio > 0.0) ? (divPpqMusical / tempoRatio) : divPpqMusical;

            if (divPpq <= 0.0 || samplesPerPpq <= 0.0 || trackLen <= 0 || silent)
                continue;

            // If the cursor has fallen far behind (cold start, late join),
            // snap it to the step boundary at/before blockStart so we don't
            // burn CPU catching up sample-by-sample.
            if (nextTriggerPpq_[i] < blockStart - divPpq)
                nextTriggerPpq_[i] = std::floor(blockStart / divPpq) * divPpq;

            const bool curFillActive = fillActiveForTrack(static_cast<int>(i));

            // Read effective swing for this track (DESIGN §19.2).
            // Song-all + song-track delta + scene-all delta; RT-safe reads mirroring
            // the existing morph/activeMask pattern (no locks needed).
            const float songAll = song().swing;
            const float songTrk = song().tracks[i].swing;
            const float sceneAll = section().swing;
            const float effSwing = effectiveSwing(songAll, songTrk, sceneAll);
            const double halfDiv = 0.5 * divPpq;

            // Emit a sequencer trig: note-on(s) + gate scheduling.
            // fireAt is a sample offset within this block, clamped to [0, numBlockSamples−1].
            auto emitTrig = [&](int stepIdx, int fireAt) {
                trigPulse_[i].store(1.0f, std::memory_order_relaxed);
                const auto trig = StateResolver::resolveTrig(track, stepIdx, curFillActive);

                // If a previous trig's note-off is still pending (gate longer than
                // the step interval), emit it now so the voice releases and retriggers
                // cleanly. Same-position insertion preserves note-off before note-on.
                {
                    auto& pnf = pendingNoteOffs_[i];
                    if (pnf.samplesRemaining >= 0 || pnf.openEnded)
                    {
                        for (int n = 0; n < pnf.noteCount; ++n)
                            trackMidi[i].addEvent(
                                juce::MidiMessage::noteOff(
                                    1, pnf.notes[static_cast<std::size_t>(n)]),
                                fireAt);
                        pnf.samplesRemaining = -1;
                        pnf.openEnded = false;
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

                struct NoteVelPair
                {
                    int note;
                    uint8_t vel;
                };
                std::array<NoteVelPair, kMaxNotesPerStep> nvPairs{};
                for (int n = 0; n < trig.noteCount; ++n)
                {
                    const auto ni = static_cast<std::size_t>(n);
                    nvPairs[ni].note = trig.notes[ni];
                    nvPairs[ni].vel = trig.hasNoteVelocities
                                          ? trig.velocities[ni]
                                          : static_cast<uint8_t>(std::clamp(trig.velocity, 1, 127));
                }
                std::sort(nvPairs.begin(), nvPairs.begin() + trig.noteCount,
                          [](const NoteVelPair& a, const NoteVelPair& b) {
                              return a.note < b.note;
                          });

                std::array<int, kMaxNotesPerStep> sortedNotes{};
                std::array<uint8_t, kMaxNotesPerStep> sortedVels{};
                for (int n = 0; n < trig.noteCount; ++n)
                {
                    sortedNotes[static_cast<std::size_t>(n)] = nvPairs[static_cast<std::size_t>(n)].note;
                    sortedVels[static_cast<std::size_t>(n)] = nvPairs[static_cast<std::size_t>(n)].vel;
                }

                std::array<int, kMaxNotesPerStep> emitNotes{};
                int notesToEmit = 0;
                if (machineVoices == 0)
                {
                    for (int n = 0; n < trig.noteCount; ++n)
                        emitNotes[static_cast<std::size_t>(n)] = sortedNotes[static_cast<std::size_t>(n)];
                    notesToEmit = trig.noteCount;
                }
                else
                {
                    pickSpreadNotes(sortedNotes, trig.noteCount, machineVoices,
                                    track.noteSelection, emitNotes, notesToEmit);
                }

                std::array<uint8_t, kMaxNotesPerStep> emitVels{};
                for (int j = 0; j < notesToEmit; ++j)
                {
                    for (int k = 0; k < trig.noteCount; ++k)
                    {
                        if (sortedNotes[static_cast<std::size_t>(k)] == emitNotes[static_cast<std::size_t>(j)])
                        {
                            emitVels[static_cast<std::size_t>(j)] =
                                sortedVels[static_cast<std::size_t>(k)];
                            break;
                        }
                    }
                }

                // Resolve authored velocity per note, then apply live overlay.
                const auto uniformVel =
                    static_cast<juce::uint8>(std::clamp(trig.velocity, 1, 127));
                std::array<juce::uint8, kMaxNotesPerStep> finalVels{};
                for (int n = 0; n < notesToEmit; ++n)
                {
                    const auto ni = static_cast<std::size_t>(n);
                    finalVels[ni] = trig.hasNoteVelocities
                        ? static_cast<juce::uint8>(std::clamp(static_cast<int>(emitVels[ni]), 1, 127))
                        : uniformVel;
                }
                // Velocity overlay: metric weight modulates velocity (Bar or Phrase mode).
                const auto& velKit = song().tracks[i].kit;
                if (velKit.velMode == VelMode::Bar || velKit.velMode == VelMode::Phrase)
                {
                    const auto velCt = effectiveTimeSig();
                    const double velBarPpq = velCt.barPpq();
                    double velPpqInBar = 0.0;
                    if (velBarPpq > 0.0)
                    {
                        // Convert host PPQ → musical PPQ for bar-position computations.
                        if (velKit.velMode == VelMode::Bar)
                            velPpqInBar = std::fmod(nextTriggerPpq_[i] * tempoRatio, velBarPpq);
                        else // Phrase: use musical step size (before tempo scaling)
                            velPpqInBar = std::fmod(static_cast<double>(stepIdx) * divPpqMusical, velBarPpq);
                    }
                    const float w = MetricGrid::metricWeight(
                        velPpqInBar, velBarPpq,
                        velCt.numerator, velCt.denominator);
                    if (velKit.velBlend == VelBlend::Replace)
                    {
                        const auto ov = static_cast<juce::uint8>(
                            accentVelocity(velKit.velCenter, velKit.velDepth, w));
                        for (int n = 0; n < notesToEmit; ++n)
                            finalVels[static_cast<std::size_t>(n)] = ov;
                    }
                    else // Mix: swing around velCenter for un-authored steps
                    {
                        const float maxSwing = static_cast<float>(
                            std::min(velKit.velCenter - 1, 127 - velKit.velCenter));
                        const float delta = velKit.velDepth * maxSwing * ((2.0f * w) - 1.0f);
                        const int roundedDelta = static_cast<int>(std::lround(delta));
                        // Check raw step override — TrigFields doesn't carry has-flags.
                        bool hasAuthoredVel = trig.hasNoteVelocities;
                        if (!hasAuthoredVel && stepIdx >= 0 && stepIdx < kMaxStepsPerTrack)
                        {
                            const auto si = static_cast<std::size_t>(stepIdx);
                            hasAuthoredVel = curFillActive
                                ? (track.steps[si].fillTrigOverride.hasVelocity
                                   || track.steps[si].fillTrigOverride.hasNoteVelocities
                                   || track.steps[si].trigOverride.hasVelocity)
                                : track.steps[si].trigOverride.hasVelocity;
                        }
                        for (int n = 0; n < notesToEmit; ++n)
                        {
                            const auto ni = static_cast<std::size_t>(n);
                            const int baseline = hasAuthoredVel
                                ? static_cast<int>(finalVels[ni])
                                : velKit.velCenter;
                            finalVels[ni] = static_cast<juce::uint8>(
                                std::clamp(baseline + roundedDelta, 1, 127));
                        }
                    }
                }
                // Per-track Scale stage (DESIGN §4.10): conform the sequenced output
                // to the effective key — Snap moves out-of-key notes to the nearest
                // in-key note, Filter drops them. Compacts notes + velocities in
                // lockstep; the note-off loop below reuses emitNotes[0..notesToEmit).
                if (velKit.scaleMode != ScaleMode::Off)
                {
                    const auto scaleKey = effectiveKeySig();
                    int w = 0;
                    for (int n = 0; n < notesToEmit; ++n)
                    {
                        const auto ni = static_cast<std::size_t>(n);
                        if (auto out = applyScaleMode(scaleKey, velKit.scaleMode, emitNotes[ni]))
                        {
                            emitNotes[static_cast<std::size_t>(w)] = *out;
                            finalVels[static_cast<std::size_t>(w)] = finalVels[ni];
                            ++w;
                        }
                    }
                    notesToEmit = w;
                }
                for (int n = 0; n < notesToEmit; ++n)
                {
                    const auto ni = static_cast<std::size_t>(n);
                    trackMidi[i].addEvent(
                        juce::MidiMessage::noteOn(1, emitNotes[ni], finalVels[ni]), fireAt);
                }

                if (trig.gateValue != MusicalGate::None)
                {
                    const double liveBpm = (samplesPerPpq > 0.0)
                                               ? (getSampleRate() * 60.0 / samplesPerPpq)
                                               : clock_.localBpm();
                    const int gateSamples = musicalGateToSamples(
                        trig.gateValue, liveBpm, getSampleRate());
                    const int noteOffAt = fireAt + gateSamples;
                    if (noteOffAt < numBlockSamples)
                    {
                        for (int n = 0; n < notesToEmit; ++n)
                            trackMidi[i].addEvent(
                                juce::MidiMessage::noteOff(
                                    1, emitNotes[static_cast<std::size_t>(n)]),
                                noteOffAt);
                    }
                    else
                    {
                        auto& pnf = pendingNoteOffs_[i];
                        pnf.samplesRemaining = noteOffAt - numBlockSamples;
                        pnf.noteCount = notesToEmit;
                        pnf.notes = emitNotes;
                        pnf.openEnded = false;
                    }
                }
                else
                {
                    // gate=None: voices play to their envelope end.
                    auto& pnf = pendingNoteOffs_[i];
                    pnf.openEnded = true;
                    pnf.noteCount = notesToEmit;
                    pnf.notes = emitNotes;
                }

                // 5.7: per-step retrig. If the step has a baked retrig rate, start
                // the rattle at the primary emitted note for this step's duration.
                if (stepIdx >= 0 && stepIdx < kMaxStepsPerTrack)
                {
                    const auto& sto = track.steps[static_cast<std::size_t>(stepIdx)]
                                          .trigOverride;
                    if (sto.hasRetrig)
                    {
                        const int retNote = notesToEmit > 0 ? emitNotes[0] : 60;
                        retrigReqNote_.store(retNote, std::memory_order_relaxed);
                        retrigReqRatePpq_.store(sto.retrigRate, std::memory_order_relaxed);
                        retrigReqTrack_.store(static_cast<int>(i),
                                              std::memory_order_release);
                    }
                }
            }; // end emitTrig

            // Drain pending trig deferred by a late shift from the previous block.
            // A late micro-offset/swing shift can be many blocks ahead: a +0.3
            // shift on a 1/16 step is ~0.075 PPQ, while one block is ~0.011 PPQ.
            // So keep the trig pending until its fire time actually lands in a
            // block — clearing it after a single block (when ptFire is still in
            // the future) silently dropped every late-shifted note.
            if (pendingTrigs_[i].pending)
            {
                const double ptFire = pendingTrigs_[i].firePpq;
                if (ptFire < blockEnd)
                {
                    const int ptStep = pendingTrigs_[i].stepIndex;
                    const auto ptStNum = pendingTrigs_[i].stepNum;
                    const int fireAt = std::clamp(
                        static_cast<int>((ptFire - blockStart) * samplesPerPpq),
                        0, numBlockSamples - 1);
                    firedStepIdx_[i] = ptStep;
                    lastScheduledStepNum_[i] = ptStNum;
                    lastStepFired_[i] = true;
                    pendingTrigs_[i].pending = false;
                    emitTrig(ptStep, fireAt);
                }
                // else: fire time is still beyond this block — keep it pending.
            }

            // Main scan: walk the grid and emit per fired step with shift applied.
            while (nextTriggerPpq_[i] < blockEnd)
            {
                if (nextTriggerPpq_[i] >= blockStart)
                {
                    const auto stepNum = static_cast<std::int64_t>(
                        nextTriggerPpq_[i] / divPpq);
                    const int stepIdx = static_cast<int>(
                        stepNum % static_cast<std::int64_t>(trackLen));

                    // Dedup: skip if emitted during pendingTrigs drain above.
                    if (stepNum != lastScheduledStepNum_[i])
                    {
                        const auto& step = track.steps[static_cast<std::size_t>(stepIdx)];
                        const TrigCondition& cond = step.condition.isTrivial()
                                                        ? track.baseCond
                                                        : step.condition;
                        bool fired = TrigEvaluator::shouldFire(
                            step, cond, i, stepNum, trackLen,
                            lastStepFired_[i], curFillActive);

                        if (fired)
                        {
                            // §39 Density gate — subtractive, downstream of all conditions.
                            // Exempt tracks skip the gate entirely (master + per-track ignored).
                            const auto& kitRef = song().tracks[i].kit;
                            if (kitRef.densitySelection == Density::DensitySelection::Exempt)
                            {
                                // fired stays true — fall through to emit
                            }
                            else
                            {
                            const float perTrack = trackDensity_[i].load(std::memory_order_relaxed);
                            const float master = masterDensity_.load(std::memory_order_relaxed);
                            const auto densCt = effectiveTimeSig();
                            const double barPpq = densCt.barPpq();
                            // Convert host PPQ → musical PPQ for bar-position math.
                            const double musicalPpq = nextTriggerPpq_[i] * tempoRatio;
                            const double ppqInBar = std::fmod(musicalPpq, barPpq);
                            const auto& kit = song().tracks[i].kit;
                            const int qLevel = static_cast<int>(
                                std::round(std::clamp(perTrack + master, 0.01f, 1.0f) * 100.0f));
                            // Reroll cadence: Uniform = per-step hash, Musical/Metric = per-bar hash.
                            // stepsPerBar uses musical step size (barPpq / divPpqMusical).
                            const auto stepsPerBar = (barPpq > 0.0 && divPpqMusical > 0.0)
                                ? std::max(std::int64_t{ 1 }, static_cast<std::int64_t>(std::round(barPpq / divPpqMusical)))
                                : std::int64_t{ 1 };
                            const auto barIndex  = (barPpq > 0.0)
                                ? static_cast<std::int64_t>(musicalPpq / barPpq) : std::int64_t{ 0 };
                            const auto stepInBar = stepNum % stepsPerBar;
                            const float rerollR =
                                (kit.densityMusicality == Density::Musicality::Uniform)
                                    ? Density::rerollPerStep(i, stepNum)
                                    : Density::rerollPerBar(i, barIndex, stepInBar);
                            if (kit.densitySelection == Density::DensitySelection::Scrub)
                            {
                                // §39 Deterministic Scrub: tier+Euclid via MetricSelect.
                                // Fixed per-track rotation de-correlates same-density tracks.
                                const int off = static_cast<int>(
                                    Density::densityScrubHash(i, 0, 0));
                                auto& cache = densityTableCache_[i];
                                const int num = densCt.numerator;
                                const int den = densCt.denominator;
                                if (cache.numerator != num || cache.denominator != den
                                    || cache.stepsPerBar != stepsPerBar)
                                {
                                    const int n = static_cast<int>(
                                        std::min(stepsPerBar, std::int64_t{ 64 }));
                                    std::array<float, 64> wts{};
                                    for (int s = 0; s < n; ++s)
                                    {
                                        const double ppqPos =
                                            static_cast<double>(s) / static_cast<double>(n) * barPpq;
                                        wts[static_cast<std::size_t>(s)] =
                                            MetricGrid::metricWeight(ppqPos, barPpq, num, den);
                                    }
                                    cache.table = MetricSelect::build(wts, n, off);
                                    cache.numerator = num;
                                    cache.denominator = den;
                                    cache.stepsPerBar = stepsPerBar;
                                }
                                const float effective = std::clamp(perTrack + master, 0.01f, 1.0f);
                                const int N = static_cast<int>(
                                    std::min(stepsPerBar, std::int64_t{ 64 }));
                                const int barStep = static_cast<int>(stepInBar % static_cast<std::int64_t>(N));
                                const int T = static_cast<int>(
                                    std::round(effective * static_cast<float>(N)));
                                if (kit.densityMusicality == Density::Musicality::Uniform)
                                {
                                    const int Tl = static_cast<int>(
                                        std::round(effective * static_cast<float>(trackLen)));
                                    fired = MetricSelect::uniformSurvives(
                                        stepIdx, trackLen, Tl, off);
                                }
                                else if (kit.densityMusicality == Density::Musicality::Metric)
                                {
                                    fired = MetricSelect::metricSurvives(cache.table, barStep, T);
                                }
                                else // Mixed
                                {
                                    fired = MetricSelect::mixedSurvives(cache.table, barStep, T);
                                }
                            }
                            else
                            {
                                fired = Density::densitySurvives(
                                    perTrack, master, ppqInBar, barPpq,
                                    densCt.numerator,
                                    densCt.denominator,
                                    kit.densityMusicality, kit.densitySelection,
                                    i, stepNum, qLevel, rerollR);
                            }
                            } // end else (not Exempt)
                        }

                        // 5.6 one-shot gate (DESIGN §30): a spent one-shot is
                        // suppressed until re-armed; the first fire marks it spent.
                        if (fired && cond.oneShot)
                        {
                            auto& spent = oneShotSpent_[i][static_cast<std::size_t>(stepIdx)];
                            if (spent) fired = false;
                            else       spent = true;
                        }

                        if (fired)
                        {
                            const bool isOdd = (stepNum % 2) == 1;
                            const float swingDelta = isOdd ? effSwing : 0.0f;
                            const float shift = totalStepShift(swingDelta, step.microOffset);
                            const double firePpq = nextTriggerPpq_[i] + static_cast<double>(shift) * divPpq;

                            firedStepIdx_[i] = stepIdx;  // ME.4: for FLTR P-Locks
                            lastScheduledStepNum_[i] = stepNum;
                            lastStepFired_[i] = true;

                            if (firePpq < blockEnd)
                            {
                                const int fireAt = std::max(
                                    0, static_cast<int>(
                                           (firePpq - blockStart) * samplesPerPpq));
                                emitTrig(stepIdx, fireAt);
                            }
                            else
                            {
                                // Late shift: fire in the next block.
                                pendingTrigs_[i] = { true, stepIdx, stepNum, firePpq };
                            }
                        }
                        else if (step.lockOnly)
                        {
                            // 5.6 trigless / lock-only (DESIGN §30): ride this step's
                            // P-Locks/overrides onto the sustaining voice. Point the
                            // resolver at this step (firedStepIdx_ drives the machine
                            // frame + FLTR/CHANNEL/ENV/insert overrides), but emit no
                            // note and never close the open gate.
                            firedStepIdx_[i] = stepIdx;
                            lastScheduledStepNum_[i] = stepNum;
                            lastStepFired_[i] = false;
                        }
                        else
                        {
                            // Step that last fired has cycled back but doesn't fire now
                            // (toggled off or condition failed). Close any open-ended note.
                            if (stepIdx == firedStepIdx_[i] && firedStepIdx_[i] >= 0)
                            {
                                auto& pnf = pendingNoteOffs_[i];
                                if (pnf.openEnded)
                                {
                                    const double off =
                                        (nextTriggerPpq_[i] - blockStart) * samplesPerPpq;
                                    const int nofAt = std::clamp(
                                        static_cast<int>(off), 0, numBlockSamples - 1);
                                    for (int n = 0; n < pnf.noteCount; ++n)
                                        trackMidi[i].addEvent(
                                            juce::MidiMessage::noteOff(
                                                1, pnf.notes[static_cast<std::size_t>(n)]),
                                            nofAt);
                                    pnf.openEnded = false;
                                }
                            }
                            lastStepFired_[i] = false;
                        }
                    }
                }
                nextTriggerPpq_[i] += divPpq;
            }

            // Lookahead: the first grid step past blockEnd may have a large negative
            // shift (early) that pulls its fire time back into this block. Check once.
            {
                const double nextGridPpq = nextTriggerPpq_[i];
                if (nextGridPpq < blockEnd + halfDiv)
                {
                    const auto stepNum = static_cast<std::int64_t>(nextGridPpq / divPpq);
                    if (stepNum != lastScheduledStepNum_[i])
                    {
                        const int stepIdx = static_cast<int>(
                            stepNum % static_cast<std::int64_t>(trackLen));
                        const auto& step = track.steps[static_cast<std::size_t>(stepIdx)];
                        const TrigCondition& cond = step.condition.isTrivial()
                                                        ? track.baseCond
                                                        : step.condition;
                        bool lookaheadFired = TrigEvaluator::shouldFire(
                            step, cond, i, stepNum, trackLen, lastStepFired_[i], curFillActive);

                        if (lookaheadFired)
                        {
                            // §39 Density gate — same computation as main scan; pure hashes
                            // guarantee identical r for the same logical step, no memoisation needed.
                            const float perTrack = trackDensity_[i].load(std::memory_order_relaxed);
                            const float master = masterDensity_.load(std::memory_order_relaxed);
                            const auto densCt2 = effectiveTimeSig();
                            const double barPpq = densCt2.barPpq();
                            // Convert host PPQ → musical PPQ for bar-position math.
                            const double musicalGridPpq = nextGridPpq * tempoRatio;
                            const double ppqInBar = std::fmod(musicalGridPpq, barPpq);
                            const auto& kit = song().tracks[i].kit;
                            const int qLevel = static_cast<int>(
                                std::round(std::clamp(perTrack + master, 0.01f, 1.0f) * 100.0f));
                            const auto stepsPerBar = (barPpq > 0.0 && divPpqMusical > 0.0)
                                ? std::max(std::int64_t{ 1 }, static_cast<std::int64_t>(std::round(barPpq / divPpqMusical)))
                                : std::int64_t{ 1 };
                            const auto barIndex  = (barPpq > 0.0)
                                ? static_cast<std::int64_t>(musicalGridPpq / barPpq) : std::int64_t{ 0 };
                            const auto stepInBar = stepNum % stepsPerBar;
                            const float rerollR =
                                (kit.densityMusicality == Density::Musicality::Uniform)
                                    ? Density::rerollPerStep(i, stepNum)
                                    : Density::rerollPerBar(i, barIndex, stepInBar);
                            if (kit.densitySelection == Density::DensitySelection::Scrub)
                            {
                                // §39 Deterministic Scrub: reuse cached table (same bar, same grid).
                                const int off = static_cast<int>(
                                    Density::densityScrubHash(i, 0, 0));
                                auto& cache = densityTableCache_[i];
                                const int num = densCt2.numerator;
                                const int den = densCt2.denominator;
                                if (cache.numerator != num || cache.denominator != den
                                    || cache.stepsPerBar != stepsPerBar)
                                {
                                    const int n = static_cast<int>(
                                        std::min(stepsPerBar, std::int64_t{ 64 }));
                                    std::array<float, 64> wts{};
                                    for (int s = 0; s < n; ++s)
                                    {
                                        const double ppqPos =
                                            static_cast<double>(s) / static_cast<double>(n) * barPpq;
                                        wts[static_cast<std::size_t>(s)] =
                                            MetricGrid::metricWeight(ppqPos, barPpq, num, den);
                                    }
                                    cache.table = MetricSelect::build(wts, n, off);
                                    cache.numerator = num;
                                    cache.denominator = den;
                                    cache.stepsPerBar = stepsPerBar;
                                }
                                const float effective = std::clamp(perTrack + master, 0.01f, 1.0f);
                                const int N = static_cast<int>(
                                    std::min(stepsPerBar, std::int64_t{ 64 }));
                                const int barStep = static_cast<int>(stepInBar % static_cast<std::int64_t>(N));
                                const int T = static_cast<int>(
                                    std::round(effective * static_cast<float>(N)));
                                const int lookaheadStepIdx = static_cast<int>(
                                    stepNum % static_cast<std::int64_t>(trackLen));
                                if (kit.densityMusicality == Density::Musicality::Uniform)
                                {
                                    const int Tl = static_cast<int>(
                                        std::round(effective * static_cast<float>(trackLen)));
                                    lookaheadFired = MetricSelect::uniformSurvives(
                                        lookaheadStepIdx, trackLen, Tl, off);
                                }
                                else if (kit.densityMusicality == Density::Musicality::Metric)
                                {
                                    lookaheadFired = MetricSelect::metricSurvives(
                                        cache.table, barStep, T);
                                }
                                else // Mixed
                                {
                                    lookaheadFired = MetricSelect::mixedSurvives(
                                        cache.table, barStep, T);
                                }
                            }
                            else
                            {
                                lookaheadFired = Density::densitySurvives(
                                    perTrack, master, ppqInBar, barPpq,
                                    densCt2.numerator,
                                    densCt2.denominator,
                                    kit.densityMusicality, kit.densitySelection,
                                    i, stepNum, qLevel, rerollR);
                            }
                        }

                        if (lookaheadFired)
                        {
                            const bool isOdd = (stepNum % 2) == 1;
                            const float swingDelta = isOdd ? effSwing : 0.0f;
                            const float shift = totalStepShift(swingDelta, step.microOffset);
                            const double firePpq = nextGridPpq + static_cast<double>(shift) * divPpq;
                            if (firePpq >= blockStart && firePpq < blockEnd)
                            {
                                // 5.6 one-shot gate — applied only when the lookahead
                                // actually emits, so a non-emitting lookahead can't
                                // silently consume the trig (DESIGN §30).
                                bool emit = true;
                                if (cond.oneShot)
                                {
                                    auto& spent = oneShotSpent_[i][static_cast<std::size_t>(stepIdx)];
                                    if (spent) emit = false;
                                    else       spent = true;
                                }
                                if (emit)
                                {
                                    const int fireAt = std::clamp(
                                        static_cast<int>((firePpq - blockStart) * samplesPerPpq),
                                        0, numBlockSamples - 1);
                                    firedStepIdx_[i] = stepIdx;
                                    lastScheduledStepNum_[i] = stepNum;
                                    lastStepFired_[i] = true;
                                    emitTrig(stepIdx, fireAt);
                                    // nextTriggerPpq_[i] is not advanced; dedup prevents re-fire.
                                }
                            }
                        }
                    }
                }
            }

            // Resolve ParamFrame for the machine using the last-fired step so that
            // P-Locks (including fill-layer overrides) persist for the full note
            // duration rather than only the block in which the step fires.
            // MG.5: if the fired step carries a sound_id override, use the pool
            // entry's baseParams as the base, then apply step P-Locks on top.
            // Falls back to the normal StateResolver path if the entry is missing.
            auto frame = [&]() -> ParamFrame {
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
                const MorphContext mcR{ &section(), static_cast<int>(i), faderNow, machines_[i].get() };
                return StateResolver::resolve(track, fi, curFillActive, &mcR);
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
                // Tracks and buses stay linear; the only structural clip is at
                // the master output stage (master-only gain-staging).
                processTrackChain(i, frame, firedStepIdx_[i], curFillActive, faderNow,
                                  numBlockSamples, trackMidi[i]);
                depositToBus(i, numBlockSamples);
            }
        }

        // A2: sum only Master-routed tracks (bus-routed audio reaches master
        // through its bus track's chain).
        sumRoutedToMaster(buffer, numBlockSamples);

        // Master insert chain — before metronome so the click is not sent through FX.
        processMasterChain(buffer, numBlockSamples);

        if (clock_.isMetronomeEnabled())
        {
            const auto metroCt = effectiveTimeSig();
            metronome_.process(blockStart, blockEnd, samplesPerPpq, buffer,
                               metroCt.numerator, metroCt.denominator);
        }

        // Output stage: smoothed gain → DC blocker → transparent soft-knee clip
        const float targetGainDb = apvts_.getRawParameterValue(ParamIDs::outputGain)->load();
        gainSmoothed_.setTargetValue(
            juce::Decibels::decibelsToGain(targetGainDb, -60.0f));

        const int numOut = buffer.getNumChannels();
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
                    const float y = s - x1 + 0.999f * y1;
                    dcX1_[static_cast<std::size_t>(ch)] = s;
                    dcY1_[static_cast<std::size_t>(ch)] = y;
                    s = y;
                }

                buffer.setSample(ch, i, dsp::softClip(s));
            }
        }
        masterPeak_.store(buffer.getMagnitude(0, 0, numSamples),
                          std::memory_order_relaxed);
        masterPeakR_.store(buffer.getNumChannels() > 1
                               ? buffer.getMagnitude(1, 0, numSamples)
                               : masterPeak_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
        captureRecorder_.writeBlock(buffer, numBlockSamples);
        cachePrevMaster(buffer, numBlockSamples);

        totalSamplesProcessed_ += numBlockSamples;
    }

    // Enqueue an EngineCmd to set one param destination.
    // All resolution (clamping, zero-crossing snap, control-all fan-out) is done by
    // the caller (writeParam); this helper just packs and enqueues.
    static void enqueueBaseParam(LockstepProcessor& p, int track, int slot, float value)
    {
        EngineCmd c;
        c.op = EngineCmd::Op::SetBaseParam;
        c.track = static_cast<uint8_t>(track);
        c.slot = static_cast<int16_t>(slot);
        c.value = value;
        p.pushEngineCmd(c);
    }
    static void enqueueFltrSlot(LockstepProcessor& p, int track, int fltrSlot, float value)
    {
        EngineCmd c;
        c.op = EngineCmd::Op::SetFltrSlot;
        c.track = static_cast<uint8_t>(track);
        c.slot = static_cast<int16_t>(fltrSlot);
        c.value = value;
        p.pushEngineCmd(c);
    }
    static void enqueueChanSlot(LockstepProcessor& p, int track, int chanSlot, float value)
    {
        // A2: refuse an invalid "Out" edit (cycle / non-bus target / self,
        // DESIGN §27). The value snaps back on the next surface refresh; the
        // reason is recorded for the editor's status banner. The engine apply has
        // a race-free guard too; this one keeps the bad command off the queue.
        if (chanSlot == TrackChannelState::kNumSlots - 1)
        {
            const auto reason = p.validateOutEdit(track, value);
            if (reason != LockstepProcessor::RouteReject::None)
            {
                p.noteRouteReject(reason, decodeOutputDest(value).track);
                return;
            }
        }
        EngineCmd c;
        c.op = EngineCmd::Op::SetChanSlot;
        c.track = static_cast<uint8_t>(track);
        c.slot = static_cast<int16_t>(chanSlot);
        c.value = value;
        p.pushEngineCmd(c);
    }
    static void enqueueEnvSlot(LockstepProcessor& p, int track, int envSlot, float value)
    {
        EngineCmd c;
        c.op = EngineCmd::Op::SetEnvSlot;
        c.track = static_cast<uint8_t>(track);
        c.slot = static_cast<int16_t>(envSlot);
        c.value = value;
        p.pushEngineCmd(c);
    }
    static void enqueueInsertParam(LockstepProcessor& p, int track, int ins, int param, float value)
    {
        // Immediate message-thread write so the 30 Hz MZ timer doesn't snap back.
        if (track >= 0 && track < static_cast<int>(kNumTracks))
        {
            auto& kIns = p.kit(track).inserts[static_cast<std::size_t>(ins)];
            if (static_cast<std::size_t>(param) < kIns.baseParams.size())
                kIns.baseParams[static_cast<std::size_t>(param)] = value;
        }
        EngineCmd c;
        c.op = EngineCmd::Op::SetInsertParam;
        c.track = static_cast<uint8_t>(track);
        c.aux = static_cast<uint8_t>(ins);
        c.slot = static_cast<int16_t>(param);
        c.value = value;
        p.pushEngineCmd(c);
    }
    static void enqueueStepOverride(LockstepProcessor& p, int track, int step, int slot, float value)
    {
        EngineCmd c;
        c.op = EngineCmd::Op::SetStepOverride;
        c.track = static_cast<uint8_t>(track);
        c.aux = static_cast<uint8_t>(step);
        c.slot = static_cast<int16_t>(slot);
        c.value = value;
        p.pushEngineCmd(c);
    }

    // Write a resolved destination for one (track, dstSlot) within writeParam.
    // All bounds are already verified by the caller.
    static void writeParamQueued(LockstepProcessor& proc, int t, int dstSlot, float value)
    {
        const auto ti = static_cast<std::size_t>(t);
        auto* dm = proc.machines_[ti].get();
        if (!dm) return;
        const int dstMnp = dm->numParams();

        if (dstSlot < dstMnp)
        {
            enqueueBaseParam(proc, t, dstSlot, value);
            return;
        }
        if (dm->isMidiOut())
        {
            // MIDI-out: no FLTR/CHAN/ENV blocks; inserts start right after machine params.
            int dstInsOff = dstMnp;
            for (int ins = 0; ins < 2; ++ins)
            {
                auto* de = proc.trackInserts_[ti][static_cast<std::size_t>(ins)].get();
                if (!de) continue;
                const int dnp = de->numParams();
                if (dstSlot >= dstInsOff && dstSlot < dstInsOff + dnp)
                {
                    enqueueInsertParam(proc, t, ins, dstSlot - dstInsOff, value);
                    return;
                }
                dstInsOff += dnp;
            }
            return;
        }
        // Audio track: FLTR → CHANNEL → ENV (if !hasInternalAmp) → inserts.
        const int chanOff = dstMnp + proc.kFltrSlots;
        const int envOff  = chanOff + proc.kChannelSlots;
        const int ins0Off = envOff + (dm->hasInternalAmp() ? 0 : proc.kEnvSlots);
        if (dstSlot < chanOff)
        {
            enqueueFltrSlot(proc, t, dstSlot - dstMnp, value);
        }
        else if (dstSlot < envOff)
        {
            enqueueChanSlot(proc, t, dstSlot - chanOff, value);
        }
        else if (!dm->hasInternalAmp() && dstSlot < ins0Off)
        {
            enqueueEnvSlot(proc, t, dstSlot - envOff, value);
        }
        else
        {
            int dstInsOff = ins0Off;
            for (int ins = 0; ins < 2; ++ins)
            {
                auto* de = proc.trackInserts_[ti][static_cast<std::size_t>(ins)].get();
                if (!de) continue;
                const int dnp = de->numParams();
                if (dstSlot >= dstInsOff && dstSlot < dstInsOff + dnp)
                {
                    enqueueInsertParam(proc, t, ins, dstSlot - dstInsOff, value);
                    break;
                }
                dstInsOff += dnp;
            }
        }
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
        if (idForSlot(track, slot) == "sample_id" || idForSlot(track, slot) == "slicer_sample_id")
        {
            const int poolSize = samplePool_.size();
            if (poolSize > 0)
                value = std::min(value, static_cast<float>(poolSize - 1));
            else
                value = 0.0f;
        }

        // Tap-fork cycle/self refusal (DESIGN §27): an input_source set to a Track
        // that would close a routing cycle across the mix+tap edge union (or tap
        // itself) is rejected — keep the current stored value. The topo-sort
        // tolerates a cycle defensively, but a clean graph keeps every tap same-block.
        if (idForSlot(track, slot) == kInputSourceSlotId)
        {
            const auto sel = decodeInputSource(value);
            if (sel.kind == InputSourceKind::Track)
            {
                const int to = sel.track;
                bool bad = (to == track) || (to < 0) || (to >= static_cast<int>(kNumTracks));
                if (!bad)
                {
                    auto tap = tapEdges();
                    tap[static_cast<std::size_t>(track)] = to;  // tentative edge
                    bad = routing::hasCycle(routingEdges(), tap);
                }
                if (bad)
                {
                    const auto& bp = sequence().tracks[static_cast<std::size_t>(track)].baseParams;
                    value = (slot < static_cast<int>(bp.size()))
                                ? bp[static_cast<std::size_t>(slot)] : 0.0f;
                }
            }
        }

        // Zero-crossing snap: if the slot is marked zeroCrossingSnap and the
        // machine is a SamplePlayingMachineBase, round the position value to the
        // nearest zero-crossing in the currently-loaded sample.
        {
            const auto ti = static_cast<std::size_t>(track);
            const auto* m = machines_[ti].get();
            if (m != nullptr && slot < m->numParams() && m->paramSpec(slot).zeroCrossingSnap)
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
        if (controlAllActive_ && !(editContext_.isActiveForEditing() && editContext_.heldTrackIndex() == track))
        {
            const juce::String srcId = idForSlot(track, slot);
            if (srcId.isEmpty()) return;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                const int dstSlot = slotForId(t, srcId);
                if (dstSlot < 0) continue;
                const auto ti = static_cast<std::size_t>(t);
                if (editContext_.isActiveForEditing() && editContext_.heldTrackIndex() == t)
                {
                    const int step = editContext_.heldStepIndex();
                    if (step >= 0 && step < kMaxStepsPerTrack)
                    {
                        enqueueStepOverride(*this, t, step, dstSlot, value);
                        editContext_.markParamWritten();
                    }
                }
                else
                {
                    writeParamQueued(*this, t, dstSlot, value);
                }
                (void)ti;
            }
            return;
        }

        const auto ti = static_cast<std::size_t>(track);

        if (editContext_.isActiveForEditing() && editContext_.heldTrackIndex() == track)
        {
            const int step = editContext_.heldStepIndex();
            if (step >= 0 && step < kMaxStepsPerTrack)
            {
                enqueueStepOverride(*this, track, step, slot, value);
                editContext_.markParamWritten();
            }
        }
        else
        {
            auto* wm = machines_[ti].get();
            const int mnp = wm->numParams();
            writeParamQueued(*this, track, slot, value);

            // Recompute slices when a slice-governing base param changes.
            // We pass a temp frame with the new value so the computation sees
            // the latest data even before the queue drains.
            // THREADING-DEBT(8.18): slicePositions_ write races the audio thread;
            // this pre-dates 8.16 and will be fixed in the mutation sweep.
            if (slot < mnp)
            {
                auto updatedParams = sequence().tracks[ti].baseParams;
                if (static_cast<std::size_t>(slot) < updatedParams.size())
                    updatedParams[static_cast<std::size_t>(slot)] = value;
                recomputeSlicesIfNeeded(static_cast<int>(ti), slot, updatedParams);
            }
        }
    }

    void LockstepProcessor::clearParam(int track, int step, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step < 0 || step >= kMaxStepsPerTrack) return;
        if (slot < 0 || slot >= numParams(track)) return;
        EngineCmd c;
        c.op = EngineCmd::Op::ClearStepOverride;
        c.track = static_cast<uint8_t>(track);
        c.aux = static_cast<uint8_t>(step);
        c.slot = static_cast<int16_t>(slot);
        pushEngineCmd(c);
    }

    void LockstepProcessor::clearTrigOverrideField(int track, int step, int field)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step < 0 || step >= kMaxStepsPerTrack) return;
        auto& s = sequence().tracks[static_cast<std::size_t>(track)].steps[static_cast<std::size_t>(step)];
        auto& trig = s.trigOverride;
        switch (field)
        {
            case 0:  trig.noteCount = 0; break;
            case 1:  trig.hasVelocity = false; break;
            case 2:  trig.hasGate = false; break;
            case 4:  s.microOffset = 0.0f; break;  // MicroTime: reset to on-grid
            default: break;
        }
    }

    // ── Swing API (DESIGN §19.2) ────────────────────────────────────────────────
    // "Edit the effective, store the delta" — mirrors the morph qualifier idiom.
    // All methods are message-thread only; audio thread reads Song/Scene directly.

    void LockstepProcessor::setSwingSongAll(float effective)
    {
        song().swing = std::clamp(effective, -0.5f, 0.5f);
    }

    void LockstepProcessor::setSwingSongTrack(int t, float effective)
    {
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
        const float clamped = std::clamp(effective, -0.5f, 0.5f);
        song().tracks[static_cast<std::size_t>(t)].swing = clamped - song().swing - section().swing;
    }

    void LockstepProcessor::setSwingSceneAll(float effective)
    {
        const float clamped = std::clamp(effective, -0.5f, 0.5f);
        section().swing = clamped - song().swing;
    }

    float LockstepProcessor::swingSongAll() const
    {
        return song().swing;
    }

    float LockstepProcessor::swingSongTrackDelta(int t) const
    {
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return 0.0f;
        return song().tracks[static_cast<std::size_t>(t)].swing;
    }

    float LockstepProcessor::swingSceneAllDelta() const
    {
        return section().swing;
    }

    float LockstepProcessor::swingSongTrackShown(int t) const
    {
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return swingSongAll();
        return song().swing + section().swing + song().tracks[static_cast<std::size_t>(t)].swing;
    }

    float LockstepProcessor::swingSceneAllShown() const
    {
        return song().swing + section().swing;
    }

    float LockstepProcessor::swingEffective(int t) const
    {
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return 0.0f;
        const float songAll = song().swing;
        const float songTrk = song().tracks[static_cast<std::size_t>(t)].swing;
        const float sceneAll = section().swing;
        return effectiveSwing(songAll, songTrk, sceneAll);
    }

    // ── End Swing API ────────────────────────────────────────────────────────────

    void LockstepProcessor::writeMorph(int track, int slot, float deltaAbs, float fader)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (slot < 0 || slot >= numParams(track)) return;

        // ~1.5 % dead zone at each extreme: write only to the near pole so
        // that pushing the fader to an end never creates a surprise two-sided
        // entry.  This is also the code path for exactly f=0 and f=1, fixing a
        // latent bug where the proportional split wrote a zero-delta to the far
        // pole and created a spurious map entry.
        static constexpr float kDeadZone = 0.015f;
        const float f = juce::jlimit(0.0f, 1.0f, fader);
        if (f <= kDeadZone || f >= 1.0f - kDeadZone)
        {
            const int pole = (f >= 1.0f - kDeadZone) ? 1 : 0;
            const auto& sc = section();
            const auto key = std::make_pair(track, slot);
            const bool hasA = (sc.morphA.count(key) > 0);
            const bool hasB = (sc.morphB.count(key) > 0);
            const float kitBase = baseParamValue(track, slot);
            const float curPole = (pole == 0)
                                      ? (hasA ? sc.morphA.at(key) : (hasB ? sc.morphB.at(key) : kitBase))
                                      : (hasB ? sc.morphB.at(key) : (hasA ? sc.morphA.at(key) : kitBase));
            const auto spec = paramSpec(track, slot);
            writeMorphPole(track, slot,
                           juce::jlimit(spec.minValue, spec.maxValue, curPole + deltaAbs), pole);
            return;
        }

        const float D = ((1.0f - f) * (1.0f - f)) + (f * f);
        if (D < 1e-6f) return;

        const auto spec = paramSpec(track, slot);
        Scene& sc = section();
        const auto key = std::make_pair(track, slot);
        const bool hasA = (sc.morphA.count(key) > 0);
        const bool hasB = (sc.morphB.count(key) > 0);
        const float kitBase = baseParamValue(track, slot);

        const float aVal = hasA ? sc.morphA.at(key) : (hasB ? sc.morphB.at(key) : kitBase);
        const float bVal = hasB ? sc.morphB.at(key) : (hasA ? sc.morphA.at(key) : kitBase);

        const float da = deltaAbs * (1.0f - f) / D;
        const float db = deltaAbs * f / D;
        sc.morphA[key] = juce::jlimit(spec.minValue, spec.maxValue, aVal + da);
        sc.morphB[key] = juce::jlimit(spec.minValue, spec.maxValue, bVal + db);
    }

    void LockstepProcessor::writeMorphPole(int track, int slot, float value, int pole)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (slot < 0 || slot >= numParams(track)) return;

        const auto spec = paramSpec(track, slot);
        const float v = juce::jlimit(spec.minValue, spec.maxValue, value);
        const auto key = std::make_pair(track, slot);
        if (pole == 0)
            section().morphA[key] = v;
        else
            section().morphB[key] = v;
    }

    int LockstepProcessor::fluidMuteLevelSlot(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return -1;
        const auto* mi = machines_[static_cast<std::size_t>(track)].get();
        if (mi->isMidiOut()) return -1;
        // CHANNEL Level is always at mnp + kFltrSlots (slot 0 of the CHANNEL block).
        return mi->numParams() + kFltrSlots;
    }

    bool LockstepProcessor::hasFluidMute(int track) const
    {
        const int slot = fluidMuteLevelSlot(track);
        if (slot < 0) return false;
        const auto key = std::make_pair(track, slot);
        return section().morphA.count(key) > 0 || section().morphB.count(key) > 0;
    }

    int LockstepProcessor::fluidMutePole(int track) const
    {
        const int slot = fluidMuteLevelSlot(track);
        if (slot < 0) return -1;
        const auto key = std::make_pair(track, slot);
        const auto& sc = section();
        const auto itA = sc.morphA.find(key);
        const auto itB = sc.morphB.find(key);
        const bool hasA = (itA != sc.morphA.end());
        const bool hasB = (itB != sc.morphB.end());
        if (!hasA && !hasB) return -1;
        if (hasA && !hasB) return 0;   // only A authored — A is the silence pole
        if (hasB && !hasA) return 1;   // only B authored — B is the silence pole
        // Both authored: smaller value = silence (0.0 for silence, base≥0 for unity).
        return (itA->second <= itB->second) ? 0 : 1;
    }

    float LockstepProcessor::fluidMuteBlend(int track) const
    {
        const int slot = fluidMuteLevelSlot(track);
        if (slot < 0) return 0.0f;
        return morphEffectiveValue(track, slot);
    }

    void LockstepProcessor::fluidMuteTrack(int track, float fader)
    {
        const int levelSlot = fluidMuteLevelSlot(track);
        if (levelSlot < 0) return;
        const float f = juce::jlimit(0.0f, 1.0f, fader);
        const float base = baseParamValue(track, levelSlot);
        // Near pole = the pole the fader currently favours; silence it, leave far at kit base.
        const int nearPole = (f < 0.5f) ? 0 : 1;
        const int farPole = 1 - nearPole;
        writeMorphPole(track, levelSlot, 0.0f, nearPole);
        writeMorphPole(track, levelSlot, base, farPole);
    }

    void LockstepProcessor::removeMorph(int track, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto key = std::make_pair(track, slot);
        section().morphA.erase(key);
        section().morphB.erase(key);
    }

    void LockstepProcessor::removeMorphPole(int track, int slot, int pole)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto key = std::make_pair(track, slot);
        if (pole == 0) section().morphA.erase(key);
        else section().morphB.erase(key);
    }

    void LockstepProcessor::bakeMorph(int track, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto& sc = section();
        const auto key = std::make_pair(track, slot);
        const bool hasA = (sc.morphA.count(key) > 0);
        const bool hasB = (sc.morphB.count(key) > 0);
        if (!hasA && !hasB) return;
        const float baked = morphEffectiveValue(track, slot);
        removeMorph(track, slot);
        writeParam(track, slot, baked);
    }

    void LockstepProcessor::bakeAllMorph(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        // Collect slots touched in either map before modifying either.
        std::vector<int> slots;
        for (const auto& kv : section().morphA)
            if (kv.first.first == track) slots.push_back(kv.first.second);
        for (const auto& kv : section().morphB)
            if (kv.first.first == track)
            {
                if (std::find(slots.begin(), slots.end(), kv.first.second) == slots.end())
                    slots.push_back(kv.first.second);
            }
        for (int s : slots)
            bakeMorph(track, s);
    }

    void LockstepProcessor::removeAllMorph(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        std::vector<int> slots;
        for (const auto& kv : section().morphA)
            if (kv.first.first == track) slots.push_back(kv.first.second);
        for (const auto& kv : section().morphB)
            if (kv.first.first == track)
            {
                if (std::find(slots.begin(), slots.end(), kv.first.second) == slots.end())
                    slots.push_back(kv.first.second);
            }
        for (int s : slots)
            removeMorph(track, s);
    }

    void LockstepProcessor::removeAllMorphInSong(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        // Morph lives per-scene, keyed by (track, slot). Wipe every entry for this
        // track index across all scenes in the current song. erase_if on a map
        // safely skips entries belonging to other tracks.
        for (auto& sc : song().scenes)
        {
            const auto isThisTrack = [track](const auto& kv) { return kv.first.first == track; };
            std::erase_if(sc.morphA, isThisTrack);
            std::erase_if(sc.morphB, isThisTrack);
        }
    }

    MorphWidgetInfo LockstepProcessor::morphWidgetInfo(int track, int slot) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return {};
        const Scene& sc = section();
        const auto key = std::make_pair(track, slot);
        const auto itA = sc.morphA.find(key);
        const auto itB = sc.morphB.find(key);
        const bool hasA = (itA != sc.morphA.end());
        const bool hasB = (itB != sc.morphB.end());
        if (!hasA && !hasB) return {};
        return { true, hasA, hasB,
                 hasA ? itA->second : 0.0f,
                 hasB ? itB->second : 0.0f };
    }

    float LockstepProcessor::morphEffectiveValue(int track, int slot) const
    {
        const float base = baseParamValue(track, slot);
        const auto& sc = section();
        const auto key = std::make_pair(track, slot);
        const bool hasA = (sc.morphA.count(key) > 0);
        const bool hasB = (sc.morphB.count(key) > 0);
        if (!hasA && !hasB) return base;

        const float f = morphFader();
        const float aVal = hasA ? sc.morphA.at(key) : (hasB ? sc.morphB.at(key) : base);
        const float bVal = hasB ? sc.morphB.at(key) : (hasA ? sc.morphA.at(key) : base);
        if (paramSpec(track, slot).isStepped) return (f < 0.5f) ? aVal : bVal;
        return aVal + (bVal - aVal) * f;
    }

    void LockstepProcessor::writeFillParam(int track, int slot, float value)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (slot < 0 || slot >= numParams(track)) return;
        if (!editContext_.isActiveForEditing() || editContext_.heldTrackIndex() != track) return;
        const int step = editContext_.heldStepIndex();
        if (step < 0 || step >= kMaxStepsPerTrack) return;
        pushEngineCmd({ EngineCmd::Op::SetFillOverride,
                        static_cast<uint8_t>(track),
                        static_cast<uint8_t>(step),
                        0,
                        static_cast<int16_t>(slot),
                        value });
        editContext_.markParamWritten();
    }

    void LockstepProcessor::clearFillParam(int track, int step, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step < 0 || step >= kMaxStepsPerTrack) return;
        if (slot < 0 || slot >= numParams(track)) return;
        pushEngineCmd({ EngineCmd::Op::ClearFillOverride,
                        static_cast<uint8_t>(track),
                        static_cast<uint8_t>(step),
                        0,
                        static_cast<int16_t>(slot),
                        0.0f });
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
        // Replace machine with stub (absent) and reset base params to its defaults.
        setTrackMachine(track, StubMachine::kMachineId);
        // Erase the track's morph layers across every scene in the song. Morph is
        // sound-shaping bound to the kit we just reset; a stale layer (including
        // one in an off-screen scene) would silently colour any track later made
        // in this slot. Patterns are deliberately left intact — the lossy pattern
        // overwrite is the user's obvious choice when they install a new machine.
        removeAllMorphInSong(track);
    }

    void LockstepProcessor::deletePart()
    {
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            setTrackMachine(t, StubMachine::kMachineId);
            removeAllMorphInSong(t);
        }
    }

    void LockstepProcessor::deletePhraseSlot(int track, int phraseIdx)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (phraseIdx < 0 || phraseIdx >= static_cast<int>(kPhrasesPerTrack)) return;
        withQuiescedEngine([&] {
            song().tracks[static_cast<std::size_t>(track)].phrases[static_cast<std::size_t>(phraseIdx)] = Phrase{};
            refreshWorkingFromModel();
        });
    }

    void LockstepProcessor::deleteSceneSlot(int sceneIdx)
    {
        if (sceneIdx < 0 || sceneIdx >= static_cast<int>(kScenesPerSong)) return;
        withQuiescedEngine([&] {
            const int active = activeSectionIdx();
            song().scenes[static_cast<std::size_t>(sceneIdx)] = Scene{};
            // If the deleted scene was active, fall back to scene 0.
            if (sceneIdx == active)
                setActiveScene(0);
        });
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
        section().initialised = true;
    }

    void LockstepProcessor::togglePatternMute(int track)
    {
        setPatternMute(track, !getPatternMute(track));
    }

    void LockstepProcessor::clearStepLocks(int track, int step)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (step < 0 || step >= kMaxStepsPerTrack) return;
        auto& s = sequence().tracks[static_cast<std::size_t>(track)].steps[static_cast<std::size_t>(step)];
        s.overrides = PLock{};
        s.trigOverride = TrigOverride{};
        s.fillOverrides = PLock{};
        s.fillTrigOverride = TrigOverride{};
    }

    void LockstepProcessor::cancelChordCapture(int /*track*/, int /*step*/)
    {
        // With the snapshot-currently-held model, chordCapture_ resets automatically
        // when all MIDI notes are released (heldCount drops to 0). No explicit cancel
        // is needed on step release; the next note-on will start a fresh capture.
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

    void LockstepProcessor::transposeTrack(int track, int semitones)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (semitones == 0) return;
        auto& trk = sequence().tracks[static_cast<std::size_t>(track)];

        const auto shift = [semitones](int n) { return std::clamp(n + semitones, 0, 127); };

        // Track base (mono OEB default) note.
        trk.trigDefaults.note = shift(trk.trigDefaults.note);

        // Every authored note across the whole step array (not just [0,len)) so a
        // later length increase never reveals an un-transposed tail. Steps with no
        // note override (noteCount 0) are untouched. Both trig layers move together.
        for (auto& s : trk.steps)
        {
            for (int n = 0; n < s.trigOverride.noteCount && n < kMaxNotesPerStep; ++n)
                s.trigOverride.notes[static_cast<std::size_t>(n)] =
                    shift(s.trigOverride.notes[static_cast<std::size_t>(n)]);
            for (int n = 0; n < s.fillTrigOverride.noteCount && n < kMaxNotesPerStep; ++n)
                s.fillTrigOverride.notes[static_cast<std::size_t>(n)] =
                    shift(s.fillTrigOverride.notes[static_cast<std::size_t>(n)]);
        }
    }

    void LockstepProcessor::swapSteps(int track, int a, int b)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& trk = sequence().tracks[static_cast<std::size_t>(track)];
        const int len = trk.length;
        if (a < 0 || a >= len || b < 0 || b >= len || a == b) return;
        std::swap(trk.steps[static_cast<std::size_t>(a)], trk.steps[static_cast<std::size_t>(b)]);
    }

    void LockstepProcessor::relocateStepSwap(int track, int anchor, int fromPos, int toPos)
    {
        if (anchor < 0) return;  // no active move session
        // Undo the current placement (step back to its anchor), then swap the
        // anchor with the new destination. Net effect: a single swap(anchor, toPos)
        // relative to the original layout, so cells in between are never disturbed.
        if (fromPos != anchor) swapSteps(track, anchor, fromPos);
        if (toPos != anchor)   swapSteps(track, anchor, toPos);
    }

    // The single UI/edit-path setter for track length (PRINCIPLES §20): updates the
    // working Track.length AND the APVTS trackLength param (read by the audio thread)
    // together, so they can never diverge. All length editors (PHRASELEN band,
    // timeline drag, Func+Phrase+step, double/halve) route through here.
    void LockstepProcessor::setTrackLength(int track, int newLen)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& trk = sequence().tracks[static_cast<std::size_t>(track)];
        const int clamped = std::clamp(newLen, 1, kMaxStepsPerTrack);
        if (clamped == trk.length) return;
        trk.length = clamped;
        if (auto* p = apvts_.getParameter(ParamIDs::trackLength(track)))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(clamped)));
    }

    void LockstepProcessor::setTrackSubdivision(int track, int idx)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& trk = sequence().tracks[static_cast<std::size_t>(track)];
        const int clamped = std::clamp(idx, kSubdivMin, kSubdivMax);
        trk.subdivIndex = clamped;
        if (auto* p = apvts_.getParameter(ParamIDs::trackDivider(track)))
            p->setValueNotifyingHost(static_cast<float>(clamped) / static_cast<float>(kSubdivMax));
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
        setTrackLength(track, newLen);
    }

    void LockstepProcessor::halveTrackLength(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto& trk = sequence().tracks[static_cast<std::size_t>(track)];
        const int len = std::max(1, trk.length);
        setTrackLength(track, std::max(1, len / 2));
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
        const auto ti = static_cast<std::size_t>(track);
        auto* m = machines_[ti].get();
        int n = m->numParams();
        if (!m->isMidiOut())
        {
            n += kFltrSlots + kChannelSlots;
            if (!m->hasInternalAmp())
                n += kEnvSlots;
        }
        for (auto& eff : trackInserts_[ti])
            if (eff) n += eff->numParams();
        return n;
    }

    // Stable string IDs for the 6 FLTR virtual slots.
    static const juce::String kFltrIds[TrackFltrState::kNumSlots] = {
        "lockstep.fltr.mode", "lockstep.fltr.slope", "lockstep.fltr.cutoff",
        "lockstep.fltr.res", "lockstep.fltr.drive", "lockstep.fltr.env"
    };

    // Stable string IDs for CHANNEL and ENV virtual slots (disk IDs preserved from
    // old TrackAmpState for backwards compatibility with saved projects).
    static const juce::String kChanIds[TrackChannelState::kNumSlots] = {
        "lockstep.amp.level", "lockstep.amp.pan",
        "lockstep.amp.sendA", "lockstep.amp.sendB",
        "lockstep.amp.out"
    };
    // A2 (DESIGN §27): CHANNEL "Out" stepped value labels — {Off, Master,
    // Trk1..TrkN}. Encoding matches decodeOutputDest (0=Off, 1=Master, 2+N=Trk).
    static const char* const kOutDestLabels[] = {
        "Off", "Master",
        "Trk1", "Trk2", "Trk3", "Trk4", "Trk5", "Trk6", "Trk7", "Trk8",
        "Trk9", "Trk10", "Trk11", "Trk12", "Trk13", "Trk14", "Trk15", "Trk16"
    };
    static_assert(sizeof(kOutDestLabels) / sizeof(kOutDestLabels[0]) == 2 + kNumTracks,
                  "Out-dest labels must cover Off + Master + every track");
    static const juce::String kEnvIds[TrackEnvState::kNumSlots] = {
        "lockstep.amp.gate", "lockstep.amp.att", "lockstep.amp.hld",
        "lockstep.amp.dec",  "lockstep.amp.sus", "lockstep.amp.rel"
    };

    ParamSpec LockstepProcessor::paramSpec(int track, int index) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};
        const auto ti = static_cast<std::size_t>(track);
        auto* m = machines_[ti].get();
        const int mnp = m->numParams();
        if (index < mnp)
            return m->paramSpec(index);
        if (m->isMidiOut())
        {
            // MIDI-out: inserts only after machine params.
            int insOff = mnp;
            for (int s = 0; s < 2; ++s)
            {
                auto* eff = trackInserts_[ti][static_cast<std::size_t>(s)].get();
                if (!eff) continue;
                const int insnp = eff->numParams();
                if (index >= insOff && index < insOff + insnp)
                    return eff->paramSpec(index - insOff);
                insOff += insnp;
            }
            return {};
        }

        const int fltrOff = mnp;
        const int chanOff = mnp + kFltrSlots;
        const int envOff  = chanOff + kChannelSlots;
        const int ins0Off = envOff + (m->hasInternalAmp() ? 0 : kEnvSlots);

        // FLTR block — always present on audio tracks.
        if (index >= fltrOff && index < fltrOff + kFltrSlots)
        {
            const int fs = index - fltrOff;
            ParamSpec p;
            p.sectionIndex = kFltrSecIdx;
            p.id = kFltrIds[fs];
            switch (fs)
            {
                case 0:
                    p.label = "Mode";
                    p.isStepped = true;
                    p.maxValue = 4.0f;  // 0=LP 1=HP 2=BP 3=NO 4=OFF
                    p.defaultValue = 4.0f;
                    break;
                case 1:
                    p.label = "Slope";
                    p.isStepped = true;
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    break;
                case 2:
                    p.label = "Cutoff";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    break;
                case 3:
                    p.label = "Reson";
                    p.maxValue = 1.0f;
                    break;
                case 4:
                    p.label = "Drive";
                    p.maxValue = 1.0f;
                    break;
                case 5:
                    p.label = "Env>Ct";
                    p.minValue = -1.0f;
                    p.maxValue = 1.0f;
                    break;
                default: break;
            }
            return p;
        }

        // CHANNEL block — always present on audio tracks (level, pan, sendA, sendB).
        if (index >= chanOff && index < chanOff + kChannelSlots)
        {
            const int cs = index - chanOff;
            ParamSpec p;
            p.sectionIndex = kAmpSecIdx;
            p.id = kChanIds[cs];
            switch (cs)
            {
                case 0:
                    p.label = "Level";
                    p.maxValue = 2.0f;
                    p.defaultValue = 1.0f;
                    p.role = ParamSpec::Role::Level;
                    break;
                case 1:
                    p.label = "Pan";
                    p.minValue = -1.0f;
                    p.maxValue = 1.0f;
                    p.role = ParamSpec::Role::Pan;
                    break;
                case 2:
                    p.label = "Send A";
                    p.maxValue = 1.0f;
                    break;
                case 3:
                    p.label = "Send B";
                    p.maxValue = 1.0f;
                    break;
                case 4:
                    // A2: output destination (DESIGN §27). Stepped enum:
                    // 0=Off, 1=Master, 2+N=Track N.
                    p.label = "Out";
                    p.isStepped = true;
                    p.maxValue = static_cast<float>(1 + kNumTracks);  // Off..Trk16
                    p.defaultValue = 1.0f;                            // Master
                    p.valueLabels = kOutDestLabels;
                    break;
                default: break;
            }
            return p;
        }

        // ENV block — only for machines without internal amp.
        if (!m->hasInternalAmp() && index >= envOff && index < envOff + kEnvSlots)
        {
            const int es = index - envOff;
            ParamSpec p;
            p.sectionIndex = kAmpSecIdx;
            p.id = kEnvIds[es];
            switch (es)
            {
                case 0:
                    p.label = "Gate";
                    p.isStepped = true;
                    p.maxValue = 1.0f;
                    break;
                case 1:
                    p.label = "Attack";
                    p.maxValue = 1000.0f;
                    p.defaultValue = 1.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Attack;
                    break;
                case 2:
                    p.label = "Hold";
                    p.maxValue = 1000.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Hold;
                    break;
                case 3:
                    p.label = "Decay";
                    p.maxValue = 2000.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Decay;
                    break;
                case 4:
                    p.label = "Sustain";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    p.role = ParamSpec::Role::Sustain;
                    break;
                case 5:
                    p.label = "Release";
                    p.maxValue = 2000.0f;
                    p.defaultValue = 10.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Release;
                    break;
                default: break;
            }
            return p;
        }

        // Insert params.
        {
            int insOff = ins0Off;
            for (int s = 0; s < 2; ++s)
            {
                auto* eff = trackInserts_[ti][static_cast<std::size_t>(s)].get();
                if (!eff) continue;
                const int insnp = eff->numParams();
                if (index >= insOff && index < insOff + insnp)
                    return eff->paramSpec(index - insOff);
                insOff += insnp;
            }
        }

        return {};
    }

    // Returns {mHasFltr, mHasAmp}: whether any machine paramSpec has sectionIndex
    // == kFltrSecIdx (2) or kAmpSecIdx (3). Determines who owns canonical sections
    // 2/3 and whether virtual extension sections are needed for the track DSP blocks.
    static std::pair<bool, bool> machineOwnsFltrAmp(const IMachine& m) noexcept
    {
        bool hasFltr = false, hasAmp = false;
        const int mnp = m.numParams();
        for (int i = 0; i < mnp && !(hasFltr && hasAmp); ++i)
        {
            const int si = m.paramSpec(i).sectionIndex;
            if (si == 2) hasFltr = true;
            if (si == 3) hasAmp  = true;
        }
        return { hasFltr, hasAmp };
    }

    int LockstepProcessor::numSections(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return 0;
        auto* m = machines_[static_cast<std::size_t>(track)].get();
        if (m->isMidiOut())
            return m->numSections();
        // Virtual extension sections (parentCanonical = 2 or 3) are appended at
        // indices >= kMaxSections so sectionsForKey() can find them.
        const auto [mHasFltr, mHasAmp] = machineOwnsFltrAmp(*m);
        const int base = std::max(m->numSections(), IMachine::kMaxSections);
        return base + (mHasFltr ? 1 : 0) + (mHasAmp ? 1 : 0);
    }

    // Computes SectionInfo including firstSlot and pageCount from the machine's ParamSpec list.
    SectionInfo LockstepProcessor::section(int track, int sectionIndex) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};
        const auto ti = static_cast<std::size_t>(track);
        auto* m = machines_[ti].get();

        const int mnp = m->numParams();

        // 6.5: FX section (canonical section 5) shows insert params.
        if (sectionIndex == kFxSecIdx)
        {
            const int ins1Off = insertParamOffset(track, 0);
            auto* e1 = trackInserts_[ti][0].get();
            auto* e2 = trackInserts_[ti][1].get();
            const int np1 = e1 ? e1->numParams() : 0;
            const int np2 = e2 ? e2->numParams() : 0;
            if (np1 + np2 == 0) return {};
            return { "FX", ins1Off, (np1 + np2 + kParamsPerPage - 1) / kParamsPerPage, -1 };
        }

        if (m->isMidiOut())
        {
            SectionInfo info = m->section(sectionIndex);
            info.firstSlot = -1;
            int count = 0;
            for (int i = 0; i < mnp; ++i)
            {
                if (m->paramSpec(i).sectionIndex == sectionIndex)
                {
                    if (info.firstSlot < 0) info.firstSlot = i;
                    ++count;
                }
            }
            info.pageCount = (count + kParamsPerPage - 1) / kParamsPerPage;
            if (info.firstSlot >= 0 && info.pageCount < 1) info.pageCount = 1;
            return info;
        }

        const auto [mHasFltr, mHasAmp] = machineOwnsFltrAmp(*m);
        const int chanOff  = mnp + kFltrSlots;
        const int ampSlots = kChannelSlots + (m->hasInternalAmp() ? 0 : kEnvSlots);

        // Canonical FLTR section (2): track block owns it unless machine has params there.
        if (sectionIndex == kFltrSecIdx && !mHasFltr)
            return { "FLTR", mnp, (kFltrSlots + kParamsPerPage - 1) / kParamsPerPage, -1 };

        // Canonical AMP section (3): track block owns it unless machine has params there.
        if (sectionIndex == kAmpSecIdx && !mHasAmp)
            return { "AMP", chanOff, (ampSlots + kParamsPerPage - 1) / kParamsPerPage, -1 };

        // Machine-owned canonical sections.
        if (sectionIndex < m->numSections())
        {
            SectionInfo info = m->section(sectionIndex);
            info.firstSlot = -1;
            int count = 0;
            for (int i = 0; i < mnp; ++i)
            {
                if (m->paramSpec(i).sectionIndex == sectionIndex)
                {
                    if (info.firstSlot < 0) info.firstSlot = i;
                    ++count;
                }
            }
            info.pageCount = (count + kParamsPerPage - 1) / kParamsPerPage;
            if (info.firstSlot >= 0 && info.pageCount < 1) info.pageCount = 1;
            return info;
        }

        // Virtual extension sections: track DSP blocks appended when machine owns
        // canonical sections 2/3. Indices start at max(m->numSections(), kMaxSections)
        // so sectionsForKey()'s extension loop (s >= kMaxSections) finds them.
        {
            const int base = std::max(m->numSections(), IMachine::kMaxSections);
            int virtIdx = sectionIndex - base;
            if (virtIdx >= 0)
            {
                if (mHasFltr)
                {
                    if (virtIdx == 0)
                        return { "FLTR", mnp,
                                 (kFltrSlots + kParamsPerPage - 1) / kParamsPerPage,
                                 kFltrSecIdx };
                    --virtIdx;
                }
                if (mHasAmp && virtIdx == 0)
                    return { "AMP", chanOff,
                             (ampSlots + kParamsPerPage - 1) / kParamsPerPage,
                             kAmpSecIdx };
            }
        }

        return {};
    }

    juce::String LockstepProcessor::idForSlot(int track, int index) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};
        const auto ti = static_cast<std::size_t>(track);
        auto* m = machines_[ti].get();
        const int mnp = m->numParams();
        if (index < mnp)
            return m->idForSlot(index);
        if (m->isMidiOut())
        {
            // MIDI-out: inserts only.
            int insOff = mnp;
            for (int s = 0; s < 2; ++s)
            {
                auto* eff = trackInserts_[ti][static_cast<std::size_t>(s)].get();
                if (!eff) continue;
                const int insnp = eff->numParams();
                if (index >= insOff && index < insOff + insnp)
                {
                    const juce::String rawId{ eff->paramSpec(index - insOff).id };
                    if (rawId.isEmpty()) return {};
                    return (s == 0 ? "lockstep.fx0." : "lockstep.fx1.") + rawId;
                }
                insOff += insnp;
            }
            return {};
        }

        const int fltrOff = mnp;
        const int chanOff = mnp + kFltrSlots;
        const int envOff  = chanOff + kChannelSlots;
        const int ins0Off = envOff + (m->hasInternalAmp() ? 0 : kEnvSlots);

        if (index >= fltrOff && index < fltrOff + kFltrSlots)
            return kFltrIds[index - fltrOff];
        if (index >= chanOff && index < chanOff + kChannelSlots)
            return kChanIds[index - chanOff];
        if (!m->hasInternalAmp() && index >= envOff && index < envOff + kEnvSlots)
            return kEnvIds[index - envOff];

        // 6.5: insert params — ID namespaced by slot so Control-All matches same
        // param in the same insert slot across tracks (same effect loaded).
        {
            int insOff = ins0Off;
            for (int s = 0; s < 2; ++s)
            {
                auto* eff = trackInserts_[ti][static_cast<std::size_t>(s)].get();
                if (!eff) continue;
                const int insnp = eff->numParams();
                if (index >= insOff && index < insOff + insnp)
                {
                    const juce::String rawId{ eff->paramSpec(index - insOff).id };
                    if (rawId.isEmpty()) return {};
                    return (s == 0 ? "lockstep.fx0." : "lockstep.fx1.") + rawId;
                }
                insOff += insnp;
            }
        }
        return {};
    }

    int LockstepProcessor::slotForId(int track, const juce::String& id) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return -1;
        const auto ti = static_cast<std::size_t>(track);
        auto* m = machines_[ti].get();
        if (id.startsWith("lockstep.fltr.") && !m->isMidiOut())
        {
            const int mnp = m->numParams();
            for (int fs = 0; fs < kFltrSlots; ++fs)
                if (id == kFltrIds[fs]) return mnp + fs;
            return -1;
        }
        if (id.startsWith("lockstep.amp."))
        {
            const int mnp = m->numParams();
            const int chanOff = mnp + kFltrSlots;
            const int envOff  = chanOff + kChannelSlots;
            for (int cs = 0; cs < kChannelSlots; ++cs)
                if (id == kChanIds[cs]) return chanOff + cs;
            if (!m->hasInternalAmp())
                for (int es = 0; es < kEnvSlots; ++es)
                    if (id == kEnvIds[es]) return envOff + es;
            return -1;
        }
        // 6.5: insert param IDs namespaced as "lockstep.fx0.<paramId>" / "lockstep.fx1.<paramId>".
        if (id.startsWith("lockstep.fx0.") || id.startsWith("lockstep.fx1."))
        {
            const int targetSlot = id.startsWith("lockstep.fx0.") ? 0 : 1;
            auto* eff = trackInserts_[ti][static_cast<std::size_t>(targetSlot)].get();
            if (!eff) return -1;
            const int prefixLen = id.startsWith("lockstep.fx0.") ? 13 : 13;
            const juce::String rawId = id.substring(prefixLen);
            const int insnp = eff->numParams();
            for (int p = 0; p < insnp; ++p)
                if (juce::String(eff->paramSpec(p).id) == rawId)
                    return insertParamOffset(track, targetSlot) + p;
            return -1;
        }
        return m->slotForId(id);
    }

    float LockstepProcessor::baseParamValue(int track, int slot) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0.0f;
        const auto ti = static_cast<std::size_t>(track);
        const auto& bp = sequence().tracks[ti].baseParams;
        if (static_cast<std::size_t>(slot) < bp.size())
            return bp[static_cast<std::size_t>(slot)];
        auto* m = machines_[ti].get();
        const int mnp = m->numParams();
        const int fltrOff = mnp;
        const int chanOff = mnp + kFltrSlots;
        const int envOff  = chanOff + kChannelSlots;
        const int ins0Off = envOff + (m->hasInternalAmp() ? 0 : kEnvSlots);
        if (!m->isMidiOut() && slot >= fltrOff && slot < fltrOff + kFltrSlots)
            return kit(static_cast<int>(ti)).fltrState.getSlot(slot - fltrOff);
        if (!m->isMidiOut() && slot >= chanOff && slot < chanOff + kChannelSlots)
            return kit(static_cast<int>(ti)).channelState.getSlot(slot - chanOff);
        if (!m->hasInternalAmp() && slot >= envOff && slot < envOff + kEnvSlots)
            return kit(static_cast<int>(ti)).envState.getSlot(slot - envOff);
        // 6.5: insert params.
        {
            int insOff = m->isMidiOut() ? mnp : ins0Off;
            for (int s = 0; s < 2; ++s)
            {
                auto* eff = trackInserts_[ti][static_cast<std::size_t>(s)].get();
                if (!eff) continue;
                const int insnp = eff->numParams();
                if (slot >= insOff && slot < insOff + insnp)
                {
                    const auto& ki = kit(static_cast<int>(ti)).inserts[static_cast<std::size_t>(s)];
                    const int p = slot - insOff;
                    if (static_cast<std::size_t>(p) < ki.baseParams.size())
                        return ki.baseParams[static_cast<std::size_t>(p)];
                    return eff->paramSpec(p).defaultValue;
                }
                insOff += insnp;
            }
        }
        return 0.0f;
    }

    void LockstepProcessor::triggerPreview(int poolIndex, int track)
    {
        previewReqTrack_.store(track, std::memory_order_relaxed);
        previewPoolIndex_.store(poolIndex, std::memory_order_release);
    }

    void LockstepProcessor::pushEngineCmd(const EngineCmd& c) noexcept
    {
        int s1, n1, s2, n2;
        engineCmdFifo_.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 > 0)
        {
            engineCmdQueue_[static_cast<std::size_t>(s1)] = c;
            engineCmdFifo_.finishedWrite(1);
        }
        // Queue full → drop + debug assert. A full queue means the audio thread is
        // not running; the stopped-audio fallback (THREADING-DEBT 8.16) will drain it.
        jassert(n1 > 0);
    }

    void LockstepProcessor::drainEngineCmds() noexcept
    {
        int s1, n1, s2, n2;
        engineCmdFifo_.prepareToRead(engineCmdFifo_.getNumReady(), s1, n1, s2, n2);

        auto apply = [this](const EngineCmd& c) {
            const auto t = static_cast<std::size_t>(c.track);
            switch (c.op)
            {
                case EngineCmd::Op::SetBaseParam:
                    if (t < kNumTracks && c.slot >= 0 && static_cast<std::size_t>(c.slot) < sequence().tracks[t].baseParams.size())
                    {
                        sequence().tracks[t].baseParams[static_cast<std::size_t>(c.slot)] = c.value;
                        kit(static_cast<int>(t)).baseParams[static_cast<std::size_t>(c.slot)] = c.value;
                    }
                    break;

                case EngineCmd::Op::SetFltrSlot:
                    if (t < kNumTracks)
                        kit(static_cast<int>(t)).fltrState.setSlot(c.slot, c.value);
                    break;

                case EngineCmd::Op::SetChanSlot:
                    if (t < kNumTracks)
                    {
                        // A2: authoritative refusal for the "Out" slot (cycle /
                        // non-bus target / self) — the race-free backstop.
                        if (c.slot == TrackChannelState::kNumSlots - 1
                            && validateOutEdit(static_cast<int>(t), c.value)
                                   != RouteReject::None)
                            break;
                        kit(static_cast<int>(t)).channelState.setSlot(c.slot, c.value);
                    }
                    break;

                case EngineCmd::Op::SetEnvSlot:
                    if (t < kNumTracks)
                        kit(static_cast<int>(t)).envState.setSlot(c.slot, c.value);
                    break;

                case EngineCmd::Op::SetInsertParam: {
                    if (t >= kNumTracks) break;
                    const auto ins = static_cast<std::size_t>(c.aux);
                    if (ins >= 2) break;
                    auto& bp = kit(static_cast<int>(t)).inserts[ins].baseParams;
                    if (c.slot >= 0 && static_cast<std::size_t>(c.slot) < bp.size())
                        bp[static_cast<std::size_t>(c.slot)] = c.value;
                    break;
                }

                case EngineCmd::Op::SetMasterInsertParam: {
                    if (c.aux >= 2) break;
                    auto& bp = song().masterInserts[static_cast<std::size_t>(c.aux)].baseParams;
                    if (c.slot >= 0 && static_cast<std::size_t>(c.slot) < bp.size())
                        bp[static_cast<std::size_t>(c.slot)] = c.value;
                    break;
                }

                case EngineCmd::Op::SetMasterSendParam: {
                    if (c.aux >= 2) break;
                    auto& bp = song().masterSends[static_cast<std::size_t>(c.aux)].baseParams;
                    if (c.slot >= 0 && static_cast<std::size_t>(c.slot) < bp.size())
                        bp[static_cast<std::size_t>(c.slot)] = c.value;
                    break;
                }

                case EngineCmd::Op::SetStepOverride:
                    if (t < kNumTracks && c.aux < kMaxStepsPerTrack && c.slot >= 0)
                        sequence().tracks[t].steps[static_cast<std::size_t>(c.aux)].overrides.set(c.slot, c.value);
                    break;

                case EngineCmd::Op::ClearStepOverride:
                    if (t < kNumTracks && c.aux < kMaxStepsPerTrack && c.slot >= 0)
                        sequence().tracks[t].steps[static_cast<std::size_t>(c.aux)].overrides.clear(c.slot);
                    break;

                case EngineCmd::Op::SetFillOverride:
                    if (t < kNumTracks && c.aux < kMaxStepsPerTrack && c.slot >= 0)
                        sequence().tracks[t].steps[static_cast<std::size_t>(c.aux)].fillOverrides.set(c.slot, c.value);
                    break;

                case EngineCmd::Op::ClearFillOverride:
                    if (t < kNumTracks && c.aux < kMaxStepsPerTrack && c.slot >= 0)
                        sequence().tracks[t].steps[static_cast<std::size_t>(c.aux)].fillOverrides.clear(c.slot);
                    break;
            }
        };

        for (int i = 0; i < n1; ++i) apply(engineCmdQueue_[static_cast<std::size_t>(s1 + i)]);
        for (int i = 0; i < n2; ++i) apply(engineCmdQueue_[static_cast<std::size_t>(s2 + i)]);
        engineCmdFifo_.finishedRead(n1 + n2);

        // 9.15 Stage 3: applying a queued param change is the discrete event that
        // settles the surface (the write is async, so a UI frame is only correct
        // once it lands here). Mark dirty so the editor refreshes — covers CC
        // writes, encoder writes (final value after the user stops turning), and
        // P-Lock writes without any per-tick poll.
        if ((n1 + n2) > 0)
            surfaceDirtyFromAudio_.store(true, std::memory_order_relaxed);
    }

    void LockstepProcessor::pushKbdCmd(const KbdNoteCmd& c) noexcept
    {
        int s1, n1, s2, n2;
        kbdFifo_.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 > 0)
        {
            kbdQueue_[static_cast<std::size_t>(s1)] = c;
            kbdFifo_.finishedWrite(1);
        }
        // Queue full → drop. A chord is a handful of commands; overflow only under
        // pathological spam, and a dropped audition is harmless.
    }

    void LockstepProcessor::triggerNote(int track, int midiNote, int durationMs, int velocity, bool bypassEditorial)
    {
        KbdNoteCmd c;
        c.track = static_cast<int16_t>(juce::jlimit(0, static_cast<int>(kNumTracks) - 1, track));
        c.note = static_cast<uint8_t>(juce::jlimit(0, 127, midiNote));
        c.velocity = static_cast<uint8_t>(juce::jlimit(1, 127, velocity));
        c.durationMs = static_cast<uint16_t>(juce::jlimit(1, 0xFFFF, durationMs));
        c.bypassEditorial = bypassEditorial;
        pushKbdCmd(c);
    }

    void LockstepProcessor::liveNoteOn(int track, int midiNote, int velocity)
    {
        KbdNoteCmd c;
        c.track = static_cast<int16_t>(juce::jlimit(0, static_cast<int>(kNumTracks) - 1, track));
        c.note = static_cast<uint8_t>(juce::jlimit(0, 127, midiNote));
        c.velocity = static_cast<uint8_t>(juce::jlimit(1, 127, velocity));
        c.durationMs = 0;   // 0 = gate: sustain until the matching liveNoteOff
        pushKbdCmd(c);
    }

    void LockstepProcessor::liveNoteOff(int track, int midiNote)
    {
        KbdNoteCmd c;
        c.track = static_cast<int16_t>(juce::jlimit(0, static_cast<int>(kNumTracks) - 1, track));
        c.note = static_cast<uint8_t>(juce::jlimit(0, 127, midiNote));
        c.noteOff = true;
        pushKbdCmd(c);
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

    void LockstepProcessor::clearTrackAllPhrases(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& st = song().tracks[static_cast<std::size_t>(track)];
        for (auto& phrase : st.phrases)
        {
            for (auto& s : phrase.steps)
            {
                s.trig = false;
                s.condition = TrigCondition{};
                s.overrides = PLock{};
                s.trigOverride = TrigOverride{};
                s.fillTrigState = FillTrigState::Inherit;
                s.fillOverrides = PLock{};
                s.fillTrigOverride = TrigOverride{};
            }
        }
        refreshWorkingFromModel();
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
        const bool isSampleId = (id == "slicer_sample_id");
        const bool isSliceSrc = (id == "slicer_slice_src");
        const bool isSliceCount = (id == "slicer_slice_count");

        if (!isSampleId && !isSliceSrc && !isSliceCount)
            return;

        const int srcSlot = slotForId(track, "slicer_slice_src");
        const int countSlot = slotForId(track, "slicer_slice_count");
        if (srcSlot < 0 || countSlot < 0) return;

        const int src = static_cast<int>(std::round(
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

    // Derive a short display name from a dot-separated machineId string.
    // "lockstep.sampler.v1" → "Sampler", "lockstep.fm.v1" → "FM", etc.
    static juce::String machineShortName(const std::string& machineId)
    {
        const auto s = juce::String(machineId);
        const int first = s.indexOfChar('.');
        const int second = (first >= 0) ? s.indexOfChar(first + 1, '.') : -1;
        if (first < 0) return s;
        const auto mid = (second > first) ? s.substring(first + 1, second) : s.substring(first + 1);
        if (mid.isEmpty()) return s;
        return mid.substring(0, 1).toUpperCase() + mid.substring(1).toLowerCase();
    }

    int LockstepProcessor::saveTrackToSoundPool(int track, const std::string& name)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return -1;
        const auto ti = static_cast<std::size_t>(track);
        const auto& k = kit(track);

        SoundEntry entry;
        entry.machineId = k.machineId;
        entry.baseParams = k.baseParams;
        entry.destinationId = k.destinationId;

        // Extract sample pool index from the first slot of baseParams (sampler tracks).
        if (!machines_[ti]->isMidiOut() && !k.baseParams.empty())
            entry.samplePoolIndex = static_cast<int>(k.baseParams[0]);

        if (!name.empty())
        {
            entry.name = name;
        }
        else
        {
            // Auto-name: "<MachineShort> T<n>", uniquified with a numeric suffix if needed.
            const juce::String base = machineShortName(k.machineId) + " T" + juce::String(track + 1);
            juce::String candidate = base;
            int suffix = 2;
            const int n = project_.soundPool.size();
            bool conflict = true;
            while (conflict)
            {
                conflict = false;
                for (int i = 0; i < n; ++i)
                {
                    const auto* e = project_.soundPool.get(i);
                    if (e && juce::String(e->name) == candidate)
                    {
                        conflict = true;
                        break;
                    }
                }
                if (conflict)
                    candidate = base + " " + juce::String(suffix++);
            }
            entry.name = candidate.toStdString();
        }

        return project_.soundPool.push(std::move(entry));
    }

    void LockstepProcessor::removeSoundEntry(int i)
    {
        if (i < 0 || i >= project_.soundPool.size()) return;
        withQuiescedEngine([&] {
            remapSoundIdsAfterRemoval(arrangement_, i);
            project_.soundPool.remove(i);
        });
    }

    void LockstepProcessor::renameSoundEntry(int i, const std::string& newName)
    {
        const auto* e = project_.soundPool.get(i);
        if (!e) return;
        // name is only read on the message thread (UI), so a direct write is safe.
        project_.soundPool.entries[static_cast<std::size_t>(i)].name = newName;
    }

    void LockstepProcessor::liveSwapTrackSound(int track, int poolIndex)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto* e = project_.soundPool.get(poolIndex);
        if (e == nullptr) return;
        const auto ti = static_cast<std::size_t>(track);
        if (e->machineId != kit(track).machineId) return;
        // Write directly to sequence track — same pattern as writeParam().
        // Audio thread picks up the new baseParams on the next processBlock.
        sequence().tracks[ti].baseParams = e->baseParams;
    }

    void LockstepProcessor::clearLiveSwap(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto ti = static_cast<std::size_t>(track);
        sequence().tracks[ti].baseParams = kit(track).baseParams;
    }

    bool LockstepProcessor::recallSoundFromPool(int track, int entryIndex)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto* e = project_.soundPool.get(entryIndex);
        if (e == nullptr) return false;

        const auto ti = static_cast<std::size_t>(track);
        auto& k = kit(track);

        // Only apply if machine types match to avoid mismatched param frames.
        if (k.machineId != e->machineId) return false;

        k.baseParams = e->baseParams;
        k.destinationId = e->destinationId;

        // Sync the sequence track's base params so the audio thread picks it up.
        sequence().tracks[ti].baseParams = e->baseParams;
        return true;
    }

    void LockstepProcessor::setRetrigActive(int track, bool active, double ratePpq, int note)
    {
        if (active)
        {
            retrigReqRatePpq_.store(ratePpq, std::memory_order_relaxed);
            retrigReqNote_.store(juce::jlimit(0, 127, note), std::memory_order_relaxed);
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
                if (c == idx) return static_cast<float>(std::max(0, std::min(c, newMax)));
                if (c > idx) return static_cast<float>(c - 1);
                return cur;
            };

            // Remap Kit baseParams and Phrase step P-locks across all songs.
            for (auto& song : arrangement_.songs)
            {
                auto& tk = song.tracks[static_cast<std::size_t>(t)];
                auto& kbp = tk.kit.baseParams;
                if (slotSz < kbp.size()) kbp[slotSz] = remapPoolIdx(kbp[slotSz]);
                for (auto& phrase : tk.phrases)
                {
                    for (auto& step : phrase.steps)
                    {
                        if (!step.overrides.has(sampleSlot)) continue;
                        step.overrides.set(sampleSlot,
                                           remapPoolIdx(step.overrides.get(sampleSlot, 0.0f)));
                    }
                }
            }
            // Remap the working buffer.
            {
                auto& seqTrk = sequence().tracks[static_cast<std::size_t>(t)];
                if (slotSz < seqTrk.baseParams.size())
                    seqTrk.baseParams[slotSz] = remapPoolIdx(seqTrk.baseParams[slotSz]);
                for (auto& step : seqTrk.steps)
                {
                    if (!step.overrides.has(sampleSlot)) continue;
                    step.overrides.set(sampleSlot,
                                       remapPoolIdx(step.overrides.get(sampleSlot, 0.0f)));
                }
            }
        }
        samplePool_.remove(idx);
        // The reserved REC slots are addressed by ordinal via nthVolatileIndex,
        // so a file removal that shifts their absolute indices needs no fix-up.
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

            // Swap Kit baseParams and Phrase step P-locks across all songs.
            for (auto& song : arrangement_.songs)
            {
                auto& tk = song.tracks[static_cast<std::size_t>(t)];
                auto& kbp = tk.kit.baseParams;
                if (slotSz < kbp.size()) kbp[slotSz] = swapPoolIdx(kbp[slotSz]);
                for (auto& phrase : tk.phrases)
                {
                    for (auto& step : phrase.steps)
                    {
                        if (!step.overrides.has(sampleSlot)) continue;
                        step.overrides.set(sampleSlot,
                                           swapPoolIdx(step.overrides.get(sampleSlot, 0.0f)));
                    }
                }
            }
            // Swap the working buffer.
            {
                auto& seqTrk = sequence().tracks[static_cast<std::size_t>(t)];
                if (slotSz < seqTrk.baseParams.size())
                    seqTrk.baseParams[slotSz] = swapPoolIdx(seqTrk.baseParams[slotSz]);
                for (auto& step : seqTrk.steps)
                {
                    if (!step.overrides.has(sampleSlot)) continue;
                    step.overrides.set(sampleSlot,
                                       swapPoolIdx(step.overrides.get(sampleSlot, 0.0f)));
                }
            }
        }
        samplePool_.swap(a, b);
        // REC slots are addressed by ordinal (nthVolatileIndex), so a swap of
        // file entries needs no slot-map fix-up.
    }

    bool LockstepProcessor::relinkSample(int index, const juce::String& newPath)
    {
        return samplePool_.relink(index, newPath);
    }

    // Install machines whose IDs match the active Kit's machineId per track.
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
        if (id == ThruMachine::kMachineId)
            return std::make_unique<ThruMachine>();
        if (id == RecorderMachine::kMachineId)
            return std::make_unique<RecorderMachine>(pool);
        if (id == LooperMachine::kMachineId)
            return std::make_unique<LooperMachine>(pool);
        if (id == StaticMachine::kMachineId)
            return std::make_unique<StaticMachine>();
        if (id == PlayerMachine::kMachineId)
            return std::make_unique<PlayerMachine>(pool);
        // "lockstep.stub" is an explicitly-empty track (unknownId = "").
        // Any other unrecognised ID keeps its original id as the unknownId.
        if (id == StubMachine::kMachineId)
            return std::make_unique<StubMachine>("");
        return std::make_unique<StubMachine>(id);
    }

    // -------------------------------------------------------------------------
    // MGX.6 — machine selection

    static constexpr LockstepProcessor::MachineInfo kAvailableMachines[] = {
        { SamplerMachine::kMachineId, "Sampler" },
        { SlicerMachine::kMachineId, "Slicer" },
        { FMMachine::kMachineId, "FM Synth" },
        { VAMachine::kMachineId, "VA Synth" },
        { DrumSynthMachine::kMachineId, "Drum Synth" },
        { ThruMachine::kMachineId, "Thru" },
        { RecorderMachine::kMachineId, "Recorder" },
        { LooperMachine::kMachineId, "Looper" },
        { StaticMachine::kMachineId, "Static" },
        { PlayerMachine::kMachineId, "Player" },
        { MidiOutMachine::kMachineId, "MIDI Out" },
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

    const char* LockstepProcessor::getMachineIdRaw(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return "";
        const auto* m = machines_[static_cast<std::size_t>(track)].get();
        return m ? m->machineId() : "";
    }

    const char* LockstepProcessor::trackBadge(int track) const noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return "";
        const auto* m = machines_[static_cast<std::size_t>(track)].get();
        return m ? m->badge() : "";
    }

    const IMachine* LockstepProcessor::machineForTrack(int track) const noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return nullptr;
        return machines_[static_cast<std::size_t>(track)].get();
    }

    // =========================================================================
    // 6.5 — FX insert management
    // =========================================================================

    int LockstepProcessor::insertParamOffset(int track, int insSlot) const noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0;
        const auto ti = static_cast<std::size_t>(track);
        auto* m = machines_[ti].get();
        const int mnp = m->numParams();
        int off = m->isMidiOut()
                    ? mnp
                    : mnp + kFltrSlots + kChannelSlots + (m->hasInternalAmp() ? 0 : kEnvSlots);
        for (int s = 0; s < insSlot && s < 2; ++s)
        {
            auto* eff = trackInserts_[ti][static_cast<std::size_t>(s)].get();
            if (eff) off += eff->numParams();
        }
        return off;
    }

    void LockstepProcessor::setTrackInsert(int track, int slot, const std::string& effectId)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (slot < 0 || slot > 1) return;
        const auto ti = static_cast<std::size_t>(track);
        const auto si = static_cast<std::size_t>(slot);

        auto& kitSlot = kit(track).inserts[si];
        kitSlot.effectId = effectId;

        auto newEff = makeEffectForId(effectId);
        if (newEff)
        {
            // Resize baseParams and fill defaults if slot is first-time.
            const int np = newEff->numParams();
            if (static_cast<int>(kitSlot.baseParams.size()) != np)
            {
                kitSlot.baseParams.resize(static_cast<std::size_t>(np));
                for (int p = 0; p < np; ++p)
                    kitSlot.baseParams[static_cast<std::size_t>(p)] = newEff->paramSpec(p).defaultValue;
            }
            newEff->prepare(preparedSampleRate_, preparedBlockSize_);
        }
        withQuiescedEngine([&] { trackInserts_[ti][si] = std::move(newEff); });
    }

    void LockstepProcessor::clearTrackInsert(int track, int slot)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (slot < 0 || slot > 1) return;
        const auto ti = static_cast<std::size_t>(track);
        const auto si = static_cast<std::size_t>(slot);
        kit(track).inserts[si] = {};
        withQuiescedEngine([&] { trackInserts_[ti][si].reset(); });
    }

    void LockstepProcessor::setTrackInsertBypass(int track, int slot, bool bypass)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (slot < 0 || slot > 1) return;
        kit(track).inserts[static_cast<std::size_t>(slot)].bypass = bypass;
    }

    std::string LockstepProcessor::trackInsertId(int track, int slot) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return {};
        if (slot < 0 || slot > 1) return {};
        return kit(track).inserts[static_cast<std::size_t>(slot)].effectId;
    }

    bool LockstepProcessor::trackInsertBypass(int track, int slot) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        if (slot < 0 || slot > 1) return false;
        return kit(track).inserts[static_cast<std::size_t>(slot)].bypass;
    }

    // -------------------------------------------------------------------------
    // 6.5 master FX bus
    // -------------------------------------------------------------------------
    void LockstepProcessor::setMasterInsert(int slot, const std::string& effectId)
    {
        if (slot < 0 || slot > 1) return;
        const auto si = static_cast<std::size_t>(slot);
        const std::string id = canonicalEffectId(effectId);
        auto& insSlot = song().masterInserts[si];
        insSlot.effectId = id;

        auto newEff = makeEffectForId(id, EffectTier::Master);
        if (newEff)
        {
            const int np = newEff->numParams();
            if (static_cast<int>(insSlot.baseParams.size()) != np)
            {
                insSlot.baseParams.resize(static_cast<std::size_t>(np));
                for (int p = 0; p < np; ++p)
                    insSlot.baseParams[static_cast<std::size_t>(p)] = newEff->paramSpec(p).defaultValue;
            }
            newEff->prepare(preparedSampleRate_, preparedBlockSize_);
        }
        withQuiescedEngine([&] { masterInserts_[si] = std::move(newEff); });
    }

    void LockstepProcessor::clearMasterInsert(int slot)
    {
        if (slot < 0 || slot > 1) return;
        const auto si = static_cast<std::size_t>(slot);
        song().masterInserts[si] = {};
        withQuiescedEngine([&] { masterInserts_[si].reset(); });
    }

    void LockstepProcessor::setMasterInsertBypass(int slot, bool bypass)
    {
        if (slot < 0 || slot > 1) return;
        song().masterInserts[static_cast<std::size_t>(slot)].bypass = bypass;
    }

    std::string LockstepProcessor::masterInsertId(int slot) const
    {
        if (slot < 0 || slot > 1) return {};
        return song().masterInserts[static_cast<std::size_t>(slot)].effectId;
    }

    bool LockstepProcessor::masterInsertBypass(int slot) const
    {
        if (slot < 0 || slot > 1) return false;
        return song().masterInserts[static_cast<std::size_t>(slot)].bypass;
    }

    void LockstepProcessor::processMasterChain(juce::AudioBuffer<float>& buf, int numSamples)
    {
        // 8.26: process each send bus through its return effect, then sum into master.
        for (int snd = 0; snd < 2; ++snd)
        {
            auto* seff = masterSends_[static_cast<std::size_t>(snd)].get();
            auto& sendBuf = sendBusBufs_[static_cast<std::size_t>(snd)];
            if (!seff)
            {
                sendBuf.clear();
                continue;
            }
            const auto& sSlot = song().masterSends[static_cast<std::size_t>(snd)];
            if (sSlot.bypass)
            {
                sendBuf.clear();
                continue;
            }
            const int snp = seff->numParams();
            ParamFrame sfxFrame(static_cast<std::size_t>(snp));
            for (int p = 0; p < snp; ++p)
                sfxFrame[static_cast<std::size_t>(p)] =
                    (static_cast<std::size_t>(p) < sSlot.baseParams.size())
                        ? sSlot.baseParams[static_cast<std::size_t>(p)]
                        : seff->paramSpec(p).defaultValue;
            seff->process(sendBuf, numSamples, sfxFrame);
            // Sum processed send return into master.
            const int numCh = std::min(buf.getNumChannels(), sendBuf.getNumChannels());
            for (int ch = 0; ch < numCh; ++ch)
                buf.addFrom(ch, 0, sendBuf, ch, 0, numSamples);
            sendBuf.clear();
        }

        // Master inserts (in-line, post send-return sum).
        for (int ins = 0; ins < 2; ++ins)
        {
            auto* meff = masterInserts_[static_cast<std::size_t>(ins)].get();
            if (!meff) continue;
            const auto& mSlot = song().masterInserts[static_cast<std::size_t>(ins)];
            if (mSlot.bypass) continue;
            const int mnp = meff->numParams();
            ParamFrame mfxFrame(static_cast<std::size_t>(mnp));
            for (int p = 0; p < mnp; ++p)
                mfxFrame[static_cast<std::size_t>(p)] =
                    (static_cast<std::size_t>(p) < mSlot.baseParams.size())
                        ? mSlot.baseParams[static_cast<std::size_t>(p)]
                        : meff->paramSpec(p).defaultValue;
            meff->process(buf, numSamples, mfxFrame);
        }
    }

    int LockstepProcessor::masterInsertNumParams(int slot) const
    {
        if (slot < 0 || slot > 1) return 0;
        auto* eff = masterInserts_[static_cast<std::size_t>(slot)].get();
        return eff ? eff->numParams() : 0;
    }

    float LockstepProcessor::masterInsertParam(int slot, int param) const
    {
        if (slot < 0 || slot > 1) return 0.0f;
        const auto si = static_cast<std::size_t>(slot);
        const auto& insSlot = song().masterInserts[si];
        if (param < 0 || static_cast<std::size_t>(param) >= insSlot.baseParams.size()) return 0.0f;
        return insSlot.baseParams[static_cast<std::size_t>(param)];
    }

    ParamSpec LockstepProcessor::masterInsertParamSpec(int slot, int param) const
    {
        if (slot < 0 || slot > 1) return {};
        auto* eff = masterInserts_[static_cast<std::size_t>(slot)].get();
        if (!eff || param < 0 || param >= eff->numParams()) return {};
        return eff->paramSpec(param);
    }

    void LockstepProcessor::setMasterInsertParam(int slot, int param, float value)
    {
        if (slot < 0 || slot > 1) return;
        const auto si = static_cast<std::size_t>(slot);
        auto& insSlot = song().masterInserts[si];
        if (param < 0 || static_cast<std::size_t>(param) >= insSlot.baseParams.size()) return;
        // Write immediately so the 30 Hz MZ timer reads the new value without
        // waiting for the audio-thread drain (mirrors the swing direct-write pattern).
        insSlot.baseParams[static_cast<std::size_t>(param)] = value;
        pushEngineCmd({ EngineCmd::Op::SetMasterInsertParam,
                        0,
                        static_cast<uint8_t>(slot),
                        0,
                        static_cast<int16_t>(param),
                        value });
    }

    // ── 8.26 Master send-return API ─────────────────────────────────────────────

    void LockstepProcessor::setMasterSend(int slot, const std::string& effectId)
    {
        if (slot < 0 || slot > 1) return;
        const auto si = static_cast<std::size_t>(slot);
        const std::string id = canonicalEffectId(effectId);
        auto& sndSlot = song().masterSends[si];
        sndSlot.effectId = id;

        auto newEff = makeEffectForId(id, EffectTier::Master);
        if (newEff)
        {
            // Preserve loaded/edited params when the schema size already matches
            // (a reload or re-pick of the same effect keeps its values); only fill
            // defaults on a first-time / size-changed slot. Mirrors setMasterInsert
            // so finishStateLoad can reinstall a loaded send without wiping params.
            const int np = newEff->numParams();
            if (static_cast<int>(sndSlot.baseParams.size()) != np)
            {
                sndSlot.baseParams.resize(static_cast<std::size_t>(np));
                for (int p = 0; p < np; ++p)
                    sndSlot.baseParams[static_cast<std::size_t>(p)] = newEff->paramSpec(p).defaultValue;
            }
            newEff->prepare(preparedSampleRate_, preparedBlockSize_);
        }
        // Empty / unknown id leaves newEff null → the move clears the live slot.
        withQuiescedEngine([&] { masterSends_[si] = std::move(newEff); });
    }

    void LockstepProcessor::clearMasterSend(int slot)
    {
        if (slot < 0 || slot > 1) return;
        const auto si = static_cast<std::size_t>(slot);
        song().masterSends[si] = {};
        withQuiescedEngine([&] { masterSends_[si].reset(); });
    }

    void LockstepProcessor::setMasterSendBypass(int slot, bool bypass)
    {
        if (slot < 0 || slot > 1) return;
        song().masterSends[static_cast<std::size_t>(slot)].bypass = bypass;
    }

    std::string LockstepProcessor::masterSendId(int slot) const
    {
        if (slot < 0 || slot > 1) return {};
        return song().masterSends[static_cast<std::size_t>(slot)].effectId;
    }

    bool LockstepProcessor::hasLiveTrackInsert(int track, int slot) const noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        if (slot < 0 || slot > 1) return false;
        return trackInserts_[static_cast<std::size_t>(track)][static_cast<std::size_t>(slot)] != nullptr;
    }

    bool LockstepProcessor::hasLiveMasterInsert(int slot) const noexcept
    {
        if (slot < 0 || slot > 1) return false;
        return masterInserts_[static_cast<std::size_t>(slot)] != nullptr;
    }

    bool LockstepProcessor::hasLiveMasterSend(int slot) const noexcept
    {
        if (slot < 0 || slot > 1) return false;
        return masterSends_[static_cast<std::size_t>(slot)] != nullptr;
    }

    bool LockstepProcessor::masterSendBypass(int slot) const
    {
        if (slot < 0 || slot > 1) return false;
        return song().masterSends[static_cast<std::size_t>(slot)].bypass;
    }

    int LockstepProcessor::masterSendNumParams(int slot) const
    {
        if (slot < 0 || slot > 1) return 0;
        auto* eff = masterSends_[static_cast<std::size_t>(slot)].get();
        return eff ? eff->numParams() : 0;
    }

    float LockstepProcessor::masterSendParam(int slot, int param) const
    {
        if (slot < 0 || slot > 1) return 0.0f;
        const auto si = static_cast<std::size_t>(slot);
        const auto& sndSlot = song().masterSends[si];
        if (param < 0 || static_cast<std::size_t>(param) >= sndSlot.baseParams.size()) return 0.0f;
        return sndSlot.baseParams[static_cast<std::size_t>(param)];
    }

    ParamSpec LockstepProcessor::masterSendParamSpec(int slot, int param) const
    {
        if (slot < 0 || slot > 1) return {};
        auto* eff = masterSends_[static_cast<std::size_t>(slot)].get();
        if (!eff || param < 0 || param >= eff->numParams()) return {};
        return eff->paramSpec(param);
    }

    void LockstepProcessor::setMasterSendParam(int slot, int param, float value)
    {
        if (slot < 0 || slot > 1) return;
        const auto si = static_cast<std::size_t>(slot);
        auto& sndSlot = song().masterSends[si];
        if (param < 0 || static_cast<std::size_t>(param) >= sndSlot.baseParams.size()) return;
        sndSlot.baseParams[static_cast<std::size_t>(param)] = value;
        pushEngineCmd({ EngineCmd::Op::SetMasterSendParam,
                        0,
                        static_cast<uint8_t>(slot),
                        0,
                        static_cast<int16_t>(param),
                        value });
    }

    int LockstepProcessor::numAvailableEffects() const { return lockstep::numAvailableEffects(); }

    EffectInfo LockstepProcessor::availableEffectInfo(int idx) const
    {
        return lockstep::availableEffectInfo(idx);
    }

    void LockstepProcessor::setTrackDensity(int track, float amount) noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) { return; }
        const float clamped = juce::jlimit(0.01f, 1.0f, amount);
        trackDensity_[static_cast<std::size_t>(track)].store(clamped, std::memory_order_relaxed);
        arrangement_.liveDensity[static_cast<std::size_t>(track)] = clamped;
    }

    float LockstepProcessor::trackDensity(int track) const noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) { return 1.0f; }
        return trackDensity_[static_cast<std::size_t>(track)].load(std::memory_order_relaxed);
    }

    void LockstepProcessor::setMasterDensity(float offset) noexcept
    {
        const float clamped = juce::jlimit(-1.0f, 1.0f, offset);
        masterDensity_.store(clamped, std::memory_order_relaxed);
        arrangement_.liveMasterDensity = clamped;
    }

    float LockstepProcessor::masterDensity() const noexcept
    {
        return masterDensity_.load(std::memory_order_relaxed);
    }

    std::unique_ptr<IMachine> LockstepProcessor::createMachineForId(const std::string& id)
    {
        return makeMachineForId(id, samplePool_);
    }

    int LockstepProcessor::slotForIdWithMachine(const IMachine& m, const juce::String& id) const
    {
        const int mnp = m.numParams();
        const int chanOff = mnp + kFltrSlots;
        const int envOff  = chanOff + kChannelSlots;
        if (id.startsWith("lockstep.fltr.") && !m.isMidiOut())
        {
            for (int fs = 0; fs < kFltrSlots; ++fs)
                if (id == kFltrIds[fs]) return mnp + fs;
            return -1;
        }
        if (id.startsWith("lockstep.amp."))
        {
            for (int cs = 0; cs < kChannelSlots; ++cs)
                if (id == kChanIds[cs]) return chanOff + cs;
            if (!m.hasInternalAmp())
                for (int es = 0; es < kEnvSlots; ++es)
                    if (id == kEnvIds[es]) return envOff + es;
            return -1;
        }
        return m.slotForId(id);
    }

    int LockstepProcessor::numSlotsWithMachine(const IMachine& m) const
    {
        if (m.isMidiOut())
            return m.numParams();
        return m.numParams() + kFltrSlots + kChannelSlots + (m.hasInternalAmp() ? 0 : kEnvSlots);
    }

    ParamSpec LockstepProcessor::paramSpecWithMachine(const IMachine& m, int index) const
    {
        // Delegate to the track-level paramSpec logic; share a dummy track index
        // of -1 here is not safe, so forward to the non-machine-aware overload
        // via an equivalent reconstruction. This offline variant is only called
        // during PluginState load (B5) before machines_ is populated — its only
        // caller passes an already-known machine reference.
        const int mnp = m.numParams();
        if (index < mnp)
            return m.paramSpec(index);

        const int fltrOff = mnp;
        const int chanOff = mnp + kFltrSlots;
        const int envOff  = chanOff + kChannelSlots;

        if (!m.isMidiOut() && index >= fltrOff && index < fltrOff + kFltrSlots)
        {
            const int fs = index - fltrOff;
            ParamSpec p;
            p.sectionIndex = kFltrSecIdx;
            p.id = kFltrIds[fs];
            switch (fs)
            {
                case 0:
                    p.label = "Mode";
                    p.isStepped = true;
                    p.maxValue = 4.0f;
                    p.defaultValue = 4.0f;
                    break;
                case 1:
                    p.label = "Slope";
                    p.isStepped = true;
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    break;
                case 2:
                    p.label = "Cutoff";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    break;
                case 3:
                    p.label = "Reson";
                    p.maxValue = 1.0f;
                    break;
                case 4:
                    p.label = "Drive";
                    p.maxValue = 1.0f;
                    break;
                case 5:
                    p.label = "Env>Ct";
                    p.minValue = -1.0f;
                    p.maxValue = 1.0f;
                    break;
                default: break;
            }
            return p;
        }

        if (!m.isMidiOut() && index >= chanOff && index < chanOff + kChannelSlots)
        {
            const int cs = index - chanOff;
            ParamSpec p;
            p.sectionIndex = kAmpSecIdx;
            p.id = kChanIds[cs];
            switch (cs)
            {
                case 0:
                    p.label = "Level";
                    p.maxValue = 2.0f;
                    p.defaultValue = 1.0f;
                    p.role = ParamSpec::Role::Level;
                    break;
                case 1:
                    p.label = "Pan";
                    p.minValue = -1.0f;
                    p.maxValue = 1.0f;
                    p.role = ParamSpec::Role::Pan;
                    break;
                case 2:
                    p.label = "Send A";
                    p.maxValue = 1.0f;
                    break;
                case 3:
                    p.label = "Send B";
                    p.maxValue = 1.0f;
                    break;
                default: break;
            }
            return p;
        }

        if (!m.hasInternalAmp() && index >= envOff && index < envOff + kEnvSlots)
        {
            const int es = index - envOff;
            ParamSpec p;
            p.sectionIndex = kAmpSecIdx;
            p.id = kEnvIds[es];
            switch (es)
            {
                case 0:
                    p.label = "Gate";
                    p.isStepped = true;
                    p.maxValue = 1.0f;
                    break;
                case 1:
                    p.label = "Attack";
                    p.maxValue = 1000.0f;
                    p.defaultValue = 1.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Attack;
                    break;
                case 2:
                    p.label = "Hold";
                    p.maxValue = 1000.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Hold;
                    break;
                case 3:
                    p.label = "Decay";
                    p.maxValue = 2000.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Decay;
                    break;
                case 4:
                    p.label = "Sustain";
                    p.maxValue = 1.0f;
                    p.defaultValue = 1.0f;
                    p.role = ParamSpec::Role::Sustain;
                    break;
                case 5:
                    p.label = "Release";
                    p.maxValue = 2000.0f;
                    p.defaultValue = 10.0f;
                    p.unit = ParamSpec::Unit::Ms;
                    p.role = ParamSpec::Role::Release;
                    break;
                default: break;
            }
            return p;
        }

        return {};
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
        // Build new machines outside the quiesce window to keep suspension brief.
        std::array<std::unique_ptr<IMachine>, kNumTracks> pending{};
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto& desired = kit(static_cast<int>(t)).machineId;
            const auto* m = machines_[t].get();
            if (!m || m->machineId() != desired)
            {
                auto nm = makeMachineForId(desired, samplePool_);
                nm->prepare(preparedSampleRate_, preparedBlockSize_);
                pending[t] = std::move(nm);
            }
        }
        // Apply all [AUDIO] writes inside a single quiesce window so the audio
        // thread sees a consistent snapshot: machine swap + baseParams copy + PLock reserve.
        withQuiescedEngine([&] {
            for (std::size_t t = 0; t < kNumTracks; ++t)
            {
                if (pending[t]) machines_[t] = std::move(pending[t]);
                sequence().tracks[t].baseParams = kit(static_cast<int>(t)).baseParams;
                const int np = numParams(static_cast<int>(t));
                for (auto& step : sequence().tracks[t].steps)
                {
                    step.overrides.reserve(np);
                    step.fillOverrides.reserve(np);
                }
            }
        });
        // Slice recompute runs outside the quiesce (involves heap ops; not audio-path safe).
        // THREADING-DEBT(8.18): slicePositions_ writes still race audio-thread reads.
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            recomputeSlicesIfNeeded(static_cast<int>(t),
                                    slotForId(static_cast<int>(t), "slicer_sample_id"),
                                    sequence().tracks[t].baseParams);
        }
        syncTrackParamsFromActiveKit();
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
        // Reset density atomics — ephemeral state does not cross song boundaries.
        for (std::size_t t = 0; t < kNumTracks; ++t)
            trackDensity_[t].store(1.0f, std::memory_order_relaxed);
        masterDensity_.store(0.0f, std::memory_order_relaxed);
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
        // Floor launch wipes density — double-tap floor = clean state.
        for (std::size_t t = 0; t < kNumTracks; ++t)
            trackDensity_[t].store(1.0f, std::memory_order_relaxed);
        masterDensity_.store(0.0f, std::memory_order_relaxed);
    }

    void LockstepProcessor::loadActivePosition(int songIdx, int sceneIdx)
    {
        arrangement_.loadPosition(songIdx, sceneIdx);
        reinstallMachinesFromActiveKit();
        // Reset density atomics — loadPosition is a hard navigation reset.
        for (std::size_t t = 0; t < kNumTracks; ++t)
            trackDensity_[t].store(1.0f, std::memory_order_relaxed);
        masterDensity_.store(0.0f, std::memory_order_relaxed);
    }

    bool LockstepProcessor::restoreOne(CheckpointScope scope, int track)
    {
        const bool ok = arrangement_.restoreOne(scope, track);
        if (ok)
            reinstallMachinesFromActiveKit();
        return ok;
    }

    void LockstepProcessor::restoreToFloor(CheckpointScope scope, int track)
    {
        arrangement_.restoreToFloor(scope, track);
        reinstallMachinesFromActiveKit();
    }

    void LockstepProcessor::swapPhraseForTrack(int t, int phraseIdx)
    {
        arrangement_.swapPhraseForTrack(t, phraseIdx);
    }

    void LockstepProcessor::deviateAllToPhrase(int phraseIdx)
    {
        arrangement_.deviateAllToPhrase(phraseIdx);
    }

    void LockstepProcessor::resyncTrackToScene(int t)
    {
        arrangement_.resyncTrackToScene(t);
    }

    void LockstepProcessor::resyncAllToScene()
    {
        arrangement_.resyncAllToScene();
    }

    void LockstepProcessor::refreshWorkingFromModel()
    {
        arrangement_.syncWorkingFromActive();
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

    void LockstepProcessor::bakeSceneState()
    {
        arrangement_.bakeSceneState();
    }

    void LockstepProcessor::createBakedCopyScene(int target)
    {
        arrangement_.createBakedCopyScene(target);
    }

    void LockstepProcessor::createBaselineCopyScene(int target)
    {
        arrangement_.createBaselineCopyScene(target);
    }

    void LockstepProcessor::createDefaultScene(int target)
    {
        arrangement_.createDefaultScene(target);
    }

    bool LockstepProcessor::phraseRowMatchesActiveContent(int slot) const
    {
        return arrangement_.phraseRowMatchesActiveContent(slot);
    }

    int LockstepProcessor::countDeviatedTracks() const
    {
        return arrangement_.countDeviatedTracks();
    }

    bool LockstepProcessor::sceneSlotOccupied(int s) const
    {
        return arrangement_.sceneSlotOccupied(s);
    }

    bool LockstepProcessor::songSlotOccupied(int s) const
    {
        return arrangement_.songSlotOccupied(s);
    }

    int LockstepProcessor::firstFreePhraseSlot() const
    {
        return arrangement_.firstFreePhraseSlot();
    }


    // ── End Phase 7 new-hierarchy methods ────────────────────────────────────


    void LockstepProcessor::setTrackMachine(int track, const std::string& machineId)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        const auto ti = static_cast<std::size_t>(track);

        // Stub → real machine: wipe any leftover state so the new track starts clean.
        if (isTrackEmpty(track))
        {
            kit(track) = TrackKit{};
            withQuiescedEngine([&] { sequence().tracks[ti] = Track{}; });
        }

        auto nm = makeMachineForId(machineId, samplePool_);
        nm->prepare(preparedSampleRate_, preparedBlockSize_);

        auto& k = kit(track);
        k.machineId = machineId;
        const int np = nm->numParams();
        k.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
        for (int s = 0; s < np; ++s)
            k.baseParams[static_cast<std::size_t>(s)] = nm->paramSpec(s).defaultValue;

        // D1: a freshly-assigned capture machine (Recorder/Looper) defaults its
        // target_buffer to the next free REC slot, so multiple capture tracks don't
        // all pile onto slot 0. Deliberate sharing is still possible by reassigning.
        const int tbSlot = nm->slotForId("target_buffer");
        if (tbSlot >= 0 && tbSlot < np)
            k.baseParams[static_cast<std::size_t>(tbSlot)] =
                static_cast<float>(nextFreeCaptureSlot(track));

        withQuiescedEngine([&] {
            machines_[ti] = std::move(nm);
            sequence().tracks[ti].baseParams = k.baseParams;
        });

        // Seed slices for slicer machine on first install so trigs fire immediately.
        recomputeSlicesIfNeeded(track,
                                slotForId(track, "slicer_sample_id"),
                                sequence().tracks[ti].baseParams);

        // VA Machine has a non-zero default sustain (0.8), so it sustains indefinitely
        // without a gate. Seed 1/8-note gate (≈250 ms at 120 BPM) on first install.
        auto& trigDef = sequence().tracks[ti].trigDefaults;
        if (machineId == VAMachine::kMachineId && trigDef.gateValue == MusicalGate::None)
            trigDef.gateValue = MusicalGate::G1_8;

        // A2: if the new machine can't be a bus, any tracks that route their Out
        // here have gone DORMANT (they fall back to Master at read time and revive
        // if this becomes a bus again — see routeForTrack). Notice the user.
        // (Edges are read from the stored Out values, not routeForTrack, which
        // already reports the dormant fallback.)
        const bool busCapable =
            !machines_[ti]->isMidiOut() && slotForId(track, kInputSourceSlotId) >= 0;
        if (!busCapable)
        {
            for (std::size_t j = 0; j < kNumTracks; ++j)
            {
                const auto sel = decodeOutputDest(kit(static_cast<int>(j)).channelState.out);
                if (sel.kind == OutputDestKind::Track && sel.track == track)
                {
                    noteRouteReject(RouteReject::Dormant, track);
                    break;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Empty-slot gestural archetype

    void LockstepProcessor::copyKitTrack(int srcTrack, int dstTrack)
    {
        if (srcTrack < 0 || srcTrack >= static_cast<int>(kNumTracks)) return;
        if (dstTrack < 0 || dstTrack >= static_cast<int>(kNumTracks)) return;
        const auto di = static_cast<std::size_t>(dstTrack);

        kit(dstTrack) = kit(srcTrack);

        // Install the copied machine.
        auto nm = makeMachineForId(kit(dstTrack).machineId, samplePool_);
        nm->prepare(preparedSampleRate_, preparedBlockSize_);
        withQuiescedEngine([&] {
            machines_[di] = std::move(nm);
            sequence().tracks[di].baseParams = kit(dstTrack).baseParams;
        });

        // Push the copied kit's subdivision to the APVTS param and working Track
        // so the engine and DIV band immediately reflect the source track's division.
        setTrackSubdivision(dstTrack, kit(dstTrack).subdivIndex);
    }

    bool LockstepProcessor::isTrackEmpty(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto ti = static_cast<std::size_t>(track);
        return machines_[ti] && std::string(machines_[ti]->machineId()) == StubMachine::kMachineId;
    }

    bool LockstepProcessor::isRecorderTrack(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto ti = static_cast<std::size_t>(track);
        return machines_[ti]
               && std::string(machines_[ti]->machineId()) == RecorderMachine::kMachineId;
    }

    bool LockstepProcessor::isLooperTrack(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        return dynamic_cast<LooperMachine*>(machines_[static_cast<std::size_t>(track)].get())
               != nullptr;
    }

    void LockstepProcessor::sendLooperCommand(int track, int cmd, bool immediate)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        if (auto* lm = dynamic_cast<LooperMachine*>(machines_[static_cast<std::size_t>(track)].get()))
            lm->postCommand(static_cast<LooperMachine::Cmd>(cmd), immediate);
    }

    int LockstepProcessor::looperState(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return -1;
        if (auto* lm = dynamic_cast<LooperMachine*>(machines_[static_cast<std::size_t>(track)].get()))
            return static_cast<int>(lm->state());
        return -1;
    }

    float LockstepProcessor::looperPhase(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return -1.0f;
        if (auto* lm = dynamic_cast<LooperMachine*>(machines_[static_cast<std::size_t>(track)].get()))
            return lm->phase01();
        return -1.0f;
    }

    int LockstepProcessor::looperGridCells(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return 16;
        if (auto* lm = dynamic_cast<LooperMachine*>(machines_[static_cast<std::size_t>(track)].get()))
            return lm->gridCells();
        return 16;
    }

    bool LockstepProcessor::isStaticTrack(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        return dynamic_cast<StaticMachine*>(machines_[static_cast<std::size_t>(track)].get())
               != nullptr;
    }

    int LockstepProcessor::captureTargetSlot(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return -1;
        const auto ti = static_cast<std::size_t>(track);
        const auto* m = machines_[ti].get();
        if (m == nullptr) return -1;
        const int slot = m->slotForId("target_buffer");
        if (slot < 0) return -1;
        const auto& bp = sequence().tracks[ti].baseParams;
        if (slot >= static_cast<int>(bp.size())) return -1;
        return std::clamp(static_cast<int>(std::lround(bp[static_cast<std::size_t>(slot)])),
                          0, kNumVolatileSlots - 1);
    }

    int LockstepProcessor::nextFreeCaptureSlot(int exceptTrack) const
    {
        std::array<bool, kNumVolatileSlots> used{};
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            if (t == exceptTrack) continue;
            const int s = captureTargetSlot(t);
            if (s >= 0 && s < kNumVolatileSlots) used[static_cast<std::size_t>(s)] = true;
        }
        for (int s = 0; s < kNumVolatileSlots; ++s)
            if (!used[static_cast<std::size_t>(s)]) return s;
        return 0;  // all slots taken — fall back to slot 0 (deliberate sharing)
    }

    bool LockstepProcessor::captureSlotShared(int track) const
    {
        const int s = captureTargetSlot(track);
        if (s < 0) return false;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            if (t == track) continue;
            if (captureTargetSlot(t) == s) return true;
        }
        return false;
    }

    bool LockstepProcessor::setStaticFile(int track, const juce::String& path)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        bool ok = false;
        withQuiescedEngine([&] {
            auto* sm = dynamic_cast<StaticMachine*>(machines_[static_cast<std::size_t>(track)].get());
            if (sm == nullptr) return;
            ok = sm->setFilePath(path);
            // Mirror the path into the Kit so it persists and survives a reload.
            kit(track).staticPath = ok ? path.toStdString() : std::string{};
        });
        return ok;
    }

    // -------------------------------------------------------------------------

    void LockstepProcessor::getStateInformation(juce::MemoryBlock& dest)
    {
        // Flush live edits and snapshot under quiesce so EngineCmd queue is drained
        // before writeBackWorkingToActive reads arrangement_.working.
        withQuiescedEngine([&] {
            arrangement_.writeBackWorkingToActive();
            PluginState::writeTo(dest, *this);
        });
    }

    void LockstepProcessor::setStateInformation(const void* data, int sizeInBytes)
    {
        withQuiescedEngine([&] {
            PluginState::readFrom(data, sizeInBytes, *this);
            finishStateLoad();
        });
    }

    void LockstepProcessor::finishStateLoad()
    {
        // Re-seed the reserved volatile REC buffers above the just-loaded file
        // samples (readSamplePool ran in readFrom). This both strips any volatile
        // entries the constructor left at low indices — shifting the loaded files
        // down to the absolute indices their saved sample_id refs expect — and
        // re-appends the REC slots at the top of the pool (DESIGN §28).
        seedVolatileSlots();

        // Project the freshly-loaded Songs into the working buffer. The serializer's
        // setActiveSong/Scene calls early-return when the saved active indices equal
        // the defaults (the common 0/0 case), so an explicit sync is required —
        // otherwise the working buffer would keep the empty constructor state.
        syncSequenceFromCurrentScene();

        // Push all MIDI-out config from a Kit track to an already-installed machine.
        auto pushMidiOutConfig = [](MidiOutMachine* mom, const TrackKit& k) {
            mom->setDestinationId(k.destinationId);
            for (int ci = 0; ci < MidiOutMachine::kNumCCs && ci < static_cast<int>(k.midiCCNumbers.size()); ++ci)
                mom->setCCNumber(ci, k.midiCCNumbers[static_cast<std::size_t>(ci)]);
            for (int ci = 0; ci < MidiOutMachine::kNumCCs && ci < static_cast<int>(k.midiCCLabels.size()); ++ci)
                mom->setCCLabel(ci, juce::String(k.midiCCLabels[static_cast<std::size_t>(ci)]));
            // MF.8: restore hardware preset CC name table.
            if (!k.midiPresetName.empty())
                mom->setCCNameTable(MidiDevicePresets::getTable(k.midiPresetName));
            else
                mom->clearCCNameTable();
        };

        // Re-open a StaticMachine's streamed source from the Kit's saved path
        // (held per-Kit, not in the SamplePool — DESIGN §29.2). The engine is
        // quiesced here, so swapping the reader is safe.
        auto pushStaticPath = [](IMachine* m, const TrackKit& k) {
            if (auto* sm = dynamic_cast<StaticMachine*>(m))
                sm->setFilePath(juce::String(k.staticPath));
        };

        // Reinstall machines from the active Kit so that any Kit loaded from disk
        // with an unknown machine ID gets StubMachine.
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const auto& k = kit(static_cast<int>(t));
            if (machines_[t] && machines_[t]->machineId() == k.machineId)
            {
                if (machines_[t]->isMidiOut())
                    pushMidiOutConfig(static_cast<MidiOutMachine*>(machines_[t].get()), k);
                pushStaticPath(machines_[t].get(), k);
                continue;
            }
            machines_[t] = makeMachineForId(k.machineId, samplePool_);
            if (machines_[t]->isMidiOut())
                pushMidiOutConfig(static_cast<MidiOutMachine*>(machines_[t].get()), k);
            if (preparedSampleRate_ > 0.0)
                machines_[t]->prepare(preparedSampleRate_, preparedBlockSize_);
            pushStaticPath(machines_[t].get(), k);
        }

        // v13: reinstall insert effects from the loaded Kit state. setTrackInsert
        // preserves baseParams when the size already matches the effect's schema,
        // so the loaded values survive. Bypass is applied afterwards.
        //
        // EMPTY slots must be torn down explicitly. finishStateLoad runs on
        // newProject() / Open as well, and a slot the loaded state leaves empty may
        // still hold a live effect instance from the *previous* project. Installing
        // only the non-empty slots (the old behaviour) left those stale effects
        // processing audio while the picker showed the slot empty — the "phantom
        // effects on a new project" report (and the delayed-sounding notes it
        // caused when the stale effect was a Delay).
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            for (int s = 0; s < 2; ++s)
            {
                const std::string id  = kit(static_cast<int>(t)).inserts[static_cast<std::size_t>(s)].effectId;
                const bool        byp = kit(static_cast<int>(t)).inserts[static_cast<std::size_t>(s)].bypass;
                if (!id.empty())
                {
                    setTrackInsert(static_cast<int>(t), s, id);
                    setTrackInsertBypass(static_cast<int>(t), s, byp);
                }
                else
                    clearTrackInsert(static_cast<int>(t), s);  // tear down stale live instance
            }
        }

        // v14: reinstall master insert effects from the active Song's masterInserts;
        // tear down empties (see above).
        for (int s = 0; s < 2; ++s)
        {
            const std::string id  = song().masterInserts[static_cast<std::size_t>(s)].effectId;
            const bool        byp = song().masterInserts[static_cast<std::size_t>(s)].bypass;
            if (!id.empty())
            {
                setMasterInsert(s, id);
                setMasterInsertBypass(s, byp);
            }
            else
                clearMasterInsert(s);
        }

        // 8.26: reinstall master send-return effects from the active Song's
        // masterSends. These were never reinstalled on load — the live instance was
        // only ever built by the editor's picker — so a loaded project's sends did
        // not actually process, and a stale send survived a newProject() / Open.
        // Install non-empty (params preserved by setMasterSend), tear down empties.
        for (int s = 0; s < 2; ++s)
        {
            const std::string id  = song().masterSends[static_cast<std::size_t>(s)].effectId;
            const bool        byp = song().masterSends[static_cast<std::size_t>(s)].bypass;
            if (!id.empty())
            {
                setMasterSend(s, id);
                setMasterSendBypass(s, byp);
            }
            else
                clearMasterSend(s);
        }

        // Seed a default gate for VA Machine tracks that have none, so that a
        // track saved before this default existed doesn't sustain forever.
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            // NOTE: machineId() returns const char*; compare via std::string to
            // avoid a pointer-equality check that is always false.
            if (machines_[t] && std::string(machines_[t]->machineId()) == VAMachine::kMachineId)
            {
                auto& trigDef = sequence().tracks[t].trigDefaults;
                if (trigDef.gateValue == MusicalGate::None)
                    trigDef.gateValue = MusicalGate::G1_8;
            }
        }

        // Pre-reserve PLock capacity for every track so audio-thread set() never
        // allocates after state load.
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            const int np = numParams(static_cast<int>(t));
            for (auto& step : sequence().tracks[t].steps)
            {
                step.overrides.reserve(np);
                step.fillOverrides.reserve(np);
            }
        }

        // Reconcile kit/phrase authoritative values with APVTS params so the engine
        // and all display bands agree from the first render block.
        syncTrackParamsFromActiveKit();
    }

    void LockstepProcessor::syncTrackParamsFromActiveKit()
    {
        // Push each track's authoritative kit/phrase values into the APVTS params
        // and the working Track so the engine, DIV band, and LEN band are consistent.
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const auto ti = static_cast<std::size_t>(t);
            const auto& k = kit(t);
            const auto& trk = sequence().tracks[ti];

            // Subdivision: kit is authoritative; push to working Track + param.
            const int subdivIdx = std::clamp(k.subdivIndex, kSubdivMin, kSubdivMax);
            sequence().tracks[ti].subdivIndex = subdivIdx;
            if (auto* p = apvts_.getParameter(ParamIDs::trackDivider(t)))
                p->setValueNotifyingHost(static_cast<float>(subdivIdx) / static_cast<float>(kSubdivMax));

            // Length: phrase length (already in working Track via syncWorkingFromActive)
            // is the authority; mirror to param for band display consistency.
            const int len = std::clamp(trk.length, 1, kMaxStepsPerTrack);
            if (auto* p = apvts_.getParameter(ParamIDs::trackLength(t)))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(len)));
        }
    }

    // ──────────────────────────────────────────────────────────────────────────────
    // Project file I/O
    // ──────────────────────────────────────────────────────────────────────────────

    std::uint32_t LockstepProcessor::stateHash()
    {
        juce::String xmlStr;
        withQuiescedEngine([&] {
            arrangement_.writeBackWorkingToActive();
            const auto xml = PluginState::buildStateTree(*this).createXml();
            if (xml) xmlStr = xml->toString();
        });
        if (xmlStr.isEmpty()) return 0;
        return Hash::xx32(xmlStr.toRawUTF8(), static_cast<std::size_t>(xmlStr.getNumBytesAsUTF8()));
    }

    void LockstepProcessor::resetArrangement()
    {
        // Arrangement is ~47 MB; a stack temporary (`arrangement_ = Arrangement{}`)
        // overflows the message-thread stack. Build the fresh one on the heap and
        // move it in — the move is field-wise and uses no large stack temporary.
        arrangement_ = std::move(*std::make_unique<Arrangement>());
    }

    void LockstepProcessor::seedVolatileSlots()
    {
        // Strip any existing volatile entries first so they never accumulate
        // across loads. They live at the top of the pool, so removing them does
        // not shift the file-backed indices below (which saved sample_id refs
        // depend on). Walk top-down for safe in-place erase.
        for (int i = samplePool_.size() - 1; i >= 0; --i)
            if (samplePool_.isVolatileIndex(i))
                samplePool_.remove(i);

        // Re-append the fixed set of REC slots above the file range. Their
        // absolute indices are resolved on demand via nthVolatileIndex (the pool
        // is the single source of truth), so nothing is cached here.
        for (int v = 0; v < kNumVolatileSlots; ++v)
            (void)samplePool_.addVolatile();

        // Size them if the rate is already known (prepareToPlay may run later).
        if (preparedSampleRate_ > 0.0)
        {
            const int cap = static_cast<int>(preparedSampleRate_ * kVolatileMaxSeconds);
            samplePool_.prepareVolatile(preparedSampleRate_,
                                        std::max(1, getTotalNumOutputChannels()), cap);
        }
    }

    void LockstepProcessor::newProject()
    {
        withQuiescedEngine([&] {
            resetArrangement();
            PluginState::readFrom(defaultStateBlob_.getData(),
                                  static_cast<int>(defaultStateBlob_.getSize()),
                                  *this);
            finishStateLoad();
        });
        currentProjectFile_ = juce::File{};
        savedStateHash_ = stateHash();
    }

    bool LockstepProcessor::saveProjectFile(const juce::File& file)
    {
        bool ok = false;
        withQuiescedEngine([&] {
            arrangement_.writeBackWorkingToActive();
            PluginState::writeToFile(file, *this);
            ok = file.existsAsFile();
        });
        if (ok)
        {
            currentProjectFile_ = file;
            savedStateHash_ = stateHash();
        }
        return ok;
    }

    bool LockstepProcessor::loadProjectFile(const juce::File& file)
    {
        bool ok = false;
        withQuiescedEngine([&] {
            resetArrangement();
            ok = PluginState::readFromFile(file, *this);
            if (ok)
                finishStateLoad();
        });
        if (ok)
        {
            currentProjectFile_ = file;
            savedStateHash_ = stateHash();
        }
        return ok;
    }

    // 8.26 C1: Performance capture API (message thread only).
    bool LockstepProcessor::startCapture()
    {
        return startCaptureTo(chooseCaptureFile(currentProjectFile_));
    }

    bool LockstepProcessor::startCaptureTo(const juce::File& masterFile)
    {
        if (captureRecorder_.isCapturing())
            return false;   // already running
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        const int numCh = std::max(1, getTotalNumOutputChannels());
        if (!captureRecorder_.arm(masterFile, sr, numCh))
            return false;

        // D: arm a stem per non-empty Master-routed track (DESIGN §27). Buses are
        // Master-routed and capture their feeders folded in; the feeders
        // themselves route to a bus, so they are skipped (no double-count).
        if (captureStems_)
        {
            for (std::size_t t = 0; t < kNumTracks; ++t)
                if (shouldStemTrack(static_cast<int>(t)))
                    stemRecorders_[t].arm(stemFileFor(masterFile, static_cast<int>(t)), sr, numCh);
        }
        return true;
    }

    bool LockstepProcessor::shouldStemTrack(int track) const
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
        const auto t = static_cast<std::size_t>(track);
        auto* m = machines_[t].get();
        if (m == nullptr || m->isMidiOut()) return false;
        if (std::string(m->machineId()) == StubMachine::kMachineId) return false;
        // Only terminal (Master-routed) tracks are stems; feeders fold into their
        // bus, Off contributes nothing.
        if (routeForTrack(track).route != Route::Master) return false;
        // A router (Thru) with no outside source and no inbound feeder is an empty
        // bus — skip it rather than write a silent file.
        const int srcSlot = slotForId(track, kInputSourceSlotId);
        if (srcSlot >= 0)
        {
            const auto& bp = kit(track).baseParams;
            const float v = (static_cast<std::size_t>(srcSlot) < bp.size())
                                ? bp[static_cast<std::size_t>(srcSlot)] : 0.0f;
            const bool hasSource = decodeInputSource(v).kind != InputSourceKind::None;
            bool hasFeeder = false;
            const auto edges = routingEdges();
            for (std::size_t j = 0; j < kNumTracks; ++j)
                if (edges[j] == track) { hasFeeder = true; break; }
            if (!hasSource && !hasFeeder) return false;
        }
        return true;
    }

    juce::RelativeTime LockstepProcessor::stopCapture()
    {
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        const int64_t samples = captureRecorder_.disarm();
        for (auto& sr_ : stemRecorders_)   // D: flush all stems on the same edge
            sr_.disarm();
        return juce::RelativeTime::seconds(static_cast<double>(samples) / sr);
    }

    bool LockstepProcessor::isCapturingStems() const noexcept
    {
        for (const auto& s : stemRecorders_)
            if (s.isCapturing()) return true;
        return false;
    }

    std::int64_t LockstepProcessor::stemSamplesWritten(int track) const noexcept
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0;
        return stemRecorders_[static_cast<std::size_t>(track)].samplesWritten();
    }
}
