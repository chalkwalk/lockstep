#pragma once

#include "IMachine.h"

namespace lockstep
{
    // Silent IMachine stand-in for unknown machine IDs loaded from disk.
    // Preserves the stored base params and trigs without producing audio.
    // Offered to the user as a relink/replace target via the UI.
    class StubMachine : public IMachine
    {
    public:
        static constexpr const char* kMachineId = "lockstep.stub";

        // unknownId is empty when the track was explicitly left empty by the user;
        // non-empty when an unknown machine ID was loaded from a saved session.
        explicit StubMachine(std::string unknownId = "")
            : unknownId_(std::move(unknownId)) {}

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge()     const noexcept override { return ""; }
        [[nodiscard]] const std::string& unknownMachineId() const { return unknownId_; }

        void prepare(double, int) override {}
        void reset() override {}
        void process(const juce::MidiBuffer&, const ParamFrame&,
                     juce::AudioBuffer<float>&) override {}

        [[nodiscard]] int       numParams()          const override { return 0; }
        [[nodiscard]] ParamSpec paramSpec(int)       const override { return {}; }

    private:
        std::string unknownId_;
    };
}
