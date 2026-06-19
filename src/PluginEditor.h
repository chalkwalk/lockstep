#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "PluginProcessor.h"
#include "command/ButtonLayers.h"
#include "command/CommandContext.h"
#include "command/CommandCore.h"
#include "command/CommandEffects.h"
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
#include "ui/StandaloneFileBar.h"
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
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

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
        EditMode editMode_;
        UiState uiState_;
        // (rawKeyCode, absStepIndex) pairs, ordered by press time.
        std::vector<std::pair<int, int>> heldStepKeys_;
        // CHROMATIC play-in: the live note a held step-pad is sounding, per pad
        // index (-1 = none), plus the track it was played on. Lets a pad-release
        // send the exact note-off (gate) even if the octave/track changed while
        // held, and lets chords ring independently (poly).
        std::array<int, 16> chromaticHeldNote_{ -1, -1, -1, -1, -1, -1, -1, -1,
                                                -1, -1, -1, -1, -1, -1, -1, -1 };
        std::array<int, 16> chromaticHeldTrack_{};
        // Key codes currently held down — used to suppress OS key-repeat in keyPressed().
        std::set<int> heldKeys_;

        // Double-press detection for Play: two presses within threshold = stop+reset.
        double lastPlayPressTime_ = 0.0;
        bool playKeyHeld_ = false;
        static constexpr double kDoublePressMsThreshold = 350.0;

        // Restore hold detection: tap (< kHoldRestoreMs) = pop one; hold = jump to floor.
        bool restoreActive_ = false;
        double restoreKeyDownMs_ = 0.0;
        static constexpr double kHoldRestoreMs = 350.0;

        // MHZ.9.2: unified double-tap detector (modifiers + steps).
        DoubleTapDetector doubleTap_;

        // MHZ.9.5: track the last step trig-toggle so latch double-tap can revert it.
        // Set on key-up trig toggle; cleared on next dispatchDown step press.
        int lastTrigToggleStep_ = -1;
        int lastTrigToggleTrack_ = -1;
        bool lastTrigToggleApplied_ = false;  // true iff the key-up actually toggled (paramWrote was false)

        // Pending-confirm state lives in uiState_.confirm (ConfirmKind + target).

        // Last-known transport state: lets timerCallback detect play/pause
        // transitions so the keyboard PLAY/PAUSE label updates promptly.
        bool lastPlayingState_ = false;

        // MHZ.9.1: physical-only held state for each latchable modifier.
        // xxxHeld in UiState = physHeld_.xxx OR uiState_.latch.xxx (effective).
        struct ModPhysHeld
        {
            bool phrase = false;
            bool morph = false;
            bool mute = false;
            bool track = false;
            bool scene = false;
            bool song = false;
            bool fill = false;
            bool cue = false;
        } physHeld_;

        // Tap tempo: rolling window of up to 5 tap timestamps (ms, high-res).
        // Requires ≥2 taps; ignores taps older than 3 s relative to the latest tap.
        static constexpr int kTapMaxCount = 5;
        static constexpr double kTapWindowMs = 3000.0;
        static constexpr double kTapMinBpm = 20.0;
        static constexpr double kTapMaxBpm = 300.0;
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
        int heldSectionRawCode_ = -1;
        int heldSectionIndex_ = -1;  // section index (0-5) while key held; -1 = none
        // 6.5 Animate bypass: track/slot bypassed by FX-held + step; restored on step-up.
        int animateBypassTrack_ = -1;
        int animateBypassSlot_ = -1;
        // 8.26 Animate: master unit (0=FX1, 1=FX2, 2=SndA, 3=SndB); -1=none.
        int animateBypassMasterUnit_ = -1;

        // Item 7 — meter drag: vertical drag on a track button adjusts its AMP level
        // (left-click drag) or sendA (right-click drag).
        struct MeterDrag
        {
            int track = -1;
            int paramSlot = -1;
            float startValue = 0.0f;
            float paramMax = 1.0f;
            int startY = 0;
        } meterDrag_;
        juce::Component* keyListenerTarget_ = nullptr;

        InPluginTransport transport_;
        std::unique_ptr<StandaloneTempoBar> tempoBar_;
        std::unique_ptr<StandaloneFileBar> fileBar_;
        int trackPage_ = 0;  // 0 = tracks 1-8 visible, 1 = tracks 9-16 visible
        juce::TextButton trackPageBtn_{ "1-8" };
        std::array<juce::TextButton, kNumTracks> trackBtns_;
        std::array<juce::ToggleButton, kNumTracks> muteBtns_;
        std::array<juce::ToggleButton, kNumTracks> soloBtns_;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
                   kNumTracks>
            muteAttachments_;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
                   kNumTracks>
            soloAttachments_;
        KeyboardArea keyboardArea_;
        ManipulationZone manipulationZone_;  // after keyboardArea_ — ctor takes KeyboardArea&

        // Drive ManipulationZone from resolveMetaBand(uiState_) + swingScopeFor(uiState_).
        // Called on any state change that may affect the band (scope press/release,
        // meta-section change, track change).
        void refreshMetaBand();

        // Returns true when the active track is a stub/empty track — content edits
        // are blocked and only machine-pick is allowed.
        [[nodiscard]] bool activeTrackContentLocked() const;

        // Thin alias for call sites that were wired before refreshMetaBand existed.
        void updateSwingQualifier();

        // Clear swingDismissed and refresh the swing qualifier in one call.
        // Use instead of the scattered `swingDismissed=false; updateSwingQualifier();` pairs.
        void clearSwingDismissed();

        // 5.5: write the current Euclidean pattern to the focused track's active phrase.
        // Takes a checkpoint first if the phrase has any existing trigs (use for final commit).
        void applyEuclidToTrack(int track);
        // Apply euclid pattern to live phrase steps without snapshotting (live preview).
        void applyEuclidLive(int track);

        // 5.5: stash of phrase steps captured at euclid-arm time; restored on No/escape.
        std::array<Step, kMaxStepsPerTrack> euclidStash_{};
        int euclidStashLen_ = 0;
        int euclidTrack_ = -1;

        // Song+FX unit-cycle helpers. Return a unit index 0-3 (0-1=inserts, 2-3=sends).
        // Skip empty units; fall back to 0 if none loaded.
        int firstLoadedMasterUnit() const noexcept;
        int nextLoadedMasterUnit(int current) const noexcept;

        // Transparent layer that draws the empty-track grey-out hints. Declared
        // before poolOverlay_ / soundBankOverlay_ so addAndMakeVisible inserts it
        // below them in JUCE's z-order, letting the popups always paint on top.
        struct GreyoutLayer : juce::Component
        {
            std::function<void(juce::Graphics&)> onPaint;
            GreyoutLayer() { setInterceptsMouseClicks(false, false); }
            void paint(juce::Graphics& g) override
            {
                if (onPaint) onPaint(g);
            }
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
        float masterMeter_  = 0.0f;
        float masterMeterR_ = 0.0f;
        void paintMeters(juce::Graphics& g);

        // Last-seen morphFader value: used to detect on-screen fader moves and
        // mark the surface model dirty so controller surfaces update.
        float lastMorphFader_ = -1.0f;

        // Transient status line — shows CPC operation result for ~1.5s.
        juce::String statusMessage_;
        juce::uint32 statusSetMs_ = 0;
        static constexpr juce::uint32 kStatusDurationMs = 1500;
        void setStatus(const juce::String& msg);
        void paintStatus(juce::Graphics& g, juce::Rectangle<int> area);

        // Capture the current live scene (effective floor + track phrases) into
        // clipboard_.scene. Sets clipboard_.type = Scene.
        void captureScene();

        // True when writing phrase content into phraseSlot would overwrite shared
        // or already-initialised content. Shows confirm when true.
        bool phraseConflictAndConfirm(int phraseSlot, ConfirmKind kind);

        void applyDisplayMode(GridDisplayMode mode);

        // Unified input dispatch — both keyPressed and mouse callbacks route here.
        // rawCode = physical key code (keyboard), 0 (mouse), or kControllerSource.
        bool dispatchDown(ControllerEvent ev, int rawCode = 0);
        void dispatchUp(ControllerEvent ev, int rawCode = 0);

        // Build the effective modifier state (physical OR latched) for resolveLayer().
        [[nodiscard]] LayerContext layerContext() const noexcept;

        // Command core seam (Phase 8.4).
        CommandContext commandContext();
        // EditorEffects adapts CommandEffects for the live editor; defined early
        // in PluginEditor.cpp so it can access private members via nested-class
        // friendship. Stored as the base-class pointer to avoid unique_ptr<T>
        // requiring a complete type at the declaration site.
        struct EditorEffects;
        std::unique_ptr<CommandEffects> editorEffects_;
        CommandCore commandCore_;

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

        // 5.2 Morph step view: dormant pole values (in-memory, not serialized).
        // Keyed by (track, slot). Cleared whenever morphHeld goes false.
        // A dormant pole is functionally absent (blend mirrors the other side) but
        // can be re-activated by tapping its step cell again.
        std::map<std::pair<int, int>, float> morphDormantA_, morphDormantB_;
        MorphViewState buildMorphViewState() const;

        // Controller surface integration (Phase 6.6 / DESIGN §35).
        ControllerPortManager controllerPorts_{ "X-TOUCH MINI" };
        std::unique_ptr<XTouchMiniSurface> xTouchSurface_;
        ControllerPortManager push1Ports_{ "Ableton Push User Port",
                                           "Ableton Push MIDI 2" };
        std::unique_ptr<Push1Surface> push1Surface_;
        ControllerEventSink buildControllerSink();

        void updateTransportGhosting();

        // MHZ.9.4: release all modifier latches and latched steps in one gesture.
        // Only called when latch.any() || ctx.hasAnyLatchedStep().
        void escapeAllLatches();

        // §39: single source for "leave density-sticky mode". Invariant: density-sticky
        // and any foreign cluster scope (Track/Phrase/Scene/Morph/Mute/Fill) are
        // mutually exclusive — enforced at dispatchDown and the toggle-on entry edge.
        void escapeDensitySticky();

        // §39: returns true and performs the density-sticky action if the button is one
        // of density's own keys (Nav→bank flip; FX section index 5→sub-page toggle).
        // Call at the top of each nav/section chokepoint to replace the inline copies.
        bool consumeDensityStickyKey(ControllerButton btn, int index = -1);

        // Velocity overlay sticky mode — parallel to density sticky.
        // Entered via double-tap AMP section key (index 3); AMP re-press cycles sub-pages.
        void escapeVelSticky();
        bool velAnyEnabled() const;
        UiState::VelSubPage nextVelSubPage(UiState::VelSubPage current) const;
        bool consumeVelStickyKey(ControllerButton btn, int index = -1);

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
