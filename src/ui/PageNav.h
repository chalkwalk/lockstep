#pragma once

#include <algorithm>

namespace lockstep
{
    // Pure step-grid page clamp with the §34.4 scroll-past-end rule, factored out
    // of KeyboardArea so it can be unit-tested without a juce::Component.
    //
    //   numPages       = ceil(trackLength / pageSteps), always >= 1.
    //   In-range pages  = [0, numPages-1] (these contain steps).
    //   While `unlocked`, one empty page (index numPages) is also reachable so a
    //   longer length can be authored out there.
    //   The unlock auto-clears once the clamped page is back within range —
    //   either the performer navigated back, or a longer length grew the span.
    struct PageClampResult
    {
        int  page;
        bool unlocked;
    };

    inline PageClampResult clampStepPage(int desiredPage, int numPages, bool unlocked)
    {
        const int lastInRange = std::max(0, numPages - 1);
        const int hardMax     = unlocked ? numPages : lastInRange;
        const int page        = std::clamp(desiredPage, 0, hardMax);
        const bool stillUnlocked = unlocked && (page > lastInRange);
        return { page, stillUnlocked };
    }

} // namespace lockstep
