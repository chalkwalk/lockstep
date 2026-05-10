#pragma once

#include "CCMappingTable.h"
#include "../machine/IMachine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

namespace lockstep
{
    class EditContext;

    // Bundles the callbacks MidiInput needs to dispatch CC events.
    struct CCMidiContext
    {
        CCMappingTable* table = nullptr;
        int focusTrack = -1;  // -1 = Global focus; 0-7 = Track
        std::function<float(int, int)>          getCurrentTrackValue;
        std::function<ParamMetadata(int, int)>  getMetadata;
        std::function<void(int, int, float)>    writeTrackParam;
    };

    class MidiInput
    {
    public:
        void process(const juce::MidiBuffer& midi,
                     EditContext& editContext,
                     const CCMidiContext& cc);
    };
}
