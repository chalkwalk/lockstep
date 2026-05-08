#include "PluginState.h"

namespace lockstep::PluginState
{
    void writeTo(juce::MemoryBlock& dest, juce::AudioProcessorValueTreeState& apvts)
    {
        if (auto xml = apvts.copyState().createXml())
            juce::AudioProcessor::copyXmlToBinary(*xml, dest);
    }

    void readFrom(const void* data, int sizeInBytes, juce::AudioProcessorValueTreeState& apvts)
    {
        if (auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}
