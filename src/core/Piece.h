#pragma once

#include <array>
#include "Phrase.h"
#include "Section.h"
#include "TrackKit.h"
#include "Sequence.h"   // kNumTracks

namespace lockstep
{
    inline constexpr int kNumPieces       = 16;
    inline constexpr int kSectionsPerPiece = 16;
    inline constexpr int kPhrasesPerTrack  = 16;

    // A song.  Holds per-track Lanes (kit + phrase pool) and Sections.
    // Phase 7 / DESIGN §4.7.  Replaces Bank.
    struct Piece
    {
        // One musician's contribution to this song.
        struct Lane
        {
            TrackKit kit{};
            std::array<Phrase, kPhrasesPerTrack> phrases{};
        };

        std::array<Lane,    kNumTracks>        tracks{};
        std::array<Section, kSectionsPerPiece> sections{};
    };
}
