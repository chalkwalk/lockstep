#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "PluginProcessor.h"
#include "controller/IControllerSurface.h"
#include "controller/XTouchMiniSurface.h"
#include "controller/Push1Surface.h"
#include "io/Clipboard.h"
#include "io/ControllerEvent.h"
#include "io/ControllerPortManager.h"
#include "io/DoubleTapDetector.h"
#include "io/EditMode.h"
#include "io/PressTracker.h"
#include "io/QwertyOverlay.h"
#include "state/UiState.h"
#include "ui/GridDisplayMode.h"
#include "ui/InPluginTransport.h"
#include "ui/KeyboardArea.h"
#include "ui/ManipulationZone.h"
#include "ui/SamplePoolOverlay.h"
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
        void focusLost(FocusChangeType cause) override;

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
        // CHROMATIC play-in: the live note a held step-pad is sounding, per pad
        // index (-1 = none), plus the track it was played on. Lets a pad-release
        // send the exact note-off (gate) even if the octave/track changed while
        // held, and lets chords ring independently (poly).
        std::array<int, 16> chromaticHeldNote_  { -1,-1,-1,-1,-1,-1,-1,-1,
                                                  -1,-1,-1,-1,-1,-1,-1,-1 };
        std::array<int, 16> chromaticHeldTrack_ {};
        // Key codes currently held down — used to suppress OS key-repeat in keyPressed().
        std::set<int> heldKeys_;

        // Double-press detection for Play: two presses within threshold = stop+reset.
        double lastPlayPressTime_            = 0.0;
        bool   playKeyHeld_                  = false;
        static constexpr double kDoublePressMsThreshold = 350.0;

        // Restore hold detection: tap (< kHoldRestoreMs) = pop one; hold = jump to floor.
        bool   restoreActive_    = false;
        double restoreKeyDownMs_ = 0.0;
        static constexpr double kHoldRestoreMs = 350.0;

        // MHZ.9.2: unified double-tap detector (modifiers + steps).
        DoubleTapDetector doubleTap_;

        // MHZ.9.5: track the last step trig-toggle so latch double-tap can revert it.
        // Set on key-up trig toggle; cleared on next dispatchDown step press.
        int  lastTrigToggleStep_    = -1;
        int  lastTrigToggleTrack_   = -1;
        bool lastTrigToggleApplied_ = false;  // true iff the key-up actually toggled (paramWrote was false)

        // Yes-held flag: true while VerbNo (P key = "Yes/confirm") is pressed without Func.
        // Used by Mute+P+step = solo gesture (additive toggle).
        bool yesHeld_ = false;

        // Pending-confirm state: set by VerbDelete (Func+O); resolved by VerbNo (P=Yes) or
        // Func+P (No/cancel). While set, a status-band prompt is shown.
        enum class PendingConfirm : uint8_t { None, Delete };
        PendingConfirm pendingConfirm_ = PendingConfirm::None;

        // Last-known transport state: lets timerCallback detect play/pause
        // transitions so the keyboard PLAY/PAUSE label updates promptly.
        bool lastPlayingState_ = false;

        // MHZ.9.1: physical-only held state for each latchable modifier.
        // xxxHeld in UiState = physHeld_.xxx OR uiState_.latch.xxx (effective).
        struct ModPhysHeld
        {
            bool phrase  = false;
            bool morph   = false;
            bool mute    = false;
            bool track   = false;
            bool scene   = false;
            bool song    = false;
            bool fill    = false;
            bool cue     = false;
        } physHeld_;

        // Tap tempo: rolling window of up to 5 tap timestamps (ms, high-res).
        // Requires ≥2 taps; ignores taps older than 3 s relative to the latest tap.
        static constexpr int    kTapMaxCount    = 5;
        static constexpr double kTapWindowMs    = 3000.0;
        static constexpr double kTapMinBpm      = 20.0;
        static constexpr double kTapMaxBpm      = 300.0;
        std::array<double, kTapMaxCount> tapTimes_{};
        int tapCount_ = 0;

        Clipboard clipboard_;

        // Per-track fill latch: captured on the transition into Func+Fill held,
        // cleared when either modifier releases. While Fill alone is held, this
        // stays -1 and fill activates on every track.
        int fillLockedTrack_ = -1;
        void updateFillActivation();

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

        // Transparent layer that draws the empty-track grey-out hints. Declared
        // before poolOverlay_ / soundBankOverlay_ so addAndMakeVisible inserts it
        // below them in JUCE's z-order, letting the popups always paint on top.
        struct GreyoutLayer : juce::Component
        {
            std::function<void(juce::Graphics&)> onPaint;
            GreyoutLayer() { setInterceptsMouseClicks(false, false); }
            void paint(juce::Graphics& g) override { if (onPaint) onPaint(g); }
        };
        GreyoutLayer greyoutLayer_;

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

        // Transient status line — shows CPC operation result for ~1.5s.
        juce::String statusMessage_;
        juce::uint32 statusSetMs_ = 0;
        static constexpr juce::uint32 kStatusDurationMs = 1500;
        void setStatus(const juce::String& msg);
        void paintStatus(juce::Graphics& g, juce::Rectangle<int> area);

        void applyDisplayMode(GridDisplayMode mode);

        // Unified input dispatch — both keyPressed and mouse callbacks route here.
        // rawCode = physical key code (keyboard), 0 (mouse), or kControllerSource.
        bool dispatchDown(ControllerEvent ev, int rawCode = 0);
        void dispatchUp  (ControllerEvent ev, int rawCode = 0);
        void handleTapTempo();

        PressTracker pressTracker_;

        juce::ComboBox syncModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncModeAttachment_;
        juce::ComboBox channelModeBox_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment_;
        juce::TextButton displayModeBtn_{ "ORL" };
        juce::TextButton poolBtn_{ "Pool..." };
        juce::TextButton soundBankBtn_{ "SND..." };
        SoundBankOverlay soundBankOverlay_;
        bool isDraggingFiles_ = false;

        // MHX.5: vertical crossfader to the right of the encoder band (Scene A top / B bottom).
        juce::Slider crossfader_;

        // Controller surface integration (Phase 6.6 / DESIGN §35).
        ControllerPortManager              controllerPorts_    { "X-TOUCH MINI" };
        std::unique_ptr<XTouchMiniSurface> xTouchSurface_;
        ControllerPortManager              push1Ports_         { "Ableton Push User Port",
                                                                  "Ableton Push MIDI 2" };
        std::unique_ptr<Push1Surface>      push1Surface_;
        ControllerEventSink buildControllerSink();

        void updateTransportGhosting();

        // Verb dispatch: called from the EditMode onVerbDispatched callback with the
        // resolved primary scope and the pressed verb key.
        void dispatchVerb(EditMode::PrimaryScope scope, ControllerButton verb);

        // MHZ.9.4: release all modifier latches and latched steps in one gesture.
        // Only called when latch.any() || ctx.hasAnyLatchedStep().
        void escapeAllLatches();

        // MHZ.9.3: toggle one modifier's latch (set=true to engage, false to release).
        // When engaging, enforces column exclusivity (releases any other latch in the same column).
        void setModifierLatch(ControllerButton cb, bool set);

        // MHZ.9.x: called from each latchable modifier's dispatchDown case.
        // Single tap on an already-latched modifier unlatches it; double-tap toggles latch.
        void handleModifierTap(ControllerButton cb, bool currentlyLatched);

        // MHZ.9.x: auto-release a transient mode's latch after its terminal action.
        // No-op when not latched; leaves physically-held (non-latched) mods alone.
        void releaseTransientLatch(ControllerButton cb);

        // Map the currently-held primary scope to a CheckpointScope + focused track.
        // Used by snapshot / restore call sites to route to the right stack.
        CheckpointScope ckScope(int& outTrack) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepEditor)
    };
}
