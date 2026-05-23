#include "MidiOutMachine.h"

namespace lockstep
{
    MidiOutMachine::MidiOutMachine()
    {
        for (int i = 0; i < kNumCCs; ++i)
        {
            ccNumbers_[static_cast<std::size_t>(i)] = i;
            prevCC_   [static_cast<std::size_t>(i)] = -1;  // -1 forces first-block emission
        }
    }

    MidiOutMachine::~MidiOutMachine() = default;

    void MidiOutMachine::prepare(double /*sampleRate*/, int /*maxBlockSize*/)
    {
        devices_ = juce::MidiOutput::getAvailableDevices();

        // Reopen the device that matches destinationId_, or close if not found.
        int foundIdx = -1;
        for (int i = 0; i < devices_.size(); ++i)
        {
            if (devices_[i].identifier.toStdString() == destinationId_)
            {
                foundIdx = i;
                break;
            }
        }
        if (foundIdx >= 0)
            openDevice(foundIdx);
        else
            currentDestIdx_ = -1;
    }

    void MidiOutMachine::reset()
    {
        prevCC_.fill(-1);    // force full CC re-emission next block
        prevProgram_ = -2;   // force program re-emit next note-on
        activeNote_    = -1;
        activeChannel_ = 1;
    }

    void MidiOutMachine::processMidi(const juce::MidiBuffer& events,
                                      const ParamFrame&       params,
                                      juce::MidiBuffer&       midiOut)
    {
        if (params.empty()) return;

        // Reopen device if dest slot changed.
        const int destIdx = juce::jlimit(0,
                                          std::max(0, devices_.size() - 1),
                                          static_cast<int>(params[kSlotDest]));
        if (destIdx != currentDestIdx_)
            openDevice(destIdx);

        const int channel = juce::jlimit(1, 16,
                                          static_cast<int>(params[kSlotChannel]));
        const int program = static_cast<int>(params[kSlotProgram]);

        // Process note events with channel remapping and clean-channel-change logic.
        for (const auto& meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                // MF.3: if the channel changed while a note is sounding, send note-off
                // on the old channel first so the downstream synth doesn't stick.
                if (activeNote_ >= 0 && activeChannel_ != channel)
                {
                    midiOut.addEvent(
                        juce::MidiMessage::noteOff(activeChannel_, activeNote_),
                        meta.samplePosition);
                    activeNote_ = -1;
                }

                // MF.3: emit program change before the note-on when program changed.
                if (program >= 0 && program != prevProgram_)
                {
                    midiOut.addEvent(
                        juce::MidiMessage::programChange(channel, program),
                        meta.samplePosition);
                    prevProgram_ = program;
                }

                midiOut.addEvent(
                    juce::MidiMessage::noteOn(channel, msg.getNoteNumber(),
                                              msg.getVelocity()),
                    meta.samplePosition);
                activeNote_    = msg.getNoteNumber();
                activeChannel_ = channel;
            }
            else if (msg.isNoteOff())
            {
                midiOut.addEvent(
                    juce::MidiMessage::noteOff(channel, msg.getNoteNumber(),
                                               msg.getVelocity()),
                    meta.samplePosition);
                if (msg.getNoteNumber() == activeNote_)
                    activeNote_ = -1;
            }
            else
            {
                midiOut.addEvent(msg, meta.samplePosition);
            }
        }

        // Emit CC messages for any slot whose value has changed since last block.
        for (int ci = 0; ci < kNumCCs; ++ci)
        {
            const int slot = kSlotCC0 + ci;
            if (slot >= static_cast<int>(params.size())) break;
            const int value = juce::jlimit(0, 127,
                                            static_cast<int>(params[static_cast<std::size_t>(slot)]));
            const auto sz = static_cast<std::size_t>(ci);
            if (value != prevCC_[sz])
            {
                prevCC_[sz] = value;
                midiOut.addEvent(
                    juce::MidiMessage::controllerEvent(channel, ccNumbers_[sz], value), 0);
            }
        }

        // Standalone: send directly to the open device.
        if (midiOutput_ && !midiOut.isEmpty())
            midiOutput_->sendBlockOfMessagesNow(midiOut);
    }

    void MidiOutMachine::openDevice(int destIdx)
    {
        midiOutput_.reset();
        currentDestIdx_ = -1;
        if (destIdx < 0 || destIdx >= devices_.size()) return;

        midiOutput_ = juce::MidiOutput::openDevice(devices_[destIdx].identifier);
        if (midiOutput_)
        {
            destinationId_  = devices_[destIdx].identifier.toStdString();
            currentDestIdx_ = destIdx;
        }
    }

    void MidiOutMachine::setDestinationId(const std::string& id)
    {
        destinationId_ = id;
        // If prepare() has already run, find and open the matching device now.
        for (int i = 0; i < devices_.size(); ++i)
        {
            if (devices_[i].identifier.toStdString() == id)
            {
                openDevice(i);
                return;
            }
        }
        // Device not found in current list — will be opened next prepare() call.
        midiOutput_.reset();
        currentDestIdx_ = -1;
    }

    ParamSpec MidiOutMachine::paramSpec(int index) const
    {
        ParamSpec p;
        if (index == kSlotDest)
        {
            p.id = "dest";  p.label = "Dest";
            p.isStepped = true;
            p.maxValue  = static_cast<float>(std::max(0, devices_.size() - 1));
            p.sectionIndex = 1;
        }
        else if (index == kSlotChannel)
        {
            p.id = "channel";  p.label = "Chan";
            p.isStepped = true;  p.minValue = 1.0f;  p.maxValue = 16.0f;
            p.defaultValue = 1.0f;
            p.sectionIndex = 1;
        }
        else if (index == kSlotProgram)
        {
            p.id = "program";  p.label = "Prog";
            p.isStepped = true;  p.minValue = -1.0f;  p.maxValue = 127.0f;
            p.defaultValue = -1.0f;  // -1 = no program change
            p.sectionIndex = 1;
        }
        else if (index >= kSlotCC0 && index < kNumSlots)
        {
            const int ci  = index - kSlotCC0;
            const auto sz = static_cast<std::size_t>(ci);
            p.sectionIndex = (ci < 8) ? 2 : 3;
            p.id    = juce::String("cc") + juce::String(ci);
            if (ccLabels_[sz].isNotEmpty())
            {
                p.label = ccLabels_[sz];
            }
            else
            {
                const auto it = nameTable_.find(ccNumbers_[sz]);
                p.label = (it != nameTable_.end())
                            ? it->second
                            : (juce::String("CC") + juce::String(ccNumbers_[sz]));
            }
            p.maxValue = 127.0f;
        }
        return p;
    }

    SectionInfo MidiOutMachine::section(int index) const
    {
        switch (index)
        {
        case 1: return { "SRC", -1, 0, -1 };
        case 2: return { "",    -1, 0, -1 };  // CC bank A — label shown by SectionBar canonical name
        case 3: return { "",    -1, 0, -1 };  // CC bank B
        default: return {};
        }
    }

    void MidiOutMachine::setCCNumber(int ccSlot, int ccNumber)
    {
        if (ccSlot < 0 || ccSlot >= kNumCCs) return;
        ccNumbers_[static_cast<std::size_t>(ccSlot)] = juce::jlimit(0, 127, ccNumber);
        prevCC_   [static_cast<std::size_t>(ccSlot)] = -1;  // force re-emit with new CC number
    }

    int MidiOutMachine::ccNumber(int ccSlot) const
    {
        if (ccSlot < 0 || ccSlot >= kNumCCs) return 0;
        return ccNumbers_[static_cast<std::size_t>(ccSlot)];
    }

    void MidiOutMachine::setCCLabel(int ccSlot, const juce::String& label)
    {
        if (ccSlot < 0 || ccSlot >= kNumCCs) return;
        ccLabels_[static_cast<std::size_t>(ccSlot)] = label;
    }

    juce::String MidiOutMachine::ccLabel(int ccSlot) const
    {
        if (ccSlot < 0 || ccSlot >= kNumCCs) return {};
        return ccLabels_[static_cast<std::size_t>(ccSlot)];
    }

    void MidiOutMachine::allNotesOff(juce::MidiBuffer& midiOut)
    {
        midiOut.addEvent(juce::MidiMessage::allNotesOff(activeChannel_), 0);
        midiOut.addEvent(juce::MidiMessage::allControllersOff(activeChannel_), 0);
        activeNote_ = -1;
        if (midiOutput_ && !midiOut.isEmpty())
            midiOutput_->sendBlockOfMessagesNow(midiOut);
    }

    void MidiOutMachine::setCCNameTable(std::unordered_map<int, juce::String> table)
    {
        nameTable_ = std::move(table);
    }

    void MidiOutMachine::clearCCNameTable()
    {
        nameTable_.clear();
    }
}
