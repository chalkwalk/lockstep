#pragma once

#include "CCMappingTable.h"
#include "../core/ChannelMode.h"
#include "../machine/IMachine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <functional>

namespace lockstep
{
    class EditContext;

    // Bundles the callbacks MidiInput needs to dispatch CC events.
    struct CCMidiContext
    {
        CCMappingTable* table = nullptr;
        int focusTrack = -1;                    // -1 = Global; 0-7 = Track
        std::array<int, 4> mzSlots{ -1, -1, -1, -1 };
        ChannelMode channelMode = ChannelMode::Omni;

        std::function<float(int, int)> getCurrentTrackValue;
        std::function<ParamSpec(int, int)> getMetadata;
        std::function<void(int, int, float)> writeTrackParam;

        // When set, the next CC received is passed here instead of dispatched.
        // Cleared by the callback itself (via the learn-complete path).
        std::function<void(int ccNumber)> onLearnCapture;

        // Drives the morph crossfader for CCScope::Crossfader mappings (5.2).
        std::function<void(float)> setCrossfaderValue;

        // Called for each note-on after channel-mode routing resolves the target track.
        // Args: (targetTrack 0-7, sampleOffset, midiNote 0-127, velocity 1-127).
        // Not called when focus is Global in Omni mode.
        std::function<void(int, int, int, int)> onNoteOn;

        // Called for each note-off after routing.
        // Args: (targetTrack 0-7, sampleOffset, midiNote 0-127).
        std::function<void(int, int, int)> onNoteOff;
    };

    class MidiInput
    {
    public:
        void process(const juce::MidiBuffer& midi,
                     EditContext& editContext,
                     const CCMidiContext& cc);
    };
}
