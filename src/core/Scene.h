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
        // Who plays in this section.  false = silent (replaces patternMutes).
        // Runtime silence: globalMute[t] || !activeMask[t].
        std::array<bool, kNumTracks> activeMask{};

        // Time signature for this section: drives launch-quantize grid,
        // metronome downbeat, and seeds new-phrase default length.
        // When hasTimeSig is false the scene inherits from Song or Set (DESIGN §4.8).
        bool hasTimeSig = false;
        TimeSig coreTime{};

        // Scene A/B parameter snapshots (Phase 7.7 / DESIGN §17).
        // Full crossfader implementation: ROADMAP 5.2.
        // Key = (trackIdx << 16) | slotIdx for a flat map lookup.
        std::map<std::pair<int, int>, float> morphA{};
        std::map<std::pair<int, int>, float> morphB{};

        // Scene-wide swing delta (DESIGN §19.2). Added to Song::swing (plus per-track
        // Song::SongTrack::swing) to form the full effective swing for this section.
        float swing = 0.0f;

        // Optional Scene-level tempo override (DESIGN §4.9).
        // Stored as a ratio vs the resolved Song tempo (which is itself vs global root).
        // When false, the scene plays at the Song/global tempo (ratio = 1.0).
        bool hasTempo = false;
        double tempoRatio = 1.0;

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
        for (const bool m : s.activeMask)
            if (!m) return true;
        if (s.hasTimeSig) return true;
        if (s.swing != 0.0f) return true;
        return !s.morphA.empty() || !s.morphB.empty();
    }
}
