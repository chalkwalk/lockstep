#include "PluginEditor.h"

namespace lockstep
{
    LockstepEditor::LockstepEditor(LockstepProcessor& proc)
        : juce::AudioProcessorEditor(&proc),
          processor_(proc)
    {
        addAndMakeVisible(pageBar_);
        addAndMakeVisible(manipulationZone_);
        addAndMakeVisible(stepGrid_);
        addAndMakeVisible(keyboard_);
        setSize(720, 420);
    }

    LockstepEditor::~LockstepEditor() = default;

    void LockstepEditor::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(20, 22, 26));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(18.0f)));
        g.drawText("Lockstep - Skeleton",
                   getLocalBounds().removeFromTop(36).reduced(12, 0),
                   juce::Justification::centredLeft);
        juce::ignoreUnused(processor_);
    }

    void LockstepEditor::resized()
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(36);                                  // header
        pageBar_.setBounds(bounds.removeFromTop(40).reduced(8, 4));
        keyboard_.setBounds(bounds.removeFromBottom(72).reduced(8, 4));
        stepGrid_.setBounds(bounds.removeFromBottom(120).reduced(8, 4));
        manipulationZone_.setBounds(bounds.reduced(8, 4));
    }
}
