#pragma once

#include <array>
#include "Phrase.h"
#include "Scene.h"
#include "TrackKit.h"
#include "Sequence.h"   // kNumTracks

namespace lockstep
{
    inline constexpr int kNumSongs       = 16;
    inline constexpr int kScenesPerSong = 16;
    inline constexpr int kPhrasesPerTrack  = 16;

    // A song.  Holds per-track Lanes (kit + phrase pool) and Sections.
    // Phase 7 / DESIGN §4.7.  Replaces Bank.
    struct Song
    {
        // One musician's contribution to this song.
        struct SongTrack
        {
            TrackKit kit{};
            std::array<Phrase, kPhrasesPerTrack> phrases{};
        };

        std::array<SongTrack,    kNumTracks>        tracks{};
        std::array<Scene, kScenesPerSong> scenes{};
    };
}
