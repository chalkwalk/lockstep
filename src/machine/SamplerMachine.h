#pragma once

#include "IMachine.h"

namespace lockstep
{
    class SamplerMachine : public IMachine
    {
    public:
        SamplerMachine();
        ~SamplerMachine() override;

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;
        void process(const ParamFrame& params, juce::AudioBuffer<float>& buffer) override;
        ParamMetadata getParamMetadata(int slot) const override;

    private:
        double sampleRate_ = 0.0;
        int maxBlockSize_ = 0;
    };
}
