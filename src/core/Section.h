#pragma once

#include <array>
#include <map>
#include <utility>
#include "Sequence.h"   // kNumTracks
#include "TimeSig.h"

namespace lockstep
{
    // A moment in a song (intro / verse / chorus / …).  Launched live; has no
    // intrinsic length — plays until the next Section is launched.
    //
    // Phase 7 / DESIGN §4.7.  Replaces Part (kit use) and Pattern::patternMutes.
    struct Section
    {
        // Which phrase (index into Piece::Lane::phrases) each musician plays.
        // Default 0 = first phrase in the lane.
        std::array<int, kNumTracks> phraseIdx{};

        // Who plays in this section.  false = silent (replaces patternMutes).
        // Runtime silence: globalMute[t] || !activeMask[t].
        std::array<bool, kNumTracks> activeMask{};

        // Time signature for this section: drives launch-quantize grid,
        // metronome downbeat, and seeds new-phrase default length.
        TimeSig coreTime{};

        // Scene A/B parameter snapshots (Phase 7.7 / DESIGN §17).
        // Full crossfader implementation: ROADMAP 5.2.
        // Key = (trackIdx << 16) | slotIdx for a flat map lookup.
        std::map<std::pair<int,int>, float> morphA{};
        std::map<std::pair<int,int>, float> morphB{};

        // True once explicitly initialised.
        bool initialised = false;

        Section()
        {
            activeMask.fill(true);   // all musicians active by default
        }
    };
}
