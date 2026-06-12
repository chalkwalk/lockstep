#pragma once

#include <array>
#include "Phrase.h"
#include "Scene.h"
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
        // Song-wide base swing (DESIGN §19.2). The "conductor" gesture — applies to all tracks.
        float swing = 0.0f;
        // 6.5 master FX: 2 post-sum insert slots, processed after all track outputs are summed.
        std::array<TrackKit::InsertSlot, 2> masterInserts{};
        // 8.26 send returns: 2 post-track-sum send buses, each with a return effect.
        // Tracks tap into them via AMP sendA/sendB (slots 8–9). Returns mix into the
        // master bus before the master inserts.
        std::array<TrackKit::InsertSlot, 2> masterSends{};
    };
}
