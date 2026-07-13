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
#include "io/CaptureController.h"
#include "io/Clipboard.h"
#include "io/ControllerEvent.h"
#include "io/ControllerPortManager.h"
#include "ui/mode/FuncReskin.h"
#include "ui/mode/GestureRecognizer.h"
#include "io/EditMode.h"
#include "io/PressTracker.h"
#include "io/QwertyOverlay.h"
#include "state/UiState.h"
#include "ui/GridDisplayMode.h"
#include "ui/InPluginTransport.h"
#include "ui/InspectorBar.h"
#include "ui/TimelineStrip.h"
#include "ui/InspectorModel.h"
#include "ui/KeyboardArea.h"
#include "ui/ManipulationZone.h"
#include "ui/SamplePoolOverlay.h"
#include "ui/SoundBankOverlay.h"
#include "ui/StandaloneFileBar.h"
#include "ui/SurfaceDispatcher.h"

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

        // 9.12: the dispatch golden net drives dispatchDown/dispatchUp directly, to
        // pin today's behaviour before the table migration rewrites it. Granting one
        // named friend keeps the entry points private to everything else (no public
        // test-only method that production code could start calling by accident).
        friend struct DispatchProbe;

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

        // 5.5 Audition (Cue scope, DESIGN §21). Cue is entered as the Func+3
        // compound (no dedicated key — hardware parity); cueViaFunc_ tracks the
        // held '3' so its release exits the scope. Audition fires resolved notes
        // via liveNoteOn/Off and writes nothing to the pattern.
        bool cueViaFunc_ = false;
        int auditionBaseTrack_ = -1;   // focused-track base-trig monitor (Cue, no step)
        int auditionBaseNote_ = -1;
        struct StepAudition { int track = -1; int count = 0;
                              std::array<int, kMaxNotesPerStep> notes{}; };
        std::array<StepAudition, kMaxStepsPerTrack> stepAudition_{};
        void enterCueScope();
        void exitCueScope();
        void auditionBaseTrigDown();
        void auditionBaseTrigOff();
        void auditionStepDown(int stepIdx);
        void auditionStepOff(int stepIdx);
        void auditionAllOff();

        bool playKeyHeld_ = false;

        // Stage 2: single home for all input-timing state (double-tap + long-press).
        GestureRecognizer gesture_;

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

        // Generator hub (9.10): true while the 3-key is physically held.
        // If held ≥350 ms the hub picker opens; on release either closes the hub or
        // calls handleTapTempo() (short tap). Checked in timerCallback.
        bool tapTempoPhysHeld_ = false;
        double tapTempoArmMs_ = 0.0;

        // Refresh the whole visible surface (chrome + the step/section grid).
        // Routes through the single invalidation channel (PRINCIPLES §22 /
        // DESIGN §35.9): invalidate() marks the surface dirty and the
        // AsyncUpdater coalesces a message-loop cycle of calls into one frame.
        // The frame repaints the chrome *and* the grid child (which does not
        // always redraw on the editor's own repaint()); never pair repaint() +
        // keyboardArea_.repaint() by hand (PRINCIPLES §20).
        void refreshSurface() { surfaceDispatcher_.invalidate(); }

        // The one place a frame is produced (DESIGN §35.9.1): repaint chrome +
        // grid, then render every open controller from a single buildSurfaceModel().
        SurfaceDispatcher surfaceDispatcher_{ [this] { renderSurfaceFrame(); } };
        void renderSurfaceFrame();
        // Build the surface model once and push it to each open controller
        // (DESIGN §35.9.1). Same function the screen paint path calls, so they
        // cannot diverge. No-op when no controller is connected.
        void renderControllers();

        // Generator hub entry helpers (extracted from legacy Phrase+Fill / Func+MOD / Func+AMP).
        void enterEuclid(int track);
        // Exit Euclid mode without committing: revert the live preview to the
        // stashed pre-Euclid phrase and clear the armed state. No-op when inactive.
        void cancelEuclid();
        // Euclid modal state is four fields that must move together (PRINCIPLES §20):
        // uiState_.euclidHeld (+params), euclidTrack_, euclidStash_, euclidStashLen_.
        // restoreEuclidStash() reverts the live preview; forgetEuclidEditorState()
        // clears the editor-owned fields. resetEuclid() clears the UiState half.
        void restoreEuclidStash();
        void forgetEuclidEditorState() { euclidTrack_ = -1; euclidStashLen_ = 0; }
        void enterDensitySticky();
        void enterVelSticky();

        // 10.7 Melodic generator — same print-model lifecycle as Euclid: enter
        // stashes the phrase + shows a live preview; the MZ band re-rolls the
        // preview; P commits (snapshot + print), Func+P / escape reverts the stash.
        void enterMelodic(int track);
        void cancelMelodic();
        void applyMelodyLive(int track);
        void applyMelodyToTrack(int track);
        void restoreMelodyStash();
        void forgetMelodyEditorState() { melodicTrack_ = -1; melodyStashLen_ = 0; }

        // 10.8 Harmonic voice-mover — the sticky multi-voice twin of the melodic
        // print model: enter stashes the phrase + seeds a default progression and
        // shows a live preview; the MZ band scrubs voices / structure and re-prints;
        // P commits (snapshot + print), Func+P / escape reverts the stash.
        void enterHarmony(int track);
        void cancelHarmony();
        void applyHarmonyLive(int track);
        void applyHarmonyToTrack(int track);
        void restoreHarmonyStash();
        void forgetHarmonyEditorState();
        // 10.10: re-strike the cursor chord through the live-note engine on every
        // edit so the ear leads; stopHarmonyAudition releases the sounding voices.
        void auditionHarmonyCursorChord();
        void stopHarmonyAudition();

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
        bool fxSectionPickerWantsMaster_ = false; // captured at Section-5 key-down
        bool fxPickerFiredMidHold_ = false;       // picker opened during the hold (not on key-up)
        bool stepInspectorFiredMidHold_ = false;  // Part 2: StepInspector opened via long-press this hold
        bool fxPickerRemoveArmed_ = false;        // loaded-cell press deferred to key-up
        bool fxPickerRemoveMaster_ = false;       // which picker the armed press targets

        // 9.24 S16: which insert slot a pool "pick IR" gesture targets. master:
        // slot 0/1 = insert, 2/3 = send. track: track + slot (0/1).
        bool irPickTargetMaster_ = false;
        int irPickTargetTrack_ = 0;
        int irPickTargetSlot_ = 0;
        bool machineConsoleArmed_ = false;        // 7b: console-section press deferred to key-up

        // ── Performance capture (tape deck) ──────────────────────────────────
        // CaptureController is the pure state machine; the editor owns the IO
        // (arming the WAV writer, finalising, deleting files, revealing folders).
        CaptureController captureController_;
        bool   captureCellHeld_ = false;        // CAPTURE cell physically down
        bool   captureLongPressFired_ = false;  // long-press already serviced this hold
        // S4: looper console cell 0 (REC/DUB) — tap is the record verb (double-tap
        // = now), a long hold is momentary punch-replace. Deferred to key-up.
        int    looperReplaceTrack_ = -1;        // track whose cell 0 is held (-1 = none)
        bool   looperReplaceFired_ = false;     // punch-replace engaged this hold
        bool   looperReplaceWasDouble_ = false; // double-tap captured at key-down
        double tapeRecLastDownMs_ = 0.0;        // §40.13: last tape REC-cell down, for the retro gap
        bool   recordResetHeld_ = false;        // bare Record down: hold = transport reset
        bool   recordResetFired_ = false;       // reset already serviced this hold (timer path)
        double captureStartMs_ = 0.0;           // for the REC elapsed timer
        juce::File captureLastFile_;            // last/active take, for discard + reveal
        void runCaptureOut(const CaptureController::Out& out);  // execute a controller decision
        void paintCaptureStrip(juce::Graphics& g);
        void paintMasterMeter(juce::Graphics& g);   // dB VU meter (peak-hold + clip + labels)
        [[nodiscard]] juce::String captureFolderDisplayPath() const;  // ~-relative path for SAVED
        // 11.11 (S1): "master + N stems" / "master only" — what this take will keep.
        [[nodiscard]] static juce::String stemOutcomeText(int stemCount);
        // Banner transient-detail window: the filename/path fades in at arm /
        // record-start, then the banner is compact for the performance.
        CaptureController::Phase lastCapturePhase_ = CaptureController::Phase::Idle;
        double captureDetailUntilMs_ = 0.0;
        // dB-meter ballistics: peak high-water mark (hold then decay) + clip latch.
        float  masterPeakHoldL_ = 0.0f, masterPeakHoldR_ = 0.0f;
        double peakHoldMsL_ = 0.0,  peakHoldMsR_ = 0.0;
        bool   masterClipL_ = false, masterClipR_ = false;
        double clipMsL_ = 0.0, clipMsR_ = 0.0;
        // Chain the capture-exit dialog (if recording) ahead of `next` (the
        // dirty-project save guard). Armed-but-not-rolling disarms silently.
        void captureExitGuard(std::function<void()> next);
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
        juce::Label tempoReadout_;           // scope-coloured BPM + time-sig display
        std::unique_ptr<StandaloneFileBar> fileBar_;
        // 9.11: inspector bar — always-on 4-region context strip.
        InspectorBar inspectorBar_;
        TimelineStrip timelineStrip_;    // 11.5 tape timeline (§40.6); S8 permanent
        ControllerButton lastFocusedButton_ = ControllerButton::None;
        int lastFocusedIndex_ = -1;
        // 9.11: row bounds stored during resized() for dynamic re-use if needed.
        juce::Rectangle<int> inspectorRow_;
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

        // 9.14: open the FX-section picker (master or track) — shared by the
        // mid-hold timer path and the key-up long-hold fallback. Sets the durable
        // picker state and refreshes the surface so it appears while held.
        void openFxSectionPicker(bool master);
        // 7b/7c: OnDemand machine console open/close. openMachineConsole snapshots the
        // Route output-dest scratch; closeMachineConsole discards it (revert);
        // commitRouteConsole applies each staged dest then closes; cycleRouteConsoleCell
        // advances one track's staged dest to the next valid target.
        void openMachineConsole();
        void closeMachineConsole();
        void commitRouteConsole();
        void cycleRouteConsoleCell(int track);
        // Tap-to-cycle the target slot while the FX picker is open (replaces the
        // old re-hold-to-cycle gesture). Master: units 0-3; Track: slots 0/1.
        void cycleFxPickerSlot(bool master);
        // 9.24 S12: page the open FX picker by ±1 (clamped). Returns true iff a
        // picker was open, so Nav is consumed as paging rather than its normal role.
        bool pageFxPicker(int delta);
        // 11.8 (§40.5): page a focused deck console (DECK ↔ TRACKS) with Nav, which
        // is otherwise idle on a looper (it does not sequence). No-op unless the
        // active track is a looper with more than one sub-track. Returns true when
        // it consumed the key.
        bool pageDeckConsole(int delta);
        // 9.24 S16: Confirm-on-a-convolution-slot opens the pool browser in pick-IR
        // mode for that slot. Returns true iff it opened (Confirm consumed).
        bool tryOpenIrPicker();
        // FX-picker step selection. Extracted so it can run from the Step case AND
        // the Track-held SelectTrack case (QwertyOverlay routes step keys to the
        // latter while Track is held, so the picker must be reachable from both).
        bool applyMasterFxPick(int index);
        bool applyTrackFxPick(int index);
        // Item 5: one-time explainer when an External send is first loaded — the
        // host also sums the Send A/B output buses into the track main unless the
        // user routes+mutes them (no portable plugin-side way to force this across
        // CLAP/VST3). Dismissible with a "don't warn again" toggle (persisted).
        void maybeWarnExternalSend();
        std::unique_ptr<juce::AlertWindow> externalSendWarn_;
        std::unique_ptr<juce::ToggleButton> externalSendWarnToggle_;
        // Long-press-remove on the loaded catalogue cell: down arms + defers,
        // up resolves (tap = bypass via apply*FxPick, long-press = remove).
        [[nodiscard]] bool fxPickerCellIsLoaded(int index, bool master) const;
        void removeFxPickerSlot(bool master);
        bool fxPickerStepDown(int index, bool master);
        bool fxPickerStepUp(int index);

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

        // 10.7: stash of phrase steps captured at melodic-arm time; restored on
        // No/escape. Mirrors the Euclid editor-owned modal triple.
        std::array<Step, kMaxStepsPerTrack> melodyStash_{};
        int melodyStashLen_ = 0;
        int melodicTrack_ = -1;

        // 10.8: stash of phrase steps captured at harmony-arm time; restored on
        // No/escape. Mirrors the Euclid/Melodic editor-owned modal triple.
        std::array<Step, kMaxStepsPerTrack> harmonyStash_{};
        int harmonyStashLen_ = 0;
        int harmonyTrack_ = -1;
        std::vector<int> harmonyAuditionNotes_;  // currently-sounding audition voices

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
        // MIDI-out VU (Part 3): midiLevel_ = the velocity-loudness strip meter
        // (audio-like ballistics); ccBlink_ = the top-right dot, now CC activity.
        std::array<float, kNumTracks> midiLevel_{};
        std::array<float, kNumTracks> ccBlink_{};
        float masterMeter_  = 0.0f;
        float masterMeterR_ = 0.0f;
        void paintMeters(juce::Graphics& g);

        // Cached chrome regions for the per-tick meter/blink decay repaint. The
        // decay animation only touches the master strip + the track/VU row, so
        // the 30 Hz `dirty` repaint is scoped to these instead of the whole
        // editor (otherwise the entire grid/keyboard/MZ repaints 30×/sec — the
        // idle-CPU / fan culprit). Recomputed in resized().
        juce::Rectangle<int> masterChromeRegion_;
        juce::Rectangle<int> trackRowChromeRegion_;

        // Master VU strip height (px), drawn at the very top edge in
        // paintOverChildren as two stacked bars (L, R). resized() reserves this
        // many pixels at the top so the header row sits below it — single source
        // so the layout and the paint cannot drift (PRINCIPLES §20).
        // kMasterStripH is the reserved top zone (dB VU meter + capture banner);
        // resized() removes exactly this many pixels before the header row.
        static constexpr int kMasterStripH = 22;
        static constexpr int kCaptureBannerW = 150;  // right margin reserved for the REC banner
        static constexpr float kMeterFloorDb = 48.0f;  // meter spans -48..0 dBFS

        // Last-seen morphFader value: used to detect on-screen fader moves and
        // mark the surface model dirty so controller surfaces update.
        float lastMorphFader_ = -1.0f;

        // Playhead tracking, split per render target (9.15):
        //  - lastScreenStep_: the focused track's step the on-screen grid last
        //    showed. The audio loop publishes the focused track's current step
        //    (LockstepProcessor::focusStepUi); the display vblank repaints the
        //    grid only when it advances — so the sequencer area is dirtied by the
        //    same loop that fires the notes, at the precise step boundary, and
        //    the grid rebuilds once per step rather than once per vblank.
        //  - lastCtrlPpq_: the PPQ the controller surface last rendered, on the
        //    always-on 30 Hz timer (survives screen-sleep; carries the sub-step
        //    phase envelope the X-Touch playhead LED needs).
        int lastScreenStep_ = -1;
        double lastCtrlPpq_  = -1.0;

        // Transient status line — shows CPC operation result for ~1.5s.
        juce::String statusMessage_;
        juce::uint32 statusSetMs_ = 0;
        // C3: persistent "N sample(s) missing" banner, recomputed each timer tick
        // from the IO-free pool flags (rescanMissing does the disk stat). Unlike the
        // fading toast, this stays until the samples are relinked so a load-time or
        // mid-session disappearance is not missed. Shown only when no toast is up.
        int missingSampleBanner_ = 0;
        // A2: last seen routing-reject sequence (polled in timerCallback).
        juce::uint32 lastRouteRejectSeq_ = 0;
        static constexpr juce::uint32 kStatusDurationMs = 1500;
        void setStatus(const juce::String& msg);
        // Route a Track+verb to the focused Loop's state machine (DESIGN §29.2).
        // cmd matches LoopMachine::Cmd (1=RecordCycle, 2=PlayStop, 3=Clear).
        void routeLooperVerb(int cmd, bool immediate = false);
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

        // 9.15: display-synced playhead. The step highlight is a step function of
        // the PPQ clock; sampling it on the 30 Hz timer quantises the advance to
        // the tick grid (visible "fast, fast, slow" since a step spans N or N+1
        // ticks). Driving it from the display vblank recomputes the position from
        // the live clock at refresh rate, so the highlight advances on the clock.
        // Refreshes only when the playhead actually moved (idle ⇒ no repaint).
        std::unique_ptr<juce::VBlankAttachment> playheadVBlank_;
        void onPlayheadVBlank();

        void updateTransportGhosting();

        // MHZ.9.4: release all modifier latches and latched steps in one gesture.
        // Only called when latch.any() || ctx.hasAnyLatchedStep().
        void escapeAllLatches();

        // Multi-step hold (Part 2): shift every held step by dir (+1 / -1). A single
        // held step keeps the anchor-restore "carry" semantics (relocateStepSwap);
        // multiple held steps block-move together (direction-sorted swaps, clamped at
        // the track boundary so the whole block stops rather than colliding/wrapping).
        // Updates EditContext held indices and heldStepKeys_ so the held set follows.
        // Returns true if anything moved.
        bool moveHeldSteps(int track, int dir);

        // Multi-step hold (Part 2): open the StepInspector (P-Lock overview) on a
        // single held step — the long-press promotion of a bare hold. Sets the
        // pLockClear* state the KeyboardArea label render + slot-tap flow read.
        void openStepInspector(int track, int step);

        // Multi-step hold (Part 2): nudge the micro-offset of every held step by delta
        // (clamped to [-0.5, 0.5]). Returns the primary held step's new offset for the
        // status line, or 0 if nothing was held.
        float nudgeHeldMicro(int track, float delta);

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

        // W7 step latch (9.12 Stage 6): one implementation shared by the Func-down
        // branch and the StepLatch action. True if it latched (caller consumes).
        bool latchHeldSteps();

        // 9.12 Stage 7a: enter a scope on a modifier press. The effect behind the
        // Hold*Scope actions; the same code the imperative branches used to run.
        void enterScopeHold(ControllerButton cb);

        // MHZ.9.x: auto-release a transient mode's latch after its terminal action.
        // No-op when not latched; leaves physically-held (non-latched) mods alone.
        void releaseTransientLatch(ControllerButton cb);

        // Map the currently-held primary scope to a CheckpointScope + focused track.
        // Used by snapshot / restore call sites to route to the right stack.
        CheckpointScope ckScope(int& outTrack) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepEditor)
    };
}
