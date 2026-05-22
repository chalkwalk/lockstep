#pragma once

#include "IMachine.h"
#include <array>

namespace lockstep
{
    // First-class MIDI-output machine (DESIGN §15).
    // Translates sequencer-emitted MIDI events to a configured external destination.
    // Bypasses both post-machine FLTR and AMP blocks; section keys 5/6 are
    // repurposed to expose the CC bank pages instead (MF.5).
    // Destination routing to actual MIDI devices is implemented in MF.2.
    class MidiOutMachine : public IMachine
    {
    public:
        MidiOutMachine();
        ~MidiOutMachine() override;

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

        // MF.2: destination routing not yet wired; events forwarded verbatim for now.
        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] const char* machineId() const override { return kMachineId; }
        static constexpr const char* kMachineId = "lockstep.midiout.v1";

        [[nodiscard]] int       numParams()          const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;
        [[nodiscard]] int       numSections()        const override { return kNumSections; }
        [[nodiscard]] SectionInfo section(int index) const override;

        [[nodiscard]] int  maxVoices()         const override { return 0; }
        [[nodiscard]] bool hasInternalFilter() const override { return true; }
        [[nodiscard]] bool hasInternalAmp()    const override { return true; }

        // Per-track configurable CC numbers (MF.4). Index 0..15 maps to cc[0..15] slots.
        // Default is CC number == slot index (cc[0] → CC 0, cc[1] → CC 1, etc.).
        void setCCNumber(int ccSlot, int ccNumber);
        [[nodiscard]] int ccNumber(int ccSlot) const;

        // Per-track configurable CC labels (MF.4). Empty string = use default "CC<N>".
        void setCCLabel(int ccSlot, const juce::String& label);
        [[nodiscard]] juce::String ccLabel(int ccSlot) const;

    private:
        // Section 1 "SRC": destination, channel, program
        static constexpr int kSlotDest    = 0;
        static constexpr int kSlotChannel = 1;
        static constexpr int kSlotProgram = 2;
        // Section 2 (repurposed FLTR key → CC bank A): cc[0..7]
        static constexpr int kSlotCC0     = 3;
        // Section 3 (repurposed AMP key → CC bank B): cc[8..15]
        static constexpr int kSlotCC8     = 11;

        static constexpr int kNumCCs      = 16;
        static constexpr int kNumSlots    = 3 + kNumCCs;   // 19
        static constexpr int kNumSections = 4;

        std::array<int, kNumCCs>         ccNumbers_{};
        std::array<juce::String, kNumCCs> ccLabels_{};
    };
}
