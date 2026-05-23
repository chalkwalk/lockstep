#pragma once

#include <array>
#include "../core/Sequence.h"   // kNumTracks
#include "../io/TrigGridMode.h"
#include "../machine/IMachine.h"  // kMaxSections

namespace lockstep
{
    // UI-local selection state. Not persisted. Not accessed from the audio thread.
    struct UiState
    {
        // Per-track active section (0 .. kMaxSections-1).
        std::array<int, kNumTracks> trackSection{};

        // Per-track, per-section active page index (0 .. pageCount-1).
        std::array<std::array<int, IMachine::kMaxSections>, kNumTracks> trackPage{};

        // Left-column modifier key states (updated by PluginEditor key events).
        bool funcHeld         = false;  // key 1
        bool trackHeld        = false;  // key Q
        bool muteHeld         = false;  // key A
        bool fillHeld         = false;  // key Z
        bool patternScopeHeld = false;  // Func+2 (while held)
        bool patternScopeUsed = false;  // true if a step was pressed while PatternScope held

        // Active trig-grid input mode.
        TrigGridMode trigGridMode = TrigGridMode::Default;

        // MG.1: root MIDI note for Keyboard trig-grid mode (default C4 = 60).
        int keyboardRoot = 60;

        // Active master section (-1 = none).
        int masterSection = -1;

        // Returns the first slot index for the currently active page on the given track.
        // Returns 0 if track is out of range or info.firstSlot is -1 (empty section).
        [[nodiscard]] int activeFirstSlot(int track, const SectionInfo& info) const
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks) || info.firstSlot < 0)
                return 0;
            const int section = trackSection[static_cast<std::size_t>(track)];
            const int page    = trackPage[static_cast<std::size_t>(track)]
                                         [static_cast<std::size_t>(section)];
            return info.firstSlot + 4 * page;
        }
    };
}
