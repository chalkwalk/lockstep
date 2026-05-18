#pragma once

#include <array>
#include <vector>
#include <utility>

#include "ClipboardType.h"
#include "../core/Sequence.h"  // Step, Track, Sequence, kNumTracks, kMaxStepsPerTrack

namespace lockstep
{
    // One step + its offset from the copy anchor (lowest step index in the selection).
    struct StepClipEntry
    {
        int  relOffset = 0;
        Step data;
    };

    // One slot's worth of per-step P-Lock data for a section copy.
    struct SectionClipSlot
    {
        int  slot = 0;
        // perStep[i] = (hasOverride, value); length == sectionTrackLength.
        std::vector<std::pair<bool, float>> perStep;
    };

    // Typed in-memory clipboard (MD.1).  One instance in the editor; never serialized.
    struct Clipboard
    {
        ClipboardType type = ClipboardType::None;

        // Step scope (MD.2): one or more steps, offsets from the anchor.
        std::vector<StepClipEntry> stepEntries;

        // Section scope (MD.3): one entry per slot in the source section.
        int                          sectionTrackLength = 16;
        std::vector<SectionClipSlot> sectionSlots;

        // Track scope (MD.4): full track copy.
        Track clipTrack;

        // Pattern scope (MD.5): sequence + pattern mutes (partRef NOT included).
        Sequence                     clipSequence;
        std::array<bool, kNumTracks> clipPatternMutes{};

        void clear()
        {
            type = ClipboardType::None;
            stepEntries.clear();
            sectionSlots.clear();
        }
    };
}
