#pragma once

#include <array>
#include <string>
#include <vector>
#include "../machine/IMachine.h"
#include "../machine/StubMachine.h"
#include "TrackFltrState.h"
#include "TrackChannelState.h"
#include "TrackEnvState.h"

namespace lockstep
{
    // Per-(track, Song) instrument identity and base parameter state.
    // A musician plays the same Kit throughout a Song; Kits may differ
    // between Songs.  Replaces PartTrack (Phase 7 / DESIGN §4.7).
    struct TrackKit
    {
        // Stable machine string id (e.g. "lockstep.sampler.v1").
        // Unknown ids on load fall back to StubMachine.
        // Default is StubMachine (empty/unmaterialised track): the serializer
        // skips writing default-stub tracks (PluginState §songTrack), so the
        // read-back default MUST be stub too — otherwise skipped tracks would
        // resurrect as samplers and newProject() would diverge from a fresh
        // construction (track 0 sampler, the rest empty).
        std::string machineId = StubMachine::kMachineId;

        // Stable MIDI output device identifier (empty = none).
        std::string destinationId;

        // Machine parameter defaults — one float per slot.
        ParamFrame baseParams{};

        // Post-machine FLTR block — always present (DESIGN §14).
        TrackFltrState fltrState;

        // CHANNEL block — always present: level, pan, sendA, sendB (DESIGN §14).
        TrackChannelState channelState;

        // ENVELOPE block — present only when !hasInternalAmp() (DESIGN §14).
        TrackEnvState envState;

        // Per-track CC slot config for MIDI-out tracks.
        std::vector<int> midiCCNumbers{};
        std::vector<std::string> midiCCLabels{};

        // Active hardware preset id (e.g. "elektron.digitone"; empty = none).
        std::string midiPresetName;

        // Clock divider: 1 = base 1/16 grid.  Moved here from Track (Phase 7);
        // it is a property of the musician for the song, not of the phrase.
        int divider = 1;

        // 6.5: per-track insert slots (post-AMP).  effectId empty = no effect.
        struct InsertSlot
        {
            std::string effectId;   // stable id (e.g. "lockstep.delay.v1"); empty = none
            ParamFrame baseParams;
            bool bypass = false;
        };
        std::array<InsertSlot, 2> inserts;
    };
}
