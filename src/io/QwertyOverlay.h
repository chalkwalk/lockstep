#pragma once

#include "ControllerEvent.h"

namespace lockstep
{
    // Translates raw JUCE key codes into ControllerEvents for the 9x4 layout.
    //
    // Physical layout (columns 1-9, rows 1-4):
    //
    //   [Fnc][Rec][Nav][Sc0][Sc1][Sc2][Sc3][Sc4][Sc5]   <- row 1
    //   [Trk][</N][ v ][>/Y][Ply][ Cp][ Pt][ Cl][Tap]   <- row 2
    //   [Mut][St1][St2][St3][St4][St5][St6][St7][St8]    <- row 3
    //   [Fil][St9][S10][S11][S12][S13][S14][S15][S16]    <- row 4
    //
    //   Left column (1/Q/A/Z) = dedicated modifier strip (Func/Track/Mute/Fill).
    //   Func layer: Func+2(Rec)=Snapshot, Func+T(Ply)=Restore, Func+E=StopReset,
    //               Func+4-9=MetaSections,
    //               Func+Y/U/I=TrigModeKeyboard/Retrig/SoundPool.
    //   Track layer: Track+S-L = SelectTrack 0-7; Track+X-. = SelectTrack 8-15.
    //   Mute layer:  Mute+S-L  = ToggleMute 0-7;  Mute+X-.  = ToggleMute 8-15.
    class QwertyOverlay
    {
    public:
        // Accepts a JUCE KeyPress key code (uppercase ASCII for letter/digit keys)
        // and the current modifier-key states.  Returns a ControllerEvent::Type of
        // ButtonDown; the caller sets ButtonUp on key release for the same button.
        [[nodiscard]] ControllerEvent resolve(int keyCode,
                                              bool funcHeld,
                                              bool trackHeld,
                                              bool muteHeld) const;

        // Returns true if the key is a decorative edge/anchor key that sits just
        // outside the 9-column grid.  resolve() already returns an empty event for
        // these, but callers should still treat them as "handled" (return true from
        // keyPressed) to prevent spurious host-level reactions.
        [[nodiscard]] static bool isEdgeKey(int keyCode) noexcept;
    };
}
