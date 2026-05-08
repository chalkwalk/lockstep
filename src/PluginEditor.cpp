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
        addAndMakeVisible(loadButton_);

        loadButton_.onClick = [this]
        {
            fileChooser_ = std::make_unique<juce::FileChooser>(
                "Load Sample",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif;*.flac;*.ogg");

            fileChooser_->launchAsync(
                juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    const auto results = fc.getResults();
                    if (!results.isEmpty())
                        processor_.samplePool().load(results[0].getFullPathName());
                });
        };

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
        auto header = bounds.removeFromTop(36);
        header.removeFromLeft(200);                                    // title text
        loadButton_.setBounds(header.reduced(4));

        pageBar_.setBounds(bounds.removeFromTop(40).reduced(8, 4));
        keyboard_.setBounds(bounds.removeFromBottom(72).reduced(8, 4));
        stepGrid_.setBounds(bounds.removeFromBottom(120).reduced(8, 4));
        manipulationZone_.setBounds(bounds.reduced(8, 4));
    }
}
