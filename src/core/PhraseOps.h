#pragma once

#include "Phrase.h"
#include "Song.h"   // kPhrasesPerTrack, Song::SongTrack::phrases layout

#include <array>

namespace lockstep
{
    // ── Pure phrase copy/move helpers (5.3 / DESIGN §23.3) ────────────────────
    //
    // JUCE-free and header-only so the phrase copy/paste/fork machinery is unit-
    // testable without a LockstepProcessor. The processor delegates its slot
    // arithmetic here; the audio path never touches these.

    // Content equality of two phrases, IGNORING the `initialised` bookkeeping flag.
    // Used to skip a needless fork prompt when a paste would be a no-op (DESIGN
    // §23.3 "Re-stamping identical content skips the prompt"). Compares exactly the
    // musical content: length, every step, and the per-phrase trig configuration.
    [[nodiscard]] inline bool phraseContentEqual(const Phrase& a, const Phrase& b) noexcept
    {
        return a.length == b.length
            && a.steps == b.steps
            && a.trigDefaults == b.trigDefaults
            && a.baseCond == b.baseCond
            && a.noteSelection == b.noteSelection;
    }

    // Lowest uninitialised phrase slot in a track's pool, or -1 when the pool is
    // full. (Kept for the create-scene conflict hint; see lastFreePhraseSlot for
    // the fork allocator.)
    [[nodiscard]] inline int firstFreePhraseSlot(
        const std::array<Phrase, kPhrasesPerTrack>& phrases) noexcept
    {
        for (int i = 0; i < kPhrasesPerTrack; ++i)
            if (!phrases[static_cast<std::size_t>(i)].initialised)
                return i;
        return -1;
    }

    // HIGHEST uninitialised phrase slot, or -1 when the pool is full. The
    // auto-fork allocator (§23.3): performed scenes grow bottom-up from slot 0,
    // so transient/library content (a fork's fresh phrase) is placed top-down —
    // the two regions meet in the middle and "pool full" is when they collide.
    [[nodiscard]] inline int lastFreePhraseSlot(
        const std::array<Phrase, kPhrasesPerTrack>& phrases) noexcept
    {
        for (int i = kPhrasesPerTrack - 1; i >= 0; --i)
            if (!phrases[static_cast<std::size_t>(i)].initialised)
                return i;
        return -1;
    }
}
