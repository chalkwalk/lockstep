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

        // Modifier key states (updated by PluginEditor key events).
        // MHY cluster:
        //   Col 1 (1/Q/A/Z): Func / Pattern / Scene / Mute.
        //   Col 2 (2/W/S/X): Track / Part / Master / Fill.
        // Col 1:
        bool funcHeld         = false;  // key 1
        bool patternScopeHeld = false;  // key Q (MHY: moved from A)
        bool patternScopeUsed = false;  // true if a step was pressed while PatternScope held
        bool sceneHeld        = false;  // key A (MHY: moved from S)
        bool muteHeld         = false;  // key Z
        // Col 2:
        bool trackHeld        = false;  // key 2 (MHY: moved from Q)
        bool partHeld         = false;  // key W (MHY new — §4.7)
        bool masterHeld       = false;  // key S (MHY: moved from X)
        bool fillHeld         = false;  // key X (MHY: moved from 2)
        // Cue is reserved (MU); no key bound post-MHY.
        bool cueHeld          = false;

        // Active trig-grid input mode.
        TrigGridMode trigGridMode = TrigGridMode::Default;

        // MG.1: root MIDI note for Keyboard trig-grid mode (default C4 = 60).
        int keyboardRoot = 60;

        // MG.2: retrig rate index; cycles through 0=1/16, 1=1/32, 2=1/48, 3=1/96.
        int retrigRateIndex = 0;

        // Returns the PPQ interval for the currently selected retrig rate.
        [[nodiscard]] static double retrigRatePpq(int idx) noexcept
        {
            static constexpr double kRates[] = { 0.25, 0.125, 1.0 / 12.0, 1.0 / 24.0 };
            if (idx < 0 || idx > 3) return 0.25;
            return kRates[idx];
        }

        // MG.2: tracks which raw key codes are currently held in Retrig mode.
        // Used to cancel retrig on key release.
        bool retrigKeyHeld = false;
        int  retrigKeyCode = -1;

        // MG.5: tracks which raw key code is held in Sound Pool mode.
        // Used to restore live-swap on key release.
        bool soundPoolKeyHeld = false;
        int  soundPoolKeyCode = -1;

        // Active master section (-1 = none).
        int masterSection = -1;

        // True whenever at least one step key is held (heldStepKeys_ non-empty).
        // Set by PluginEditor so KeyboardArea can show COP/PST/CLR on verb keys.
        bool stepHeld = false;

        // MHZ.3.4: step-driven P-Lock clear mode. Entered when Func+step is pressed
        // (no other step held). Step cells re-skin to show the target step's P-locked
        // slot labels; pressing a cell clears that slot's P-Lock. Released when Func
        // is released.
        bool pLockClearMode  = false;
        int  pLockClearTrack = -1;
        int  pLockClearStep  = -1;

        // MHZ.3.5: true while Func+Part are both held (machine picker mode).
        // Step cells re-skin to show available machine names; pressing a cell assigns
        // the machine for the active track.
        bool funcPartHeld = false;

        // Returns the first slot index for the currently active page on the given track.
        // Returns 0 if track is out of range or info.firstSlot is -1 (empty section).
        [[nodiscard]] int activeFirstSlot(int track, const SectionInfo& info) const
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks) || info.firstSlot < 0)
                return 0;
            const int section = trackSection[static_cast<std::size_t>(track)];
            const int page    = trackPage[static_cast<std::size_t>(track)]
                                         [static_cast<std::size_t>(section)];
            return info.firstSlot + (kParamsPerPage * page);
        }
    };
}
