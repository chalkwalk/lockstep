// PluginEditorFactory.cpp -- the plugin target's UI registration and entry point.
//
// This file is what makes the processor have an editor at all. The engine
// (lockstep_engine) is deliberately UI-free and asks a registered factory for
// one; this registers it.
//
// WHY THE REGISTRATION LIVES IN createPluginFilter(). A factory registered by
// a static initialiser in some other translation unit would not help: that
// unit is an archive member, and an archive member is only linked if something
// already references it, which is the circular problem this replaced. The JUCE
// plugin wrappers call createPluginFilter(), so this unit is always linked and
// always runs.
//
// It used to define LockstepProcessor::createEditor() out of line instead,
// which left lockstep_engine's vtable referencing a symbol in an archive the
// linker had already passed. See 6.8 in ROADMAP.md.

#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    lockstep::setEditorFactory([](lockstep::LockstepProcessor& p)
                                   -> juce::AudioProcessorEditor*
                               { return new lockstep::LockstepEditor(p); });

    return new lockstep::LockstepProcessor();
}
