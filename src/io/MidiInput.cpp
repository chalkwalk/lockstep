#include "MidiInput.h"
#include "EditContext.h"

namespace lockstep
{
    void MidiInput::process(const juce::MidiBuffer& midi, EditContext& editContext)
    {
        juce::ignoreUnused(midi, editContext);
    }
}
