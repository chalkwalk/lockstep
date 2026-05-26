#pragma once

#include "IMachine.h"
#include <array>
#include <string>
#include <unordered_map>
#include <juce_audio_devices/juce_audio_devices.h>

namespace lockstep
{
    // First-class MIDI-output machine (DESIGN §15).
    // Translates sequencer-emitted MIDI events to a configured external destination.
    // Bypasses both post-machine FLTR and AMP blocks (hasInternalFilter/Amp = true).
    // Section keys 5/6 are repurposed to expose CC bank pages instead (MF.5).
    class MidiOutMachine : public IMachine
    {
    public:
        MidiOutMachine();
        ~MidiOutMachine() override;

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

        // Audio path unused — called via processMidi() instead.
        void process(const juce::MidiBuffer&, const ParamFrame&,
                     juce::AudioBuffer<float>&) override {}

        // Routes note events (channel-remapped) and CC messages to the open device.
        // midiOut receives the same messages so the processor can also forward them
        // to the host MIDI output bus in plugin mode.
        void processMidi(const juce::MidiBuffer& events,
                         const ParamFrame&       params,
                         juce::MidiBuffer&       midiOut) override;

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge()     const noexcept override { return "M"; }
        static constexpr const char* kMachineId = "lockstep.midiout.v1";

        [[nodiscard]] int       numParams()          const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;
        [[nodiscard]] int       numSections()        const override { return kNumSections; }
        [[nodiscard]] SectionInfo section(int index) const override;

        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            // MIDI-out routes the step's full chord through unchanged.
            return Polyphony::V0;
        }
        [[nodiscard]] bool isMidiOut()         const override { return true; }
        [[nodiscard]] bool hasInternalFilter() const override { return true; }
        [[nodiscard]] bool hasInternalAmp()    const override { return true; }

        static constexpr int kNumCCs = 16;

        // MF.2: stable device identifier used to reopen the correct device after
        // a session reload (device-list order may differ between runs).
        void setDestinationId(const std::string& id);
        [[nodiscard]] const std::string& destinationId() const { return destinationId_; }

        // Returns the device list refreshed at the last prepare() call.
        [[nodiscard]] const juce::Array<juce::MidiDeviceInfo>& availableDevices() const
            { return devices_; }

        // Per-track configurable CC numbers (MF.4). Default: cc[i] → MIDI CC i.
        void setCCNumber(int ccSlot, int ccNumber);
        [[nodiscard]] int ccNumber(int ccSlot) const;

        // Per-track configurable CC labels (MF.4). Empty = show name from table or "CC<N>".
        void setCCLabel(int ccSlot, const juce::String& label);
        [[nodiscard]] juce::String ccLabel(int ccSlot) const;

        // MF.4: destination-specific CC name table. Maps CC number → friendly name string.
        // Used as a fallback label when no per-slot ccLabel is set.
        // Loaded from hardware-preset tables (MF.8); per-track override labels take priority.
        void setCCNameTable(std::unordered_map<int, juce::String> table);
        void clearCCNameTable();

        // MF.6: emit All-Notes-Off (CC 123) + Reset-All-Controllers (CC 121) on
        // the active channel. Called by the processor on transport stop to prevent
        // stuck notes on the downstream synth. Also clears the activeNote_ state.
        void allNotesOff(juce::MidiBuffer& midiOut);

    private:
        // Section 1 "SRC": destination, channel, program
        static constexpr int kSlotDest    = 0;
        static constexpr int kSlotChannel = 1;
        static constexpr int kSlotProgram = 2;
        // Section 2 (repurposed FLTR key → CC bank A): cc[0..7]
        static constexpr int kSlotCC0     = 3;
        // Section 3 (repurposed AMP key → CC bank B): cc[8..15]
        // cc[8] is at index 11, cc[15] at index 18.

        static constexpr int kNumSlots    = 3 + kNumCCs;   // 19
        static constexpr int kNumSections = 4;

        void openDevice(int destIdx);

        juce::Array<juce::MidiDeviceInfo>  devices_;
        std::unique_ptr<juce::MidiOutput>  midiOutput_;
        std::string                        destinationId_;
        int                                currentDestIdx_ = -1;

        std::array<int, kNumCCs>          ccNumbers_{};
        std::array<juce::String, kNumCCs> ccLabels_{};
        std::array<int, kNumCCs>          prevCC_{};   // change-detection cache

        // MF.4: destination-specific CC name table; keyed by CC number.
        std::unordered_map<int, juce::String> nameTable_;

        // MF.3: active voice tracking for clean channel-change note-offs.
        int activeNote_    = -1;  // -1 = no note sounding
        int activeChannel_ = 1;

        // MF.3: program change change-detection. -2 = never sent (forces first-block emit).
        int prevProgram_ = -2;
    };
}
