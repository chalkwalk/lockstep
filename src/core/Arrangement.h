#pragma once

#include "Song.h"
#include "Sequence.h"
#include "HierarchyNav.h"
#include <map>
#include <utility>
#include <vector>

namespace lockstep
{
    // Scope for scope-respecting Checkpoints (DESIGN §13.6).
    // None-scope = Song; the enum here names the four explicit forms.
    enum class CheckpointScope
    {
        Song,
        Track,
        Scene,
        Phrase
    };

    // Owns the new musical hierarchy (Songs) plus the live playhead position and
    // the working Sequence the resolver reads. This is the single source of truth
    // for per-track content (Phrase) and sound base (Kit); the working Sequence is
    // a projected cache, kept reversible through HierarchyNav so a scene / song /
    // phrase switch never silently drops live edits — the defect of the old
    // one-way syncSequenceFromCurrentScene().
    //
    // Pure / JUCE-free / header-only so it is directly unit-testable without a
    // LockstepProcessor. The processor delegates its switching + sync to this and
    // shrinks toward a thin shell (DESIGN §4.7; ROADMAP 7.9e-pre).
    //
    // Note on sound: a Track (hence the working Sequence) carries only step data,
    // length, conditions and baseParams. Post-machine FLTR/AMP live solely on the
    // Kit and are never projected into the working buffer; edit them on the Kit
    // directly. Arrangement therefore reconciles exactly the Phrase-owned and
    // Kit.baseParams/divider-owned fields.
    class Arrangement
    {
    public:
        Arrangement() { liveDensity.fill(1.0f); syncWorkingFromActive(); }

        // ── Model ────────────────────────────────────────────────────────────
        std::array<Song, kNumSongs> songs{};
        int launchQuantizeBars = 1;

        // ── Playhead position ────────────────────────────────────────────────
        int songIdx = 0;
        int sceneIdx = 0;
        // Sticky per-track phrase deviation (a musician doing their own thing).
        // This is the LIVE overlay for the scene currently under the playhead.
        std::array<bool, kNumTracks> deviated{};
        std::array<int, kNumTracks> deviationPhraseIdx{};
        // §39 Density — ephemeral live state (not serialized).
        // Mirrors the processor's trackDensity_[] / masterDensity_ atomics on the
        // message thread so that stash/restore and song-reset work correctly.
        std::array<float, kNumTracks> liveDensity{};   // default {} → 0.0f, filled to 1.0f in ctor
        float liveMasterDensity = 0.0f;

        // ── Per-scene remembered live overlay (DESIGN §4.7/§16, build 3) ──────
        // Each scene remembers its own uncommitted deviations while the set runs.
        // Runtime-only: NOT serialized (an overlay is discarded on commit / not
        // part of the saved floor). On a single-tap launch the departing scene's
        // overlay is stashed here and the arriving scene's is restored; a
        // double-tap launch arrives at the saved floor (overlay discarded).
        struct SceneOverlay
        {
            bool active = false;   // has a remembered overlay been stashed?
            std::array<bool, kNumTracks> deviated{};
            std::array<int, kNumTracks> deviationPhraseIdx{};
            // §39 Density — per-scene remembered amounts.
            std::array<float, kNumTracks> density{};  // default {} = 0.0f; real default is 1.0f
            float masterDensity = 0.0f;
        };
        // Factory for a cleared overlay with density defaults (1.0 per track, 0.0 master).
        static SceneOverlay defaultOverlay() noexcept
        {
            SceneOverlay o{};
            o.density.fill(1.0f);
            return o;
        }
        std::array<std::array<SceneOverlay, kScenesPerSong>, kNumSongs> overlays{};

        // ── Accessors ────────────────────────────────────────────────────────
        [[nodiscard]] Song& song() { return songs[idx(songIdx)]; }
        [[nodiscard]] const Song& song() const { return songs[idx(songIdx)]; }
        [[nodiscard]] Scene& scene() { return song().scenes[idx(sceneIdx)]; }
        [[nodiscard]] const Scene& scene() const { return song().scenes[idx(sceneIdx)]; }

        [[nodiscard]] TrackKit& kit(int t) { return song().tracks[idx(t)].kit; }
        [[nodiscard]] const TrackKit& kit(int t) const { return song().tracks[idx(t)].kit; }

        [[nodiscard]] int activePhraseIdx(int t) const
        {
            return resolveActivePhraseIdx(sceneIdx, deviated[idx(t)],
                                          deviationPhraseIdx[idx(t)], t);
        }
        [[nodiscard]] Phrase& activePhrase(int t)
        {
            return song().tracks[idx(t)].phrases[idx(activePhraseIdx(t))];
        }
        [[nodiscard]] const Phrase& activePhrase(int t) const
        {
            return song().tracks[idx(t)].phrases[idx(activePhraseIdx(t))];
        }

        // ── Working buffer (the resolver / audio path reads this) ─────────────
        Sequence working{};
        [[nodiscard]] Track& workingTrack(int t) { return working.tracks[idx(t)]; }
        [[nodiscard]] const Track& workingTrack(int t) const { return working.tracks[idx(t)]; }

        // ── Switching (write-back THEN re-project, so edits survive) ──────────
        // Single-tap Scene launch: stash the departing scene's live overlay and
        // restore the arriving scene's remembered overlay (DESIGN §16). Live
        // deviations therefore persist per scene across launches.
        void setActiveScene(int s)
        {
            if (s < 0 || s >= kScenesPerSong || s == sceneIdx) return;
            writeBackWorkingToActive();      // capture live edits into current phrases/kits
            stashCurrentOverlay();           // remember this scene's deviations
            sceneIdx = s;
            restoreOverlayForCurrent();      // bring back the target scene's overlay
            syncWorkingFromActive();
        }

        // Double-tap Scene launch: arrive at the scene's SAVED FLOOR, discarding
        // its remembered overlay (DESIGN §16). Double-tapping the *current* scene
        // is "revert to stock", so s == sceneIdx is allowed here.
        void setActiveSceneToFloor(int s)
        {
            if (s < 0 || s >= kScenesPerSong) return;
            writeBackWorkingToActive();
            if (s != sceneIdx) stashCurrentOverlay();   // remember departing scene
            sceneIdx = s;
            clearOverlayForCurrent();                   // drop the target's overlay
            deviated.fill(false);
            deviationPhraseIdx.fill(0);
            liveDensity.fill(1.0f);                     // §39: floor wipe resets density
            liveMasterDensity = 0.0f;
            syncWorkingFromActive();
        }

        // ── Pre-staged boundary switch (DESIGN §38 / 8.17) ──────────────────────
        // Split setActiveScene into a message-thread prepare + audio-thread apply
        // so the working Sequence is swapped exactly at the bar boundary with no
        // heap activity on the audio thread.
        //
        // Step 1 (message thread, called from queueScene): flush live edits, stash
        // the departing scene's overlay, and project the target scene's data into
        // the output buffers. The caller stores these in a pre-allocated
        // StagedSceneSwap struct and signals stagedSwapReady_.
        void prepareSceneLaunch(int targetSceneIdx, bool toFloor,
                                Sequence& outWorking,
                                std::array<bool, kNumTracks>& outDeviated,
                                std::array<int, kNumTracks>& outDeviationPhraseIdx,
                                std::array<float, kNumTracks>& outDensity,
                                float& outMasterDensity)
        {
            writeBackWorkingToActive();
            if (targetSceneIdx != sceneIdx) stashCurrentOverlay();
            if (toFloor)
                overlays[idx(songIdx)][idx(targetSceneIdx)] = defaultOverlay();
            // Build deviation + density state for the target scene.
            if (toFloor)
            {
                outDeviated.fill(false);
                outDeviationPhraseIdx.fill(0);
                outDensity.fill(1.0f);
                outMasterDensity = 0.0f;
            }
            else
            {
                const auto& ov = overlays[idx(songIdx)][idx(targetSceneIdx)];
                if (ov.active)
                {
                    outDeviated = ov.deviated;
                    outDeviationPhraseIdx = ov.deviationPhraseIdx;
                    outDensity = ov.density;
                    outMasterDensity = ov.masterDensity;
                }
                else
                {
                    outDeviated.fill(false);
                    outDeviationPhraseIdx.fill(0);
                    outDensity.fill(1.0f);
                    outMasterDensity = 0.0f;
                }
            }
            // Project working sequence for the target scene.
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                const int pi = resolveActivePhraseIdx(targetSceneIdx,
                                                      outDeviated[idx(t)],
                                                      outDeviationPhraseIdx[idx(t)], t);
                outWorking.tracks[idx(t)] = projectPhraseToTrack(
                    songs[idx(songIdx)].tracks[idx(t)].phrases[idx(pi)],
                    songs[idx(songIdx)].tracks[idx(t)].kit);
            }
        }

        // Step 2 (audio thread, at block boundary): swap the pre-built Sequence
        // into `working` and update playhead state. O(kNumTracks * kMaxStepsPerTrack)
        // bounded non-allocating swaps; safe on the audio thread provided the
        // message thread is done with the staged buffer (guarded by stagedSwapReady_).
        // THREADING-DEBT(8.18): sceneIdx/deviated writes race with message-thread reads.
        void applySceneLaunch(int targetSceneIdx,
                              Sequence& stagedWorking,
                              const std::array<bool, kNumTracks>& newDeviated,
                              const std::array<int, kNumTracks>& newDeviationPhraseIdx)
        {
            std::swap(working, stagedWorking);
            sceneIdx = targetSceneIdx;
            deviated = newDeviated;
            deviationPhraseIdx = newDeviationPhraseIdx;
        }

        void setActiveSong(int s)
        {
            if (s < 0 || s >= kNumSongs || s == songIdx) return;
            writeBackWorkingToActive();
            stashCurrentOverlay();
            songIdx = s;
            sceneIdx = 0;
            restoreOverlayForCurrent();      // remembered overlay for the new song's scene 0
            // §39: song change resets density (ephemerality boundary).
            liveDensity.fill(1.0f);
            liveMasterDensity = 0.0f;
            syncWorkingFromActive();
            seedFloor();                     // re-seed floor on song switch (DESIGN §13.6)
        }

        // Jump the playhead to a loaded position WITHOUT writing back. On state
        // load the model (songs/phrases/scenes) is the source of truth and the
        // working buffer is stale; the normal switch would write that stale buffer
        // over the just-loaded phrases of the *previous* scene. Use this from the
        // serializer's load path instead of setActiveSong/setActiveScene.
        void loadPosition(int song, int scene)
        {
            songIdx = std::clamp(song, 0, kNumSongs - 1);
            sceneIdx = std::clamp(scene, 0, kScenesPerSong - 1);
            deviated.fill(false);
            deviationPhraseIdx.fill(0);
            liveDensity.fill(1.0f);          // §39: project load resets density
            liveMasterDensity = 0.0f;
            clearAllOverlays();              // a fresh load carries no live overlay
            syncWorkingFromActive();
            seedFloor();                     // re-seed floor from loaded state (DESIGN §13.6)
        }

        void swapPhraseForTrack(int t, int phraseIdx)
        {
            if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
            writeBackWorkingTrack(t);
            deviated[idx(t)] = true;
            deviationPhraseIdx[idx(t)] = std::clamp(phraseIdx, 0, kPhrasesPerTrack - 1);
            syncWorkingTrackFromActive(t);
        }

        // Scene+Phrase+step: deviate every track to phraseIdx.
        // Landing on the diagonal (phraseIdx == sceneIdx) un-deviates all tracks.
        void deviateAllToPhrase(int phraseIdx)
        {
            writeBackWorkingToActive();
            const int N = std::clamp(phraseIdx, 0, kPhrasesPerTrack - 1);
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                if (N == sceneIdx)
                {
                    deviated[idx(t)] = false;
                    deviationPhraseIdx[idx(t)] = 0;
                }
                else
                {
                    deviated[idx(t)] = true;
                    deviationPhraseIdx[idx(t)] = N;
                }
            }
            syncWorkingFromActive();
        }

        void resyncTrackToScene(int t)
        {
            if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
            writeBackWorkingTrack(t);
            deviated[idx(t)] = false;
            syncWorkingTrackFromActive(t);
        }

        void resyncAllToScene()
        {
            writeBackWorkingToActive();
            deviated.fill(false);
            syncWorkingFromActive();
        }

        // Scene authoring (Scene + Record, §16): bake live deviations onto the
        // scene's diagonal row (row == sceneIdx), then clear them.
        // The caller is responsible for snapshotting before calling this.
        void bakeSceneState()
        {
            writeBackWorkingToActive();
            const int g = sceneIdx;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                if (!deviated[idx(t)]) continue;
                const int src = deviationPhraseIdx[idx(t)];
                if (src != g)
                    song().tracks[idx(t)].phrases[idx(g)] =
                        song().tracks[idx(t)].phrases[idx(src)];
            }
            deviated.fill(false);
            deviationPhraseIdx.fill(0);
            clearOverlayForCurrent();
            scene().initialised = true;
            syncWorkingFromActive();
        }

        // Count deviated tracks (used for the confirm status band).
        [[nodiscard]] int countDeviatedTracks() const
        {
            int n = 0;
            for (const bool d : deviated)
                if (d) ++n;
            return n;
        }

        // True if a scene slot has been explicitly initialised.
        [[nodiscard]] bool sceneSlotOccupied(int s) const
        {
            if (s < 0 || s >= kScenesPerSong) return false;
            return song().scenes[idx(s)].initialised;
        }

        // True if a song slot is the active (used) song.
        [[nodiscard]] bool songSlotOccupied(int s) const
        {
            if (s < 0 || s >= kNumSongs) return false;
            return s == songIdx;
        }

        // Lowest phrase-slot index that is not the diagonal row of any initialised
        // scene AND holds no track content. Returns -1 if all slots are occupied.
        [[nodiscard]] int firstFreePhraseSlot() const
        {
            for (int n = 0; n < kPhrasesPerTrack; ++n)
            {
                // Diagonal: scene N exclusively owns row N.
                if (song().scenes[idx(n)].initialised) continue;

                bool hasTrackContent = false;
                for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                {
                    if (song().tracks[idx(t)].phrases[idx(n)].initialised)
                    {
                        hasTrackContent = true;
                        break;
                    }
                }
                if (!hasTrackContent) return n;
            }
            return -1;
        }


        // ── Placeable payload create operations (DESIGN §23.3) ───────────────
        // All are destructive; caller must snapshot CheckpointScope::Song before
        // calling. The target is launched by the caller afterwards.

        // Baked-copy create: stamp the current effective state (live phrases +
        // floor) into the target slot, then mark it initialised.
        void createBakedCopyScene(int target)
        {
            if (target < 0 || target >= kScenesPerSong) return;
            writeBackWorkingToActive();    // flush live edits into active phrases
            auto& dst = song().scenes[idx(target)];
            dst = Scene{};
            // Copy effective floor from the active scene.
            dst.activeMask = scene().activeMask;
            dst.coreTime = scene().coreTime;
            dst.morphA = scene().morphA;
            dst.morphB = scene().morphB;
            // Copy each track's effective phrase into the target slot.
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                auto& ph = song().tracks[idx(t)].phrases[idx(target)];
                ph = activePhrase(t);
                ph.initialised = true;
            }
            dst.initialised = true;
        }

        // Baseline-copy create: copy the floor's diagonal row (phrases[sceneIdx])
        // into the target slot, ignoring live deviations.
        void createBaselineCopyScene(int target)
        {
            if (target < 0 || target >= kScenesPerSong) return;
            writeBackWorkingToActive();
            auto& dst = song().scenes[idx(target)];
            dst = Scene{};
            dst.activeMask = scene().activeMask;
            dst.coreTime = scene().coreTime;
            dst.morphA = scene().morphA;
            dst.morphB = scene().morphB;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                auto& ph = song().tracks[idx(t)].phrases[idx(target)];
                ph = song().tracks[idx(t)].phrases[idx(sceneIdx)];
                ph.initialised = true;
            }
            dst.initialised = true;
        }

        // Default create: allocate a blank scene at the target slot.
        void createDefaultScene(int target)
        {
            if (target < 0 || target >= kScenesPerSong) return;
            auto& dst = song().scenes[idx(target)];
            dst = Scene{};
            dst.initialised = true;
        }

        // True if phrases[slot][t] matches activePhrase(t) (trig + length) for all
        // tracks. Used to skip the overwrite-conflict prompt when re-stamping is a no-op.
        [[nodiscard]] bool phraseRowMatchesActiveContent(int slot) const
        {
            if (slot < 0 || slot >= kPhrasesPerTrack) return false;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                const auto& target = song().tracks[idx(t)].phrases[idx(slot)];
                if (!target.initialised) return false;
                const auto& active = activePhrase(t);
                if (target.length != active.length) return false;
                for (int s = 0; s < static_cast<int>(active.steps.size()); ++s)
                    if (target.steps[static_cast<std::size_t>(s)].trig != active.steps[static_cast<std::size_t>(s)].trig)
                        return false;
            }
            return true;
        }

        // ── Per-scene overlay store helpers (build 3) ─────────────────────────
        void stashCurrentOverlay()
        {
            auto& o = overlays[idx(songIdx)][idx(sceneIdx)];
            o.active = true;
            o.deviated = deviated;
            o.deviationPhraseIdx = deviationPhraseIdx;
            o.density = liveDensity;
            o.masterDensity = liveMasterDensity;
        }
        // Returns the density arrays from the restored (or default) overlay.
        // Caller must sync the processor atomics after calling this.
        void restoreOverlayForCurrent()
        {
            const auto& o = overlays[idx(songIdx)][idx(sceneIdx)];
            if (o.active)
            {
                deviated = o.deviated;
                deviationPhraseIdx = o.deviationPhraseIdx;
                liveDensity = o.density;
                liveMasterDensity = o.masterDensity;
            }
            else
            {
                deviated.fill(false);
                deviationPhraseIdx.fill(0);
                liveDensity.fill(1.0f);
                liveMasterDensity = 0.0f;
            }
        }
        void clearOverlayForCurrent()
        {
            overlays[idx(songIdx)][idx(sceneIdx)] = defaultOverlay();
        }
        void clearAllOverlays()
        {
            for (auto& songOverlays : overlays)
                for (auto& o : songOverlays)
                    o = defaultOverlay();
        }

        // ── Sync primitives (active model ⇄ working buffer) ───────────────────
        void syncWorkingTrackFromActive(int t)
        {
            workingTrack(t) = projectPhraseToTrack(activePhrase(t), kit(t));
        }
        void syncWorkingFromActive()
        {
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                syncWorkingTrackFromActive(t);
        }
        void writeBackWorkingTrack(int t)
        {
            applyTrackEditsToPhrase(workingTrack(t), activePhrase(t));
            applyTrackBaseToKit(workingTrack(t), kit(t));
        }
        void writeBackWorkingToActive()
        {
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                writeBackWorkingTrack(t);
        }

        // ── Scope-respecting Checkpoints (DESIGN §13.6) ───────────────────────
        // Floor = the saved song state at load / song-switch time. Cannot be popped.
        // Scratch stacks hold snapshots above the floor; depth capped at kMaxCkDepth.
        static constexpr int kMaxCkDepth = 8;

        void seedFloor()
        {
            floorSong_ = song();
            songStack_.clear();
            trackStack_.clear();
            sceneStack_.clear();
            phraseStack_.clear();
        }

        void snapshot(CheckpointScope scope, int track)
        {
            writeBackWorkingToActive();
            switch (scope)
            {
                case CheckpointScope::Song: {
                    songStack_.push_back(song());
                    if (static_cast<int>(songStack_.size()) > kMaxCkDepth)
                        songStack_.erase(songStack_.begin());
                    break;
                }
                case CheckpointScope::Track: {
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) break;
                    auto& stk = trackStack_[track];
                    stk.push_back(song().tracks[idx(track)]);
                    if (static_cast<int>(stk.size()) > kMaxCkDepth)
                        stk.erase(stk.begin());
                    break;
                }
                case CheckpointScope::Scene: {
                    auto& stk = sceneStack_[sceneIdx];
                    stk.push_back(scene());
                    if (static_cast<int>(stk.size()) > kMaxCkDepth)
                        stk.erase(stk.begin());
                    break;
                }
                case CheckpointScope::Phrase: {
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) break;
                    const int pIdx = activePhraseIdx(track);
                    auto& stk = phraseStack_[{ track, pIdx }];
                    stk.push_back(activePhrase(track));
                    if (static_cast<int>(stk.size()) > kMaxCkDepth)
                        stk.erase(stk.begin());
                    break;
                }
            }
        }

        // Restore one step toward the floor. Idempotent at floor (restores from
        // floorSong_ when the scratch stack is empty). Returns false only if the
        // scope+track arguments are out of range.
        bool restoreOne(CheckpointScope scope, int track)
        {
            switch (scope)
            {
                case CheckpointScope::Song: {
                    if (!songStack_.empty())
                    {
                        song() = songStack_.back();
                        songStack_.pop_back();
                    }
                    else
                    {
                        // DESIGN-DEBT(undo-model): floor-fallback silently wipes to project baseline.
                        // Rethink in the undo-model redesign session — should this be a no-op or prompt?
                        song() = floorSong_;
                    }
                    syncWorkingFromActive();
                    return true;
                }
                case CheckpointScope::Track: {
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
                    auto it = trackStack_.find(track);
                    if (it != trackStack_.end() && !it->second.empty())
                    {
                        song().tracks[idx(track)] = it->second.back();
                        it->second.pop_back();
                    }
                    else
                    {
                        song().tracks[idx(track)] = floorSong_.tracks[idx(track)];
                    }
                    syncWorkingTrackFromActive(track);
                    return true;
                }
                case CheckpointScope::Scene: {
                    auto it = sceneStack_.find(sceneIdx);
                    if (it != sceneStack_.end() && !it->second.empty())
                    {
                        scene() = it->second.back();
                        it->second.pop_back();
                    }
                    else
                    {
                        scene() = floorSong_.scenes[idx(sceneIdx)];
                    }
                    deviated.fill(false);      // live deviations are relative to old scene
                    syncWorkingFromActive();
                    return true;
                }
                case CheckpointScope::Phrase: {
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) return false;
                    const int pIdx = activePhraseIdx(track);
                    auto it = phraseStack_.find({ track, pIdx });
                    if (it != phraseStack_.end() && !it->second.empty())
                    {
                        activePhrase(track) = it->second.back();
                        it->second.pop_back();
                    }
                    else
                    {
                        activePhrase(track) = floorSong_.tracks[idx(track)].phrases[idx(pIdx)];
                    }
                    syncWorkingTrackFromActive(track);
                    return true;
                }
            }
            return false;
        }

        // Restore directly to floor, clearing that scope's scratch stack.
        void restoreToFloor(CheckpointScope scope, int track)
        {
            switch (scope)
            {
                case CheckpointScope::Song:
                    song() = floorSong_;
                    songStack_.clear();
                    syncWorkingFromActive();
                    break;
                case CheckpointScope::Track:
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) break;
                    song().tracks[idx(track)] = floorSong_.tracks[idx(track)];
                    trackStack_.erase(track);
                    syncWorkingTrackFromActive(track);
                    break;
                case CheckpointScope::Scene:
                    scene() = floorSong_.scenes[idx(sceneIdx)];
                    sceneStack_.erase(sceneIdx);
                    deviated.fill(false);
                    syncWorkingFromActive();
                    break;
                case CheckpointScope::Phrase:
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) break;
                    {
                        const int pIdx = activePhraseIdx(track);
                        activePhrase(track) = floorSong_.tracks[idx(track)].phrases[idx(pIdx)];
                        phraseStack_.erase({ track, pIdx });
                        syncWorkingTrackFromActive(track);
                    }
                    break;
            }
        }

        [[nodiscard]] int checkpointDepth(CheckpointScope scope, int track) const
        {
            switch (scope)
            {
                case CheckpointScope::Song:
                    return static_cast<int>(songStack_.size());
                case CheckpointScope::Track: {
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0;
                    auto it = trackStack_.find(track);
                    return (it != trackStack_.end()) ? static_cast<int>(it->second.size()) : 0;
                }
                case CheckpointScope::Scene: {
                    auto it = sceneStack_.find(sceneIdx);
                    return (it != sceneStack_.end()) ? static_cast<int>(it->second.size()) : 0;
                }
                case CheckpointScope::Phrase: {
                    if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0;
                    const int pIdx = activePhraseIdx(track);
                    auto it = phraseStack_.find({ track, pIdx });
                    return (it != phraseStack_.end()) ? static_cast<int>(it->second.size()) : 0;
                }
            }
            return 0;
        }

    private:
        [[nodiscard]] static std::size_t idx(int i) noexcept
        {
            return static_cast<std::size_t>(i);
        }

        // Checkpoint floor + scratch stacks (current song only; cleared on song switch).
        Song floorSong_{};
        std::vector<Song> songStack_;
        std::map<int, std::vector<Song::SongTrack>> trackStack_;
        std::map<int, std::vector<Scene>> sceneStack_;
        std::map<std::pair<int, int>, std::vector<Phrase>> phraseStack_;
    };
}
