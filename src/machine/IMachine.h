#pragma once

#include <array>
#include <string>
#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    inline constexpr int kNumPages = 12;
    inline constexpr int kParamsPerPage = 4;
    inline constexpr int kNumParamSlots = kNumPages * kParamsPerPage; // 48

    using ParamFrame = std::array<float, kNumParamSlots>;

    struct ParamMetadata
    {
        std::string label;     // shown in the Manipulation Zone
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        bool isStepped = false;
    };

    // Describes one logical grouping of consecutive pages within the 48-slot frame.
    // Machines declare their section taxonomy; the UI renders it generically.
    struct SectionInfo
    {
        std::string primaryLabel;   // top text on the section button
        std::string secondaryLabel; // bottom text (master-side function, may be empty)
        int pageCount = 1;          // number of 4-slot pages in this section (≥1)
        int firstSlot = 0;          // slot index of (page 0, slot 0) in the ParamFrame
    };

    class IMachine
    {
    public:
        virtual ~IMachine() = default;

        virtual void prepare(double sampleRate, int maxBlockSize) = 0;
        virtual void reset() = 0;

        // The sequencer resolves Override-ELSE-Base into a single ParamFrame
        // per block and hands it across the boundary. The machine writes
        // additively into `buffer`.
        // triggerAtSample: sample offset within buffer where a new step trig fires,
        // or -1 if no trig this block.
        virtual void process(int triggerAtSample, const ParamFrame& params,
                             juce::AudioBuffer<float>& buffer) = 0;

        virtual ParamMetadata getParamMetadata(int slot) const = 0;

        // Section taxonomy: machines declare how their 48 slots are grouped.
        // numTrackSections() must equal kNumSections (6). Sum of
        // trackSection(i).pageCount * kParamsPerPage must equal kNumParamSlots.
        static constexpr int kNumSections = 6;
        virtual int         numTrackSections() const { return 0; }
        virtual SectionInfo trackSection(int /*index*/) const { return {}; }

        // Optional: master sections (shift side). Default: none.
        virtual int         numMasterSections() const { return 0; }
        virtual SectionInfo masterSection(int /*index*/) const { return {}; }
    };
}
