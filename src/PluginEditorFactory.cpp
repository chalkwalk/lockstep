// PluginEditorFactory.cpp -- createEditor() definition for the plugin target.
// Kept separate from PluginProcessor.cpp so lockstep_engine (the headless
// testable lib) does not pull in PluginEditor.h and its UI dependencies.
// tests/TestEditorStub.cpp provides an alternative definition for the test binary.

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace lockstep
{
    juce::AudioProcessorEditor* LockstepProcessor::createEditor()
    {
        return new LockstepEditor(*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lockstep::LockstepProcessor();
}
