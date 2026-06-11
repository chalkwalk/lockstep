#pragma once

#include <algorithm>
#include "Song.h"    // Song, Scene, Phrase, TrackKit, kNumTracks, kPhrasesPerTrack
#include "Track.h"   // Track — the working/merge representation the resolver consumes

namespace lockstep
{
    // ── New-hierarchy navigation + Phrase/Kit ⇄ working-Track projection ──────
    //
    // Source-of-truth contract (DESIGN §4.7): a track's musical content lives in
    // its **Phrase** (steps, length, conditions, note selection) and its sound
    // lives in its **Kit** (baseParams, divider, machine). The audio path and
    // step-edits operate on a `Track` (the legacy merge representation); these
    // free functions are the *only* sanctioned bridge between the two, so the
    // projection stays reversible — the property the old one-way
    // `syncSequenceFromCurrentScene()` lacked (edits to the working Track were
    // never written back, so a scene/phrase switch silently dropped them).
    //
    // Pure, header-only, JUCE-free: directly unit-testable without a processor.

    // Which phrase index a track plays right now: its sticky deviation if it has
    // one, else the scene's own diagonal row (scene S plays row S). Always clamped.
    [[nodiscard]] inline int resolveActivePhraseIdx(int sceneIdx,
                                                    bool deviated,
                                                    int deviationIdx,
                                                    [[maybe_unused]] int track) noexcept
    {
        const int idx = deviated ? deviationIdx : sceneIdx;
        return std::clamp(idx, 0, kPhrasesPerTrack - 1);
    }

    // Project a Phrase (content) + Kit (sound) into the merged working Track the
    // resolver consumes. Phrase owns the step/timing fields; Kit owns baseParams
    // and the clock divider.
    [[nodiscard]] inline Track projectPhraseToTrack(const Phrase& phrase,
                                                    const TrackKit& kit)
    {
        Track t;
        t.length = phrase.length;
        t.divider = kit.divider;
        t.baseParams = kit.baseParams;
        t.baseCond = phrase.baseCond;
        t.trigDefaults = phrase.trigDefaults;
        t.noteSelection = phrase.noteSelection;
        t.steps = phrase.steps;
        return t;
    }

    // Reverse projection — write the Phrase-owned fields of a working Track back
    // into its Phrase. This is the write-back the legacy sync omitted; calling it
    // before any scene/phrase switch is what makes live edits survive.
    inline void applyTrackEditsToPhrase(const Track& src, Phrase& dst) noexcept
    {
        dst.length = src.length;
        dst.baseCond = src.baseCond;
        dst.trigDefaults = src.trigDefaults;
        dst.noteSelection = src.noteSelection;
        dst.steps = src.steps;
        dst.initialised = true;
    }

    // Reverse projection for the Kit-owned fields of a working Track.
    inline void applyTrackBaseToKit(const Track& src, TrackKit& dst)
    {
        dst.baseParams = src.baseParams;
        dst.divider = src.divider;
    }
}
