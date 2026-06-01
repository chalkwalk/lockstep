#pragma once

#include "Song.h"
#include "Sequence.h"
#include "HierarchyNav.h"

namespace lockstep
{
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
        Arrangement() { syncWorkingFromActive(); }

        // ── Model ────────────────────────────────────────────────────────────
        std::array<Song, kNumSongs> songs{};
        int launchQuantizeBars = 1;

        // ── Playhead position ────────────────────────────────────────────────
        int songIdx  = 0;
        int sceneIdx = 0;
        // Sticky per-track phrase deviation (a musician doing their own thing).
        // This is the LIVE overlay for the scene currently under the playhead.
        std::array<bool, kNumTracks> deviated{};
        std::array<int,  kNumTracks> deviationPhraseIdx{};

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
            std::array<int,  kNumTracks> deviationPhraseIdx{};
        };
        std::array<std::array<SceneOverlay, kScenesPerSong>, kNumSongs> overlays{};

        // ── Accessors ────────────────────────────────────────────────────────
        [[nodiscard]] Song&        song()        { return songs[idx(songIdx)]; }
        [[nodiscard]] const Song&  song()  const { return songs[idx(songIdx)]; }
        [[nodiscard]] Scene&       scene()       { return song().scenes[idx(sceneIdx)]; }
        [[nodiscard]] const Scene& scene() const { return song().scenes[idx(sceneIdx)]; }

        [[nodiscard]] TrackKit&       kit(int t)       { return song().tracks[idx(t)].kit; }
        [[nodiscard]] const TrackKit& kit(int t) const { return song().tracks[idx(t)].kit; }

        [[nodiscard]] int activePhraseIdx(int t) const
        {
            return resolveActivePhraseIdx(scene(), deviated[idx(t)],
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
        [[nodiscard]] Track&       workingTrack(int t)       { return working.tracks[idx(t)]; }
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
            syncWorkingFromActive();
        }

        void setActiveSong(int s)
        {
            if (s < 0 || s >= kNumSongs || s == songIdx) return;
            writeBackWorkingToActive();
            stashCurrentOverlay();
            songIdx  = s;
            sceneIdx = 0;
            restoreOverlayForCurrent();      // remembered overlay for the new song's scene 0
            syncWorkingFromActive();
        }

        // Jump the playhead to a loaded position WITHOUT writing back. On state
        // load the model (songs/phrases/scenes) is the source of truth and the
        // working buffer is stale; the normal switch would write that stale buffer
        // over the just-loaded phrases of the *previous* scene. Use this from the
        // serializer's load path instead of setActiveSong/setActiveScene.
        void loadPosition(int song, int scene)
        {
            songIdx  = std::clamp(song,  0, kNumSongs - 1);
            sceneIdx = std::clamp(scene, 0, kScenesPerSong - 1);
            deviated.fill(false);
            deviationPhraseIdx.fill(0);
            clearAllOverlays();              // a fresh load carries no live overlay
            syncWorkingFromActive();
        }

        void swapPhraseForTrack(int t, int phraseIdx)
        {
            if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
            writeBackWorkingTrack(t);
            deviated[idx(t)]          = true;
            deviationPhraseIdx[idx(t)] = std::clamp(phraseIdx, 0, kPhrasesPerTrack - 1);
            syncWorkingTrackFromActive(t);
        }

        // Phrase+step: set the scene's global (home) phrase. Non-deviated tracks
        // follow it; the FOCUSED track un-deviates and rejoins the unison; other
        // deviated tracks keep their deviation (DESIGN §4.7/§16). focusedTrack < 0
        // = none focused. Write-back runs first (with the OLD assignment) so live
        // edits land on the right phrases before the reassignment.
        void setGlobalPhrase(int focusedTrack, int phrase)
        {
            const int N = std::clamp(phrase, 0, kPhrasesPerTrack - 1);
            writeBackWorkingToActive();
            if (focusedTrack >= 0 && focusedTrack < static_cast<int>(kNumTracks))
                deviated[idx(focusedTrack)] = false;   // focused rejoins the unison
            scene().globalPhrase = N;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                if (!deviated[idx(t)])
                    scene().phraseIdx[idx(t)] = N;      // non-deviated follow global
            syncWorkingFromActive();
        }

        // Scene+Phrase+step: force the whole band onto one phrase, clearing every
        // deviation (the breadth=all variant of the swap, DESIGN §16).
        void forceAllToPhrase(int phrase)
        {
            const int N = std::clamp(phrase, 0, kPhrasesPerTrack - 1);
            writeBackWorkingToActive();
            deviated.fill(false);
            scene().globalPhrase = N;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                scene().phraseIdx[idx(t)] = N;
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

        // Scene authoring (Scene + Record, §16): commit the live deviations into
        // the active Scene's stored assignment, then clear them.
        void commitSceneState()
        {
            writeBackWorkingToActive();
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                if (deviated[idx(t)])
                    scene().phraseIdx[idx(t)] = deviationPhraseIdx[idx(t)];
            deviated.fill(false);
            clearOverlayForCurrent();        // overlay is now part of the floor
            syncWorkingFromActive();
        }

        // ── Per-scene overlay store helpers (build 3) ─────────────────────────
        void stashCurrentOverlay()
        {
            auto& o = overlays[idx(songIdx)][idx(sceneIdx)];
            o.active             = true;
            o.deviated           = deviated;
            o.deviationPhraseIdx = deviationPhraseIdx;
        }
        void restoreOverlayForCurrent()
        {
            const auto& o = overlays[idx(songIdx)][idx(sceneIdx)];
            if (o.active)
            {
                deviated           = o.deviated;
                deviationPhraseIdx = o.deviationPhraseIdx;
            }
            else
            {
                deviated.fill(false);
                deviationPhraseIdx.fill(0);
            }
        }
        void clearOverlayForCurrent()
        {
            overlays[idx(songIdx)][idx(sceneIdx)] = SceneOverlay{};
        }
        void clearAllOverlays()
        {
            for (auto& song : overlays)
                for (auto& o : song)
                    o = SceneOverlay{};
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

    private:
        [[nodiscard]] static std::size_t idx(int i) noexcept
        {
            return static_cast<std::size_t>(i);
        }
    };
}
