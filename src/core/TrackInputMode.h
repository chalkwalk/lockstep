#pragma once

#include <cstdint>

namespace lockstep
{
    // Per-track input mode. Determines how incoming MIDI and step-key presses
    // are interpreted on the focused track. Applies to the focused track only.
    // Default is PLAY (standard step-toggle + P-Lock editing).
    //
    // RAM-only; not serialised until play-testing proves it performance-sticky.
    enum class TrackInputMode : std::uint8_t
    {
        Play      = 0,  // Default: step grid is the trig/P-Lock editor.
        Edit      = 1,  // Reserved: role-tagged param target selector (sub-mode of Levels — MHZ.7).
        Chromatic = 2,  // Step cells are a 1-octave chromatic keyboard for live play.
        Levels    = 3,  // Step cells are 16 velocity buckets (1/16..16/16 of 127).
    };
}
