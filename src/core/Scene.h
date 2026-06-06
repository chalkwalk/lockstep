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
    struct Scene
    {
        // The scene's "home" row — the phrase index every non-deviated track
        // plays (DESIGN §4.7). Default 0; a made scene seeds this to its index.
        int globalPhrase = 0;

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

        Scene()
        {
            activeMask.fill(true);   // all musicians active by default
        }
    };

    // True when a Scene differs from a freshly-constructed default and is
    // therefore worth persisting. This is the serializer's write gate: the old
    // gate used Scene::initialised, which was never set true anywhere, so every
    // scene's per-scene assignment (phraseIdx) was silently dropped on save.
    [[nodiscard]] inline bool sceneHasContent(const Scene& s)
    {
        if (s.globalPhrase != 0)                         return true;
        for (const bool m  : s.activeMask) if (!m)       return true;
        if (!(s.coreTime == TimeSig{}))                  return true;
        return !s.morphA.empty() || !s.morphB.empty();
    }
}
