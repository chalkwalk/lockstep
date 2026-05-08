#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    class EditContext;

    // Seam for MIDI ingestion. M0 only forwards note-on/off into a sink for
    // logging; the abs/rel CC routing and edit-context interception land in
    // M5.
    class MidiInput
    {
    public:
        void process(const juce::MidiBuffer& midi, EditContext& editContext);
    };
}
