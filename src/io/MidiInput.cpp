#include "MidiInput.h"
#include "EditContext.h"

namespace lockstep
{
    void MidiInput::process(const juce::MidiBuffer& midi,
                            EditContext& editContext,
                            const CCMidiContext& cc)
    {
        juce::ignoreUnused(editContext);

        if (cc.table == nullptr || !cc.getCurrentTrackValue
            || !cc.getMetadata || !cc.writeTrackParam)
            return;

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (!msg.isController())
                continue;

            cc.table->dispatch(
                msg.getControllerNumber(),
                msg.getControllerValue(),
                cc.focusTrack,
                cc.getCurrentTrackValue,
                cc.getMetadata,
                cc.writeTrackParam);
        }
    }
}
