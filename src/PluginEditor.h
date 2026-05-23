#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <utility>
#include <vector>

#include "PluginProcessor.h"
#include "io/Clipboard.h"
#include "io/ControllerEvent.h"
#include "io/EditMode.h"
#include "io/QwertyOverlay.h"
#include "state/UiState.h"
#include "ui/GridDisplayMode.h"
#include "ui/InPluginTransport.h"
#include "ui/KeyboardArea.h"
#include "ui/ManipulationZone.h"
#include "ui/SamplePoolOverlay.h"
#include "ui/MachineSelectOverlay.h"
#include "ui/SoundBankOverlay.h"
#include "ui/StandaloneTempoBar.h"

namespace lockstep
{
    class LockstepEditor : public juce::AudioProcessorEditor,
                           public juce::KeyListener,
                           public juce::AudioProcessorValueTreeState::Listener,
                           public juce::FileDragAndDropTarget,
                           public juce::Timer
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

        // juce::Timer — drives the diagnostic VU meters / activity blinks.
        void timerCallback() override;

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

        Clipboard clipboard_;

        // MD.7/MD.8: deferred pattern mute track indices — collected while Func
        // is held inside mute mode; applied atomically on Func release.
        std::vector<int> deferredPatternMutes_;
        int              heldSectionRawCode_ = -1;
        juce::Component* keyListenerTarget_ = nullptr;

        InPluginTransport transport_;
        std::unique_ptr<StandaloneTempoBar> tempoBar_;
        int trackPage_ = 0;  // 0 = tracks 1-8 visible, 1 = tracks 9-16 visible
        juce::TextButton trackPageBtn_{ "1-8" };
        std::array<juce::TextButton,   kNumTracks> trackBtns_;
        std::array<juce::ToggleButton, kNumTracks> muteBtns_;
        std::array<juce::ToggleButton, kNumTracks> soloBtns_;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
                   kNumTracks> muteAttachments_;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
                   kNumTracks> soloAttachments_;
        KeyboardArea keyboardArea_;
        ManipulationZone manipulationZone_;  // after keyboardArea_ — ctor takes KeyboardArea&
        SamplePoolOverlay poolOverlay_;      // after processor_ — ctor takes LockstepProcessor&

        GridDisplayMode gridMode_ = GridDisplayMode::Ortholinear;
        juce::ApplicationProperties appProps_;

        // Diagnostic metering state — UI-thread copies with ballistic decay,
        // updated each timerCallback() from the processor's atomic meters.
        std::array<float, kNumTracks> trackMeter_{};
        std::array<float, kNumTracks> trigBlink_{};
        std::array<float, kNumTracks> midiBlink_{};
        float masterMeter_ = 0.0f;
        void paintMeters(juce::Graphics& g);

        void applyDisplayMode(GridDisplayMode mode);
        juce::ComboBox syncModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncModeAttachment_;
        juce::ComboBox channelModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment_;
        juce::TextButton displayModeBtn_{ "ORL" };
        juce::TextButton poolBtn_{ "Pool..." };
        juce::TextButton soundBankBtn_{ "SND..." };
        SoundBankOverlay soundBankOverlay_;
        juce::TextButton machineSelectBtn_{ "MACH" };
        MachineSelectOverlay machineSelectOverlay_;
        bool isDraggingFiles_ = false;

        // MHX.5: vertical crossfader to the right of the encoder band (Scene A top / B bottom).
        juce::Slider crossfader_;

        void updateTransportGhosting();

        // Verb dispatch: called from the EditMode onVerbDispatched callback with the
        // resolved primary scope and the pressed verb key.
        void dispatchVerb(EditMode::PrimaryScope scope, ControllerButton verb);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepEditor)
    };
}
