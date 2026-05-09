#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/ManipulationZone.h"
#include "ui/PageBar.h"
#include "ui/StepGrid.h"

namespace lockstep
{
    class LockstepEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit LockstepEditor(LockstepProcessor& processor);
        ~LockstepEditor() override;

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        LockstepProcessor& processor_;
        juce::MidiKeyboardState keyboardState_;
        juce::MidiKeyboardComponent keyboard_{ keyboardState_, juce::MidiKeyboardComponent::horizontalKeyboard };
        PageBar pageBar_;
        ManipulationZone manipulationZone_;
        StepGrid stepGrid_;  // initialized in ctor init-list with processor_
        juce::TextButton loadButton_{ "Load Sample" };
        std::unique_ptr<juce::FileChooser> fileChooser_;
        juce::String sampleStatus_{ "No samples loaded" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepEditor)
    };
}
