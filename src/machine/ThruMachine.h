#pragma once

#include "IMachine.h"
#include "InputSource.h"
#include <array>

namespace lockstep
{
    // ThruMachine — routes audio from an input_source into the track's signal
    // path (DESIGN §27 / §29). It synthesises nothing: the sequencer fills the
    // track buffer from the chosen source before process(), so process() is a
    // pure pass-through. The universal FILTER / AMP / FX chain colours it, and
    // the track ENVELOPE gate defaults to held-open, so a Thru track passes
    // audio continuously (the basis of continuous Thru and drones, §14).
    //
    // Subsumes the Octatrack Thru + Neighbour split: External = classic Thru;
    // neighbour-style inter-track routing is now output-directed (route other
    // tracks' CHANNEL "Out" here — this track reads their sum, DESIGN §27), so
    // input_source is just the outside-world tap {None, External, Master}. A
    // fresh Thru defaults to None: silent until you route audio in or pick a
    // source, which is the natural default for using it as a sub-bus. It is the
    // only stock machine declaring input_source for now; Recorder / Looper
    // follow at 6.2 / 6.3.
    class ThruMachine : public IMachine
    {
    public:
        static constexpr const char* kMachineId = "lockstep.thru.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "THRU"; }

        void prepare(double, int) override {}
        void reset() override {}

        // Audio is already in the buffer (filled from input_source upstream).
        void process(const juce::MidiBuffer&, const ParamFrame&,
                     juce::AudioBuffer<float>&) override {}

        [[nodiscard]] int numParams() const override { return 1; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            if (index != 0) return {};
            ParamSpec s;
            s.id = kInputSourceSlotId;
            s.label = "Source";
            s.minValue = 0.0f;
            // None / External / Master / Track N (tap-fork, DESIGN §27). A Track
            // value is a read-only post-chain tap of that track; cyclic/self picks
            // are refused at write time.
            s.maxValue = kInputSourceMaxValue;
            s.defaultValue = 0.0f;  // None — silent until routed/sourced (sub-bus default)
            s.isStepped = true;
            s.sectionIndex = kSrcSecIdx;
            s.valueLabels = std::span<const char* const>(kInputSourceLabels.data(),
                                                         kInputSourceLabels.size());
            return s;
        }

        [[nodiscard]] int numSections() const override { return 2; }

        [[nodiscard]] SectionInfo section(int index) const override
        {
            if (index == kSrcSecIdx) return { "SRC" };
            return {};
        }

        // Continuous router — no note voices; the sequencer governs nothing.
        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V0;
        }
    };
}
