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
        float pos01 = 0.0f;   // marker position as a fraction of the bar domain
        int ordinal = 0;
    };

    // S8: the recorded end of one tape, as a fraction of the bar domain, plus
    // whether it is the currently chosen (focused/first) tape. When several tapes
    // exist the strip shows an end lug per tape and highlights the chosen one.
    struct TapeEnd
    {
        float end01 = 0.0f;
        int track = -1;
        bool focused = false;
    };

    // §19 hardware proxy: how close the playhead is to the NEXT marker ahead, as a
    // 0→1 ramp over `windowSamples` before it (0 far, 1 at the marker). 0 when there
    // is no marker ahead or the playhead has passed it. Pure so it can be unit-tested
    // and rendered identically on an external controller (dual-target, PRINCIPLES §19).
    [[nodiscard]] inline float markerApproach01(double pos, double nextMarkerPos,
                                                double windowSamples) noexcept
    {
        if (nextMarkerPos < 0.0 || windowSamples <= 0.0) return 0.0f;
        const double d = nextMarkerPos - pos;          // distance ahead
        if (d < 0.0 || d > windowSamples) return 0.0f;  // passed it, or still too far
        return static_cast<float>(1.0 - d / windowSamples);
    }

    struct TimelineModel
    {
        // S8: the strip is ALWAYS visible. `active` now means "a tape exists" (so
        // the extent / markers / tape-ends are meaningful and the medium-full
        // warning applies); the bar ruler, cursor and captions render regardless,
        // driven by the transport so you see time advance with no tape at all.
        bool active = false;          // a tape exists
        bool recording = false;       // a tape is punched in (the cursor is hot)

        juce::String position;        // "bars.beats" caption (transport-driven)
        juce::String wallTime;        // "m:ss" wall-clock caption (transport-driven)
        int domainBars = 32;          // strip domain in bars = max(32, longest tape, cursor)
        double cursorBars = 0.0;      // transport position in bars
        double secondsPerBar = 0.0;   // for the wall-clock ruler
        float cursor01 = 0.0f;        // playhead as a fraction of the bar domain [0,1]
        float recordedExtent01 = 0.0f;// chosen tape's take extent in the bar domain
        float mediumFull01 = 0.0f;    // used / capacity, for the near-full warning
        float chaseRatio = 1.0f;      // §40.2: reel/engine rate; !=1 = varispeed
        float markerApproach = 0.0f;  // §19: 0→1 as the playhead nears the next marker

        std::vector<TimelineMarker> markers;  // chosen tape, re-based to the bar domain
        std::vector<TapeEnd> tapeEnds;        // every tape's recorded end
    };

    // Build the always-on strip. `samplesPerBar`, `barPpq` and `sampleRate` come
    // from the live transport so the bar domain, position caption and wall-clock
    // ruler read in musical + real time — with or without a tape. Pure: reads the
    // processor, allocates only the marker / tape-end vectors.
    TimelineModel buildTimelineModel(const LockstepProcessor& proc, double samplesPerBar,
                                     double barPpq, double sampleRate) noexcept;
}
