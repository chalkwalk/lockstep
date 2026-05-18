#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <utility>
#include <vector>

#include "PluginProcessor.h"
#include "io/ClipboardType.h"
#include "io/ControllerEvent.h"
#include "io/EditMode.h"
#include "io/QwertyOverlay.h"
#include "state/UiState.h"
#include "ui/FunctionBar.h"
#include "ui/GridDisplayMode.h"
#include "ui/InPluginTransport.h"
#include "ui/ManipulationZone.h"
#include "ui/SamplePoolOverlay.h"
#include "ui/SectionBar.h"
#include "ui/StandaloneTempoBar.h"
#include "ui/StepGrid.h"

namespace lockstep
{
    class LockstepEditor : public juce::AudioProcessorEditor,
                           public juce::KeyListener,
                           public juce::AudioProcessorValueTreeState::Listener,
                           public juce::FileDragAndDropTarget
    {
    public:
        explicit LockstepEditor(LockstepProcessor& processor);
        ~LockstepEditor() override;

        void paint(juce::Graphics& g) override;
        void paintOverChildren(juce::Graphics& g) override;
        void resized() override;
        void parentHierarchyChanged() override;

        // juce::FileDragAndDropTarget
        bool isInterestedInFileDrag(const juce::StringArray& files) override;
        void fileDragEnter(const juce::StringArray& files, int x, int y) override;
        void fileDragExit(const juce::StringArray& files) override;
        void filesDropped(const juce::StringArray& files, int x, int y) override;

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
        EditMode      editMode_;
        UiState uiState_;
        // (rawKeyCode, absStepIndex) pairs, ordered by press time.
        std::vector<std::pair<int,int>> heldStepKeys_;

        // Double-press detection for PlayStop: two presses within threshold = StopReset.
        double lastPlayPressTime_            = 0.0;
        bool   playKeyHeld_                  = false;
        static constexpr double kDoublePressMsThreshold = 350.0;

        ClipboardType clipboardType_  = ClipboardType::None;
        int           checkpointDepth_ = 0;  // stub: real stack in MD

        // MD.7/MD.8: deferred pattern mute track indices — collected while Func
        // is held inside mute mode; applied atomically on Func release.
        std::vector<int> deferredPatternMutes_;
        juce::Component* keyListenerTarget_ = nullptr;

        juce::MidiKeyboardState keyboardState_;
        juce::MidiKeyboardComponent keyboard_{ keyboardState_, juce::MidiKeyboardComponent::horizontalKeyboard };
        InPluginTransport transport_;
        std::unique_ptr<StandaloneTempoBar> tempoBar_;
        std::array<juce::TextButton,   kNumTracks> trackBtns_;
        std::array<juce::ToggleButton, kNumTracks> muteBtns_;
        std::array<juce::ToggleButton, kNumTracks> soloBtns_;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
                   kNumTracks> muteAttachments_;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
                   kNumTracks> soloAttachments_;
        StepGrid stepGrid_;
        ManipulationZone manipulationZone_;  // after stepGrid_ — ctor takes StepGrid&
        SamplePoolOverlay poolOverlay_;      // after processor_ — ctor takes LockstepProcessor&
        SectionBar sectionBar_;              // after manipulationZone_ and stepGrid_
        FunctionBar functionBar_;            // Q-row key display

        GridDisplayMode gridMode_ = GridDisplayMode::Ortholinear;
        juce::ApplicationProperties appProps_;

        void applyDisplayMode(GridDisplayMode mode);
        juce::ComboBox syncModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncModeAttachment_;
        juce::ComboBox channelModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment_;
        juce::TextButton displayModeBtn_{ "ORL" };
        juce::TextButton poolBtn_{ "Pool..." };
        bool isDraggingFiles_ = false;

        void updateTransportGhosting();

        // Verb dispatch: called from the EditMode onVerbDispatched callback with the
        // resolved primary scope and the pressed verb key.
        void dispatchVerb(EditMode::PrimaryScope scope, ControllerButton verb);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepEditor)
    };
}
