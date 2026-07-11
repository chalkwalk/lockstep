#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace lockstep
{
    class LockstepProcessor;

    // TimelineModel — the always-on tape timeline strip (DESIGN §40.6).
    //
    // A tape you cannot see the position of is a tape you cannot punch into. The
    // answer is not a canvas (fence #5) and not a mode: it is a second
    // inspector-class, DISPLAY-ONLY chrome strip built from a pure function, so a
    // controller display can render the same thing the screen does (dual-target,
    // PRINCIPLES §19). You never click it — interaction is the console and the jog.
    //
    // Everything is normalised to [0,1] across the reel so the renderer is a dumb
    // mapper: cursor, markers, and the recorded extent are fractions of the reel
    // length. `active` is false when no tape exists, and the strip hides.

    struct TimelineMarker
    {
        float pos01 = 0.0f;   // marker position as a fraction of the reel
        int ordinal = 0;
    };

    struct TimelineModel
    {
        bool active = false;          // a tape exists → show the strip
        bool recording = false;       // tape is punched in (the cursor is hot)

        juce::String position;        // "bars.beats" caption of the current position
        float cursor01 = 0.0f;        // playhead as a fraction of the reel [0,1]
        float recordedExtent01 = 0.0f;// how much of the reel holds a take [0,1]
        float mediumFull01 = 0.0f;    // used / capacity, for the near-full warning
        float chaseRatio = 1.0f;      // §40.2: reel/engine rate; !=1 = varispeed

        std::vector<TimelineMarker> markers;
    };

    // Build the strip from the focused (or first) tape's state. `barPpq` and
    // `samplesPerBar` come from the live transport so the position caption reads in
    // musical time. Pure: reads the processor, allocates only the marker vector.
    TimelineModel buildTimelineModel(const LockstepProcessor& proc,
                                     double samplesPerBar, double barPpq) noexcept;
}
