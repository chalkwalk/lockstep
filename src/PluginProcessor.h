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
#include "io/CaptureRecorder.h"
#include "io/MidiClockReceiver.h"
#include "io/MidiInput.h"
#include "machine/IEffect.h"
#include "machine/EffectFactory.h"
#include "machine/IMachine.h"
#include "machine/SamplePool.h"
#include "core/EngineCommand.h"
#include "machine/TrackFltrDsp.h"
#include "machine/TrackEnvDsp.h"
#include "machine/TrackChannelDsp.h"
#include "machine/VoiceChoke.h"
#include "core/MetricSelect.h"

namespace lockstep
{
    // Info returned when querying whether a ManipulationZone widget has morph data.
    struct MorphWidgetInfo
    {
        bool exists = false;
        bool inA = false;
        bool inB = false;
        float aValue = 0.0f;
        float bValue = 0.0f;
    };

    // Info returned when querying whether a ManipulationZone widget has a CC mapping.
    struct WidgetMappingInfo
    {
        bool exists = false;
        CCScope scope = CCScope::Track;
        int trackIndex = -1;  // for Track scope badge label
        int slot = -1;
        int mzPosition = -1;
        int ccNumber = -1;
    };

    class LockstepProcessor : public juce::AudioProcessor
    {
    public:
        static constexpr int kFxSecIdx = 5;  // canonical FX section index (public for editor)
        static constexpr int kDensitySecIdx = 4;  // MOD: density-sticky entry + sub-page toggle
        static constexpr int kVelSecIdx = 3;      // AMP: velocity-sticky entry + sub-page toggle

        LockstepProcessor();
        ~LockstepProcessor() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "Lockstep"; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}

        void getStateInformation(juce::MemoryBlock& dest) override;
        void setStateInformation(const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& apvts() { return apvts_; }

        // ── New hierarchy accessors (Phase 7 / DESIGN §4.7) ──────────────────
        // The Songs, playhead position, per-track deviation, and the working
        // Sequence the resolver reads all live in arrangement_ (7.9e-pre 2b).
        Song& song() { return arrangement_.song(); }
        const Song& song() const { return arrangement_.song(); }
        Song& songAt(int i) { return arrangement_.songs[static_cast<std::size_t>(i)]; }
        const Song& songAt(int i) const { return arrangement_.songs[static_cast<std::size_t>(i)]; }
        Scene& section() { return arrangement_.scene(); }
        const Scene& section() const { return arrangement_.scene(); }
        Song::SongTrack& lane(int t) { return song().tracks[static_cast<std::size_t>(t)]; }
        const Song::SongTrack& lane(int t) const { return song().tracks[static_cast<std::size_t>(t)]; }
        TrackKit& kit(int t) { return arrangement_.kit(t); }
        const TrackKit& kit(int t) const { return arrangement_.kit(t); }

        int activePieceIdx() const { return arrangement_.songIdx; }
        int activeSectionIdx() const { return arrangement_.sceneIdx; }

        // Resolved time signature: Scene → Song → Set (DESIGN §4.8).
        [[nodiscard]] TimeSig effectiveTimeSig() const
        {
            const auto& sc = section();
            if (sc.hasTimeSig) return sc.coreTime;
            const auto& sg = song();
            if (sg.hasTimeSig) return sg.timeSig;
            return project_.defaultTimeSig;
        }

        // Resolved key signature: Scene → Song → Set (DESIGN §4.10).
        [[nodiscard]] KeySig effectiveKeySig() const
        {
            const auto& sc = section();
            if (sc.hasKeySig) return sc.coreKeySig;
            const auto& sg = song();
            if (sg.hasKeySig) return sg.keySig;
            return project_.defaultKeySig;
        }

        // Resolved tempo ratio: globalRoot × songRatio × sceneRatio (DESIGN §4.9).
        // Returns 1.0 when no overrides are active.
        [[nodiscard]] double effectiveTempoRatio() const
        {
            const auto& sg = song();
            const auto& sc = section();
            return (sg.hasTempo ? sg.tempoRatio : 1.0)
                   * (sc.hasTempo ? sc.tempoRatio : 1.0);
        }

        // Effective BPM: global root × effectiveTempoRatio.
        [[nodiscard]] double effectiveBpm() const
        {
            return clock_.bpm() * effectiveTempoRatio();
        }

        // ── Working buffer = arrangement_.working (the resolver reads this) ───
        Sequence& sequence() { return arrangement_.working; }
        const Sequence& sequence() const { return arrangement_.working; }
        Arrangement& arrangement() { return arrangement_; }
        const Arrangement& arrangement() const { return arrangement_; }
        Project& project() { return project_; }
        const Project& project() const { return project_; }

        // ── New hierarchy navigation + gestures (Phase 7) ────────────────────
        void setActiveSong(int pieceIdx);
        void setActiveScene(int sectionIdx);          // single-tap: keep overlay
        void setActiveSceneToFloor(int sectionIdx);   // double-tap: discard overlay
        // Load path only: jump to a saved position without writing the (stale)
        // working buffer back over the loaded phrases. See Arrangement::loadPosition.
        void loadActivePosition(int songIdx, int sceneIdx);
        Phrase& activePhrase(int t);
        const Phrase& activePhrase(int t) const;
        // swapPhraseForTrack: deviate one track (Phrase+step / Track+Phrase+step).
        void swapPhraseForTrack(int t, int phraseIdx);
        // deviateAllToPhrase: deviate every track (Scene+Phrase+step); landing on
        // sceneIdx un-deviates all.
        void deviateAllToPhrase(int phraseIdx);
        void resyncTrackToScene(int t);    // Track + Part
        void resyncAllToScene();           // Part + Yes
        void refreshWorkingFromModel();    // re-project model → working (no write-back)
        void bakeSceneState();             // Scene + Record (Yes/No confirmed)
        void createBakedCopyScene(int target);      // DESIGN §23.3 placeable payloads
        void createBaselineCopyScene(int target);  // floor phrase only, no deviations
        void createDefaultScene(int target);       // blank
        bool phraseRowMatchesActiveContent(int slot) const;
        int countDeviatedTracks() const;
        bool sceneSlotOccupied(int s) const;
        bool songSlotOccupied(int s) const;
        int firstFreePhraseSlot() const;
        // Read-only deviation state for UI (surface model, badge rendering).
        bool isTrackDeviated(int t) const;
        int deviationPhraseIdxForTrack(int t) const;

        // Copy the Kit (machine + base params) from srcTrack to dstTrack.
        // Does NOT copy step data (steps live per Phrase, not per Kit).
        void copyKitTrack(int srcTrack, int dstTrack);

        // True when the installed machine on the given track is a stub (empty track).
        [[nodiscard]] bool isTrackEmpty(int track) const;
        // True when the track's machine is a RecorderMachine. A trig on such a
        // track is a recorder trig (capture), so the lock-only (trigless) state is
        // disallowed — the off→note→lock-only cycle becomes off→note (DESIGN §30).
        [[nodiscard]] bool isRecorderTrack(int track) const;

        // ── Scene launch queue (Phase 7 / DESIGN §4.8, §16) ─────────────────
        // Queue a Section launch to fire at the next core-time bar boundary.
        // Safe to call from the message thread. cancelQueuedScene() clears it.
        // toFloor = double-tap launch (arrive at saved floor, discard overlay).
        void queueScene(int sectionIdx, bool toFloor = false);
        void cancelQueuedScene();
        bool hasQueuedScene() const;
        int queuedSectionIdx() const;

        Clock& clock() { return clock_; }
        const Clock& clock() const { return clock_; }

        // Mark that the next transport start should re-anchor the pattern to step 0
        // (call alongside a stop/reset). Without it, a plain resume-from-pause would
        // restart the pattern phase while the playhead continued — an audio/visual
        // desync. Safe to call from the message thread.
        void requestFreshStart() { freshStartPending_.store(true, std::memory_order_relaxed); }
        SamplePool& samplePool() { return samplePool_; }

        // Reserved volatile (RAM-only) recorder buffers (DESIGN §28). A fixed set
        // of REC slots so recorder trigs always have somewhere to write; they live
        // at the top of the pool's index range (above file-backed samples) and are
        // re-seeded on every load. volatilePoolIndex(slot) maps a logical slot
        // 0..kNumVolatileSlots-1 to its absolute pool index (the pool is the single
        // source of truth via nthVolatileIndex), or -1 if out of range.
        static constexpr int kNumVolatileSlots = 8;
        // Per-slot capacity for the reserved REC buffers (seconds at the prepared
        // rate). A recorder captures up to this length before truncating.
        static constexpr double kVolatileMaxSeconds = 12.0;
        int volatilePoolIndex(int slot) const
        {
            if (slot < 0 || slot >= kNumVolatileSlots) return -1;
            return samplePool_.nthVolatileIndex(slot);
        }

        EditContext& editContext() { return editContext_; }
        CCMappingTable& ccMappingTable() { return ccMappingTable_; }

        int focusTrack() const { return focusTrack_; }
        void setFocusTrack(int track) { focusTrack_ = track; }

        // MD.10: Control-All — broadcast param writes to all tracks with a matching slot id.
        // Set true when Track scope is held without a specific track selected.
        // UI-thread only; no atomic needed.
        void setControlAllActive(bool v) { controlAllActive_ = v; }
        bool controlAllActive() const { return controlAllActive_; }

        // 7.9e: Scope-respecting Checkpoints (DESIGN §13.6).
        // Delegate to arrangement_; the processor is a thin shell.
        void snapshot(CheckpointScope scope, int track) { arrangement_.snapshot(scope, track); }
        bool restoreOne(CheckpointScope scope, int track);
        void restoreToFloor(CheckpointScope scope, int track);
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
            fillActive_.store(active, std::memory_order_relaxed);
            fillAllTracks_.store(allTracks, std::memory_order_relaxed);
            fillLockedTrack_.store(active && !allTracks ? trackIfNotAll : -1,
                                   std::memory_order_relaxed);
        }
        bool fillActive() const { return fillActive_.load(std::memory_order_relaxed); }
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
        // Resets the kit to StubMachine and wipes the track's morph layers across
        // every scene in the song. Patterns are preserved — overwriting them is the
        // user's obvious choice at the next machine install (a "lossy step").
        void deleteTrack(int track);   // → StubMachine + song-wide morph wipe
        void deletePart();             // → every track → StubMachine + morph wipe

        // Slot-specific deletions (8.24 Stage 7): reset without touching other slots.
        void deletePhraseSlot(int track, int phraseIdx);  // reset one phrase to uninitialised
        void deleteSceneSlot(int sceneIdx);               // clear one scene; fallback if active


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
        // removeMorphPole: erases only one pole (0=A, 1=B).
        void writeMorph(int track, int slot, float deltaAbs, float fader);
        void writeMorphPole(int track, int slot, float value, int pole);
        void removeMorph(int track, int slot);
        void removeMorphPole(int track, int slot, int pole);
        // Fluid mute: Morph+Mute on a track — captures AMP Level→silence into
        // the near pole (the one the fader favours) and current level into the
        // far pole, so sweeping the fader fades the track in/out (DESIGN §17.3).
        void fluidMuteTrack(int track, float fader);
        // Returns the Level slot index within the machine's param schema for
        // fluid mute purposes, or -1 if the track has no amplitude control.
        int fluidMuteLevelSlot(int track) const;
        // True if the active scene has morph data on the Level slot for this track.
        bool hasFluidMute(int track) const;
        int fluidMutePole(int track) const;  // 0=A is silence, 1=B is silence, -1=unknown
        // Current equal-power blend of the Level slot morph poles at the live fader.
        // Returns the kit-base level unchanged if no fluid mute is authored.
        float fluidMuteBlend(int track) const;

        // Returns morph endpoint data for a ManipulationZone widget slot.
        MorphWidgetInfo morphWidgetInfo(int track, int slot) const;

        // Swing edit API (DESIGN §19.2). "Edit the effective, store the delta" model,
        // mirroring the morph qualifier idiom. All methods are message-thread only.
        //
        // Setters accept the *effective* value the user dialled; storage is transformed:
        //   setSwingSongAll:    stores value directly into Song::swing (it is the root).
        //   setSwingSongTrack:  stores (value − songAll) into SongTrack::swing.
        //   setSwingSceneAll:   stores (value − songAll) into Scene::swing.
        //
        // Getters expose the three stored levels and the two "shown" seeds for the UI
        // (what to seed the control at when a scope qualifier activates).
        void setSwingSongAll(float effective);
        void setSwingSongTrack(int t, float effective);
        void setSwingSceneAll(float effective);

        float swingSongAll() const;         // stored song-all
        float swingSongTrackDelta(int t) const;      // stored song-track delta
        float swingSceneAllDelta() const;         // stored scene-all delta
        float swingSongTrackShown(int t) const;      // songAll + sceneAll + songTrk[t] (cumulative track floor)
        float swingSceneAllShown() const;         // songAll + sceneAll   (seed for Scene qualifier)
        float swingEffective(int t) const;         // full clamped sum for focused track in active scene

        // Bake: write the fader-blended value to kit base, then erase morph data.
        // Default delete gesture. Use removeMorph() for revert-without-bake.
        void bakeMorph(int track, int slot);
        // Bulk variants: operate on all morph data for the given track in the
        // active scene only.
        void bakeAllMorph(int track);
        void removeAllMorph(int track);
        // Song-wide variant: erase the track's morph layers in EVERY scene of the
        // current song. Used by the delete gestures — morph is sound-shaping bound
        // to the kit, which deleteTrack resets song-wide, so a stale layer in an
        // off-screen scene would silently colour a freshly-created track.
        void removeAllMorphInSong(int track);

        // Returns the fader-blended effective value for a slot, or the kit base
        // if the slot has no morph data. Stepped slots snap instead of lerping.
        float morphEffectiveValue(int track, int slot) const;

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

        // 5.6: re-arm one-shot trigs (clear spent state). track < 0 = all tracks.
        // Auto-called on transport (re)start and scene switch; also the per-track
        // arm-all performance command.
        void rearmOneShots(int track = -1);

        // A2: would routing track `from`'s output to `toTrack` create a feedback
        // cycle, given the current edges? Used to refuse the CHANNEL "Out" edit
        // (DESIGN §27). toTrack is a 0-based track index.
        bool wouldRoutingCycle(int from, int toTrack) const;

        // A2: why a CHANNEL "Out" edit was refused, or a routing notice
        // (DESIGN §27). None = accepted. Dormant = an inbound edge went dormant
        // because the target's machine changed to a non-bus (a notice, not a
        // refusal — the stored edge survives and revives if it becomes a bus again).
        enum class RouteReject : std::uint8_t { None = 0, Cycle, NoAudioInput, Self, Dormant };
        // Validate an Out-slot edit on track `from` to encoded destination `value`.
        // Master/Off always validate; a Track target must be an input-aware bus
        // machine (declares input_source, not MIDI-out), not self, not cyclic.
        [[nodiscard]] RouteReject validateOutEdit(int from, float value) const;
        // WS4: the ordered set of currently-valid "Out" destinations for a track,
        // as encoded OutputDest values — {Off, Master, then every track that is a
        // valid bus target right now}. Lets the editor present a rotary that steps
        // only through selectable destinations (no jogging through synths /
        // MIDI-out / cycles). If the track's current stored dest is not among
        // them (e.g. dormant after a machine swap) it is appended so the control
        // can still display and leave it. Message-thread query (reads working kit).
        [[nodiscard]] std::vector<float> validOutTargets(int fromTrack) const;
        // Decoupled reject feedback: the engine bumps a sequence + reason (+ the
        // track the message names) when it refuses an Out edit or marks one
        // dormant; the editor polls this from its timer and flashes the status
        // line. (Set on the message thread; atomic for safety.)
        [[nodiscard]] std::uint32_t routeRejectSeq() const noexcept
        {
            return routeRejectSeq_.load(std::memory_order_relaxed);
        }
        [[nodiscard]] RouteReject routeRejectReason() const noexcept
        {
            return static_cast<RouteReject>(routeRejectReason_.load(std::memory_order_relaxed));
        }
        [[nodiscard]] int routeRejectTrack() const noexcept
        {
            return routeRejectTrack_.load(std::memory_order_relaxed);
        }
        void noteRouteReject(RouteReject r, int track = -1) noexcept
        {
            routeRejectReason_.store(static_cast<int>(r), std::memory_order_relaxed);
            routeRejectTrack_.store(track, std::memory_order_relaxed);
            routeRejectSeq_.fetch_add(1, std::memory_order_relaxed);
        }

        // MG.2: start / stop retrig on the focused track.
        // ratePpq: 0.25=1/16, 0.125=1/32, 1/12.0=1/48, 1/24.0=1/96.
        // note: the MIDI note to rattle (pass -1 to keep the current track note).
        // Pass active=false to cancel (track/rate/note are ignored on cancel).
        void setRetrigActive(int track, bool active, double ratePpq = 0.25, int note = 60);

        // §39 Density overlay — ephemeral per-track amount [0.01..1.0] and master offset [-1.0..1.0].
        // Written on the message thread; read atomically on the audio thread.
        void setTrackDensity(int track, float amount) noexcept;
        float trackDensity(int track) const noexcept;
        void setMasterDensity(float offset) noexcept;
        float masterDensity() const noexcept;

        // MG.3: slice queries + set (message thread; don't call while audio thread is running).
        bool hasTrackSlices(int track) const;
        int trackSliceCount(int track) const;
        void setTrackEqualSlices(int track, int count);
        void clearTrackSlices(int track);
        // Blanks trig + condition + P-locks on every phrase slot for one track.
        // The working sequence is re-projected from the model afterward.
        void clearTrackAllPhrases(int track);

        // MG.5: Sound Pool live-swap — message thread only.
        // liveSwapTrackSound temporarily applies a pool entry's baseParams to the
        // sequence track so the audio thread immediately hears the new sound.
        // clearLiveSwap restores the track's baseParams from the active Part.
        void liveSwapTrackSound(int track, int poolIndex);
        void clearLiveSwap(int track);

        // Project file I/O (message thread, standalone chrome + future DAW import/export).
        // newProject: resets to the pristine default state; no confirm dialog (caller's job).
        void newProject();
        // saveProjectFile: flushes working state then serializes to a .lockstep XML file.
        //   Returns false if the file cannot be written.
        bool saveProjectFile(const juce::File& file);
        // loadProjectFile: parses file, applies the upgrade chain, replaces processor state.
        //   Returns false and leaves processor UNCHANGED on parse failure.
        bool loadProjectFile(const juce::File& file);
        // stateHash: content-hash of the current state tree (quiesces engine internally).
        //   Used for dirty-checking only — not stable across builds.
        [[nodiscard]] std::uint32_t stateHash();
        // currentProjectFile: last file successfully opened or saved, or invalid if none.
        [[nodiscard]] juce::File currentProjectFile() const { return currentProjectFile_; }
        // savedStateHash: hash at the last new/load/save (for dirty comparison).
        [[nodiscard]] std::uint32_t savedStateHash() const { return savedStateHash_; }

        // 8.26 C1: Performance WAV capture (message thread only).
        // startCapture() arms recording to a timestamped WAV next to the project file.
        // Returns false if the file cannot be opened. stopCapture() disarms and flushes;
        // returns the captured duration as a juce::RelativeTime (zero if not capturing).
        bool startCapture();
        juce::RelativeTime stopCapture();
        [[nodiscard]] bool isCapturing() const noexcept { return captureRecorder_.isCapturing(); }
        [[nodiscard]] juce::File captureFile() const { return captureRecorder_.captureFile(); }

        // D (stems): per-track post-fader/post-FX capture written alongside the
        // master take. When enabled, start/stopCapture also arm/flush a WAV per
        // audio track (MIDI-out tracks produce no stem). DESIGN §22 / §27.
        void setCaptureStems(bool on) noexcept { captureStems_ = on; }
        [[nodiscard]] bool captureStems() const noexcept { return captureStems_; }
        [[nodiscard]] bool isCapturingStems() const noexcept;
        // Arm the master take to a specific file (and stems next to it when
        // enabled). startCapture() routes here with a timestamped path; tests use
        // it to target a temp directory.
        bool startCaptureTo(const juce::File& masterFile);
        [[nodiscard]] std::int64_t stemSamplesWritten(int track) const noexcept;

        // MG.4: Sound Pool CRUD (message thread only).
        // saveTrackToSoundPool: snapshots the active Part's track state + sample index.
        // Returns the new pool index, or -1 on failure.
        int saveTrackToSoundPool(int track, const std::string& name = "Sound");
        // recallSoundFromPool: applies the pool entry's params to the active Part + sequence track.
        bool recallSoundFromPool(int track, int entryIndex);
        int soundPoolSize() const { return project_.soundPool.size(); }
        const SoundEntry* soundPoolEntry(int i) const { return project_.soundPool.get(i); }
        void removeSoundEntry(int i);   // quiesces engine, remaps soundId refs, removes entry
        void renameSoundEntry(int i, const std::string& name);
        void pushSoundEntry(SoundEntry e) { project_.soundPool.push(std::move(e)); }

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
        // transposeTrack: shift every authored note in the track's phrase (the track
        // base/default note + each step's main & fill trig-override notes) by
        // `semitones`, clamped to [0,127]. +12/-12 = octave; +/-1 = chromatic.
        void transposeTrack(int track, int semitones);
        // swapSteps: exchange the full Step structs at indices a and b on the given track.
        void swapSteps(int track, int a, int b);
        // relocateStepSwap: swap-with-destination move of a held step (9.14). The
        //   step's home is `anchor`; it currently sits at `fromPos`. Restore it to
        //   anchor, then swap anchor<->toPos so the step lands at toPos and only the
        //   destination cell trades back to anchor — every cell in between stays put.
        void relocateStepSwap(int track, int anchor, int fromPos, int toPos);
        // setTrackLength: set the working track length, clamped to
        //   [1, kMaxStepsPerTrack]. The single write path for phrase-length
        //   authoring (DESIGN §34.4: Phrase+Func+step, Scene+Func+step, the LEN
        //   encoder); mirrors to the APVTS trackLength param so display, audio,
        //   and Phrase write-back stay single-sourced.
        void setTrackLength(int track, int newLen);
        // setTrackSubdivision: set the working track subdivision index, clamped to
        //   [kSubdivMin, kSubdivMax]. The single write path for clock-division
        //   authoring; mirrors to the APVTS trackDivider param and Track.subdivIndex
        //   so the engine, DIV band, and Kit write-back stay single-sourced.
        void setTrackSubdivision(int track, int idx);
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
        // Returns the raw const char* machineId for the given track (static lifetime).
        [[nodiscard]] const char* getMachineIdRaw(int track) const;

        // Returns the short display badge for the given track's live machine.
        // Empty string means no badge (stub / null machine).
        [[nodiscard]] const char* trackBadge(int track) const noexcept;

        // Returns a const pointer to the live machine on the given track, or nullptr.
        // Valid on the message thread only; do not cache across processBlock calls.
        [[nodiscard]] const IMachine* machineForTrack(int track) const noexcept;

        // State-loading helpers: create a fresh machine for a given ID and compute
        // slot indices using an explicit machine rather than machines_[t].
        // Used by PluginState so that round-trip works when a non-default machine
        // was saved (e.g. FM track deserialised while Sampler is still installed).
        [[nodiscard]] std::unique_ptr<IMachine> createMachineForId(const std::string& id);
        [[nodiscard]] int slotForIdWithMachine(const IMachine& m, const juce::String& id) const;
        [[nodiscard]] int numSlotsWithMachine(const IMachine& m) const;
        [[nodiscard]] ParamSpec paramSpecWithMachine(const IMachine& m, int slot) const;

        // Machine catalogue — list of all available machine types.
        struct MachineInfo
        {
            const char* id;
            const char* displayName;
        };
        [[nodiscard]] int numAvailableMachines() const;
        [[nodiscard]] MachineInfo availableMachineInfo(int idx) const;

        // 6.5: FX insert management (message thread).
        void setTrackInsert(int track, int slot, const std::string& effectId);
        void clearTrackInsert(int track, int slot);
        void setTrackInsertBypass(int track, int slot, bool bypass);
        [[nodiscard]] std::string trackInsertId(int track, int slot) const;
        [[nodiscard]] bool trackInsertBypass(int track, int slot) const;

        // 6.5 master FX bus — 2 post-sum insert slots at Song scope.
        void setMasterInsert(int slot, const std::string& effectId);
        void clearMasterInsert(int slot);
        void setMasterInsertBypass(int slot, bool bypass);
        [[nodiscard]] std::string masterInsertId(int slot) const;
        [[nodiscard]] bool masterInsertBypass(int slot) const;
        [[nodiscard]] int masterInsertNumParams(int slot) const;
        [[nodiscard]] float masterInsertParam(int slot, int param) const;
        [[nodiscard]] ParamSpec masterInsertParamSpec(int slot, int param) const;
        void setMasterInsertParam(int slot, int param, float value);

        // 8.26 send-return FX (mirrors the master-insert API).
        void setMasterSend(int slot, const std::string& effectId);
        void clearMasterSend(int slot);
        void setMasterSendBypass(int slot, bool bypass);
        [[nodiscard]] std::string masterSendId(int slot) const;
        [[nodiscard]] bool masterSendBypass(int slot) const;
        [[nodiscard]] int masterSendNumParams(int slot) const;
        [[nodiscard]] float masterSendParam(int slot, int param) const;
        [[nodiscard]] ParamSpec masterSendParamSpec(int slot, int param) const;
        void setMasterSendParam(int slot, int param, float value);

        // Live-instance presence (distinct from the *Id() getters, which report the
        // serialized Song/Kit slot). Used to assert the invariant "an empty slot has
        // no live effect" after newProject() / Open — the phantom-effects guard.
        [[nodiscard]] bool hasLiveTrackInsert(int track, int slot) const noexcept;
        [[nodiscard]] bool hasLiveMasterInsert(int slot) const noexcept;
        [[nodiscard]] bool hasLiveMasterSend(int slot) const noexcept;

        [[nodiscard]] int numAvailableEffects() const;
        [[nodiscard]] EffectInfo availableEffectInfo(int idx) const;

        // Schema query helpers — forward to the machine on the given track.
        int numParams(int track) const;
        ParamSpec paramSpec(int track, int index) const;
        int numSections(int track) const;
        SectionInfo section(int track, int sectionIndex) const;

        // Slot identity bridge — forwarded to the machine on the given track.
        // Used by the M8 serializer to translate between runtime indices and
        // stable string ids. Returns {} / -1 for out-of-range inputs.
        juce::String idForSlot(int track, int index) const;
        int slotForId(int track, const juce::String& id) const;

        // --- Diagnostic metering (audio thread writes, UI thread reads) ---
        // trackPeak / masterPeak: instantaneous block-peak magnitude (linear).
        // trigPulse / midiPulse: set to 1.0 on a sequencer trig / external MIDI
        // note-on; the UI reads-and-clears them to drive a decaying blink.
        float trackPeak(int track) const
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0.0f;
            return trackPeak_[static_cast<std::size_t>(track)].load(std::memory_order_relaxed);
        }
        float masterPeak()  const { return masterPeak_.load(std::memory_order_relaxed); }
        float masterPeakR() const { return masterPeakR_.load(std::memory_order_relaxed); }
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

        // 9.15 Stage 3 — audio→UI discrete bridge (DESIGN §35.9.2). The audio
        // thread sets this when it applies a queued param change (drainEngineCmds:
        // CC writes, encoder writes, P-Lock writes — all async via the engine
        // FIFO). The editor reads-and-clears it to refresh the surface, so a
        // param settles on screen and on controllers without a per-tick poll.
        [[nodiscard]] bool takeSurfaceDirty() noexcept
        {
            return surfaceDirtyFromAudio_.exchange(false, std::memory_order_relaxed);
        }

        // 9.15: the focused track's current playhead step, published every
        // processBlock so the editor can repaint the sequencer grid exactly when
        // the playhead advances — driven by the same loop that fires the notes,
        // not a separately-sampled approximation. -1 when there is no focus track.
        [[nodiscard]] int focusStepUi() const noexcept
        {
            return focusStepUi_.load(std::memory_order_relaxed);
        }

        using juce::AudioProcessor::processBlock;

        // Reinstall machines from the current Song's kit (public so tests can
        // change kit(t).machineId and call this to apply it without a full reload).
        void reinstallMachinesFromActiveKit();

        // Quiesce the audio engine: suspend processing, drain the EngineCmd queue
        // on the message thread so pending param writes land before structural edits,
        // call fn(), then resume. Use this wrapper for all [SUSPEND]-class mutations
        // (machine swap, insert swap, sample pool update, full state load).
        // Must be called from the message thread. Re-entrant: nested calls are no-ops
        // (suspend/resume only on the outermost boundary).
        template <typename Fn>
        void withQuiescedEngine(Fn&& fn)
        {
            if (quiesceDepth_++ == 0)
            {
                suspendProcessing(true);
                drainEngineCmds();
            }
            fn();
            if (--quiesceDepth_ == 0)
                suspendProcessing(false);
        }

        // Copies active section's phrase data + kit baseParams into sequence tracks.
        void syncSequenceFromCurrentScene();

        // If the written slot on the given track governs slice layout
        // (slicer_sample_id, slicer_slice_src, slicer_slice_count), recompute
        // the ISliceable's slice array from the current base params.
        void recomputeSlicesIfNeeded(int track, int slot, const ParamFrame& baseParams);

        // ── Threading ownership (see DESIGN §38) ────────────────────────────────
        // [AUDIO]   — audio thread owns exclusively; message thread must not write.
        // [ATOMIC]  — atomic (or APVTS-managed atomic*); any thread may read/write.
        // [QUEUE]   — message thread enqueues; audio thread drains at block start.
        //             (post-8.16: param writes, P-Locks, trig overrides, mutes)
        // [SUSPEND] — accessed only while processing is suspended via
        //             suspendProcessing()/resumeProcessing() (or withQuiescedEngine).
        //
        // Members without a tag are message-thread-only (UI state, construction).
        // ─────────────────────────────────────────────────────────────────────────

        juce::AudioProcessorValueTreeState apvts_;
        SamplePool samplePool_;          // [SUSPEND] structural; audio reads only
        Project project_;                // [SUSPEND] soundPool + launchQuantizeBars
        Arrangement arrangement_;        // [SUSPEND] for full load; audio owns working seq
        // Per-track launch mode: false = fire at global bar boundary,
        // true = fire at end of current phrase cycle.
        std::array<bool, kNumTracks> phraseEndMode_{};  // [QUEUE] target state
        Clock clock_;                    // [AUDIO] (internal BPM/PPQ state)
        EditContext editContext_;        // message thread only
        CCMappingTable ccMappingTable_;  // [SUSPEND] (learn writes via atomic gate)
        int focusTrack_ = -1;   // message thread only
        bool controlAllActive_ = false; // message thread only

        std::atomic<bool> fillActive_{ false };  // [ATOMIC]
        std::atomic<bool> fillAllTracks_{ true };   // [ATOMIC]
        std::atomic<int> fillLockedTrack_{ -1 };     // [ATOMIC]

        // Current slot index for each MZ display position.
        // Written by the UI thread, read by the audio thread (atomic).
        std::array<std::atomic<int>, 4> mzSlots_;  // [ATOMIC]

        // Pending MIDI Learn request. UI thread writes fields then raises
        // learnActive_ (release); audio thread reads after acquire.
        struct PendingLearnRequest
        {
            CCScope scope = CCScope::Track;
            int trackIndex = -1;
            int slot = -1;
            int mzPosition = -1;
        };
        std::atomic<bool> learnActive_{ false };  // [ATOMIC]
        PendingLearnRequest learnRequest_;           // protected by learnActive_ gate

        // Preview request: message thread writes both fields (track first, then
        // poolIndex with release ordering); audio thread consumes with acq_rel exchange.
        // Queued Section launch: fires at next core-time bar boundary (Phase 7).
        // -1 = none pending.
        std::atomic<int> queuedSceneIdx_{ -1 };    // [ATOMIC]
        std::atomic<bool> queuedSceneToFloor_{ false };  // [ATOMIC] pairs with queuedSceneIdx_

        // [QUEUE] Pre-staged scene switch (8.17 / DESIGN §38.4). Message thread calls
        // prepareSceneLaunch into stagedSwap_ then sets stagedSwapReady_. At the bar
        // boundary the audio thread sets pendingSceneApply_. Top-of-next-block applies
        // the swap via applySceneLaunch (bounded O(N), no allocation on audio thread).
        // After the swap sceneSwitchApplied_ fires; message thread reinstalls machines.
        struct StagedSceneSwap
        {
            Sequence working{};
            int sceneIdx = -1;
            bool toFloor = false;
            std::array<bool, kNumTracks> deviated{};
            std::array<int, kNumTracks> deviationPhraseIdx{};
            // §39 Density state for the target scene.
            std::array<float, kNumTracks> density{};  // filled to 1.0f by queueScene
            float masterDensity = 0.0f;
        };
        StagedSceneSwap stagedSwap_{};                        // [QUEUE]
        std::atomic<bool> stagedSwapReady_{ false };        // [ATOMIC]
        std::atomic<bool> pendingSceneApply_{ false };        // [ATOMIC]
        std::atomic<bool> sceneSwitchApplied_{ false };        // [ATOMIC]
        // Legacy: queued pattern switch. -1/-1 means no switch pending.
        std::atomic<int> previewPoolIndex_{ -1 };  // [ATOMIC]
        std::atomic<int> previewReqTrack_{ 0 };   // [ATOMIC]
        // Panic request: UI thread sets true; audio thread consumes (exchange false)
        // to send All-Notes-Off on MIDI-out tracks and flush pending audio note-offs.
        std::atomic<bool> panicPending_{ false };  // [ATOMIC]

        // [QUEUE] EngineCmd FIFO — message thread enqueues, audio thread drains at
        // block top. Sized for 1024 entries; Control-All fan-out to 16 tracks uses
        // at most 16 × numParams ≈ 16×32 = 512 entries per UI event — well within limit.
        static constexpr int kEngineCmdQueueSize = 1024;
        juce::AbstractFifo engineCmdFifo_{ kEngineCmdQueueSize };
        std::array<EngineCmd, kEngineCmdQueueSize> engineCmdQueue_{};
        void pushEngineCmd(const EngineCmd& c) noexcept;
        void drainEngineCmds() noexcept;  // called at top of processBlock

        // MG.1 / poly: keyboard note command queue (UI thread writes, audio thread
        // drains). [QUEUE] SPSC lock-free; commands carry a note-on (durationMs > 0 =
        // fixed audition, 0 = sustain until matching note-off) or a note-off.
        struct KbdNoteCmd
        {
            int16_t track = 0;
            uint8_t note = 60;
            uint8_t velocity = 100;
            uint16_t durationMs = 0;     // 0 = gate (sustain until note-off)
            bool noteOff = false; // true = release (track, note)
            bool bypassEditorial = false;
        };
        static constexpr int kKbdQueueSize = 64;
        juce::AbstractFifo kbdFifo_{ kKbdQueueSize };
        std::array<KbdNoteCmd, kKbdQueueSize> kbdQueue_{};
        void pushKbdCmd(const KbdNoteCmd& c) noexcept;

        // Preview playback state — [AUDIO] audio thread only (no atomics needed).
        bool previewActive_ = false;
        int previewTrack_ = 0;
        int previewSampleIndex_ = -1;
        int previewNoteOffRemaining_ = -1;
        int previewNote_ = 60;

        // MG.1 / poly: live keyboard voices — audio thread only. Each holds one
        // ringing note so chords sustain independently. samplesRemaining < 0 =
        // gate (held until a matching note-off); >= 0 = fixed-duration countdown.
        struct LiveVoice
        {
            int track = -1;
            int note = -1;
            int samplesRemaining = -1;
            bool bypass = false;
            bool active = false;
        };
        static constexpr int kMaxLiveVoices = 16;
        std::array<LiveVoice, kMaxLiveVoices> liveVoices_{};

        // MG.2 / 5.7: retrig state.
        // retrigReqTrack_: -2 = cancel, -1 = idle, >=0 = activate on that track.
        std::atomic<int> retrigReqTrack_{ -1 };    // [ATOMIC]
        std::atomic<double> retrigReqRatePpq_{ 0.25 };  // [ATOMIC]
        std::array<std::atomic<float>, kNumTracks> trackDensity_;  // [ATOMIC] per-track density [0.01..1.0]
        std::atomic<float> masterDensity_{ 0.0f };               // [ATOMIC] master offset [-1.0..1.0]
        // §39 Scrub: per-track cached MetricSelect tables (audio-thread-owned, rebuilt on meter/divider change).
        struct DensityTableCache
        {
            int numerator = 0;
            int denominator = 0;
            std::int64_t stepsPerBar = 0;
            MetricSelect::Table table{};
        };
        std::array<DensityTableCache, kNumTracks> densityTableCache_{};
        std::atomic<int> retrigReqNote_{ 60 };    // [ATOMIC]
        // [AUDIO] retrig state consumed and advanced by the audio thread only.
        int retrigActiveTrack_ = -1;
        double retrigRatePpq_ = 0.25;
        int retrigNote_ = 60;
        double retrigNextFireSamples_ = 0.0;
        int retrigNoteOffRemaining_ = -1;

        Metronome metronome_;
        MidiInput midiInput_;
        MidiClockReceiver midiClockReceiver_;
        // Slot block sizes for the always-present track blocks (DESIGN §14).
        static constexpr int kFltrSlots    = TrackFltrState::kNumSlots;      // 6
        static constexpr int kChannelSlots = TrackChannelState::kNumSlots;   // 4
        static constexpr int kEnvSlots     = TrackEnvState::kNumSlots;       // 6
        static constexpr int kFltrSecIdx   = 2;  // canonical FLTR section index
        static constexpr int kAmpSecIdx    = 3;  // canonical AMP section index

        // Absolute slot index where insert `insSlot` (0 or 1) params begin.
        [[nodiscard]] int insertParamOffset(int track, int insSlot) const noexcept;

        // Apply the master insert chain in-place. Called from both transport paths.
        // Also processes the send buses (if any) before the inserts.
        void processMasterChain(juce::AudioBuffer<float>& buf, int numSamples);

        // [SUSPEND] structural: swapped only while processing is suspended.
        std::array<std::unique_ptr<IMachine>, kNumTracks> machines_;
        using InsertPair = std::array<std::unique_ptr<IEffect>, 2>;
        std::array<InsertPair, kNumTracks> trackInserts_;
        InsertPair masterInserts_;
        InsertPair masterSends_;    // 8.26: send return FX (post track-sum, pre master inserts)

        // [AUDIO] per-block scratch and DSP state — audio thread only.
        std::array<juce::AudioBuffer<float>, kNumTracks> trackBuffers_;
        // A2: per-block inbound-bus accumulators (DESIGN §27). A track routed to
        // bus N adds its post-chain output here; N reads it as extra input.
        // Cleared each block; resized in prepareToPlay alongside trackBuffers_.
        std::array<juce::AudioBuffer<float>, kNumTracks> busInputBufs_;
        // 8.26: per-block send buses (resized in prepareToPlay).
        std::array<juce::AudioBuffer<float>, 2> sendBusBufs_;
        // 6.1: captured plugin audio input for this block (External source), and a
        // copy of the prior block's master sum (the one sanctioned Master tap,
        // DESIGN §27). Both resized in prepareToPlay.
        juce::AudioBuffer<float> inputCapture_;
        juce::AudioBuffer<float> prevMasterBuf_;
        // 6.1: fill trackBuffers_[track] from the track machine's resolved
        // input_source before process(). No-op (leaves the cleared buffer) for
        // None and, until A2, Track-N. Called from both transport paths.
        void fillTrackInput(int track, const ParamFrame& frame, int numSamples);
        // A2: the per-track audio chain (machine → FILTER → CHANNEL → ENV →
        // level/pan → inserts → sends → peak), rendered into trackBuffers_[i].
        // Shared by both transport paths and driven in routing order so a bus
        // track's inbound audio is present before it runs. resolveStep is the
        // step whose FLTR/CHANNEL/ENV overrides apply (-1 = base only).
        void processTrackChain(std::size_t i, const ParamFrame& frame,
                               int resolveStep, bool fillActive, float faderNow,
                               int numBlockSamples, juce::MidiBuffer& trackMidiI);

        // A2: where a track's finished signal goes (DESIGN §27).
        enum class Route { Master, Bus, Off };
        struct TrackRoute { Route route; int busTrack; };  // busTrack valid iff Bus
        // Validated per-track routing decision from the CHANNEL "Out" base value.
        // An invalid bus target (out of range / self / MIDI-out) falls back to
        // Master defensively. MIDI-out source tracks route nowhere audible.
        TrackRoute routeForTrack(int track) const;
        // Build the dest[] edge array (audio bus target or -1) for the block.
        std::array<int, kNumTracks> routingEdges() const;
        // A2: after a track's chain, deposit its output into its bus (if routed
        // to one). Topo order guarantees the bus has not run yet.
        void depositToBus(std::size_t track, int numBlockSamples);
        // A2: sum every Master-routed track into the main output (the dest-aware
        // replacement for the old "sum all tracks" combine pass).
        void sumRoutedToMaster(juce::AudioBuffer<float>& buffer, int numBlockSamples);
        // 6.1: cache the final master output into prevMasterBuf_ (Master tap).
        void cachePrevMaster(const juce::AudioBuffer<float>& buf, int numSamples);
        std::array<VoiceChoke, kNumTracks> trackChokes_;
        std::array<TrackFltrDsp, kNumTracks> trackFltrs_;
        std::array<TrackEnvDsp, kNumTracks> trackEnvs_;
        // Last step index that actually fired per track; -1 until first fire.
        // Used for FLTR P-Lock resolution in the sequencer path.
        std::array<int, kNumTracks> firedStepIdx_{};

        // 5.6 one-shot spent state (RAM-only, DESIGN §30): true once a one-shot
        // step has fired, suppressing it until re-armed (transport start, scene
        // switch, or rearmOneShots()). Keeps the grammar deterministic given arm
        // state (PRINCIPLES §11).
        std::array<std::array<bool, kMaxStepsPerTrack>, kNumTracks> oneShotSpent_{};

        // [AUDIO] sequencer note-off tracking and pending-trig state.
        // Pending sequencer-scheduled note-offs that spill past the current block boundary.
        struct PendingNoteOff
        {
            int samplesRemaining = -1;    // -1 = none; else samples from start of next block
            int noteCount = 1;
            std::array<int, kMaxNotesPerStep> notes{ 60, 0, 0, 0 };
            bool openEnded = false; // gate=None: note playing indefinitely; close on
                                           // cycle-back or sequencer stop
        };
        std::array<PendingNoteOff, kNumTracks> pendingNoteOffs_{};

        // Pending sequencer trigs deferred by a late swing/microOffset shift past a block edge.
        struct PendingTrig
        {
            bool pending = false;
            int stepIndex = 0;
            int64_t stepNum = 0;   // for dedup with lastScheduledStepNum_
            double firePpq = 0.0; // absolute PPQ at which to fire
        };
        std::array<PendingTrig, kNumTracks> pendingTrigs_{};

        // [AUDIO] Per-track absolute step number of the last step scheduled (emitted or deferred).
        // -1 = none. Prevents double-emitting when the extended look-ahead or
        // deferred-trig drain revisits a step already handled.
        std::array<int64_t, kNumTracks> lastScheduledStepNum_{};

        // [AUDIO] chord capture — written entirely from the audio thread.
        // Each note-on snapshots the physically-held MIDI set into all held steps.
        // Gate timing is finalised when all MIDI notes are released.
        struct ChordCapture
        {
            bool active = false;  // true while ≥1 note physically held
            int heldCount = 0;      // number of physically-held notes
            int64_t gateStartSample = 0;      // sample of first note-on in current chord
            int maxVelocity = 0;
            int totalVelocity = 0;      // sum of velocities (for mean)
            int capturedCount = 0;      // total notes pressed (for mean)
            std::array<bool, 128> heldNotes{};  // which MIDI notes are currently held
        };
        ChordCapture chordCapture_{};

        // [AUDIO] realtime-record step tracking.
        // Per-track last absolute quantized step number written by realtime record.
        // INT64_MIN = no step recorded.
        std::array<int64_t, kNumTracks> lastRecordedStepNum_{};

        // MHZ.6.1: per-track, per-note gate tracker for realtime record.
        struct RealtimeNoteEntry
        {
            int stepIdx = -1;
            int64_t noteOnSample = 0;
        };
        std::array<std::array<RealtimeNoteEntry, 128>, kNumTracks> realtimeNotes_{};
        int64_t totalSamplesProcessed_ = 0;  // [AUDIO]
        std::array<double, kNumTracks> nextTriggerPpq_{};  // [AUDIO]
        std::array<bool, kNumTracks> lastStepFired_{};   // [AUDIO]
        double anchorPpq_ = 0.0;  // [AUDIO]
        // When true, the next transport rising edge re-anchors the pattern to the
        // current position (step 0 here). Set on stop/reset; cleared on resume so
        // pause→resume continues in phase instead of restarting the pattern.
        std::atomic<bool> freshStartPending_{ true };
        bool wasInPluginPlaying_ = false;
        bool wasSequencerRunning_ = false;  // MF.6: falling-edge transport stop detection
        std::array<bool, kNumTracks> wasSilent_{};  // MF.7: per-track mute rising-edge detection

        // [ATOMIC]* APVTS-managed parameter atomics — any thread may read.
        std::atomic<float>* syncModeParam_ = nullptr;
        std::atomic<float>* channelModeParam_ = nullptr;
        std::array<std::atomic<float>*, kNumTracks> trackLengthParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackDividerParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackMuteParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackSoloParams_{};
        // 5.2: Morph crossfader.
        std::atomic<float> morphFaderTarget_{ 0.0f };           // [ATOMIC]
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> morphFaderSmoothed_;  // [AUDIO]
        std::array<bool, kNumTracks> morphLastSide_{};           // [AUDIO]

        // Cached from prepareToPlay — getSampleRate()/getBlockSize() are 0 until
        // the host calls setRateAndBufferSizeDetails, so install/swap helpers must
        // use these values instead.
        double preparedSampleRate_ = 44100.0;
        int preparedBlockSize_ = 512;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed_;
        std::array<float, 2> dcX1_{};
        std::array<float, 2> dcY1_{};

        // [ATOMIC] diagnostic metering — audio thread writes, UI timer reads.
        std::array<std::atomic<float>, kNumTracks> trackPeak_{};
        std::array<std::atomic<float>, kNumTracks> trigPulse_{};
        std::array<std::atomic<float>, kNumTracks> midiPulse_{};
        std::atomic<float> masterPeak_{ 0.0f };
        std::atomic<float> masterPeakR_{ 0.0f };

        // [ATOMIC] 9.15 Stage 3 — set when the audio thread applies a queued param
        // change (drainEngineCmds); read-and-cleared by the editor to refresh the
        // surface. See takeSurfaceDirty().
        std::atomic<bool> surfaceDirtyFromAudio_{ false };

        // [ATOMIC] 9.15 — focused track's current playhead step, published each
        // processBlock; the editor repaints the grid when it advances. See
        // focusStepUi().
        std::atomic<int> focusStepUi_{ -1 };

        // 8.26 C1: WAV performance capture.
        CaptureRecorder captureRecorder_;
        // D: per-track stem recorders + always-on toggle. Stems are saved by
        // default (a take is a tape of a live performance — never make a user
        // regret only keeping the main out); the flag is a latent master-only
        // override, currently unbound.
        std::array<CaptureRecorder, kNumTracks> stemRecorders_;
        bool captureStems_ = true;
        // A2: routing-rejection feedback (editor polls seq + reads reason/track).
        std::atomic<int> routeRejectReason_{ 0 };
        std::atomic<int> routeRejectTrack_{ -1 };
        std::atomic<std::uint32_t> routeRejectSeq_{ 0 };
        // D: should track `t` produce a stem? Non-MIDI-out, non-stub, routed to
        // Master (feeders fold into their bus; Off goes nowhere), and — for a
        // router/Thru — not an empty bus (no outside source and no inbound feeder).
        [[nodiscard]] bool shouldStemTrack(int track) const;

        // Project-file state — message thread only.
        juce::MemoryBlock defaultStateBlob_;          // pristine state captured at construction
        juce::File currentProjectFile_;               // last opened/saved .lockstep file (invalid = none)
        std::uint32_t savedStateHash_ = 0;            // hash at last new/load/save
        int quiesceDepth_ = 0;                        // withQuiescedEngine re-entrancy counter
        void finishStateLoad();                       // post-readFrom reinstall pass; must be called inside withQuiescedEngine
        void seedVolatileSlots();                      // (re)create the reserved REC buffers at the top of the pool (DESIGN §28)
        void syncTrackParamsFromActiveKit();          // push kit subdivIndex + phrase length into APVTS params and working Track
        // Reset arrangement_ to a fresh default in place. sizeof(Arrangement) is
        // ~47 MB, so `arrangement_ = Arrangement{}` would materialize that as a
        // stack temporary and blow the message-thread stack — allocate on the heap.
        void resetArrangement();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepProcessor)
    };
}
