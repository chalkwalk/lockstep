// TestEditorStub.cpp -- headless createEditor() for the lockstep_tests binary.
// LockstepProcessor::createEditor() is defined here (returning nullptr) so the
// test binary can link lockstep_engine without pulling in PluginEditor.h or
// any UI dependencies. ODR-safe: exactly one definition per binary.

#include "../src/PluginProcessor.h"

namespace lockstep
{
    juce::AudioProcessorEditor* LockstepProcessor::createEditor()
    {
        return nullptr;
    }
}
