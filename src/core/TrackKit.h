#pragma once

#include <array>
#include <string>
#include <vector>
#include "../machine/IMachine.h"
#include "../machine/StubMachine.h"
#include "AccentVel.h"
#include "Density.h"
#include "Scale.h"
#include "Subdivision.h"
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
        // Stable machine string id (e.g. "lockstep.sample.v1").
        // Unknown ids on load fall back to StubMachine.
        // Default is StubMachine (empty/unmaterialised track): the serializer
        // skips writing default-stub tracks (PluginState §songTrack), so the
        // read-back default MUST be stub too — otherwise skipped tracks would
        // resurrect as samplers and newProject() would diverge from a fresh
        // construction (track 0 sampler, the rest empty).
        std::string machineId = StubMachine::kMachineId;

        // Stable MIDI output device identifier (empty = none).
        std::string destinationId;

        // Disk-streamed source file path for StreamMachine (DESIGN §29.2). Held as
        // a per-Kit path rather than a SamplePool entry because Static streams from
        // disk and never decodes the audio into RAM (PRINCIPLES §12). Empty = none.
        std::string streamPath;

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

        // Musical subdivision: combined index (0-26, see Subdivision.h).
        // Default 18 = 1/16 straight.  Moved here from Track (Phase 7).
        int subdivIndex = kSubdivDefault;

        // Density overlay (§39) — durable per-song-per-track; serialized v19+.
        Density::Musicality densityMusicality = Density::Musicality::Mixed;
        Density::DensitySelection densitySelection = Density::DensitySelection::Scrub;

        // Live velocity overlay — durable per-song-per-track; serialized v20+.
        // When velMode==Bar, metric weight against coreTime modulates note velocity
        // at emit time (Replace = override authored; Mix = add delta on top).
        VelMode  velMode   = VelMode::Off;
        VelBlend velBlend  = VelBlend::Replace;
        float    velDepth  = 0.6f;   // [0,1]
        int      velCenter = 90;     // [1,127]

        // Per-track Scale stage (DESIGN §4.10): conform the track's output notes
        // (live + sequenced) to the effective key. Off / Snap / Filter. v23+.
        ScaleMode scaleMode = ScaleMode::Off;

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
