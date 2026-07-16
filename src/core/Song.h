#pragma once

#include <array>
#include <string>
#include "Phrase.h"
#include "Scene.h"
#include "TimeSig.h"
#include "Scale.h"
#include "TrackKit.h"
#include "Sequence.h"   // kNumTracks

namespace lockstep
{
    inline constexpr int kNumSongs = 16;
    inline constexpr int kScenesPerSong = 16;
    inline constexpr int kPhrasesPerTrack = 16;

    // A song.  Holds per-track Lanes (kit + phrase pool) and Sections.
    // Phase 7 / DESIGN §4.7.  Replaces Bank.
    struct Song
    {
        // One musician's contribution to this song.
        struct SongTrack
        {
            TrackKit kit{};
            std::array<Phrase, kPhrasesPerTrack> phrases{};
            // Per-track swing delta within this song (DESIGN §19.2).
            // Added to Song::swing to form the song-level effective swing for this track.
            float swing = 0.0f;
        };

        std::array<SongTrack, kNumTracks> tracks{};
        std::array<Scene, kScenesPerSong> scenes{};

        // Slot existence (5.3 Item C). A song comes into being only when created
        // (newProject seeds song 0; empty-slot select creates on demand), mirroring
        // Scene::initialised. Un-created slots read as empty in the Song selector and
        // create-on-select rather than launch an empty song. Backfilled for old saves
        // (deriveSongOccupancy) since it is a new field.
        bool initialised = false;

        // Identity (5.3 / DESIGN §23.1). Optional user-assigned name (<=16 chars)
        // and colour (palette index, §24; -1 = unset ⇒ slot-derived at display time).
        std::string name;
        int colour = -1;

        // Song-wide base swing (DESIGN §19.2). The "conductor" gesture — applies to all tracks.
        float swing = 0.0f;
        // Optional Song-level time-signature override (DESIGN §4.8).
        // When false the song inherits from Project::defaultTimeSig.
        bool hasTimeSig = false;
        TimeSig timeSig{};

        // Optional Song-level key-signature override (DESIGN §4.10).
        // When false the song inherits from Project::defaultKeySig.
        bool hasKeySig = false;
        KeySig keySig{};

        // Optional Song-level tempo override (DESIGN §4.9).
        // Stored as a ratio vs the global root (localBpm or host BPM).
        // When false, the song plays at the global tempo (ratio = 1.0).
        bool hasTempo = false;
        double tempoRatio = 1.0;
        // 6.5 master FX: 2 post-sum insert slots, processed after all track outputs are summed.
        std::array<TrackKit::InsertSlot, 2> masterInserts{};
        // 8.26 send returns: 2 post-track-sum send buses, each with a return effect.
        // Tracks tap into them via AMP sendA/sendB (slots 8–9). Returns mix into the
        // master bus before the master inserts.
        std::array<TrackKit::InsertSlot, 2> masterSends{};
    };

    // True when scene `si`'s diagonal phrase row (row si is owned by scene si)
    // holds content in any track. A scene whose ONLY content is phrase data has
    // no scene-level attributes, so sceneHasContent() alone would drop its node
    // on save — the multi-scene content-loss bug. The serializer save gate ORs
    // this in; the load path derives occupancy from it (below).
    [[nodiscard]] inline bool sceneDiagonalOccupied(const Song& song, int si)
    {
        if (si < 0 || si >= kScenesPerSong) return false;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            if (song.tracks[static_cast<std::size_t>(t)]
                    .phrases[static_cast<std::size_t>(si)].initialised)
                return true;
        return false;
    }

    // Rescue derivation for pre-fix projects: a scene whose only content was its
    // diagonal phrase row got no scene node (old save gate), so Scene::initialised
    // was never set on load. Mark such scenes initialised so sceneSlotOccupied()
    // reports correctly and the Scene+step handler does not take the destructive
    // create-on-empty path. Idempotent; safe on fresh/new files (no-op).
    inline void deriveSceneOccupancyFromPhrases(Song& song)
    {
        for (int si = 0; si < kScenesPerSong; ++si)
            if (!song.scenes[static_cast<std::size_t>(si)].initialised
                && sceneDiagonalOccupied(song, si))
                song.scenes[static_cast<std::size_t>(si)].initialised = true;
    }

    // True when a song holds any authored content: an initialised scene, a
    // user-assigned name/colour, or a non-stub track. Used to backfill
    // Song::initialised for pre-Item-C saves (the field did not exist, so it loads
    // false even for songs the user built). `trackIsStub(kit)` decides "non-stub".
    template <typename StubPred>
    [[nodiscard]] inline bool songHasContent(const Song& song, StubPred trackIsStub)
    {
        if (!song.name.empty() || song.colour >= 0) return true;
        for (const auto& sc : song.scenes)
            if (sc.initialised) return true;
        for (const auto& t : song.tracks)
            if (!trackIsStub(t.kit)) return true;
        return false;
    }
}
