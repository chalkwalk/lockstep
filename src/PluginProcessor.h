#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "core/ChannelMode.h"
#include "core/Clock.h"
#include "core/Metronome.h"
#include "core/Project.h"
#include "core/SoundPool.h"
#include "core/SyncMode.h"
#include "io/CCMappingTable.h"
#include "io/EditContext.h"
#include "io/MidiClockReceiver.h"
#include "io/MidiInput.h"
#include "machine/IMachine.h"
#include "machine/SamplePool.h"
#include "machine/TrackAmpDsp.h"
#include "machine/TrackFltrDsp.h"
#include "machine/VoiceChoke.h"

namespace lockstep
{
    // Info returned when querying whether a ManipulationZone widget has a CC mapping.
    struct WidgetMappingInfo
    {
        bool    exists     = false;
        CCScope scope      = CCScope::Track;
        int     trackIndex = -1;  // for Track scope badge label
        int     slot       = -1;
        int     mzPosition = -1;
        int     ccNumber   = -1;
    };

    class LockstepProcessor : public juce::AudioProcessor
    {
    public:
        LockstepProcessor();
        ~LockstepProcessor() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "Lockstep"; }
        bool acceptsMidi() const override                    { return true; }
        bool producesMidi() const override                   { return true; }
        bool isMidiEffect() const override                   { return false; }
        double getTailLengthSeconds() const override         { return 0.0; }

        int getNumPrograms() override                        { return 1; }
        int getCurrentProgram() override                     { return 0; }
        void setCurrentProgram(int) override                 {}
        const juce::String getProgramName(int) override      { return {}; }
        void changeProgramName(int, const juce::String&) override {}

        void getStateInformation(juce::MemoryBlock& dest) override;
        void setStateInformation(const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& apvts() { return apvts_; }

        // Active pattern / part accessors — point into the live project.
        Sequence&       sequence()       { return activePattern().sequence; }
        const Sequence& sequence() const { return activePattern().sequence; }
        Pattern&        activePattern()  { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].patterns[static_cast<std::size_t>(activePatternIdx_)]; }
        const Pattern&  activePattern()  const { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].patterns[static_cast<std::size_t>(activePatternIdx_)]; }
        Part&           activePart()     { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].parts[static_cast<std::size_t>(activePattern().partRef)]; }
        const Part&     activePart()     const { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].parts[static_cast<std::size_t>(activePattern().partRef)]; }
        Project&        project()        { return project_; }
        const Project&  project()        const { return project_; }

        int activeBankIdx()    const { return activeBankIdx_; }
        int activePatternIdx() const { return activePatternIdx_; }

        // Switch the active pattern (no-op if indices unchanged or out of range).
        // Always syncs Track.baseParams from the new active Part.
        void setActivePattern(int bankIdx, int patternIdx);

        // Fork the active Part: copy it into the first free Part slot so the
        // active pattern no longer shares its Part with any other pattern.
        // Returns true on success; false if the Part is not shared or all Part
        // slots are occupied.
        bool forkActivePart();

        // Returns the number of patterns in the active bank that reference the
        // same Part as the active pattern.
        int activePartShareCount() const;

        // Empty-slot gestural archetype — see DESIGN for the copy/create convention.
        // materialisePattern copies the current active pattern if copy=true, else
        // creates a blank pattern referencing the current Part.
        // materialisePart copies the active Part if copy=true, else creates a default
        // Part (T0=sampler, T1-15=stub). Both mark the slot as initialised.
        [[nodiscard]] bool isPatternInitialised(int bankIdx, int patternIdx) const;
        [[nodiscard]] bool isPartInitialised(int bankIdx, int partIdx) const;
        void materialisePattern(int bankIdx, int patternIdx, bool copy);
        void materialisePart(int bankIdx, int partIdx, bool copy);

        // Copy the active Part's PartTrack (machine + base params) from srcTrack
        // to dstTrack. Does NOT copy step data (sequence is per-pattern, not per-Part).
        void copyPartTrack(int srcTrack, int dstTrack);

        // True when the installed machine on the given track is a stub (empty track).
        [[nodiscard]] bool isTrackEmpty(int track) const;

        // Queue a pattern switch to fire at the next grid boundary (end of the
        // longest running track's cycle). Safe to call from the message thread.
        // cancelQueuedPattern() clears any pending switch.
        void queuePattern(int bankIdx, int patternIdx);
        void cancelQueuedPattern();
        bool hasQueuedPattern()      const;
        int  queuedPatternBankIdx()  const;
        int  queuedPatternPatIdx()   const;

        // Called on the message thread after a queued pattern switch fires.
        std::function<void()> onActivePatternChanged;

        // Chain mode: RAM-only ordered queue of upcoming pattern switches.
        // appendToChain adds to the back; the queue advances automatically as
        // each queued switch fires. clearChain cancels the remaining entries.
        // When chainLoopEnabled the current pattern is re-appended when consumed.
        void appendToChain(int bankIdx, int patternIdx);
        void clearChain();
        bool chainLoopEnabled() const { return chainLoopEnabled_; }
        void setChainLoopEnabled(bool v) { chainLoopEnabled_ = v; }
        int  chainLength()      const { return static_cast<int>(chain_.size()); }
        // Message-thread-only: returns the (bankIdx, patIdx) for chain position i.
        std::pair<int,int> chainEntry(int i) const { return chain_[static_cast<std::size_t>(i)]; }

        Clock&       clock()       { return clock_; }
        const Clock& clock() const { return clock_; }
        SamplePool& samplePool() { return samplePool_; }
        EditContext& editContext() { return editContext_; }
        CCMappingTable& ccMappingTable() { return ccMappingTable_; }

        int  focusTrack() const       { return focusTrack_; }
        void setFocusTrack(int track) { focusTrack_ = track; }

        // MD.10: Control-All — broadcast param writes to all tracks with a matching slot id.
        // Set true when Track scope is held without a specific track selected.
        // UI-thread only; no atomic needed.
        void setControlAllActive(bool v) { controlAllActive_ = v; }
        bool controlAllActive()    const { return controlAllActive_; }

        // MD.11: Checkpoint stack — RAM-only LIFO of (Pattern, Part) snapshots.
        // Per active pattern, capped at kMaxCheckpoints (oldest evicted on overflow).
        void pushCheckpoint();
        bool popCheckpoint();    // returns false if stack empty for the active pattern
        int  checkpointDepth() const;

        // MD.9: Fill scope state — set by the UI thread, read by the audio thread.
        // Two activation scopes share the same fill data:
        //   * Fill alone  → allTracks=true:  every track sees fillActive during play.
        //   * Func+Fill   → allTracks=false: only `trackIfNotAll` sees fillActive.
        // Editing routes identically through writeFillParam (held step on current track).
        void setFillActive(bool active, bool allTracks = true, int trackIfNotAll = -1)
        {
            fillActive_      .store(active,                       std::memory_order_relaxed);
            fillAllTracks_   .store(allTracks,                    std::memory_order_relaxed);
            fillLockedTrack_ .store(active && !allTracks ? trackIfNotAll : -1,
                                                                  std::memory_order_relaxed);
        }
        bool fillActive()    const { return fillActive_.load(std::memory_order_relaxed); }
        bool fillActiveForTrack(int i) const
        {
            if (!fillActive_.load(std::memory_order_relaxed)) return false;
            if (fillAllTracks_.load(std::memory_order_relaxed)) return true;
            return i == fillLockedTrack_.load(std::memory_order_relaxed);
        }

        // MD.6: Global mutes — per-track, live in the APVTS (trackMute params).
        // getGlobalMute reads the APVTS param; setGlobalMute writes through APVTS.
        bool getGlobalMute(int track) const;
        void setGlobalMute(int track, bool muted);
        void toggleGlobalMute(int track);

        // Solo toggle — additive (multiple tracks can be soloed simultaneously).
        void toggleSolo(int track);

        // Panic: flush all active voices + send All-Notes-Off without touching the clock.
        // UI thread: call requestPanic(). Audio thread consumes panicPending_ in processBlock.
        void requestPanic() { panicPending_.store(true, std::memory_order_release); }

        // Delete gestures (scope + No verb): return object to absent/empty state.
        void deleteTrack(int track);   // → StubMachine + cleared steps
        void deletePart();             // → all tracks in active part → StubMachine

        // Discard the top checkpoint without restoring state (cleans up after deliberate deletes).
        void dropCheckpoint();

        // MD.7: Pattern mutes — per-track, live in the active Pattern.
        bool getPatternMute(int track) const;
        void setPatternMute(int track, bool muted);
        void togglePatternMute(int track);

        // Called from ManipulationZone (UI thread) when its slot offset changes.
        // Records which absolute slot each of the 4 display positions currently shows.
        void setMZSlots(int slotOffset);

        // MIDI Learn: arms the next CC received as a mapping.
        // slot and mzPosition are mutually exclusive (only one applies per scope).
        void startLearn(CCScope scope, int trackIndex, int slot, int mzPosition = -1);
        void cancelLearn();
        bool isLearning() const { return learnActive_.load(std::memory_order_acquire); }

        // Query whether a ManipulationZone widget column has a CC mapping.
        // slot is the absolute slot index; mzPosition is the widget's display position (0-3).
        WidgetMappingInfo queryWidgetMapping(int slot, int mzPosition) const;

        // Returns the base value for any slot on a track — machine slots from
        // the sequence track's baseParams, FLTR virtual slots from the Part's
        // fltrState. Safe to call from the message thread.
        float baseParamValue(int track, int slot) const;

        // Route a parameter write to the correct layer. If EditContext is
        // active for the given track, the value lands in the held step's
        // P-Lock; otherwise it updates the track's base params.
        void writeParam(int track, int slot, float value);

        // Remove the P-Lock override for one slot on a specific step.
        void clearParam(int track, int step, int slot);

        // Remove one TrigOverride field (0=note, 1=velocity, 2=gate) for a step.
        void clearTrigOverrideField(int track, int step, int field);

        // Write / clear a fill-layer P-Lock (requires step held + fill active).
        void writeFillParam(int track, int slot, float value);
        void clearFillParam(int track, int step, int slot);

        // Remove all P-Lock overrides and trig overrides for a step.
        void clearStepLocks(int track, int step);

        // MHZ.3.1: cancel in-flight chord capture for the given step (called from
        // the UI thread on step release so the next hold of the same step starts fresh).
        void cancelChordCapture(int track, int step);

        // Trigger a one-shot preview of the sample at poolIndex on the given track.
        // Safe to call from the message thread; the audio thread consumes the request
        // on the next processBlock call and injects a note-on + scheduled note-off.
        void triggerPreview(int poolIndex, int track);

        // MG.1: trigger a note-on + scheduled note-off for the given MIDI note on the given track.
        // durationMs is approximate (rounded to the next block boundary).
        // MHZ.7.4: velocity (1-127, default 100) is carried via upper bits of kbdNoteReq_.
        // bypassEditorial = true: raw note-on injected directly (no record-arm, no P-Lock writes).
        // Use this for LEVELS step-held audition to avoid the note-chord-capture path in onNoteOn.
        void triggerNote(int track, int midiNote, int durationMs = 350, int velocity = 100, bool bypassEditorial = false);

        // MG.2: start / stop retrig on the focused track.
        // ratePpq: 0.25=1/16, 0.125=1/32, 1/12.0=1/48, 1/24.0=1/96.
        // Pass active=false to cancel (track is ignored on cancel).
        void setRetrigActive(int track, bool active, double ratePpq = 0.25);

        // MG.3: slice queries + set (message thread; don't call while audio thread is running).
        bool hasTrackSlices(int track)       const;
        int  trackSliceCount(int track)      const;
        void setTrackEqualSlices(int track, int count);
        void clearTrackSlices(int track);

        // MG.5: Sound Pool live-swap — message thread only.
        // liveSwapTrackSound temporarily applies a pool entry's baseParams to the
        // sequence track so the audio thread immediately hears the new sound.
        // clearLiveSwap restores the track's baseParams from the active Part.
        void liveSwapTrackSound(int track, int poolIndex);
        void clearLiveSwap(int track);

        // MG.4: Sound Pool CRUD (message thread only).
        // saveTrackToSoundPool: snapshots the active Part's track state + sample index.
        // Returns the new pool index, or -1 on failure.
        int  saveTrackToSoundPool(int track, const std::string& name = "Sound");
        // recallSoundFromPool: applies the pool entry's params to the active Part + sequence track.
        bool recallSoundFromPool(int track, int entryIndex);
        int  soundPoolSize()                    const { return project_.soundPool.size(); }
        const SoundEntry* soundPoolEntry(int i) const { return project_.soundPool.get(i); }
        void removeSoundEntry(int i)                  { project_.soundPool.remove(i); }

        // Sample pool helpers — message-thread only.
        // sampleShortName returns the filename stem for a given pool index, or "(none)".
        juce::String sampleShortName(int poolIndex) const;
        // removeSample remaps all sequence sample references before erasing the entry.
        void removeSample(int poolIndex);
        // swapSamples remaps references and swaps two pool entries (reorder).
        void swapSamples(int a, int b);
        // relinkSample replaces a missing (or any) pool entry in-place with a newly loaded file.
        // Call only when the sequencer is stopped to avoid audio-thread data races.
        bool relinkSample(int index, const juce::String& newPath);

        // Sequence editing helpers — message thread only.
        // rotateTrackSteps: shift all steps in [0, trackLen) by +1 (dir>0) or -1 (dir<0),
        //   wrapping so the sequence sounds the same but starts one step earlier/later.
        void rotateTrackSteps(int track, int dir);
        // doubleTrackLength: copy steps [0,len) into [len, 2*len), up to kMaxStepsPerTrack.
        //   No-op if already at max. APVTS trackLength param is updated.
        void doubleTrackLength(int track);
        // halveTrackLength: trim to max(1, len/2). APVTS trackLength param is updated.
        void halveTrackLength(int track);

        // Returns true when the machine on the given track is a MIDI-out machine.
        bool isTrackMidiOut(int track) const;

        // MGX.6 — machine selection.
        // Reassigns the machine on one track. Auto-forks the Part if shared.
        // Resets baseParams to the new machine's defaults and syncs to the sequence.
        void setTrackMachine(int track, const std::string& machineId);

        // Returns the stable machineId string for the given track.
        [[nodiscard]] juce::String getMachineId(int track) const;

        // Returns the short display badge for the given track's live machine.
        // Empty string means no badge (stub / null machine).
        [[nodiscard]] const char* trackBadge(int track) const noexcept;

        // State-loading helpers: create a fresh machine for a given ID and compute
        // slot indices using an explicit machine rather than machines_[t].
        // Used by PluginState so that round-trip works when a non-default machine
        // was saved (e.g. FM track deserialised while Sampler is still installed).
        [[nodiscard]] std::unique_ptr<IMachine> createMachineForId(const std::string& id);
        [[nodiscard]] int      slotForIdWithMachine   (const IMachine& m, const juce::String& id) const;
        [[nodiscard]] int      numSlotsWithMachine     (const IMachine& m) const;
        [[nodiscard]] ParamSpec paramSpecWithMachine   (const IMachine& m, int slot) const;

        // Reassigns the active pattern to reference a different Part in the active bank.
        // Reinstalls any machines whose type differs between the old and new Part.
        void setActivePatternPart(int partIdx);

        // Returns the Part index currently referenced by the active pattern.
        [[nodiscard]] int activePatternPartRef() const;

        // Machine catalogue — list of all available machine types.
        struct MachineInfo { const char* id; const char* displayName; };
        [[nodiscard]] int         numAvailableMachines()        const;
        [[nodiscard]] MachineInfo availableMachineInfo(int idx) const;

        // Schema query helpers — forward to the machine on the given track.
        int         numParams(int track)              const;
        ParamSpec   paramSpec(int track, int index)   const;
        int         numSections(int track)            const;
        SectionInfo section(int track, int sectionIndex) const;

        // Slot identity bridge — forwarded to the machine on the given track.
        // Used by the M8 serializer to translate between runtime indices and
        // stable string ids. Returns {} / -1 for out-of-range inputs.
        juce::String idForSlot(int track, int index)       const;
        int slotForId(int track, const juce::String& id)   const;

        // --- Diagnostic metering (audio thread writes, UI thread reads) ---
        // trackPeak / masterPeak: instantaneous block-peak magnitude (linear).
        // trigPulse / midiPulse: set to 1.0 on a sequencer trig / external MIDI
        // note-on; the UI reads-and-clears them to drive a decaying blink.
        float trackPeak(int track) const
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0.0f;
            return trackPeak_[static_cast<std::size_t>(track)].load(std::memory_order_relaxed);
        }
        float masterPeak() const { return masterPeak_.load(std::memory_order_relaxed); }
        float takeTrigPulse(int track)
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0.0f;
            return trigPulse_[static_cast<std::size_t>(track)].exchange(0.0f, std::memory_order_relaxed);
        }
        float takeMidiPulse(int track)
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0.0f;
            return midiPulse_[static_cast<std::size_t>(track)].exchange(0.0f, std::memory_order_relaxed);
        }

        using juce::AudioProcessor::processBlock;

    private:
        // Reinstalls machines_ entries that don't match activePart()'s machineIds,
        // then syncs all sequence baseParams. Suspends audio only if needed.
        void reinstallMachinesFromActivePart();

        // If the written slot on the given track governs slice layout
        // (slicer_sample_id, slicer_slice_src, slicer_slice_count), recompute
        // the ISliceable's slice array from the current base params.
        void recomputeSlicesIfNeeded(int track, int slot, const ParamFrame& baseParams);

        juce::AudioProcessorValueTreeState apvts_;
        SamplePool samplePool_;
        Project project_;          // full Project/Bank/Pattern/Part hierarchy
        int activeBankIdx_    = 0;
        int activePatternIdx_ = 0;
        Clock clock_;
        EditContext editContext_;
        CCMappingTable ccMappingTable_;
        int  focusTrack_       = -1;   // -1 = Global; 0-7 = Track
        bool controlAllActive_ = false;

        // MD.11: per-pattern checkpoint stacks; key = bankIdx * kPatternsPerBank + patIdx.
        struct CheckpointEntry { Pattern savedPattern; Part savedPart; };
        static constexpr int kMaxCheckpoints = 8;
        std::map<int, std::vector<CheckpointEntry>> checkpoints_;
        std::atomic<bool> fillActive_      { false };
        std::atomic<bool> fillAllTracks_   { true };
        std::atomic<int>  fillLockedTrack_ { -1 };

        // Current slot index for each MZ display position.
        // Written by the UI thread, read by the audio thread (atomic).
        std::array<std::atomic<int>, 4> mzSlots_;

        // Pending MIDI Learn request. UI thread writes fields then raises
        // learnActive_ (release); audio thread reads after acquire.
        struct PendingLearnRequest
        {
            CCScope scope      = CCScope::Track;
            int trackIndex     = -1;
            int slot           = -1;
            int mzPosition     = -1;
        };
        std::atomic<bool>  learnActive_ { false };
        PendingLearnRequest learnRequest_;

        // Preview request: message thread writes both fields (track first, then
        // poolIndex with release ordering); audio thread consumes with acq_rel exchange.
        // Queued pattern switch: message thread writes, audio thread consumes at
        // the next grid boundary. -1/-1 means no switch is pending.
        std::atomic<int> queuedPatternBankIdx_ { -1 };
        std::atomic<int> queuedPatternPatIdx_  { -1 };

        // Chain queue: message-thread only. Pair = (bankIdx, patternIdx).
        std::deque<std::pair<int,int>> chain_;
        bool chainLoopEnabled_ = true;

        std::atomic<int>  previewPoolIndex_ { -1 };
        std::atomic<int>  previewReqTrack_  { 0 };
        // Panic request: UI thread sets true; audio thread consumes (exchange false)
        // to send All-Notes-Off on MIDI-out tracks and flush pending audio note-offs.
        std::atomic<bool> panicPending_ { false };

        // MG.1: keyboard note trigger request (UI thread writes, audio thread consumes).
        // kbdNoteReq_ stores (midiNote << 16 | durationMs); -1 = no request.
        std::atomic<int> kbdNoteReq_   { -1 };
        std::atomic<int> kbdNoteTrack_ { 0 };

        // Preview playback state — audio thread only (no atomics needed).
        bool previewActive_           = false;
        int  previewTrack_            = 0;
        int  previewSampleIndex_      = -1;
        int  previewNoteOffRemaining_ = -1;  // samples until note-off; -1 = inactive
        int  previewNote_             = 60;

        // MG.1: keyboard note one-shot state — audio thread only.
        int kbdNoteOffRemaining_ = -1;  // samples until note-off; -1 = inactive
        int kbdNoteActiveTrack_  = 0;
        int kbdNoteActive_       = 60;

        // MG.2: retrig state.
        // retrigReqTrack_: -1 = cancel, >=0 = activate on that track.
        std::atomic<int>    retrigReqTrack_  { -1 };
        std::atomic<double> retrigReqRatePpq_ { 0.25 };  // written UI thread, read audio
        // Audio-thread-only retrig state (no atomics needed).
        int    retrigActiveTrack_      = -1;
        double retrigRatePpq_          = 0.25;
        double retrigNextFireSamples_  = 0.0;  // samples until next retrig fire
        int    retrigNoteOffRemaining_ = -1;

        Metronome metronome_;
        MidiInput midiInput_;
        MidiClockReceiver midiClockReceiver_;
        // ME.4: virtual slot count for the post-machine FLTR block (added to machine.numParams()).
        static constexpr int kFltrSlots  = TrackFltrState::kNumSlots;  // 6
        static constexpr int kFltrSecIdx = 2;  // canonical FLTR section index
        static constexpr int kAmpSlots   = TrackAmpState::kNumSlots;  // 8
        static constexpr int kAmpSecIdx  = 3;  // canonical AMP section index

        std::array<std::unique_ptr<IMachine>, kNumTracks> machines_;
        // Per-track scratch buffers: each machine writes here, then they are
        // summed to the main output bus. Sized in prepareToPlay; cleared each block.
        std::array<juce::AudioBuffer<float>, kNumTracks> trackBuffers_;
        // Per-track choke faders for monophonic re-trigger (MA.4).
        std::array<VoiceChoke, kNumTracks> trackChokes_;
        // ME.4: post-machine FLTR DSP state (audio-thread only).
        std::array<TrackFltrDsp, kNumTracks> trackFltrs_;
        std::array<TrackAmpDsp, kNumTracks> trackAmps_;
        // Last step index that actually fired per track; -1 until first fire.
        // Used for FLTR P-Lock resolution in the sequencer path.
        std::array<int, kNumTracks> firedStepIdx_{};

        // Pending sequencer-scheduled note-offs that spill past the current block boundary.
        struct PendingNoteOff
        {
            int  samplesRemaining = -1;    // -1 = none; else samples from start of next block
            int  noteCount        = 1;
            std::array<int, kMaxNotesPerStep> notes{ 60, 0, 0, 0 };
            bool openEnded        = false; // gate=None: note playing indefinitely; close on
                                           // cycle-back or sequencer stop
        };
        std::array<PendingNoteOff, kNumTracks> pendingNoteOffs_{};

        // Chord capture: snapshot-currently-held semantics.
        // Each note-on snapshots the physically-held MIDI set into all held steps.
        // Gate timing is finalised when all MIDI notes are released.
        struct ChordCapture
        {
            bool    active          = false;  // true while ≥1 note physically held
            int     heldCount       = 0;      // number of physically-held notes
            int64_t gateStartSample = 0;      // sample of first note-on in current chord
            int     maxVelocity     = 0;
            int     totalVelocity   = 0;      // sum of velocities (for mean)
            int     capturedCount   = 0;      // total notes pressed (for mean)
            std::array<bool, 128> heldNotes{};  // which MIDI notes are currently held
        };
        ChordCapture chordCapture_{};

        // Per-track last absolute quantized step number written by realtime record.
        // Used for chord aggregation (same absolute step = aggregate) and overwrite
        // (new absolute step = clear before first note). INT64_MIN = no step recorded.
        std::array<int64_t, kNumTracks> lastRecordedStepNum_{};

        // MHZ.6.1: per-track, per-note gate tracker for realtime record.
        // stepIdx == -1 means this note slot is inactive.
        struct RealtimeNoteEntry { int stepIdx = -1; int64_t noteOnSample = 0; };
        std::array<std::array<RealtimeNoteEntry, 128>, kNumTracks> realtimeNotes_{};
        int64_t totalSamplesProcessed_ = 0;
        std::array<double, kNumTracks> nextTriggerPpq_{};
        std::array<bool, kNumTracks>   lastStepFired_{};
        double anchorPpq_ = 0.0;
        bool   wasInPluginPlaying_  = false;
        bool   wasSequencerRunning_ = false;  // MF.6: falling-edge transport stop detection
        std::array<bool, kNumTracks> wasSilent_{};  // MF.7: per-track mute rising-edge detection

        std::atomic<float>* syncModeParam_    = nullptr;
        std::atomic<float>* channelModeParam_ = nullptr;

        std::array<std::atomic<float>*, kNumTracks> trackLengthParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackDividerParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackMuteParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackSoloParams_{};

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed_;
        std::array<float, 2> dcX1_{};
        std::array<float, 2> dcY1_{};

        // Diagnostic metering — see public accessors above.
        std::array<std::atomic<float>, kNumTracks> trackPeak_{};
        std::array<std::atomic<float>, kNumTracks> trigPulse_{};
        std::array<std::atomic<float>, kNumTracks> midiPulse_{};
        std::atomic<float> masterPeak_ { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepProcessor)
    };
}
