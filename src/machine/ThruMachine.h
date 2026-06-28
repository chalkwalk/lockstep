#pragma once

#include "IMachine.h"
#include "InputSource.h"
#include "../core/Sequence.h"  // kNumTracks
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
    // Subsumes the Octatrack Thru + Neighbour split: External = classic Thru,
    // Track N = neighbour-style. It is the only stock machine declaring
    // input_source for now; Recorder / Looper follow at 6.2 / 6.3.
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
            // None, External, Master, then Track 1..kNumTracks.
            s.maxValue = static_cast<float>(2 + kNumTracks);
            s.defaultValue = 1.0f;  // External — a fresh Thru hears the input bus
            s.isStepped = true;
            s.sectionIndex = kSrcSecIdx;
            s.valueLabels = std::span<const char* const>(kSourceLabels.data(),
                                                         kSourceLabels.size());
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

    private:
        // Index ↔ value: 0=None, 1=Ext, 2=Master, 3+N = Track N (matches
        // decodeInputSource()). Keep in sync with kNumTracks.
        static constexpr std::array<const char* const, 3 + kNumTracks> kSourceLabels = {
            "None", "Ext", "Master",
            "Trk1",  "Trk2",  "Trk3",  "Trk4",  "Trk5",  "Trk6",  "Trk7",  "Trk8",
            "Trk9",  "Trk10", "Trk11", "Trk12", "Trk13", "Trk14", "Trk15", "Trk16"
        };
        static_assert(kNumTracks == 16, "kSourceLabels lists 16 tracks explicitly");
    };
}
