#include "MidiInput.h"
#include "EditContext.h"

namespace lockstep
{
    void MidiInput::process(const juce::MidiBuffer& midi,
                            EditContext& editContext,
                            const CCMidiContext& cc)
    {
        juce::ignoreUnused(editContext);

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();

            // In Per-Track mode, channels 9-16 carry no sequencer meaning.
            if (cc.channelMode == ChannelMode::PerTrack && msg.getChannel() > 8)
                continue;

            if (!msg.isController())
                continue;

            // Pending MIDI Learn: capture first CC, suppress normal dispatch.
            if (cc.onLearnCapture)
            {
                cc.onLearnCapture(msg.getControllerNumber());
                return;
            }

            if (cc.table == nullptr || !cc.getCurrentTrackValue
                || !cc.getMetadata || !cc.writeTrackParam)
                continue;

            cc.table->dispatch(
                msg.getControllerNumber(),
                msg.getControllerValue(),
                cc.focusTrack,
                cc.mzSlots,
                cc.getCurrentTrackValue,
                cc.getMetadata,
                cc.writeTrackParam);
        }
    }
}
