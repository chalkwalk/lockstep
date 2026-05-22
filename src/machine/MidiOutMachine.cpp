#include "MidiOutMachine.h"

namespace lockstep
{
    MidiOutMachine::MidiOutMachine()
    {
        for (int i = 0; i < kNumCCs; ++i)
            ccNumbers_[static_cast<std::size_t>(i)] = i;
    }

    MidiOutMachine::~MidiOutMachine() = default;

    void MidiOutMachine::prepare(double, int) {}

    void MidiOutMachine::reset() {}

    void MidiOutMachine::process(const juce::MidiBuffer& /*events*/,
                                  const ParamFrame& /*params*/,
                                  juce::AudioBuffer<float>& /*buffer*/)
    {
        // MF.2: wire destination routing here.
        // Events (note-on/off) and CC param values will be forwarded to the
        // configured MIDI output device / host bus.
    }

    ParamSpec MidiOutMachine::paramSpec(int index) const
    {
        ParamSpec p;
        if (index == kSlotDest)
        {
            p.id = "dest";  p.label = "Dest";
            p.isStepped = true;  p.maxValue = 15.0f;
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
            p.defaultValue = -1.0f;  // -1 = no program change on pattern start
            p.sectionIndex = 1;
        }
        else if (index >= kSlotCC0 && index < kNumSlots)
        {
            const int ci = index - kSlotCC0;
            const auto cSz = static_cast<std::size_t>(ci);
            // Slots 3-10 (cc[0..7]) live in section 2; slots 11-18 (cc[8..15]) in section 3.
            p.sectionIndex = (ci < 8) ? 2 : 3;
            p.id = juce::String("cc") + juce::String(ci);
            p.label = ccLabels_[cSz].isEmpty()
                        ? (juce::String("CC") + juce::String(ccNumbers_[cSz]))
                        : ccLabels_[cSz];
            p.maxValue = 127.0f;
        }
        return p;
    }

    SectionInfo MidiOutMachine::section(int index) const
    {
        // Labels for sections 2 and 3 are shown by the SectionBar via the canonical
        // kCanonicalSectionNames array; the machine provides empty labels for them
        // since the display name is overridden in the UI by MF.5's CC bank chrome.
        switch (index)
        {
        case 1: return { "SRC", -1, 0, -1 };
        case 2: return { "",    -1, 0, -1 };  // CC bank A (repurposed FLTR key)
        case 3: return { "",    -1, 0, -1 };  // CC bank B (repurposed AMP key)
        default: return {};
        }
    }

    void MidiOutMachine::setCCNumber(int ccSlot, int ccNumber)
    {
        if (ccSlot < 0 || ccSlot >= kNumCCs) return;
        ccNumbers_[static_cast<std::size_t>(ccSlot)] = juce::jlimit(0, 127, ccNumber);
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
}
