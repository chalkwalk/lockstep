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
#include "core/Song.h"
#include "core/Arrangement.h"
#include "core/Project.h"
#include "core/Scene.h"
#include "core/TrackKit.h"
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
    // Info returned when querying whether a ManipulationZone widget has morph data.
    struct MorphWidgetInfo
    {
        bool  exists = false;
        bool  inA    = false;
        bool  inB    = false;
        float aValue = 0.0f;
        float bValue = 0.0f;
    };

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

        // ── New hierarchy accessors (Phase 7 / DESIGN §4.7) ──────────────────
        // The Songs, playhead position, per-track deviation, and the working
        // Sequence the resolver reads all live in arrangement_ (7.9e-pre 2b).
        Song&         song()              { return arrangement_.song(); }
        const Song&   song()        const { return arrangement_.song(); }
        Song&         songAt(int i)       { return arrangement_.songs[static_cast<std::size_t>(i)]; }
        const Song&   songAt(int i) const { return arrangement_.songs[static_cast<std::size_t>(i)]; }
        Scene&       section()            { return arrangement_.scene(); }
        const Scene& section()      const { return arrangement_.scene(); }
        Song::SongTrack&        lane(int t)       { return song().tracks[static_cast<std::size_t>(t)]; }
        const Song::SongTrack&  lane(int t) const { return song().tracks[static_cast<std::size_t>(t)]; }
        TrackKit&           kit(int t)        { return arrangement_.kit(t); }
        const TrackKit&     kit(int t)  const { return arrangement_.kit(t); }

        int activePieceIdx()   const { return arrangement_.songIdx; }
        int activeSectionIdx() const { return arrangement_.sceneIdx; }

        // ── Working buffer = arrangement_.working (the resolver reads this) ───
        Sequence&       sequence()       { return arrangement_.working; }
        const Sequence& sequence() const { return arrangement_.working; }
        Project&        project()        { return project_; }
        const Project&  project()        const { return project_; }

        // ── New hierarchy navigation + gestures (Phase 7) ────────────────────
        void setActiveSong(int pieceIdx);
        void setActiveScene(int sectionIdx);          // single-tap: keep overlay
        void setActiveSceneToFloor(int sectionIdx);   // double-tap: discard overlay
        // Load path only: jump to a saved position without writing the (stale)
        // working buffer back over the loaded phrases. See Arrangement::loadPosition.
        void loadActivePosition(int songIdx, int sceneIdx);
        Phrase&       activePhrase(int t);
        const Phrase& activePhrase(int t) const;
        // swapPhraseForTrack: sticky local deviation (Track + Pattern + step).
        void swapPhraseForTrack(int t, int phraseIdx);
        // setGlobalPhrase: Phrase+step — set the scene's home phrase; non-deviated
        // tracks follow, the focused track rejoins (DESIGN §4.7/§16).
        void setGlobalPhrase(int focusedTrack, int phrase);
        void resyncTrackToScene(int t);    // Track + Part
        void resyncAllToScene();           // Part + Yes
        void refreshWorkingFromModel();    // re-project model → working (no write-back)
        void bakeSceneState();             // Scene + Record (Yes/No confirmed)
        void createBakedCopyScene(int target);   // DESIGN §23.3 placeable payloads
        void createDefaultScene(int target);
        int  countDeviatedTracks() const;
        int  scenesSharingHomePhrase() const;
        bool sceneSlotOccupied(int s) const;
        int  firstFreePhraseSlot() const;
        int  phraseSlotSharers(int phraseIdx) const;
        // Read-only deviation state for UI (surface model, badge rendering).
        bool isTrackDeviated(int t) const;
        int  deviationPhraseIdxForTrack(int t) const;

        // Copy the Kit (machine + base params) from srcTrack to dstTrack.
        // Does NOT copy step data (steps live per Phrase, not per Kit).
        void copyKitTrack(int srcTrack, int dstTrack);

        // True when the installed machine on the given track is a stub (empty track).
        [[nodiscard]] bool isTrackEmpty(int track) const;

        // ── Scene launch queue (Phase 7 / DESIGN §4.8, §16) ─────────────────
        // Queue a Section launch to fire at the next core-time bar boundary.
        // Safe to call from the message thread. cancelQueuedScene() clears it.
        // toFloor = double-tap launch (arrive at saved floor, discard overlay).
        void queueScene(int sectionIdx, bool toFloor = false);
        void cancelQueuedScene();
        bool hasQueuedScene() const;
        int  queuedSectionIdx() const;

        Clock&       clock()       { return clock_; }
        const Clock& clock() const { return clock_; }

        // Mark that the next transport start should re-anchor the pattern to step 0
        // (call alongside a stop/reset). Without it, a plain resume-from-pause would
        // restart the pattern phase while the playhead continued — an audio/visual
        // desync. Safe to call from the message thread.
        void requestFreshStart() { freshStartPending_.store(true, std::memory_order_relaxed); }
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

        // 7.9e: Scope-respecting Checkpoints (DESIGN §13.6).
        // Delegate to arrangement_; the processor is a thin shell.
        void snapshot       (CheckpointScope scope, int track) { arrangement_.snapshot(scope, track); }
        bool restoreOne     (CheckpointScope scope, int track) { return arrangement_.restoreOne(scope, track); }
        void restoreToFloor (CheckpointScope scope, int track) { arrangement_.restoreToFloor(scope, track); }
        [[nodiscard]] int checkpointDepth(CheckpointScope scope, int track) const
        {
            return arrangement_.checkpointDepth(scope, track);
        }

        // 5.2: Morph crossfader — fader position f ∈ [0,1] (0=A, 1=B).
        // UI / message thread writes morphFaderTarget_; audio thread reads it
        // each block and advances a smoothed follower (DESIGN §17.2).
        void setMorphFader(float f)
        {
            morphFaderTarget_.store(juce::jlimit(0.0f, 1.0f, f),
                                    std::memory_order_relaxed);
        }
        float morphFader() const
        {
            return morphFaderTarget_.load(std::memory_order_relaxed);
        }

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

        // 5.2: Morph overlay write paths (message thread; DESIGN §17.3).
        // writeMorph: normalised proportional split at fader position f.
        // writeMorphPole: pole-forced write (pole 0=A, 1=B; for ^/v qualifiers).
        // removeMorph: erases (track, slot) from both morphA and morphB maps.
        void writeMorph(int track, int slot, float deltaAbs, float fader);
        void writeMorphPole(int track, int slot, float value, int pole);
        void removeMorph(int track, int slot);

        // Returns morph endpoint data for a ManipulationZone widget slot.
        MorphWidgetInfo morphWidgetInfo(int track, int slot) const;

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
        // MHZ.7.4: velocity (1-127, default 100). Queued via the poly note FIFO.
        // bypassEditorial = true: raw note-on injected directly (no record-arm, no P-Lock writes).
        // Use this for LEVELS step-held audition to avoid the note-chord-capture path in onNoteOn.
        void triggerNote(int track, int midiNote, int durationMs = 350, int velocity = 100, bool bypassEditorial = false);

        // Poly play-in: sustained note that rings until liveNoteOff (gate). Routes
        // through the record/chord-capture path (onNoteOn/onNoteOff), so held
        // chords both sound and capture. Safe from the message thread; multiple
        // simultaneous notes are honoured (up to kMaxLiveVoices).
        void liveNoteOn(int track, int midiNote, int velocity);
        void liveNoteOff(int track, int midiNote);

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
        // rotateTrackSteps: rotate steps in [0, trackLen) one place, wrapping. dir>0 shifts
        //   content right (toward higher index); dir<0 shifts left (toward lower index).
        void rotateTrackSteps(int track, int dir);
        // setTrackLength: set the working track length, clamped to
        //   [1, kMaxStepsPerTrack]. The single write path for phrase-length
        //   authoring (DESIGN §34.4: Phrase+Func+step, Scene+Func+step, the LEN
        //   encoder); mirrors to the APVTS trackLength param so display, audio,
        //   and Phrase write-back stay single-sourced.
        void setTrackLength(int track, int newLen);
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
        // Reinstalls machines_ from the current Song's kit machineIds (Phase 7).
        void reinstallMachinesFromActiveKit();
        // Copies active section's phrase data + kit baseParams into sequence tracks.
        void syncSequenceFromCurrentScene();

        // If the written slot on the given track governs slice layout
        // (slicer_sample_id, slicer_slice_src, slicer_slice_count), recompute
        // the ISliceable's slice array from the current base params.
        void recomputeSlicesIfNeeded(int track, int slot, const ParamFrame& baseParams);

        juce::AudioProcessorValueTreeState apvts_;
        SamplePool samplePool_;
        Project project_;          // soundPool + launchQuantizeBars
        Arrangement arrangement_;  // new hierarchy: Songs + playhead + working buffer
        // Per-track launch mode: false = fire at global bar boundary,
        // true = fire at end of current phrase cycle.
        std::array<bool, kNumTracks> phraseEndMode_{};
        Clock clock_;
        EditContext editContext_;
        CCMappingTable ccMappingTable_;
        int  focusTrack_       = -1;   // -1 = Global; 0-7 = Track
        bool controlAllActive_ = false;

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
        // Queued Section launch: fires at next core-time bar boundary (Phase 7).
        // -1 = none pending.
        std::atomic<int> queuedSceneIdx_ { -1 };
        // Pairs with queuedSceneIdx_: true = double-tap launch to the saved floor.
        std::atomic<bool> queuedSceneToFloor_ { false };
        // Legacy: queued pattern switch. -1/-1 means no switch pending.
        std::atomic<int>  previewPoolIndex_ { -1 };
        std::atomic<int>  previewReqTrack_  { 0 };
        // Panic request: UI thread sets true; audio thread consumes (exchange false)
        // to send All-Notes-Off on MIDI-out tracks and flush pending audio note-offs.
        std::atomic<bool> panicPending_ { false };

        // MG.1 / poly: keyboard note command queue (UI thread writes, audio thread
        // drains). Replaces the old single-slot mailbox, which collapsed a chord to
        // its last note. SPSC lock-free; commands carry a note-on (durationMs > 0 =
        // fixed audition, 0 = sustain until matching note-off) or a note-off.
        struct KbdNoteCmd
        {
            int16_t  track      = 0;
            uint8_t  note       = 60;
            uint8_t  velocity   = 100;
            uint16_t durationMs = 0;     // 0 = gate (sustain until note-off)
            bool     noteOff    = false; // true = release (track, note)
            bool     bypassEditorial = false;
        };
        static constexpr int kKbdQueueSize = 64;
        juce::AbstractFifo               kbdFifo_ { kKbdQueueSize };
        std::array<KbdNoteCmd, kKbdQueueSize> kbdQueue_{};
        void pushKbdCmd(const KbdNoteCmd& c) noexcept;

        // Preview playback state — audio thread only (no atomics needed).
        bool previewActive_           = false;
        int  previewTrack_            = 0;
        int  previewSampleIndex_      = -1;
        int  previewNoteOffRemaining_ = -1;  // samples until note-off; -1 = inactive
        int  previewNote_             = 60;

        // MG.1 / poly: live keyboard voices — audio thread only. Each holds one
        // ringing note so chords sustain independently. samplesRemaining < 0 =
        // gate (held until a matching note-off); >= 0 = fixed-duration countdown.
        struct LiveVoice
        {
            int  track = -1;
            int  note  = -1;
            int  samplesRemaining = -1;
            bool bypass = false;
            bool active = false;
        };
        static constexpr int kMaxLiveVoices = 16;
        std::array<LiveVoice, kMaxLiveVoices> liveVoices_{};

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

        // Pending sequencer trigs deferred by a late swing/microOffset shift past a block edge.
        struct PendingTrig
        {
            bool    pending   = false;
            int     stepIndex = 0;
            int64_t stepNum   = 0;   // for dedup with lastScheduledStepNum_
            double  firePpq   = 0.0; // absolute PPQ at which to fire
        };
        std::array<PendingTrig, kNumTracks> pendingTrigs_{};

        // Per-track absolute step number of the last step scheduled (emitted or deferred).
        // -1 = none. Used to prevent double-emitting when the extended look-ahead or
        // deferred-trig drain visits a step that was already handled.
        std::array<int64_t, kNumTracks> lastScheduledStepNum_{};

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
        // When true, the next transport rising edge re-anchors the pattern to the
        // current position (step 0 here). Set on stop/reset; cleared on resume so
        // pause→resume continues in phase instead of restarting the pattern.
        std::atomic<bool> freshStartPending_{ true };
        bool   wasInPluginPlaying_  = false;
        bool   wasSequencerRunning_ = false;  // MF.6: falling-edge transport stop detection
        std::array<bool, kNumTracks> wasSilent_{};  // MF.7: per-track mute rising-edge detection

        std::atomic<float>* syncModeParam_    = nullptr;
        std::atomic<float>* channelModeParam_ = nullptr;

        std::array<std::atomic<float>*, kNumTracks> trackLengthParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackDividerParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackMuteParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackSoloParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackSwingParams_{};
        std::atomic<float>* globalSwingParam_ = nullptr;

        // 5.2: Morph crossfader fader state (audio-thread only; not serialized).
        // morphFaderTarget_ is written by any thread via setMorphFader().
        // The audio thread reads it each block and drives morphFaderSmoothed_.
        std::atomic<float> morphFaderTarget_ { 0.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> morphFaderSmoothed_;

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
