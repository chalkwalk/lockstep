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

            // --- Note routing (on and off) ---
            if (msg.isNoteOnOrOff())
            {
                int targetTrack = -1;
                if (cc.channelMode == ChannelMode::PerTrack)
                    targetTrack = msg.getChannel() - 1;  // ch 1-8 → track 0-7
                else
                    targetTrack = cc.focusTrack;  // -1 (Global focus) → ignored below

                if (targetTrack >= 0)
                {
                    if (msg.isNoteOn() && cc.onNoteOn)
                        cc.onNoteOn(targetTrack, metadata.samplePosition,
                                    msg.getNoteNumber(), msg.getVelocity());
                    else if (msg.isNoteOff() && cc.onNoteOff)
                        cc.onNoteOff(targetTrack, metadata.samplePosition,
                                     msg.getNoteNumber());
                }
                continue;
            }

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
