#pragma once

#include "ControllerEvent.h"

namespace lockstep
{
    // Translates raw JUCE key codes into ControllerEvents for the 10x4 layout (MHX).
    //
    // Physical layout (columns 1-10, rows 1-4):
    //
    //   [Fnc][Fil][TAP][ ^ ][Sc0][Sc1][Sc2][Sc3][Sc4][Sc5]  <- row 1  1 2 3 4 5 6 7 8 9 0
    //   [Trk][Cue][ < ][ v ][ > ][MCH][SNP][REC][PLY][STP]  <- row 2  Q W E R T Y U I O P
    //   ─────────┼──────────────────────────────────────────
    //   [Pat][Scn][St0][St1][St2][St3][St4][St5][St6][St7]  <- row 3  A S D F G H J K L ;
    //   [Mut][Mst][St8][St9][S10][S11][S12][S13][S14][S15]  <- row 4  Z X C V B N M , . /
    //
    //   Col 1 (1/Q/A/Z) = structural modifiers: Func / Track / Pattern / Mute.
    //   Col 2 (2/W/S/X) = performance modifiers: Fill / Cue / Scene / Master.
    //   Inverted-T nav: 4=NavUp above E=NavLeft, R=NavDown, T=NavRight.
    //   Func layer: Func+3=MetronomeToggle, Func+4=TrigModeSoundPool,
    //               Func+5-0=MetaSections 0-5,
    //               Func+E=StopReset, Func+R=TrigModeKeyboard, Func+T=TrigModeRetrig,
    //               Func+Y=ForkPart, Func+U=Restore, Func+I=VerbRecord(CPY),
    //               Func+O=VerbPlay(PST), Func+P=VerbStop(CLR).
    //   Track layer: Track+D-; = SelectTrack 0-7; Track+C-/ = SelectTrack 8-15.
    //   Mute layer:  Mute+D-;  = ToggleMute 0-7;  Mute+C-/  = ToggleMute 8-15.
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
