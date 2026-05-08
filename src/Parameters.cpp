#include "Parameters.h"
#include "ParameterIDs.h"

namespace lockstep
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ ParamIDs::outputGain, 1 },
            "Output Gain",
            juce::NormalisableRange<float>(-60.0f, 6.0f, 0.01f),
            0.0f));

        return layout;
    }
}
