#pragma once
// Pure free functions for Project::soundPool maintenance.
// No JUCE dependency — directly unit-testable.

#include "Arrangement.h"

namespace lockstep
{
    // After removing pool entry at index k, update all step soundId references
    // in the arrangement (all songs × tracks × phrases + working sequence):
    //   hasSoundId && soundId == k → clear (hasSoundId = false, soundId = -1)
    //   hasSoundId && soundId >  k → decrement by 1
    //
    // Must be called under withQuiescedEngine before the pool entry is removed.
    //
    // Note: checkpoint scratch stacks (songStack_ / trackStack_ / phraseStack_)
    // are private and therefore not remapped here. Restoring a checkpoint taken
    // before the deletion may temporarily reinstate stale soundIds; the resolver
    // null-checks at read time (PluginProcessor.cpp) so this fails soft.
    inline void remapSoundIdsAfterRemoval(Arrangement& arr, int k)
    {
        auto fix = [k](TrigOverride& to) {
            if (!to.hasSoundId) return;
            if (to.soundId == k)
            {
                to.hasSoundId = false;
                to.soundId = -1;
            }
            else if (to.soundId > k)
            {
                --to.soundId;
            }
        };
        auto fixStep = [&](Step& step) {
            fix(step.trigOverride);
            fix(step.fillTrigOverride);
        };

        // Working sequence (the buffer the audio thread reads).
        for (auto& track : arr.working.tracks)
            for (auto& step : track.steps)
                fixStep(step);

        // All songs × tracks × phrases.
        for (auto& song : arr.songs)
            for (auto& st : song.tracks)
                for (auto& phrase : st.phrases)
                    for (auto& step : phrase.steps)
                        fixStep(step);
    }
}
