#pragma once

#include <array>
#include <string>
#include <vector>
#include <utility>

#include "ClipboardType.h"
#include "../core/Sequence.h"  // Step, Track, Sequence, kNumTracks, kMaxStepsPerTrack
#include "../core/Phrase.h"    // Phrase
#include "../core/Scene.h"     // Scene, kPhrasesPerTrack implied via Song.h deps

namespace lockstep
{
    // One step + its offset from the copy anchor (lowest step index in the selection).
    struct StepClipEntry
    {
        int relOffset = 0;
        Step data;
    };

    // One slot's worth of per-step P-Lock data for a section copy.
    struct SectionClipSlot
    {
        int slot = 0;
        // perStep[i] = (hasOverride, value); length == sectionTrackLength.
        std::vector<std::pair<bool, float>> perStep;
    };

    // Scene scope (DESIGN §23.3): the effective floor + all track phrases.
    struct SceneClip
    {
        Scene floor{};
        std::array<Phrase, kNumTracks> phrases{};
    };

    // Typed in-memory clipboard (MD.1).  One instance in the editor; never serialized.
    struct Clipboard
    {
        ClipboardType type = ClipboardType::None;

        // Step scope (MD.2): one or more steps, offsets from the anchor.
        std::vector<StepClipEntry> stepEntries;

        // Section scope (MD.3): one entry per slot in the source section.
        int sectionTrackLength = 16;
        std::vector<SectionClipSlot> sectionSlots;

        // Track scope (MD.4): full track copy.
        Track clipTrack;

        // Machine scope (9.29): the SOUND -- which engine, and its base params.
        // Deliberately not a Track copy: Track owns identity (routing, mute, phrase
        // assignment), Machine owns the sound, and pasting a sound must not drag the
        // channel's identity across with it.
        std::string clipMachineId;
        std::vector<float> clipMachineParams;

        // Pattern scope (MD.5): sequence + pattern mutes (partRef NOT included).
        Sequence clipSequence;
        std::array<bool, kNumTracks> clipPatternMutes{};

        // Scene scope (DESIGN §23.3) and omni-grab container.
        SceneClip scene;

        void clear()
        {
            type = ClipboardType::None;
            stepEntries.clear();
            sectionSlots.clear();
        }
    };
}
