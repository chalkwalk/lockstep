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

    class IMachine
    {
    public:
        virtual ~IMachine() = default;

        virtual void prepare(double sampleRate, int maxBlockSize) = 0;
        virtual void reset() = 0;

        // The sequencer resolves Override-ELSE-Base into a single ParamFrame
        // per block and hands it across the boundary. The machine writes
        // additively into `buffer`.
        virtual void process(const ParamFrame& params, juce::AudioBuffer<float>& buffer) = 0;

        virtual ParamMetadata getParamMetadata(int slot) const = 0;
    };
}
