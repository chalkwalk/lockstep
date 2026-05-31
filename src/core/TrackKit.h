#pragma once

#include <string>
#include <vector>
#include "../machine/IMachine.h"
#include "TrackAmpState.h"
#include "TrackFltrState.h"

namespace lockstep
{
    // Per-(track, Piece) instrument identity and base parameter state.
    // A musician plays the same Kit throughout a Piece; Kits may differ
    // between Pieces.  Replaces PartTrack (Phase 7 / DESIGN §4.7).
    struct TrackKit
    {
        // Stable machine string id (e.g. "lockstep.sampler.v1").
        // Unknown ids on load fall back to StubMachine.
        std::string machineId    = "lockstep.sampler.v1";

        // Stable MIDI output device identifier (empty = none).
        std::string destinationId;

        // Machine parameter defaults — one float per slot.
        ParamFrame baseParams{};

        // Post-machine FLTR block (DESIGN §14).
        TrackFltrState fltrState;

        // Post-machine AMP block (DESIGN §14).
        TrackAmpState ampState;

        // Per-track CC slot config for MIDI-out tracks.
        std::vector<int>         midiCCNumbers{};
        std::vector<std::string> midiCCLabels{};

        // Active hardware preset id (e.g. "elektron.digitone"; empty = none).
        std::string midiPresetName;

        // Clock divider: 1 = base 1/16 grid.  Moved here from Track (Phase 7);
        // it is a property of the musician for the song, not of the phrase.
        int divider = 1;
    };
}
