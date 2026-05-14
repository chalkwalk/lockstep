#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

#include "PluginProcessor.h"
#include "io/QwertyOverlay.h"
#include "state/UiState.h"
#include "ui/FunctionBar.h"
#include "ui/GridDisplayMode.h"
#include "ui/InPluginTransport.h"
#include "ui/ManipulationZone.h"
#include "ui/SectionBar.h"
#include "ui/StandaloneTempoBar.h"
#include "ui/StepGrid.h"

namespace lockstep
{
    class LockstepEditor : public juce::AudioProcessorEditor,
                           public juce::KeyListener,
                           public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        explicit LockstepEditor(LockstepProcessor& processor);
        ~LockstepEditor() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void parentHierarchyChanged() override;

        // juce::KeyListener — registered on the top-level window so focus
        // changes among child components cannot break key-up routing.
        bool keyPressed(const juce::KeyPress& key, juce::Component* originator) override;
        bool keyStateChanged(bool isKeyDown, juce::Component* originator) override;
        using juce::Component::keyPressed;
        using juce::Component::keyStateChanged;

        // juce::AudioProcessorValueTreeState::Listener
        void parameterChanged(const juce::String& paramID, float newValue) override;

    private:
        LockstepProcessor& processor_;
        QwertyOverlay qwerty_;
        UiState uiState_;
        int heldStepKey_ = -1;
        juce::Component* keyListenerTarget_ = nullptr;

        juce::MidiKeyboardState keyboardState_;
        juce::MidiKeyboardComponent keyboard_{ keyboardState_, juce::MidiKeyboardComponent::horizontalKeyboard };
        InPluginTransport transport_;
        std::unique_ptr<StandaloneTempoBar> tempoBar_;
        StepGrid stepGrid_;
        ManipulationZone manipulationZone_;  // after stepGrid_ — ctor takes StepGrid&
        SectionBar sectionBar_;              // after manipulationZone_ and stepGrid_
        FunctionBar functionBar_;            // Q-row key display

        GridDisplayMode gridMode_ = GridDisplayMode::Ortholinear;
        juce::ApplicationProperties appProps_;

        void applyDisplayMode(GridDisplayMode mode);
        juce::ComboBox syncModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncModeAttachment_;
        juce::ComboBox channelModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment_;
        juce::TextButton displayModeBtn_{ "ORL" };  // cycles Staggered/Ortholinear/Clean
        juce::TextButton loadButton_{ "Load Sample" };
        std::unique_ptr<juce::FileChooser> fileChooser_;
        juce::String sampleStatus_{ "No samples loaded" };

        void updateTransportGhosting();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepEditor)
    };
}
