#include "SamplerMachine.h"

namespace lockstep
{
    SamplerMachine::SamplerMachine() = default;
    SamplerMachine::~SamplerMachine() = default;

    void SamplerMachine::prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
    }

    void SamplerMachine::reset() {}

    void SamplerMachine::process(const ParamFrame& params, juce::AudioBuffer<float>& buffer)
    {
        juce::ignoreUnused(params, buffer);
        // M0: no samples loaded, nothing to write.
    }

    ParamMetadata SamplerMachine::getParamMetadata(int slot) const
    {
        juce::ignoreUnused(slot);
        return {};
    }
}
