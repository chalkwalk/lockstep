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
        bool funcHeld  = false;  // key 1
        bool trackHeld = false;  // key Q
        bool muteHeld  = false;  // key A
        bool fillHeld  = false;  // key Z

        // Active trig-grid input mode.
        TrigGridMode trigGridMode = TrigGridMode::Default;

        // Active master section (-1 = none).
        int masterSection = -1;

        // Returns the first slot index for the currently active page on the given track.
        [[nodiscard]] int activeFirstSlot(int track, const SectionInfo& info) const
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks))
                return 0;
            const int section = trackSection[static_cast<std::size_t>(track)];
            const int page    = trackPage[static_cast<std::size_t>(track)]
                                         [static_cast<std::size_t>(section)];
            return info.firstSlot + 4 * page;
        }
    };
}
