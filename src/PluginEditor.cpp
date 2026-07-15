#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "command/ScopePriority.h"
#include "command/StatusText.h"
#include "core/Euclidean.h"
#include "core/StateResolver.h"
#include "core/MelodyGen.h"
#include "core/HarmonyGen.h"
#include "core/MetricGrid.h"
#include "core/StepBlockMove.h"
#include "core/OutputDest.h"
#include "core/TrackInputMode.h"
#include "machine/InputSource.h"
#include "io/TrigGridMode.h"
#include "machine/IMachine.h"
#include "machine/ISliceable.h"
#include "machine/SampleMachine.h"
#include "machine/RouteMachine.h"
#include "machine/EffectPickerModel.h"
#include "ui/KeyLabel.h"
#include "ui/MeterMath.h"
#include "ui/MetaBand.h"
#include "ui/ScopedSectionMatrix.h"
#include "ui/SectionResolve.h"
#include "ui/SurfaceModel.h"
#include "ui/UITheme.h"
#include "ui/mode/ModeReducer.h"
#include "ui/mode/LatchOps.h"
#include <algorithm>

// D2: dirty-guard hook requires access to StandaloneFilterWindow (standalone target only).
#if JucePlugin_Build_Standalone
  #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace lockstep
{
    // §40.2 hold-to-wind speed: reel samples advanced per engine sample while a
    // FF/RW console cell is held (~8×, slewed by the machine so it winds not jumps).
    static constexpr double kTapeWindRate = 8.0;

    // -------------------------------------------------------------------------
    // Command core seam (Phase 8.4)
    // Defined here (before constructor) so the nested struct is complete when
    // make_unique<EditorEffects> is called in the member-initialiser list.

    struct LockstepEditor::EditorEffects final : CommandEffects
    {
        LockstepEditor& ed;
        explicit EditorEffects(LockstepEditor& e) : ed(e) {}

        void status(const juce::String& msg) override { ed.setStatus(msg); }
        void requestRepaint() override { ed.refreshSurface(); }
        void transport(TransportAction a) override
        {
            using A = TransportAction;
            auto& clk = ed.processor_.clock();
            switch (a)
            {
                case A::Play:
                    // Mode-aware: hosted Locked toggles the arm gate, standalone/Auto
                    // toggles in-plugin Play. Single home for the decision (v27).
                    ed.processor_.transportPlay();
                    break;
                case A::Pause:
                    ed.processor_.transportPause();
                    break;
                case A::StopReset:
                    ed.processor_.transportStopReset();
                    break;
                case A::Panic:
                    ed.processor_.requestPanic();
                    break;
                case A::RecArm:
                    clk.setRecordArmed(!clk.isRecordArmed());
                    break;
                case A::Metronome:
                    clk.setMetronomeEnabled(!clk.isMetronomeEnabled());
                    break;
                case A::TapTempo:
                    ed.handleTapTempo();
                    break;
            }
            // Immediate label/colour sync — no need to wait for the 15 Hz timer tick.
            ed.transport_.refresh(buildTransportModel(clk));
        }
        void machineAssign(int track, const char* id) override
        {
            ed.processor_.setTrackMachine(track, id);
            ed.refreshSurface();
        }
        void machineParams(int track, const std::vector<float>& params) override
        {
            // writeParam is the single owner of a param write (it routes base-vs-P-Lock
            // and runs the machine's own hooks — sample_id opens a reader, slice params
            // recompute). Pasting a sound therefore behaves exactly as if the user had
            // dialled every slot by hand, which is the only way it can be correct for
            // every machine without this code knowing what a machine is.
            const int n = std::min(static_cast<int>(params.size()),
                                   ed.processor_.numParams(track));
            for (int s = 0; s < n; ++s)
                ed.processor_.writeParam(track, s, params[static_cast<std::size_t>(s)]);
            ed.keyboardArea_.syncToActiveTrack();
            ed.refreshSurface();
        }
        void openOverlay(OverlayId id, int param) override
        {
            switch (id)
            {
                case OverlayId::SamplePool:
                    ed.poolOverlay_.setVisible(true);
                    break;
                case OverlayId::SoundBank:
                    ed.soundBankOverlay_.setVisible(true);
                    break;
                case OverlayId::MachinePicker:
                    ed.uiState_.machinePickerOpen = (param != 0);
                    ed.refreshSurface();
                    break;
            }
        }
        void selectMetaBand(MetaBandId id) override
        {
            switch (id)
            {
                case MetaBandId::Mixer:
                    // Toggle: the chord that opens the mixer closes it, like every
                    // other latched meta page.
                    ed.keyboardArea_.selectMetaSection(kMetaContentMixer, true);
                    ed.refreshSurface();
                    break;
            }
        }
        void crossfader(float value) override
        {
            ed.crossfader_.setValue(static_cast<double>(value),
                                    juce::sendNotificationAsync);
        }
        void releaseLatch(ControllerButton cb) override
        {
            ed.releaseTransientLatch(cb);
        }

        void morphBake(int track) override
        {
            ed.processor_.bakeAllMorph(track);
        }
        void morphErase(int track) override
        {
            ed.processor_.removeAllMorph(track);
        }
        // 9.17: mute/solo/scene-mute arm to the launch grid; a step double-tap
        // (per-lane token so lanes don't cross-trigger) is the instant override.
        void globalMuteToggle(int track) override
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            const bool dbl = ed.gesture_.doubleTap(kMuteStepTokenBase + track, now);
            ed.processor_.queueGlobalMuteToggle(track, dbl);
            ed.refreshSurface();
        }
        void soloToggle(int track) override
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            const bool dbl = ed.gesture_.doubleTap(kMuteStepTokenBase + 16 + track, now);
            ed.processor_.queueSolo(track, dbl);
            ed.refreshSurface();
        }
        void sceneMuteToggle(int track) override
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            const bool dbl = ed.gesture_.doubleTap(kMuteStepTokenBase + 32 + track, now);
            ed.processor_.queueSceneMute(track, dbl);
            ed.refreshSurface();
        }
        void fluidMuteToggle(int track) override
        {
            if (ed.processor_.hasFluidMute(track))
            {
                const int slot = ed.processor_.fluidMuteLevelSlot(track);
                if (slot >= 0)
                {
                    ed.processor_.removeMorphPole(track, slot, 0);
                    ed.processor_.removeMorphPole(track, slot, 1);
                }
                ed.setStatus(status::morphMuteCleared());
            }
            else
            {
                ed.processor_.fluidMuteTrack(track, ed.processor_.morphFader());
                ed.setStatus(status::morphMuteSet());
            }
            ed.refreshSurface();
        }
        void toggleCapture() override
        {
            // Single owner: the CAPTURE cell is intercepted in the editor's
            // VerbRecord dispatch and routed through captureController_ with full
            // tap / double-tap / long-press semantics, so this CommandCore path is
            // normally unreachable. Kept as a sane fallback: a plain tap.
            ed.runCaptureOut(
                ed.captureController_.onTap(ed.processor_.clock().inPluginPlaying()));
        }

        // ── 9.12 Stage 6 ─────────────────────────────────────────────────────
        // Each of these delegates to the SAME editor code its imperative branch in
        // dispatchDown/dispatchUp calls today, so the branch and the action share one
        // implementation. Stages 7-8 delete the branches; nothing here changes then,
        // which is what makes the golden net's "behaviour did not move" claim mean
        // something.
        void enterScope(ControllerButton cb) override { ed.enterScopeHold(cb); }
        void latchModifier(ControllerButton cb) override { ed.setModifierLatch(cb, true); }

        void escapeOverlay() override
        {
            // Func double-tap: leave whatever Func-layer mode is up. FuncReskin owns
            // the unified exit for the five picker/editor modes; the overlay family
            // exits through ModeReducer.
            if (activeFuncReskin(ed.uiState_) != FuncReskin::None)
            {
                exitFuncReskin(ed.uiState_);
            }
            else
            {
                const Overlay ov = activeOverlay(ed.uiState_);
                if (ov != Overlay::None)
                    lockstep::escapeOverlay(ed.uiState_, ov);
            }
            ed.refreshSurface();
        }

        void restorePop() override
        {
            int ckTrk = 0;
            const CheckpointScope scp = ed.ckScope(ckTrk);
            // 9.4 item A: an empty stack no longer wipes to the project baseline — it
            // does nothing, and the user is told so. A restore that silently reverts
            // half an hour of work is worse than a restore that refuses.
            if (!ed.processor_.restoreOne(scp, ckTrk))
                ed.setStatus(status::nothingToRestore());
            ed.refreshSurface();
        }

        void restoreFloor() override
        {
            int ckTrk = 0;
            const CheckpointScope scp = ed.ckScope(ckTrk);
            ed.processor_.restoreToFloor(scp, ckTrk);
            ed.refreshSurface();
        }

        // 9.4 item E: Func+O = UNDO. Reverts the held scope's last destructive op
        // (clear / paste / delete / bake / generator print / transpose / restore).
        // Bare Func with no scope resolves to the Song scope via ckScope, which is the
        // grammar default and where most destructive ops arm.
        void undo() override
        {
            int ckTrk = 0;
            const CheckpointScope scp = ed.ckScope(ckTrk);
            if (!ed.processor_.undo(scp, ckTrk))
            {
                ed.setStatus(status::nothingToUndo());
            }
            else
            {
                const char* name = "Song";
                switch (scp)
                {
                    case CheckpointScope::Song:   name = "Song";   break;
                    case CheckpointScope::Track:  name = "Track";  break;
                    case CheckpointScope::Scene:  name = "Scene";  break;
                    case CheckpointScope::Phrase: name = "Phrase"; break;
                }
                ed.setStatus(status::undid(name));
            }
            ed.refreshSurface();
        }

        void recordArmOverdub() override
        {
            auto& clk = ed.processor_.clock();
            clk.setRecordArmed(true);      // overdub implies armed
            clk.setOverdubArmed(true);
            ed.refreshSurface();
        }

        // Latches every held step, not the one whose index arrived: the gesture is
        // "free my fingers", and the branch it replaces has always latched the whole
        // held set. Same helper the branch calls.
        void stepLatch(int) override { (void)ed.latchHeldSteps(); }

        void navPageUnlock() override
        {
            ed.keyboardArea_.unlockScrollPastEnd();
            ed.keyboardArea_.nextPage();
        }

        // ── 9.12 st.7e: QUANT ─────────────────────────────────────────────────
        // Moved verbatim out of the VerbConfirm cascade. The three targets are a scope
        // cascade the table cannot express, because the first of them is a HELD STEP:
        // Trig (held steps) → those steps; Track → the whole track; Phrase → all tracks.
        void quantizeHeld() override
        {
            using PS = EditMode::PrimaryScope;
            const auto scope = ed.editMode_.primaryScope();
            const auto& heldSteps = ed.processor_.editContext().heldSteps();

            if (!heldSteps.empty())
            {
                const int t = ed.keyboardArea_.getActiveTrack();
                if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
                ed.processor_.armUndo(CheckpointScope::Track, t);
                auto& trk = ed.processor_.sequence().tracks[static_cast<std::size_t>(t)];
                for (const int si : heldSteps)
                    if (si >= 0 && si < kMaxStepsPerTrack)
                        trk.steps[static_cast<std::size_t>(si)].microOffset = 0.0f;
            }
            else if (scope == PS::Track)
            {
                const int t = ed.keyboardArea_.getActiveTrack();
                if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
                ed.processor_.armUndo(CheckpointScope::Track, t);
                for (auto& st : ed.processor_.sequence().tracks[static_cast<std::size_t>(t)].steps)
                    st.microOffset = 0.0f;
            }
            else if (scope == PS::Phrase)
            {
                int ckTrk = 0;
                ed.processor_.armUndo(ed.ckScope(ckTrk), ckTrk);
                for (auto& trk : ed.processor_.sequence().tracks)
                    for (auto& st : trk.steps)
                        st.microOffset = 0.0f;
            }
            else
            {
                return;   // no quantizable scope
            }
            ed.setStatus(status::quantized());
            ed.refreshSurface();
        }

        // ── 9.12 st.7d: the section family's effects ──────────────────────────
        // Both TAP bodies below are MOVED verbatim out of dispatchDown. They stay one
        // resolver deep (resolveSectionKey) because that resolver is already the single
        // owner shared with the painter and the MZ — moving them here changes WHO calls
        // them (the table, via handleAction), not what they do.
        void selectSection(int index) override
        {
            using PS = EditMode::PrimaryScope;
            const PS sectionScope = firstHeldSectionSuiteScope(ed.uiState_);

            // Morph keeps its bespoke branch: it has no stack floor (the MZ writes the
            // morph overlay while morphHeld), so a section press is a plain param nav.
            if (sectionScope == PS::Morph)
            {
                ed.keyboardArea_.selectSection(index, /*trackScope*/ false);
                return;
            }

            const int at = ed.keyboardArea_.getActiveTrack();
            const SecOrigin floor = sectionFloorForScope(sectionScope);
            // Held-step promotion (9.26): bare TRIG → per-step COND while a step is held.
            const auto res = resolveSectionKey(ed.processor_, at, index, floor,
                                               /*funcLayer*/ false, ed.uiState_.stepHeld);
            if (!res.hasContent)
                return;   // dim: nothing at/below the held floor owns this key

            switch (res.action)
            {
                case SecAction::MetaSection:
                    ed.keyboardArea_.selectMetaSection(res.metaIndex);   // Track DIV, Phrase LEN...
                    return;
                case SecAction::TimeSticky:
                    applyTimeEntry(ed.uiState_);                          // Song/Scene + TRIG
                    ed.refreshMetaBand();
                    return;
                case SecAction::ParamSection:
                    break;   // machine/track param page — below
            }

            // 9.14 Stage 3 / Part 2: SRC tap while step(s) are held → NoteEdit for ALL
            // held steps (dismisses any P-lock view). Reachable from a bare multi-hold,
            // so the note editor edits every held step at once (Elektron flow).
            if (index == IMachine::kSrcSecIdx && ed.processor_.editContext().isActiveForEditing())
            {
                const auto& held = ed.processor_.editContext().heldSteps();
                const int primary = ed.processor_.editContext().heldStepIndex();
                const int track = ed.processor_.editContext().heldTrackIndex();
                ed.uiState_.noteEditSteps.clear();
                ed.uiState_.noteEditSteps.insert(held.begin(), held.end());
                ed.uiState_.noteEditStaged.clear();
                if (track >= 0 && primary >= 0)
                {
                    const auto& st = ed.processor_.sequence()
                                         .tracks[static_cast<std::size_t>(track)]
                                         .steps[static_cast<std::size_t>(primary)];
                    if (st.trigOverride.noteCount > 0)
                        ed.uiState_.noteEditOctave = st.trigOverride.notes[0] / 12 - 1;
                }
                ed.uiState_.noteEditMode = true;
                ed.uiState_.resetPLockClear();   // NoteEdit outranks the P-lock view
                ed.refreshSurface();
                return;
            }

            // Page the layer the resolver actually LANDED on: a Track winner routes the
            // track-level view (P6); anything else uses the machine-preferring view. Keyed
            // off the winner, not the held floor, so a key pages the layer it is coloured by.
            ed.keyboardArea_.selectSection(index, res.winner == SecOrigin::Track);
        }
        void selectMetaSection(int index) override
        {
            // Func-layer section dispatch (9.22), through the same resolver so it agrees
            // with the painter + MZ. sectionResolveMode owns the split (bare Func = the
            // meta hierarchy; Func+Song = Set; Func+Track = Machine — 9.29).
            // FX stays swallowed under Func: its picker is a HOLD gesture, and Func+FX
            // was retired in 9.14 st.2.
            if (index == LockstepProcessor::kFxSecIdx)
                return;
            const int at = ed.keyboardArea_.getActiveTrack();
            const auto mode = sectionResolveMode(ed.uiState_);
            const auto res = resolveSectionKey(ed.processor_, at, index, mode.floor,
                                               mode.funcLayer, mode.stepHeld);
            if (!res.hasContent)
                return;   // dim under Func → swallow the press
            switch (res.action)
            {
                case SecAction::MetaSection: ed.keyboardArea_.selectMetaSection(res.metaIndex); return;
                case SecAction::TimeSticky:  applyTimeEntry(ed.uiState_); ed.refreshMetaBand(); return;
                case SecAction::ParamSection:
                    ed.keyboardArea_.selectSection(index, res.winner == SecOrigin::Track);
                    return;
            }
        }
        void openFxPicker(bool master) override { ed.openFxSectionPicker(master); }

        // ── 9.12 st.7c: the nav family's effects ──────────────────────────────
        // Every body below is MOVED from the NavUp/NavDown/NavLeft/NavRight cascades,
        // not rewritten. What changed is who DECIDES which one runs: the table, via
        // handleAction, instead of a hand-maintained if-chain per key.
        void navFocusTrack(int delta) override
        {
            // Don't change track while a step is held: the inspector / move targets one
            // track, and crossing tracks made the move "drag" onto a different track
            // (9.14 follow-up). Consume and no-op. This guard belongs to THIS action --
            // it must not block Func+Nav length or rotate, which are legal under a hold.
            if (ed.processor_.editContext().heldStepIndex() >= 0) return;
            const int t = ed.keyboardArea_.getActiveTrack();
            ed.keyboardArea_.setActiveTrack(
                std::clamp(t + delta, 0, static_cast<int>(kNumTracks) - 1));
        }
        void navPage(int delta) override
        {
            if (delta < 0) { ed.keyboardArea_.prevPage(); return; }
            // DESIGN §34.4 / PRINCIPLES §17 (nav reveal/unlock family): at the last
            // in-length page a single NavRight is a clamped no-op; a double-tap unlocks
            // one empty page past the end so a longer length can be set.
            if (ed.keyboardArea_.currentPage() >= ed.keyboardArea_.numPages() - 1)
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (ed.gesture_.doubleTap(GestureRecognizer::kNavRightUnlock, now))
                {
                    ed.keyboardArea_.unlockScrollPastEnd();
                    ed.setStatus(status::scrolledPastEnd());
                }
            }
            ed.keyboardArea_.nextPage();
        }
        void navOctave(int delta) override
        {
            ed.uiState_.noteEditOctave =
                std::clamp(ed.uiState_.noteEditOctave + delta, 0, 8);
            ed.refreshSurface();
        }
        void trackLengthScale(int delta) override
        {
            const int t = ed.keyboardArea_.getActiveTrack();
            if (t >= 0 && t < static_cast<int>(kNumTracks))
            {
                // S4: a looper has no editable pattern length (it IS the track grid), so
                // this is the loop-window Double/Halve — parity with the console cells.
                if (ed.processor_.isLooperTrack(t))
                    ed.routeLooperVerb(delta > 0 ? 6 /*Double*/ : 5 /*Halve*/);
                else if (delta > 0)
                    ed.processor_.doubleTrackLength(t);
                else
                    ed.processor_.halveTrackLength(t);
            }
            ed.refreshSurface();
        }
        void rotateSteps(int delta) override
        {
            const int t = ed.keyboardArea_.getActiveTrack();
            if (t >= 0 && t < static_cast<int>(kNumTracks))
                ed.processor_.rotateTrackSteps(t, delta);
            ed.refreshSurface();
        }
        void cycleInputMode(int delta) override
        {
            const int t = ed.keyboardArea_.getActiveTrack();
            // MHZ.9.7: the cycle only applies while Track is held with NO track picked
            // (Control-All). Once a specific track is selected, Track+Nav goes back to
            // moving the focus. That gate is ENGINE state, not a modifier, so the table
            // cannot see it — which is why the fallback lives here and not in a row.
            if (!ed.processor_.controlAllActive() || t < 0 || t >= static_cast<int>(kNumTracks))
            {
                navFocusTrack(delta);
                return;
            }
            auto& mode = ed.uiState_.trackInputMode[static_cast<std::size_t>(t)];
            if (delta > 0)
            {
                switch (mode)
                {
                    case TrackInputMode::Play:      mode = TrackInputMode::Levels; break;
                    case TrackInputMode::Chromatic: mode = TrackInputMode::Play; break;
                    case TrackInputMode::Levels:    mode = TrackInputMode::Chromatic; break;
                    default:                        break;
                }
            }
            else
            {
                switch (mode)
                {
                    case TrackInputMode::Play:      mode = TrackInputMode::Chromatic; break;
                    case TrackInputMode::Chromatic: mode = TrackInputMode::Levels; break;
                    case TrackInputMode::Levels:    mode = TrackInputMode::Play; break;
                    default:                        break;
                }
            }
            ed.escapeAllLatches();   // entering a new modality exits the current latch
            ed.refreshSurface();
        }
        void morphPole(int pole) override
        {
            ed.uiState_.morphNavQualifier = pole;
            ed.manipulationZone_.setMorphQualifier(pole);
            ed.manipulationZone_.setMorphHeld(ed.uiState_.morphHeld);
            ed.refreshSurface();
        }
        void transposeTrack(int semitones) override
        {
            const int t = ed.keyboardArea_.getActiveTrack();
            if (t >= 0 && t < static_cast<int>(kNumTracks))
            {
                ed.processor_.armUndo(CheckpointScope::Phrase, t);
                ed.processor_.transposeTrack(t, semitones);
                ed.setStatus(juce::String(semitones > 0 ? "TRANSPOSE +" : "TRANSPOSE -")
                             + (std::abs(semitones) == 12 ? "oct" : "1"));
            }
            ed.uiState_.phraseScopeUsed = true;
            ed.refreshSurface();
        }

        void openGeneratorHub() override
        {
            ed.uiState_.generatorHubHeld = true;
            ed.refreshSurface();
        }

        void setTrigGridMode(TrigGridMode mode) override
        {
            ed.uiState_.trigGridMode = mode;
            ed.refreshSurface();
        }

        void sceneFloorPaste() override
        {
            auto& cl = ed.clipboard_;
            auto& dst = ed.processor_.section();
            dst.activeMask = cl.scene.floor.activeMask;
            dst.coreTime = cl.scene.floor.coreTime;
            dst.morphA = cl.scene.floor.morphA;
            dst.morphB = cl.scene.floor.morphB;
            dst.initialised = true;
            ed.setStatus(status::pastedSceneFloor());
        }

        void sceneFullPaste(int destSlot) override
        {
            auto& cl = ed.clipboard_;
            if (ed.phraseConflictAndConfirm(destSlot, ConfirmKind::PasteScene))
                return;  // waiting for Yes/No
            ed.processor_.armUndo(CheckpointScope::Song, 0);
            auto& dst = ed.processor_.section();
            dst.activeMask = cl.scene.floor.activeMask;
            dst.coreTime = cl.scene.floor.coreTime;
            dst.morphA = cl.scene.floor.morphA;
            dst.morphB = cl.scene.floor.morphB;
            dst.initialised = true;
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                auto& ph = ed.processor_.song()
                               .tracks[static_cast<std::size_t>(t)]
                               .phrases[static_cast<std::size_t>(destSlot)];
                ph = cl.scene.phrases[static_cast<std::size_t>(t)];
                ph.initialised = true;
            }
            ed.processor_.refreshWorkingFromModel();
            ed.setStatus(status::pastedScene());
        }

        // Executes a confirmed (CONFIRM) action. Called by CommandCore on P-press in
        // PendingConfirm layer; confirm state already reset by the time this returns.
        void executeConfirm(ConfirmKind kind, int target) override
        {
            if (kind == ConfirmKind::BakeScene)
            {
                ed.processor_.armUndo(CheckpointScope::Song, 0);
                ed.processor_.bakeSceneState();
                ed.setStatus(status::baked());
            }
            else if (kind == ConfirmKind::CreateScene)
            {
                ed.processor_.armUndo(CheckpointScope::Song, 0);
                ed.processor_.createBakedCopyScene(target);
                if (ed.processor_.clock().inPluginPlaying())
                    ed.processor_.queueScene(target, false);
                else
                    ed.processor_.setActiveScene(target);
                ed.setStatus(status::sceneCreated(target + 1));
            }
            else if (kind == ConfirmKind::CreateBaselineScene)
            {
                ed.processor_.armUndo(CheckpointScope::Song, 0);
                ed.processor_.createBaselineCopyScene(target);
                if (ed.processor_.clock().inPluginPlaying())
                    ed.processor_.queueScene(target, false);
                else
                    ed.processor_.setActiveScene(target);
                ed.setStatus(status::sceneBaseline(target + 1));
            }
            else if (kind == ConfirmKind::PasteScene)
            {
                ed.processor_.armUndo(CheckpointScope::Song, 0);
                auto& dst = ed.processor_.section();
                auto& cl = ed.clipboard_;
                dst.activeMask = cl.scene.floor.activeMask;
                dst.coreTime = cl.scene.floor.coreTime;
                dst.morphA = cl.scene.floor.morphA;
                dst.morphB = cl.scene.floor.morphB;
                dst.initialised = true;
                for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                {
                    auto& ph = ed.processor_.song()
                                   .tracks[static_cast<std::size_t>(t)]
                                   .phrases[static_cast<std::size_t>(target)];
                    ph = cl.scene.phrases[static_cast<std::size_t>(t)];
                    ph.initialised = true;
                }
                ed.processor_.refreshWorkingFromModel();
                ed.setStatus(status::pastedScene());
            }
            else if (kind == ConfirmKind::DeleteTrack)
            {
                if (target >= 0 && target < static_cast<int>(kNumTracks))
                {
                    ed.processor_.armUndo(CheckpointScope::Track, target);
                    ed.processor_.deleteTrack(target);
                    ed.setStatus(status::deletedTrack(target));
                }
            }
            else if (kind == ConfirmKind::DeletePhrase)
            {
                if (target >= 0 && target < static_cast<int>(kPhrasesPerTrack))
                {
                    // Slot-specific deletion (Stage 7): reset one phrase on focused track.
                    const int trk = ed.keyboardArea_.getActiveTrack();
                    ed.processor_.armUndo(CheckpointScope::Track, trk >= 0 ? trk : 0);
                    ed.processor_.deletePhraseSlot(trk >= 0 ? trk : 0, target);
                    ed.setStatus(status::deletedPhrase());
                }
                else
                {
                    // Legacy fallback: old-style global phrase wipe (no slot selected).
                    int ckTrk = 0;
                    ed.processor_.armUndo(ed.ckScope(ckTrk), ckTrk);
                    for (auto& trk : ed.processor_.sequence().tracks)
                    {
                        for (auto& s : trk.steps)
                        {
                            s.trig = false;
                            s.condition = TrigCondition{};
                            s.overrides = PLock{};
                            s.trigOverride = TrigOverride{};
                            s.fillTrigState = FillTrigState::Off;
                            s.fillOverrides = PLock{};
                            s.fillTrigOverride = TrigOverride{};
                        }
                    }
                    ed.setStatus(status::deletedPhrase());
                }
            }
            else if (kind == ConfirmKind::DeleteScene)
            {
                if (target >= 0 && target < static_cast<int>(kScenesPerSong))
                {
                    // Slot-specific scene deletion (Stage 7).
                    int ckTrk = 0;
                    ed.processor_.armUndo(ed.ckScope(ckTrk), ckTrk);
                    ed.processor_.deleteSceneSlot(target);
                    ed.releaseTransientLatch(ControllerButton::SceneScope);
                    ed.setStatus(status::deletedPart());
                }
                else
                {
                    // Legacy fallback (should not happen with picker flow).
                    int ckTrk = 0;
                    ed.processor_.armUndo(ed.ckScope(ckTrk), ckTrk);
                    ed.processor_.deletePart();
                    ed.releaseTransientLatch(ControllerButton::SceneScope);
                    ed.setStatus(status::deletedPart());
                }
            }
            else if (kind == ConfirmKind::ClearTrack)
            {
                if (target >= 0 && target < static_cast<int>(kNumTracks))
                {
                    ed.processor_.armUndo(CheckpointScope::Track, target);
                    auto& trk = ed.processor_.sequence().tracks[static_cast<std::size_t>(target)];
                    for (auto& s : trk.steps)
                    {
                        s.trig = false;
                        s.condition = TrigCondition{};
                        s.overrides = PLock{};
                        s.trigOverride = TrigOverride{};
                    }
                    ed.releaseTransientLatch(ControllerButton::TrackScope);
                    ed.setStatus(status::clearedTrack(target));
                }
            }
            else if (kind == ConfirmKind::ClearTrackAll)
            {
                if (target >= 0 && target < static_cast<int>(kNumTracks))
                {
                    ed.processor_.armUndo(CheckpointScope::Song, target);
                    ed.processor_.clearTrackAllPhrases(target);
                    ed.releaseTransientLatch(ControllerButton::TrackScope);
                    ed.setStatus(status::clearedTrackAll(target));
                }
            }
            else if (kind == ConfirmKind::ClearPhrase)
            {
                int ckTrk = 0;
                ed.processor_.armUndo(ed.ckScope(ckTrk), ckTrk);
                for (auto& trk : ed.processor_.sequence().tracks)
                {
                    for (auto& s : trk.steps)
                    {
                        s.trig = false;
                        s.condition = TrigCondition{};
                        s.overrides = PLock{};
                        s.trigOverride = TrigOverride{};
                        s.fillTrigState = FillTrigState::Inherit;
                        s.fillOverrides = PLock{};
                        s.fillTrigOverride = TrigOverride{};
                    }
                }
                ed.setStatus(status::clearedPhrase());
            }
            ed.refreshSurface();
        }
    };

    // Narrow IMachineCatalog adapter — forwards to LockstepProcessor.
    // Defined here so test targets can supply their own fixture.
    struct ProcessorCatalog final : IMachineCatalog
    {
        LockstepProcessor& p;
        explicit ProcessorCatalog(LockstepProcessor& proc) : p(proc) {}

        [[nodiscard]] int numParams(int track) const override
        {
            return p.numParams(track);
        }
        [[nodiscard]] ParamSpec paramSpec(int track, int slot) const override
        {
            return p.paramSpec(track, slot);
        }
        [[nodiscard]] SectionInfo section(int track, int idx) const override
        {
            return p.section(track, idx);
        }
        [[nodiscard]] float baseParam(int track, int slot) const override
        {
            return p.baseParamValue(track, slot);
        }
        [[nodiscard]] const char* machineId(int track) const override
        {
            return p.getMachineIdRaw(track);
        }
    };

    // -------------------------------------------------------------------------
    // Constructor / destructor

    LockstepEditor::LockstepEditor(LockstepProcessor& proc)
        : juce::AudioProcessorEditor(&proc),
          processor_(proc),
          transport_(proc.clock()),
          keyboardArea_(proc, uiState_),
          manipulationZone_(proc, keyboardArea_),
          poolOverlay_(proc),
          editorEffects_(std::make_unique<EditorEffects>(*this)),
          soundBankOverlay_(proc)
    {
        // The chrome look, installed on the editor so EVERY child inherits it -- the
        // file bar, the transport, the rail buttons and the combo boxes were each
        // styled (or not styled) where they were built, which is why the same widget
        // had three looks and three font sizes at the top of the window.
        setLookAndFeel(&chromeLnf_);

        // Load persisted display mode.
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Lockstep";
            o.filenameSuffix = ".xml";
            o.folderName = "Lockstep";
            o.osxLibrarySubFolder = "Application Support";
            appProps_.setStorageParameters(o);
        }
        if (auto* prefs = appProps_.getUserSettings())
            gridMode_ = static_cast<GridDisplayMode>(
                prefs->getIntValue("gridMode", static_cast<int>(GridDisplayMode::Ortholinear)));
        applyDisplayMode(gridMode_);
        addAndMakeVisible(transport_);
        // v27: in the hosted-Locked regime the header transport parks/unparks the
        // plugin (arm gate) instead of being inert. Route the buttons through the
        // mode-aware verbs so Play/Stop toggle arm and reflect Armed/Park state.
        transport_.onPlayVerb = [this] { processor_.transportPlay(); };
        transport_.onStopVerb = [this] { processor_.transportStopReset(); };
        transport_.isArmed = [this] { return processor_.isPluginArmed(); };

        tempoReadout_.setJustificationType(juce::Justification::centredLeft);
        tempoReadout_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(tempoReadout_);
        addAndMakeVisible(inspectorBar_);
        addAndMakeVisible(timelineStrip_);

        if (juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone)
        {
            fileBar_ = std::make_unique<StandaloneFileBar>(proc, appProps_);
            fileBar_->onStatus = [this](const juce::String& msg) { setStatus(msg); };
            addAndMakeVisible(fileBar_.get());

            // Auto-open the last project on launch.  We prefer the wrapper-session state
            // (already loaded via setStateInformation before the editor exists) and only
            // rebind currentProjectFile_ if the file still matches (hashes equal).
            // If the file has changed we still open it so the UI title is correct.
            if (auto* prefs = appProps_.getUserSettings())
            {
                const auto lastPath = prefs->getValue(juce::String("lastProjectFile"));
                if (lastPath.isNotEmpty())
                {
                    const juce::File lastFile(lastPath);
                    if (lastFile.existsAsFile())
                        proc.loadProjectFile(lastFile);
                }
            }
        }

        // Sync mode ComboBox + APVTS attachment
        syncModeBox_.addItem("Locked", 1);
        syncModeBox_.addItem("Auto", 2);
        syncModeBox_.setWantsKeyboardFocus(false);
        addAndMakeVisible(syncModeBox_);
        syncModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                proc.apvts(), ParamIDs::syncMode, syncModeBox_);

        // Channel mode ComboBox + APVTS attachment
        channelModeBox_.addItem("Omni", 1);
        channelModeBox_.addItem("Per-Track", 2);
        channelModeBox_.setWantsKeyboardFocus(false);
        addAndMakeVisible(channelModeBox_);
        channelModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                proc.apvts(), ParamIDs::channelMode, channelModeBox_);

        proc.apvts().addParameterListener(ParamIDs::syncMode, this);
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            proc.apvts().addParameterListener(ParamIDs::trackMute(i), this);
            proc.apvts().addParameterListener(ParamIDs::trackSolo(i), this);
            // 9.15 Stage 3: length/divider change the grid layout. KeyboardArea no
            // longer polls them, so the APVTS listener refreshes the surface
            // however they change (step-grid slider attachment, nav keys, Euclid,
            // host automation).
            proc.apvts().addParameterListener(ParamIDs::trackLength(i), this);
            proc.apvts().addParameterListener(ParamIDs::trackDivider(i), this);
        }
        updateTransportGhosting();

        // The grid's own state edits (active track, page, section, display mode,
        // mouse release) join the one invalidation channel (PRINCIPLES §22) instead
        // of self-repainting: a self-repaint redraws the grid but leaves the
        // controllers' LEDs stale.
        keyboardArea_.onSurfaceDirty = [this] { refreshSurface(); };

        // Wire section-change callbacks -> update ManipulationZone.
        keyboardArea_.onSectionChanged = [this](int section, int page, int firstSlot) {
            manipulationZone_.setSlotOffset(firstSlot);
            const int track = keyboardArea_.getActiveTrack();
            if (track >= 0)
            {
                const auto info = processor_.section(track, section);
                manipulationZone_.setNormalTitle(info.label, page, info.pageCount);
                // Item 7 / 9.22: capture the resolved scope origin of the page at
                // selection time (the modifier is still held here) so the MZ banner
                // names it even after the scope is released. sectionResolveMode owns
                // the floor+Func-promotion rule (single source): truly unqualified,
                // the origin is per-page (a mixed machine+track section pages through
                // both), read from the actual slot; otherwise the resolver names the
                // TRUE winner (Scene+FILTER → MACHINE; Phrase+FILTER → TRACK;
                // Func+Song fall-up pages → their winner).
                const auto mode = sectionResolveMode(uiState_);
                SecOrigin origin;
                if (mode.floor == SecOrigin::Machine && !mode.funcLayer)
                    origin = (firstSlot >= processor_.numParams(track))
                                 ? SecOrigin::Track : SecOrigin::Machine;
                else
                {
                    const auto res = resolveSectionKey(processor_, track, section,
                                                       mode.floor, mode.funcLayer,
                                                       mode.stepHeld);
                    origin = res.hasContent ? res.winner : SecOrigin::Machine;
                }
                // mode.funcLayer is the single fact about *how this page was
                // reached*; it latches the MZ's Func border until the next
                // selection (9.31), which a bare selection clears by writing false.
                manipulationZone_.setPageOrigin(origin, mode.funcLayer);
            }
            refreshMetaBand();
        };

        // Mini-sequencer strip mouse aids (item 6).
        keyboardArea_.onMiniSeqToggle = [this](int absStep) {
            if (activeTrackContentLocked()) return;
            const int track = keyboardArea_.getActiveTrack();
            if (track < 0) return;
            auto* lp = processor_.apvts().getRawParameterValue(ParamIDs::trackLength(track));
            const int len = lp ? static_cast<int>(lp->load()) : KeyboardArea::kPageSteps;
            if (absStep >= len) return;
            auto& s = processor_.sequence()
                          .tracks[static_cast<std::size_t>(track)]
                          .steps[static_cast<std::size_t>(absStep)];
            s.trig = !s.trig;
            refreshSurface();   // a trig changed: grid + controllers, not chrome alone
        };
        keyboardArea_.onMiniSeqScrollToStep = [this](int absStep) {
            keyboardArea_.setPage(absStep / KeyboardArea::kPageSteps);
        };
        keyboardArea_.onMiniSeqSetLength = [this](int absStep) {
            const int track = keyboardArea_.getActiveTrack();
            if (track < 0) return;
            // Route through setTrackLength so the working Track.length and the APVTS
            // param stay in sync (see setTrackLength). Writing the param alone left
            // tracks[].length stale for readers like the Euclid generator.
            processor_.setTrackLength(track, absStep + 1);
        };

        keyboardArea_.onMetaSectionChanged = [this](int metaSection) {
            // Navigating to any meta section other than FX/Global (5 or -1) clears
            // the sticky master FX band so the newly-selected section wins.
            // Navigating to a different meta section closes the FX picker.
            if (metaSection != 5 && metaSection != -1)
                uiState_.masterFxPickerOpen = false;
            refreshMetaBand();
        };

        // Track page toggle: flips between tracks 1-8 and 9-16.
        trackPageBtn_.setWantsKeyboardFocus(false);
        trackPageBtn_.onClick = [this] {
            trackPage_ = 1 - trackPage_;
            trackPageBtn_.setButtonText(trackPage_ == 0 ? "1-8" : "9-16");
            resized();
            repaint();   // chrome only: which 8 track buttons the header shows
        };
        addAndMakeVisible(trackPageBtn_);

        // Track header: selector + mute/solo.
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            const auto ti = static_cast<std::size_t>(i);

            trackBtns_[ti].setButtonText(juce::String(i + 1));
            trackBtns_[ti].setClickingTogglesState(false);
            trackBtns_[ti].setWantsKeyboardFocus(false);
            trackBtns_[ti].onClick = [this, i] { keyboardArea_.setActiveTrack(i); };
            // semantic colour: transparent, because a track cell is a WINDOW -- the
            // per-track VU (drawn behind in paint()) shows through it, and the number
            // paints on top. A chrome fill here would paint over the meter.
            trackBtns_[ti].setColour(juce::TextButton::buttonColourId,
                                     juce::Colours::transparentBlack);
            trackBtns_[ti].setColour(juce::TextButton::buttonOnColourId,
                                     juce::Colours::transparentBlack);
            trackBtns_[ti].setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            trackBtns_[ti].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            addAndMakeVisible(trackBtns_[ti]);

            muteBtns_[ti].setButtonText("M");
            muteBtns_[ti].setWantsKeyboardFocus(false);
            addAndMakeVisible(muteBtns_[ti]);
            muteAttachments_[ti] =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    proc.apvts(), ParamIDs::trackMute(i), muteBtns_[ti]);

            soloBtns_[ti].setButtonText("S");
            soloBtns_[ti].setWantsKeyboardFocus(false);
            addAndMakeVisible(soloBtns_[ti]);
            soloAttachments_[ti] =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    proc.apvts(), ParamIDs::trackSolo(i), soloBtns_[ti]);
        }
        trackBtns_[0].setToggleState(true, juce::dontSendNotification);

        keyboardArea_.onActiveTrackChanged = [this](int newTrack) {
            // Dismiss any sticky meta band so it never silently edits a stale track.
            if (uiState_.masterSection != -1)
                uiState_.masterSection = -1;
            refreshMetaBand();

            // Auto-flip page when the active track changes bank.
            const int newPage = (newTrack >= 8) ? 1 : 0;
            if (newPage != trackPage_)
            {
                trackPage_ = newPage;
                trackPageBtn_.setButtonText(trackPage_ == 0 ? "1-8" : "9-16");
                resized();
            }
            for (auto& b : trackBtns_)
                b.setToggleState(false, juce::dontSendNotification);
            trackBtns_[static_cast<std::size_t>(newTrack)].setToggleState(
                true, juce::dontSendNotification);
            keyboardArea_.syncToActiveTrack();
        };

        displayModeBtn_.setWantsKeyboardFocus(false);
        displayModeBtn_.onClick = [this] {
            applyDisplayMode(static_cast<GridDisplayMode>(
                (static_cast<int>(gridMode_) + 1) % 3));
        };
        addAndMakeVisible(displayModeBtn_);
        // MHX.5: vertical crossfader — A=0 (bottom), B=1 (top); fader rest = A.
        crossfader_.setSliderStyle(juce::Slider::LinearBarVertical);
        crossfader_.setRange(0.0, 1.0, 0.0);
        crossfader_.setValue(1.0, juce::dontSendNotification);  // top = A (f=0)
        crossfader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        crossfader_.setColour(juce::Slider::trackColourId,
                              juce::Colour::fromRGB(100, 80, 200).withAlpha(0.6f));
        crossfader_.setWantsKeyboardFocus(false);
        crossfader_.setTooltip("Morph crossfader (0=A / 1=B); right-click to MIDI-learn");
        crossfader_.onValueChange = [this] {
            // Slider is inverted: top (1.0) = A (f=0), bottom (0.0) = B (f=1).
            processor_.setMorphFader(1.0f - static_cast<float>(crossfader_.getValue()));
        };
        crossfader_.addMouseListener(this, false);
        addAndMakeVisible(crossfader_);

        addAndMakeVisible(manipulationZone_);
        addAndMakeVisible(keyboardArea_);

        greyoutLayer_.onPaint = [this](juce::Graphics& g) {
            const juce::Colour emptyGrey{ juce::uint32(0x66444444u) };
            g.setColour(emptyGrey);

            // (A) per-empty-track strip = number button ∪ mute ∪ solo.
            for (std::size_t t = 0; t < kNumTracks; ++t)
            {
                if (!trackBtns_[t].isVisible()) continue;
                if (!processor_.isTrackEmpty(static_cast<int>(t))) continue;
                g.fillRect(trackBtns_[t].getBounds().getUnion(muteBtns_[t].getBounds()).getUnion(soloBtns_[t].getBounds()));
            }

            // (B) focused track empty → grey the edit area (not the strip rows).
            const int at = keyboardArea_.getActiveTrack();
            if (at >= 0 && at < static_cast<int>(kNumTracks) && processor_.isTrackEmpty(at))
            {
                g.fillRect(manipulationZone_.getBounds().getUnion(crossfader_.getBounds()));
                g.fillRect(keyboardArea_.getBounds());
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.setFont(11.0f);
                g.drawFittedText("EMPTY  --  Func+Track: choose machine  |  Track+here: copy",
                                 keyboardArea_.getBounds().reduced(8, 4),
                                 juce::Justification::centred, 2);
            }
        };
        addAndMakeVisible(greyoutLayer_);

        poolBtn_.setWantsKeyboardFocus(false);
        poolBtn_.onClick = [this] {
            poolOverlay_.setVisible(!poolOverlay_.isVisible());
            if (poolOverlay_.isVisible())
                poolOverlay_.toFront(false);
        };
        addAndMakeVisible(poolBtn_);

        poolOverlay_.onClose = [this] { poolOverlay_.setVisible(false); };
        poolOverlay_.getActiveTrack = [this]() { return keyboardArea_.getActiveTrack(); };
        // 9.23 S6: Props… on a pool row opens the sticky sample-properties band.
        // Close the pool overlay, enter Overlay::SampleProps on the chosen index,
        // and refresh through the single-owner surface path so the MZ shows it.
        poolOverlay_.onEditProps = [this](int poolIndex) {
            if (poolIndex < 0 || poolIndex >= processor_.samplePool().size())
                return;
            poolOverlay_.setVisible(false);
            uiState_.overlay = Overlay::SampleProps;
            uiState_.samplePropsPoolIndex = poolIndex;
            refreshMetaBand();
            refreshSurface();
        };
        // 9.24 S16: pick-IR mode — route the chosen sample to the armed convolution
        // insert slot's IR ref (v31). The processor pushes the PCM to the live
        // effect; the convolution's IR-select defaults to Pool so it takes effect.
        poolOverlay_.onPickIr = [this](int poolIndex) {
            if (poolIndex < 0 || poolIndex >= processor_.samplePool().size()) return;
            const SampleId id = processor_.samplePool().idOf(poolIndex);
            if (irPickTargetMaster_)
            {
                if (irPickTargetSlot_ >= 2)
                    processor_.setMasterSendIrRef(irPickTargetSlot_ - 2, id);
                else
                    processor_.setMasterInsertIrRef(irPickTargetSlot_, id);
            }
            else
            {
                processor_.setTrackInsertIrRef(irPickTargetTrack_, irPickTargetSlot_, id);
            }
            poolOverlay_.setPickIrMode(false);
            setStatus("IR set: " + processor_.samplePool().displayName(poolIndex));
            refreshMetaBand();
            refreshSurface();
        };
        addChildComponent(poolOverlay_);

        soundBankBtn_.setWantsKeyboardFocus(false);
        soundBankBtn_.onClick = [this] {
            soundBankOverlay_.setVisible(!soundBankOverlay_.isVisible());
            if (soundBankOverlay_.isVisible())
                soundBankOverlay_.toFront(false);
        };
        addAndMakeVisible(soundBankBtn_);

        soundBankOverlay_.onClose = [this] { soundBankOverlay_.setVisible(false); };
        soundBankOverlay_.getActiveTrack = [this]() { return keyboardArea_.getActiveTrack(); };
        soundBankOverlay_.onStatus = [this](const juce::String& msg) { setStatus(msg); };
        addChildComponent(soundBankOverlay_);

        manipulationZone_.setUiState(&uiState_);
        manipulationZone_.onOpenPoolManager = [this] {
            poolOverlay_.setVisible(true);
            poolOverlay_.toFront(false);
        };

        // 9.18: surface samples that could not be located when a project loads, so
        // the user knows to relink them (pool identity is by content hash, so a
        // single relink heals every reference to that sample). Marshal to the
        // message thread — some hosts call setStateInformation off it.
        processor_.onStateLoaded = [this] {
            juce::Component::SafePointer<LockstepEditor> self(this);
            juce::MessageManager::callAsync([self] {
                if (self == nullptr) return;
                const int missing = self->processor_.missingSampleCount();
                if (missing > 0)
                    self->setStatus(juce::String(missing)
                                    + (missing == 1 ? " sample missing" : " samples missing")
                                    + " — open the pool (Manage) to relink");
            });
        };
        // The live generator previews rewrite the phrase under the user's hands, so
        // each edit must reach the grid *and* the controllers: one refreshSurface()
        // does both (its frame repaints chrome + grid), so no bare repaint() here.
        manipulationZone_.onEuclidParamChanged = [this] {
            if (uiState_.euclidHeld && euclidTrack_ >= 0)
            {
                applyEuclidLive(euclidTrack_);
                refreshSurface();
            }
        };
        manipulationZone_.onMelodyParamChanged = [this] {
            if (uiState_.melodicHeld && melodicTrack_ >= 0)
            {
                applyMelodyLive(melodicTrack_);
                refreshSurface();
            }
        };
        manipulationZone_.onHarmonyParamChanged = [this] {
            if (uiState_.harmonyHeld && harmonyTrack_ >= 0)
            {
                applyHarmonyLive(harmonyTrack_);
                auditionHarmonyCursorChord();   // re-strike on any edit (voice/MOVE/OCT/CUR)
                refreshSurface();
            }
        };
        // 9.14: Step-Position encoder write → canonical surface refresh so the moved
        // step previews in realtime (same feedback the nav ←/→ keys give).
        manipulationZone_.onStepPositionChanged = [this] { refreshSurface(); };
        // Wire mouse button events from KeyboardArea through the unified dispatch.
        keyboardArea_.onButtonDown = [this](ControllerEvent ev) {
            pressTracker_.press(PressTracker::kMouseSource, ev.button, ev.index);
            dispatchDown(ev, PressTracker::kMouseSource);
            refreshSurface();
        };
        keyboardArea_.onButtonUp = [this](ControllerEvent ev) {
            pressTracker_.release(PressTracker::kMouseSource);
            dispatchUp(ev, PressTracker::kMouseSource);
            refreshSurface();
        };

        // Give KeyboardArea a pointer to the shared PressTracker so its paint
        // methods can query pressed state from both keyboard and mouse sources.
        keyboardArea_.setPressTracker(&pressTracker_);


        setSize(990, 626);  // MHX 4x2 MZ band; S8: +22 permanent timeline strip row
        setWantsKeyboardFocus(true);

        // Controller surfaces (DESIGN §35).
        xTouchSurface_ = std::make_unique<XTouchMiniSurface>();
        push1Surface_ = std::make_unique<Push1Surface>();

        controllerPorts_.onStateChange = [this](bool open) {
            setStatus(open ? "Controller: X-Touch Mini connected"
                           : "Controller: X-Touch Mini disconnected");
            // Connect/disconnect is an event: render a frame so a freshly-opened
            // surface gets its initial LED state (renderSurface fires onConnect).
            refreshSurface();
        };

        push1Ports_.onStateChange = [this](bool open) {
            setStatus(open ? "Controller: Ableton Push 1 connected"
                           : "Controller: Ableton Push 1 disconnected");
            refreshSurface();
        };

        startTimerHz(30);  // diagnostic VU meters / activity blinks

        // Display-synced playhead (9.15). Attaches when the editor gets a peer.
        playheadVBlank_ = std::make_unique<juce::VBlankAttachment>(
            this, [this] { onPlayheadVBlank(); });
    }

    LockstepEditor::~LockstepEditor()
    {
        // Must outlive nothing: a LookAndFeel has to be detached before it dies, or
        // children repaint through a dangling pointer on the way out.
        setLookAndFeel(nullptr);
        // Stop vblank callbacks before any members it touches are destroyed.
        playheadVBlank_.reset();
        // 9.18: drop the post-load hook so finishStateLoad never calls into a
        // destroyed editor (the lambda captures `this`).
        processor_.onStateLoaded = nullptr;
        processor_.apvts().removeParameterListener(ParamIDs::syncMode, this);
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            processor_.apvts().removeParameterListener(ParamIDs::trackMute(i), this);
            processor_.apvts().removeParameterListener(ParamIDs::trackSolo(i), this);
            processor_.apvts().removeParameterListener(ParamIDs::trackLength(i), this);
            processor_.apvts().removeParameterListener(ParamIDs::trackDivider(i), this);
        }
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
    }

    void LockstepEditor::parameterChanged(const juce::String& paramID, float /*newValue*/)
    {
        if (paramID == ParamIDs::syncMode)
        {
            juce::MessageManager::callAsync(
                [safe = juce::Component::SafePointer<LockstepEditor>(this)]
                { if (safe != nullptr) safe->updateTransportGhosting(); });
            return;
        }
        // Mute/solo (VU colour + track-cell mute state) or length/divider (grid
        // layout) changed — via track-bar buttons, step-grid slider attachment,
        // host automation, or MIDI learn. Refresh the surface so screen and
        // controllers update; this is what replaced KeyboardArea's length poll.
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            if (paramID == juce::String(ParamIDs::trackMute(i))
                || paramID == juce::String(ParamIDs::trackSolo(i))
                || paramID == juce::String(ParamIDs::trackLength(i))
                || paramID == juce::String(ParamIDs::trackDivider(i)))
            {
                juce::MessageManager::callAsync(
                    [safe = juce::Component::SafePointer<LockstepEditor>(this)]
                    { if (safe != nullptr) safe->refreshSurface(); });
                return;
            }
        }
    }

    void LockstepEditor::renderControllers()
    {
        const bool xtOpen = (xTouchSurface_ && controllerPorts_.isOpen());
        const bool p1Open = (push1Surface_ && push1Ports_.isOpen());
        if (!xtOpen && !p1Open)
            return;

        // Single build — the same buildSurfaceModel() the screen paint path calls,
        // so screen and controllers cannot diverge (DESIGN §35.8.1 / §35.9.1).
        const auto model = buildSurfaceModel(uiState_,
                                             processor_.editContext(),
                                             &pressTracker_,
                                             processor_,
                                             keyboardArea_.getActiveTrack(),
                                             keyboardArea_.currentPage(),
                                             gridMode_,
                                             manipulationZone_.slotOffset(),
                                             1.0f - processor_.morphFader(),
                                             buildMorphViewState());
        if (xtOpen)
            controllerPorts_.renderSurface(*xTouchSurface_, model);
        if (p1Open)
            push1Ports_.renderSurface(*push1Surface_, model);
    }

    void LockstepEditor::renderSurfaceFrame()
    {
        // The single frame producer (DESIGN §35.9.1): chrome + grid + controllers.
        // These three are the *only* direct paint calls; everywhere else routes
        // through refreshSurface() so screen and controllers move together.
        repaint();
        keyboardArea_.repaint();
        renderControllers();
        // 9.15: the ManipulationZone tracks param state from the surface frame
        // instead of its own perpetual 30 Hz poll. Safe mid-drag (runs outside
        // onValueChange; rotaries ignore setValue while dragging).
        manipulationZone_.refreshSliders();
    }

    void LockstepEditor::onPlayheadVBlank()
    {
        // The on-screen sequencer playhead is driven by the audio loop: the
        // processor publishes the focused track's current step each block
        // (focusStepUi), and we repaint the grid here — display-synced — only
        // when it advances. So the highlight moves on the same clock that fires
        // the notes, the grid rebuilds once per step (not once per vblank), and a
        // stopped transport (static step) issues no repaints. Grid only: the
        // controller playhead is on the always-on timer so it survives the vblank
        // pausing when the display sleeps or the window is hidden.
        const int step = processor_.focusStepUi();
        if (step == lastScreenStep_)
            return;
        lastScreenStep_ = step;
        keyboardArea_.repaint();
    }

    // ── Performance capture (tape deck) ───────────────────────────────────────
    // Execute a CaptureController decision: the controller is pure, so all IO
    // (arming the WAV writer, flushing, deleting files, revealing folders) and
    // the lifecycle callbacks happen here.
    void LockstepEditor::runCaptureOut(const CaptureController::Out& out)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        if (out.start)
        {
            if (processor_.startCapture())
            {
                captureController_.onStarted(now);
                captureLastFile_ = processor_.captureFile();
                captureStartMs_  = now;
                // D: a take is a directory (master.wav + stems); show its name.
                setStatus(status::captureArmed(captureLastFile_.getParentDirectory().getFileName()));
            }
            else
            {
                captureController_.onStartFailed();
                setStatus(status::captureFailed());
            }
        }
        if (out.finalize)
        {
            const juce::RelativeTime dur = processor_.stopCapture();
            captureController_.onFinalized(now);
            const int totalSec = static_cast<int>(dur.inSeconds());
            const juce::String durStr = juce::String(totalSec / 60)
                + ":" + juce::String(totalSec % 60).paddedLeft('0', 2);
            setStatus(status::captureDisarmed(durStr,
                                              captureLastFile_.getParentDirectory().getFileName()));
        }
        if (out.discard)
        {
            // D: discard the whole take directory (master.wav + any stems).
            const juce::File takeDir = captureLastFile_.getParentDirectory();
            if (takeDir.isDirectory())
                takeDir.deleteRecursively();
            captureLastFile_ = juce::File();
            setStatus(status::captureDiscarded());
        }
        if (out.reveal)
        {
            // Reveal the Captures root (parent of the per-take directories).
            const juce::File dir = captureLastFile_.existsAsFile()
                ? captureLastFile_.getParentDirectory().getParentDirectory()
                : chooseCaptureFile(processor_.currentProjectFile())
                      .getParentDirectory().getParentDirectory();
            dir.createDirectory();
            dir.revealToUser();
        }
        refreshSurface();
    }

    void LockstepEditor::captureExitGuard(std::function<void()> next)
    {
        using Phase = CaptureController::Phase;
        const Phase ph = captureController_.phase();
        if (ph == Phase::Armed)
        {
            runCaptureOut(captureController_.onTap(false));  // disarm silently
            next();
            return;
        }
        if (ph != Phase::Recording)
        {
            next();
            return;
        }
        // A take is rolling — confirm before quitting, ahead of the project guard.
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Recording in progress")
                .withMessage("A performance capture is still recording.")
                .withButton("Stop recording & exit")
                .withButton("Discard recording & exit")
                .withButton("Cancel"),
            [this, next = std::move(next)](int result) mutable {
                if (result == 1)        // Stop & exit — finalise the take
                {
                    runCaptureOut(captureController_.onDoubleTap());
                    next();
                }
                else if (result == 2)   // Discard & exit — drop the partial file
                {
                    processor_.stopCapture();
                    if (captureLastFile_.existsAsFile())
                        captureLastFile_.deleteFile();
                    captureController_.onFinalized(
                        juce::Time::getMillisecondCounterHiRes());
                    next();
                }
                // result 3 / 0 = Cancel — stay open, keep recording.
            });
    }

    static juce::Colour meterColour(float level);  // fwd decl (defined below)

    // Stereo dB VU meter with peak high-water mark and a clip pip. Persistent —
    // always present, so levels read at a glance.
    // 9.30 st.5: the master meter is VERTICAL, in the MZ's right flank beside the
    // crossfader. That flank is now the output column and reads top-to-bottom as the
    // signal does: SCULPT (the MZ) -> BLEND (the crossfader) -> LEVEL (this).
    //
    // The old full-width strip across the top of the window was not there because a
    // horizontal meter is better; it was there because it was built first. Its one real
    // virtue was being the only full-width attention lane on the surface, which is why
    // the capture banner ended up living on it — a role the STATUS lane now serves on
    // purpose (§42.2) rather than by accident.
    void LockstepEditor::paintMasterMeter(juce::Graphics& g)
    {
        const auto strip = masterChromeRegion_;
        if (strip.isEmpty()) return;

        g.setColour(juce::Colour::fromRGB(24, 27, 33));
        g.fillRect(strip);

        // The VOL chip sits under the meter: the level you are giving away, beneath the
        // level you are getting. 9.31: the meter is also the master fader — drag it,
        // double-click to return to 0 dB — so the chip is now the readout of a control
        // you can see, rather than the only trace of one you could not.
        constexpr int kChipH = 12;
        auto column = strip.reduced(1, 2);
        const auto chip = column.removeFromBottom(kChipH);

        const int meterH = column.getHeight();

        // dB mapping: -kMeterFloorDb..0 dBFS bottom-to-top.
        auto dbToPx = [meterH](float lin) {
            const float db = lin > 1.0e-5f ? 20.0f * std::log10(lin) : -120.0f;
            const float n = juce::jlimit(0.0f, 1.0f, (db + kMeterFloorDb) / kMeterFloorDb);
            return juce::roundToInt(n * static_cast<float>(meterH));
        };

        // Faint dB gridlines. No labels: the column is 14 px wide, and a legend nobody
        // can read is just noise — the ticks give the shape, the clip pip gives the
        // only number that matters in performance.
        static constexpr int kTicks[] = { -24, -12, -6, 0 };
        for (const int db : kTicks)
        {
            const int ty = column.getBottom()
                         - dbToPx(std::pow(10.0f, static_cast<float>(db) / 20.0f));
            g.setColour(juce::Colour::fromRGBA(120, 130, 145, 60));
            g.fillRect(column.getX(), ty, column.getWidth(), 1);
        }

        const int barW = (column.getWidth() - 1) / 2;
        const float levelL = juce::jlimit(0.0f, 1.5f, masterMeter_);
        const float levelR = juce::jlimit(0.0f, 1.5f, masterMeterR_);

        auto drawBar = [&](int x, float level, float hold, bool clip) {
            g.setColour(juce::Colour::fromRGB(15, 17, 21));
            g.fillRect(x, column.getY(), barW, meterH);
            const int h = dbToPx(level);
            if (h > 0)
            {
                g.setColour(meterColour(juce::jlimit(0.0f, 1.0f, level)));
                g.fillRect(x, column.getBottom() - h, barW, h);
            }
            // Peak high-water mark.
            const int hy = dbToPx(hold);
            if (hy > 1)
            {
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.fillRect(x, column.getBottom() - hy - 1, barW, 2);
            }
            // Clip pip, at the TOP — where the ceiling is.
            if (clip)
            {
                g.setColour(juce::Colours::red);
                g.fillRect(x, column.getY(), barW, 3);
            }
        };
        drawBar(column.getX(), levelL, masterPeakHoldL_, masterClipL_);
        drawBar(column.getX() + barW + 1, levelR, masterPeakHoldR_, masterClipR_);

        const float gainDb = processor_.apvts().getRawParameterValue(ParamIDs::outputGain)->load();

        // Gain tick across the meter, on the meter's own dB scale — so "where the
        // fader is" and "how hot the signal is" are read against one ruler. A boosted
        // gain (> 0 dB) pins to the top, where the scale runs out; the chip has the
        // number.
        {
            const int ty = meter::gainTickY(gainDb, column.getY(), meterH, kMeterFloorDb);
            g.setColour(juce::Colour::fromRGBA(235, 240, 245, 190));
            g.fillRect(column.getX(), ty, column.getWidth(), 2);
        }

        g.setColour(juce::Colour::fromRGBA(10, 12, 15, 205));
        g.fillRect(chip);
        g.setFont(juce::Font(juce::FontOptions(8.0f)));
        g.setColour(gainDb > 0.01f ? juce::Colour::fromRGB(232, 200, 120)
                                   : juce::Colour::fromRGBA(175, 185, 200, 220));
        g.drawText(juce::String(gainDb, 1), chip, juce::Justification::centred);
    }

    // Compact capture banner, floating in the right margin of the master strip.
    // Persistent part is small (REC + elapsed); the filename/path fades in only
    // at arm / record-start (captureDetailUntilMs_) and persists while just-saved,
    // so it never occludes the meter during the performance.
    // 9.30 st.3: the Sg:Sc identity, painted in the transport band. It is a READOUT,
    // not a control — the old header grew a dashboard precisely by giving every readout
    // a Label component and then hand-placing it at an absolute x.
    void LockstepEditor::paintTransportBand(juce::Graphics& g)
    {
        if (songSceneRegion_.isEmpty()) return;

        const int sg = processor_.activePieceIdx() + 1;
        const int sc = processor_.activeSectionIdx() + 1;

        g.setColour(juce::Colour(0xFF1A1E26u));
        g.fillRoundedRectangle(songSceneRegion_.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFF9AB0C8u));
        g.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
        g.drawText("Sg " + juce::String(sg) + " : Sc " + juce::String(sc),
                   songSceneRegion_, juce::Justification::centred);
    }

    // 9.30 st.4: the controller-connection indicator, in the rail that is actually
    // about "what am I plugged into". This kills the bottom banner — status number six.
    void LockstepEditor::paintProjectRail(juce::Graphics& g)
    {
        if (controllerIndicatorRegion_.isEmpty()) return;

        juce::String text;
        if (controllerPorts_.isOpen()) text = "X-TOUCH MINI";
        if (push1Ports_.isOpen())      text = text.isEmpty() ? "PUSH 1" : text + " + PUSH 1";

        const bool any = text.isNotEmpty();
        g.setColour(any ? juce::Colour(0xFF2A7A50u) : juce::Colour(0xFF2A2E38u));
        const auto dot = juce::Rectangle<float>(
            static_cast<float>(controllerIndicatorRegion_.getX()),
            controllerIndicatorRegion_.toFloat().getCentreY() - 3.0f, 6.0f, 6.0f);
        g.fillEllipse(dot);

        g.setColour(any ? juce::Colour(0xFF80C8A0u) : juce::Colour(0xFF6A6E78u));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(any ? text : juce::String("no controller"),
                   controllerIndicatorRegion_.withTrimmedLeft(12),
                   juce::Justification::centredLeft, true);
    }

    void LockstepEditor::paintCaptureStrip(juce::Graphics& g)
    {
        using Phase = CaptureController::Phase;
        const Phase ph = captureController_.phase();
        if (ph == Phase::Idle) return;

        const double now = juce::Time::getMillisecondCounterHiRes();
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now) * 0.006f);
        const bool showDetail = (now < captureDetailUntilMs_) || ph == Phase::JustSaved;

        juce::String label, elapsed, detail;
        juce::Colour dot;
        switch (ph)
        {
            case Phase::Armed:
                label  = "ARMED";
                dot    = juce::Colours::red.withAlpha(0.35f + 0.5f * pulse);
                // 11.11 (S1): name the outcome before the take, not the morning
                // after — how many stems this arm would keep, alongside the master.
                detail = "starts on Play " + juce::String(juce::CharPointer_UTF8("\xc2\xb7"))
                       + " " + stemOutcomeText(processor_.stemmableCount());
                break;
            case Phase::Recording:
            {
                const int sec = static_cast<int>((now - captureStartMs_) / 1000.0);
                elapsed = juce::String(sec / 60) + ":" + juce::String(sec % 60).paddedLeft('0', 2);
                if (captureController_.windingDown())
                {
                    label  = "STOPPING";
                    dot    = juce::Colour(0xFFFFB020u);
                    detail = "waiting for silence";
                }
                else
                {
                    label  = "REC";
                    dot    = juce::Colours::red;
                    // Live count: a track that joins mid-take is kept (11.11), so
                    // the number can only grow while the tape rolls.
                    detail = captureLastFile_.getFileName()
                           + "  " + stemOutcomeText(processor_.stemsKeptCount());
                }
                break;
            }
            case Phase::JustSaved:
                label  = "SAVED";
                dot    = juce::Colour(0xFF40C060u);
                detail = captureFolderDisplayPath();
                break;
            case Phase::Idle:
                return;
        }

        // Compose the right-floating text: "<label> <elapsed>" always; append the
        // detail (filename / folder path) only while the detail window is open.
        juce::String head = label;
        if (elapsed.isNotEmpty()) head += "  " + elapsed;
        const juce::String text = (showDetail && detail.isNotEmpty())
                                      ? head + "   " + detail : head;

        // 9.30 st.3: this indicator now lives in the TRANSPORT band, not on the master
        // VU strip. It is the highest-stakes status in the core CUJ (a live set going to
        // stems), and it was sitting in peripheral vision on the one element the eye
        // treats as decoration. It now sits beside the Play button that starts the take.
        if (captureIndicatorRegion_.isEmpty()) return;

        g.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
        // The detail (filename / folder path) is longer than the indicator: let it
        // overflow LEFTWARD across the band. That is a deliberate, brief occlusion of a
        // readout — never of a control (§42.3's rule, applied here too).
        auto area = captureIndicatorRegion_;
        if (showDetail)
            area = area.withLeft(transportBandRegion_.getX() + 200).withTrimmedRight(0);

        g.setColour(juce::Colour(0xFF14181Fu));
        g.fillRoundedRectangle(area.toFloat(), 3.0f);

        g.setColour(dot);
        g.fillEllipse(static_cast<float>(area.getX() + 4),
                      area.toFloat().getCentreY() - 4.0f, 8.0f, 8.0f);

        g.setColour(juce::Colours::white);
        g.drawText(text, area.withTrimmedLeft(16), juce::Justification::centredLeft, true);
    }

    // Folder path for the SAVED banner: project-relative when a project is open,
    // else the ~/Music/Lockstep/Captures home. Filename appended.
    juce::String LockstepEditor::captureFolderDisplayPath() const
    {
        const juce::File f = captureLastFile_.existsAsFile()
            ? captureLastFile_
            : chooseCaptureFile(processor_.currentProjectFile());
        const juce::String home = juce::File::getSpecialLocation(
                                      juce::File::userHomeDirectory).getFullPathName();
        juce::String p = f.getFullPathName();
        if (p.startsWith(home)) p = "~" + p.substring(home.length());
        return p;
    }

    // 11.11 (S1): the take's outcome in words. "Routing is the stem grouping"
    // (DESIGN §41.3), so this number is how the performer reads their routing
    // back before committing to a take.
    juce::String LockstepEditor::stemOutcomeText(int stemCount)
    {
        if (stemCount <= 0) return "master only";
        return "master + " + juce::String(stemCount)
             + (stemCount == 1 ? " stem" : " stems");
    }

    void LockstepEditor::timerCallback()
    {
        // S7: pump the FreeLen background-bake orchestration — queue a stretch for
        // any newly-engaged streaming fit and adopt completed bakes (decoupled from
        // the transport; the loop streams correctly until the bake lands).
        processor_.pollLoopBakes();

        // A2: flash the status line when the engine refused / dormant-marked a
        // routing edit (DESIGN §27). Decoupled poll: any seq bump = a new notice.
        {
            const auto seq = processor_.routeRejectSeq();
            if (seq != lastRouteRejectSeq_)
            {
                lastRouteRejectSeq_ = seq;
                using RR = LockstepProcessor::RouteReject;
                const int trk = processor_.routeRejectTrack();           // 0-based
                const juce::String trkName = "Trk" + juce::String(trk + 1);
                switch (processor_.routeRejectReason())
                {
                    case RR::Cycle:
                        setStatus("Out: would feed back — routing refused");
                        break;
                    case RR::NoAudioInput:
                        setStatus("Out: " + trkName + " has no audio input — route into a Route track");
                        break;
                    case RR::Self:
                        setStatus("Out: a track can't route to itself");
                        break;
                    case RR::Dormant:
                        setStatus(trkName + " is no longer a bus — inbound routing is dormant");
                        break;
                    case RR::None:
                        break;
                }
            }
        }

        // C3: keep the persistent missing-sample banner current. missingSampleCount
        // is a cheap flag scan (no disk IO — rescanMissing does the stat on pool
        // open); recomputing every tick lets a relink clear the banner immediately.
        {
            const int missing = processor_.missingSampleCount();
            if (missing != missingSampleBanner_)
            {
                missingSampleBanner_ = missing;
                repaint();   // chrome only: the missing-sample banner has no cell state
            }
        }

        // Generator hub (9.10): promote a held 3-key to the hub picker after 350 ms.
        if (tapTempoPhysHeld_ && !uiState_.generatorHubHeld)
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (nowMs - tapTempoArmMs_ >= GestureRecognizer::kLongPressMs)
            {
                // 9.12 st.7f: the table already says TAP + hold = GEN HUB. Fire the row,
                // don't set the flag: the hub re-skins the step grid, so a controller
                // must be able to open it through the same door (PRINCIPLES §19).
                fireGesture(ControllerButton::TapTempo, -1, Gesture::Hold);
            }
        }

        // 9.12 st.7d: a section picker opens MID-HOLD (not on key-up) so it appears
        // while the key is still down and stays open. ONE block for every picker now —
        // the table decided which one at arm time, so adding a picker adds no code here.
        if (sectionHoldAction_ != ActionId::None && !sectionHoldFired_)
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (gesture_.longPressElapsed(kSectionHoldToken, nowMs))
            {
                sectionHoldFired_ = true;
                fireSectionHold();
            }
        }

        // 9.29: the deletion picker opens mid-hold, so the user sees "DELETE WHICH
        // TRACK?" while the key is still down rather than after letting go — the hold
        // announces itself, which is what makes it safe to put a destructive verb on.
        if (clearHoldArmed_ && !clearHoldFired_)
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (gesture_.longPressElapsed(kDeleteHoldToken, nowMs))
            {
                clearHoldFired_ = true;
                fireDeleteHold();
            }
        }

        // Multi-step hold (Part 2): long-press a LONE held step → StepInspector.
        // Fires mid-hold so the P-Lock overview appears while still held. Two-plus
        // held steps never open it (a bare multi-hold stays a pure edit context);
        // any param write or a move during the hold cancels the arm (wasParamWritten).
        {
            auto& ctx = processor_.editContext();
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (!stepInspectorFiredMidHold_ && !uiState_.pLockClearMode
                && ctx.heldSteps().size() == 1 && !ctx.wasParamWritten())
            {
                const int step = ctx.heldStepIndex();
                if (gesture_.longPressElapsed(step, nowMs))
                {
                    stepInspectorFiredMidHold_ = true;
                    openStepInspector(ctx.heldTrackIndex(), step);
                }
            }
        }

        // ── Performance capture ───────────────────────────────────────────────
        // CAPTURE long-press fires mid-hold (discard in the just-saved window /
        // reveal folder when idle), then the per-tick finalize rule runs.
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (captureCellHeld_ && !captureLongPressFired_
                && gesture_.longPressElapsed(kCaptureToken, nowMs))
            {
                captureLongPressFired_ = true;
                runCaptureOut(captureController_.onLongPress());
            }
            // Hold-Record = transport reset (rewind), fired mid-hold so it lands
            // while held rather than on release.
            if (recordResetHeld_ && !recordResetFired_
                && gesture_.longPressElapsed(kTransportResetToken, nowMs))
            {
                recordResetFired_ = true;
                processor_.transportStopReset();
                setStatus(status::transportReset());
            }
            // Stage 1b: surface a lossy Tape depth down-convert as a status toast.
            if (processor_.consumeTapeDepthWarn())
                setStatus(juce::String("Reel down-converted to 16-bit (lossy)"));

            // S4: looper console REC/DUB cell held past the long-press threshold →
            // engage momentary punch-replace (fires while held; ended on release).
            if (looperReplaceTrack_ >= 0 && !looperReplaceFired_
                && gesture_.longPressElapsed(kLooperReplaceToken, nowMs))
            {
                looperReplaceFired_ = true;
                processor_.sendLooperPerf(looperReplaceTrack_, 12 /*ReplacePunch*/, true);
                setStatus(juce::String("Loop: punch-replace"));
                refreshSurface();
            }
            const float capPeak = std::max(processor_.masterPeak(), processor_.masterPeakR());
            const bool capPlaying = processor_.clock().inPluginPlaying();
            const auto prevPhase = captureController_.phase();
            const auto capOut = captureController_.tick(nowMs, capPeak, capPlaying);
            if (capOut.start || capOut.finalize)
                runCaptureOut(capOut);
            // Detail-window: the filename/path fades in on entering Armed /
            // Recording, then the banner is compact for the take.
            const auto ph = captureController_.phase();
            if (ph != lastCapturePhase_)
            {
                if (ph == CaptureController::Phase::Armed
                    || ph == CaptureController::Phase::Recording)
                    captureDetailUntilMs_ = nowMs + 3000.0;
                lastCapturePhase_ = ph;
            }
            // Keep the feedback strip live (elapsed timer). The capture *cell*'s
            // state changes invalidate the surface on their own; this only drives
            // the readout under the master meter.
            if (ph != CaptureController::Phase::Idle
                || prevPhase != CaptureController::Phase::Idle)
                repaint();   // chrome only: the capture feedback strip's elapsed clock
        }

        // Peak meters: fast attack, slow ballistic decay. Activity blinks: a
        // pulse from the audio thread snaps to 1.0, then decays each tick.
        // Only repaint if any value actually changed; floor tiny values to zero
        // so decay terminates and the repaint loop stops when transport is idle.
        static constexpr float kMeterFloor = 0.001f;
        static constexpr float kBlinkFloor = 0.005f;
        bool dirty = false;

        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const float peak = processor_.trackPeak(static_cast<int>(i));
            const float newMeter = std::max(peak, trackMeter_[i] * 0.80f);
            const float floored = (newMeter < kMeterFloor) ? 0.0f : newMeter;
            if (std::abs(floored - trackMeter_[i]) > 0.0f)
            {
                trackMeter_[i] = floored;
                dirty = true;
            }

            if (processor_.takeTrigPulse(static_cast<int>(i)) > 0.5f)
            {
                trigBlink_[i] = 1.0f;
                dirty = true;
            }
            else if (trigBlink_[i] > 0.0f)
            {
                trigBlink_[i] = (trigBlink_[i] > kBlinkFloor) ? trigBlink_[i] * 0.70f : 0.0f;
                dirty = true;
            }

            // MIDI-out VU (Part 3): the velocity-loudness strip meter — same
            // audio-like ballistics as trackMeter_ (attack to the new peak, 0.80
            // decay per tick, floor). Only meaningful for MIDI-out tracks (audio
            // tracks keep their trackMeter_ VU).
            const float midiAct = processor_.takeMidiActivity(static_cast<int>(i));
            const float newMidi = std::max(midiAct, midiLevel_[i] * 0.80f);
            const float flooredMidi = (newMidi < kMeterFloor) ? 0.0f : newMidi;
            if (std::abs(flooredMidi - midiLevel_[i]) > 0.0f)
            {
                midiLevel_[i] = flooredMidi;
                dirty = true;
            }

            // The former MIDI dot now signals CC activity.
            if (processor_.takeMidiCcPulse(static_cast<int>(i)) > 0.5f)
            {
                ccBlink_[i] = 1.0f;
                dirty = true;
            }
            else if (ccBlink_[i] > 0.0f)
            {
                ccBlink_[i] = (ccBlink_[i] > kBlinkFloor) ? ccBlink_[i] * 0.70f : 0.0f;
                dirty = true;
            }
        }

        const float newMasterL  = std::max(processor_.masterPeak(),  masterMeter_  * 0.80f);
        const float newMasterR  = std::max(processor_.masterPeakR(), masterMeterR_ * 0.80f);
        const float flooredL    = (newMasterL < kMeterFloor) ? 0.0f : newMasterL;
        const float flooredR    = (newMasterR < kMeterFloor) ? 0.0f : newMasterR;
        if (std::abs(flooredL - masterMeter_) > 0.0f || std::abs(flooredR - masterMeterR_) > 0.0f)
        {
            masterMeter_  = flooredL;
            masterMeterR_ = flooredR;
            dirty = true;
        }

        // Master peak high-water mark: jump to a new peak, hold ~1.5 s, then decay.
        // Clip latch: trips at 0 dBFS, auto-clears ~2.5 s after the last clip.
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            auto trackHold = [&](float raw, float& hold, double& holdMs,
                                 bool& clip, double& clipMs) {
                // Strict `>`: at idle raw==hold==0, so `>=` would latch a new
                // "peak" every tick and force a 30 Hz repaint in silence. `>`
                // only refreshes the high-water mark on a genuinely higher peak.
                if (raw > hold)         { hold = raw; holdMs = nowMs; dirty = true; }
                else if (nowMs - holdMs > 1500.0)
                {
                    const float decayed = hold * 0.90f;
                    if (decayed != hold) { hold = (decayed < kMeterFloor) ? 0.0f : decayed; dirty = true; }
                }
                if (raw >= 0.999f)      { clip = true; clipMs = nowMs; dirty = true; }
                else if (clip && nowMs - clipMs > 2500.0) { clip = false; dirty = true; }
            };
            trackHold(processor_.masterPeak(),  masterPeakHoldL_, peakHoldMsL_, masterClipL_, clipMsL_);
            trackHold(processor_.masterPeakR(), masterPeakHoldR_, peakHoldMsR_, masterClipR_, clipMsR_);
        }

        // `dirty` above is chrome-only animation (meters/blinks) → a cheap chrome
        // repaint. `modelDirty` is a change to the surface *model* — it must drive
        // the whole frame (grid + controllers), so it routes through the
        // invalidation channel (DESIGN §35.9). Stage 4 moves the meter decay onto
        // a dedicated animation clock; Stage 3 replaces these polls with the
        // audio-thread discrete bridge.
        bool modelDirty = false;

        // Transport state change: PLAY/PAUSE label + controller transport.
        const bool nowPlaying = processor_.clock().inPluginPlaying();
        if (nowPlaying != lastPlayingState_)
        {
            lastPlayingState_ = nowPlaying;
            modelDirty = true;
        }

        // 9.15: the transport widget reads its state from this always-on tick
        // (shadow-gated, no-op when unchanged) instead of its own timer — so
        // host-driven play/rec/metronome changes still surface.
        transport_.pollState();

        // Morph fader: detect on-screen crossfader moves so controller surfaces update.
        const float curMorphFader = processor_.morphFader();
        if (curMorphFader != lastMorphFader_)
        {
            lastMorphFader_ = curMorphFader;
            modelDirty = true;
        }

        // The SCREEN playhead is driven by the display vblank (onPlayheadVBlank).
        // The CONTROLLER playhead must keep moving even when the vblank is paused
        // (screen asleep, window hidden) — the performer reads the hardware, not
        // the screen (§19) — so it is driven here, on the always-on timer, from
        // the same audio clock. Timer-limited (there is no vblank for MIDI LEDs).
        const double ppq = processor_.clock().cumulativePpq();
        const bool ppqMoved = (ppq != lastCtrlPpq_);
        lastCtrlPpq_ = ppq;

        // 9.15 Stage 3: the audio thread applied a queued param change (CC /
        // encoder / P-Lock write). It is async, so this is the discrete event
        // that settles the value on screen and on controllers — no per-tick poll.
        if (processor_.takeSurfaceDirty())
            modelDirty = true;

        // Push the morph view state to KeyboardArea so its paint() gets current slot states.
        keyboardArea_.setMorphViewState(buildMorphViewState(),
                                        manipulationZone_.slotOffset(),
                                        1.0f - processor_.morphFader());

        // modelDirty covers everything (chrome + grid + controllers). Otherwise
        // the controller playhead (ppqMoved) and the chrome meter/blink decay
        // (dirty) are independent — both may need to fire on the same tick, so
        // they are NOT mutually exclusive.
        if (modelDirty)
        {
            refreshSurface();           // chrome + grid + controllers
        }
        else
        {
            if (ppqMoved)
                renderControllers();    // controller playhead — screen is on the vblank
            if (dirty)
            {
                // Scoped repaint: the meter/blink decay only touches the master
                // strip + the track/VU row, so invalidate just those bands rather
                // than the whole editor (a full 30 Hz repaint of the grid /
                // keyboard / MZ was the idle-CPU / fan culprit).
                repaint(masterChromeRegion_);
                repaint(trackRowChromeRegion_);
            }
            // 9.30 st.3: the capture indicator moved to the transport band, so its
            // scoped repaint must move with it — a pulsing dot in a region nobody
            // invalidates is a dot that does not pulse (PRINCIPLES §22: the region and
            // the pixels are one decision).
            if (captureController_.phase() != CaptureController::Phase::Idle)
                repaint(transportBandRegion_);   // chrome only: the capture indicator animates
        }

        // Update the scope-coloured tempo + time-sig readout.
        {
            const auto effTs = processor_.effectiveTimeSig();
            const double effBpm = processor_.effectiveBpm();
            // 9.30 st.3: the KEY joins the time readout. It is a time-group fact — the
            // other thing that is true of every note you are about to play — and it was
            // displayed NOWHERE in the chrome, despite Phase 10 building a whole
            // key-signature hierarchy behind it.
            const juce::String keyName{ juce::CharPointer_UTF8(
                classicalName(processor_.effectiveKeySig()).c_str()) };
            const juce::String readout =
                juce::String(static_cast<int>(std::round(effBpm))) + " BPM  "
                + juce::String(effTs.numerator) + "/" + juce::String(effTs.denominator)
                + "  " + keyName;
            // Colour by the scope that currently owns the resolved tempo:
            // Scene owns if scene has tempo, Song owns if song has, else global (Song colour).
            const auto& sc = processor_.section();
            const auto& sg = processor_.song();
            juce::Colour readoutColour;
            if (sc.hasTempo || sc.hasTimeSig)
                readoutColour = scopeColour(EditMode::PrimaryScope::Scene);
            else if (sg.hasTempo || sg.hasTimeSig)
                readoutColour = scopeColour(EditMode::PrimaryScope::Song);
            else
                readoutColour = juce::Colours::grey;
            tempoReadout_.setColour(juce::Label::textColourId, readoutColour);
            tempoReadout_.setText(readout, juce::dontSendNotification);
        }

        // 9.11 + 9.30: refresh the inspector, including the STATUS lane. The toast and
        // the missing-sample count are the only status inputs the model cannot derive
        // from UiState — everything else in the lane is STATE, read straight from it,
        // which is exactly why an armed confirm can no longer fade out from under the
        // user (§42.2). Rebuilt every tick: the lane IS the state, rendered.
        {
            StatusInput si;
            si.toast = statusMessage_;
            si.toastAgeMs = juce::Time::getMillisecondCounter() - statusSetMs_;
            si.toastDurationMs = kStatusDurationMs;
            si.missingSamples = missingSampleBanner_;
            // 9.30 st.2: the badges that used to live in the header dashboard. The
            // checkpoint depth is asked of the scope the user is HOLDING — the same
            // scope its verbs would act on — which is what makes it worth showing.
            si.clipboard = clipboard_.type;
            // 9.14 st.5: the verbs' operand. Steps and section keys are scopes but not
            // modifiers, so UiState alone cannot see them — the lane gets it from the
            // same EditMode the verbs dispatch through.
            si.scope = editMode_.primaryScope();
            {
                int ckTrk = 0;
                const CheckpointScope cs = ckScope(ckTrk);
                si.checkpointDepth = processor_.checkpointDepth(cs, ckTrk);
                si.undoDepth = processor_.undoDepth(cs, ckTrk);
            }
            inspectorBar_.setModel(buildInspectorModel(
                uiState_, processor_.editContext(), processor_,
                lastFocusedButton_, lastFocusedIndex_, si));

            // The pop-over is painted by the editor (it extends out of the child's
            // bounds, over the MZ), so the editor must repaint when it appears or
            // disappears. Edge-triggered: no repaint while it merely persists.
            const bool confirmUp =
                inspectorBar_.model().statusKind == StatusKind::Confirm;
            if (confirmUp != confirmPopoverUp_)
            {
                confirmPopoverUp_ = confirmUp;
                repaint(confirmPopoverRegion_);   // chrome only: the confirm pop-over carries no cell state
            }
            else if (confirmUp)
            {
                repaint(confirmPopoverRegion_);   // chrome only: re-derived every frame, by design (§42.2)
            }
        }

        // S8 (§40.6): the timeline strip is ALWAYS on — its row is permanent, so no
        // appear/disappear relayout. Rebuild the model each tick (cheap) so the
        // cursor + captions track the transport with or without a tape.
        {
            const double barPpq = processor_.effectiveTimeSig().barPpq();
            const double spb = processor_.clock().samplesPerPpq() * barPpq;
            timelineStrip_.setModel(
                buildTimelineModel(processor_, spb, barPpq, processor_.getSampleRate()));
        }

        // Controller *input* only — drain encoder/button MIDI every tick, since
        // input must be serviced even when nothing is redrawing (DESIGN §35.9.3:
        // input ≠ render). Feedback LEDs are rendered from renderSurfaceFrame()
        // (the invalidation channel's onFrame), not polled here. Controller input
        // is itself an event: if anything was drained it may have moved a param,
        // so mark the surface dirty once so the device's LED rings re-render the
        // new value. Writes are message-thread-synchronous (encoder deltas must
        // accumulate), so a frame after the drain reads the settled value.
        if ((xTouchSurface_ && controllerPorts_.isOpen()) || (push1Surface_ && push1Ports_.isOpen()))
        {
            auto sink = buildControllerSink();
            bool drainedInput = false;
            if (xTouchSurface_ && controllerPorts_.isOpen())
                drainedInput |= controllerPorts_.drainInput(*xTouchSurface_, sink);
            if (push1Surface_ && push1Ports_.isOpen())
                drainedInput |= push1Ports_.drainInput(*push1Surface_, sink);
            if (drainedInput)
                refreshSurface();
        }

        // Reconcile: release any keyboard press whose key is no longer physically
        // down (catches stuck modifiers/steps after Alt-Tab or window deactivation).
        std::vector<std::pair<int, ControllerEvent>> toRelease;
        pressTracker_.forEachReleasedKeyboard([&](int src, ControllerButton btn, int idx) {
            toRelease.push_back({ src, { ControllerEvent::Type::ButtonUp, btn, idx, 0 } });
        });
        if (!toRelease.empty())
        {
            std::erase_if(heldKeys_, [](int code) {
                return !juce::KeyPress::isKeyCurrentlyDown(code);
            });
            for (auto& [src, relEv] : toRelease)
            {
                pressTracker_.release(src);
                dispatchUp(relEv, src);
            }
            refreshSurface();   // a stuck key was released: held state cleared everywhere
        }
    }

    // Maps a linear meter level [0,1] to a green→yellow→red colour.
    static juce::Colour meterColour(float level)
    {
        if (level < 0.5f) return juce::Colour::fromRGB(60, 200, 90);
        if (level < 0.85f) return juce::Colour::fromRGB(220, 200, 60);
        return juce::Colour::fromRGB(230, 80, 60);
    }

    // MIDI-out VU (Part 3): a distinct magenta-family gradient so a MIDI-out
    // track's velocity meter reads differently from an audio track's VU at a glance.
    static juce::Colour midiMeterColour(float level)
    {
        if (level < 0.5f) return juce::Colour::fromRGB(150, 60, 190);
        if (level < 0.85f) return juce::Colour::fromRGB(210, 70, 210);
        return juce::Colour::fromRGB(240, 90, 200);
    }

    void LockstepEditor::paintMeters(juce::Graphics& g)
    {
        // Determine which tracks are silenced (muted or solo-excluded) so the VU
        // can flag them — a silenced track's machine is skipped entirely, which
        // is a common cause of "no sound" confusion.
        // Read each track's solo once into a local; reused for the any-soloed
        // scan and the per-track classification below (this paints on the 30 Hz
        // timer, so avoid the duplicate APVTS lookups).
        std::array<bool, kNumTracks> soloFlags{};
        bool anySoloed = false;
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const auto* p = processor_.apvts().getRawParameterValue(
                ParamIDs::trackSolo(static_cast<int>(i)));
            soloFlags[i] = p && p->load() >= 0.5f;
            anySoloed = anySoloed || soloFlags[i];
        }

        // Routing-group classification (WS3 / DESIGN §27). edges[i] = the bus
        // track i feeds, or -1 (Master/Off). A track that is some other track's
        // dest is a BUS; one whose own edge is >= 0 is a FEEDER. Each bus gets a
        // palette colour by ascending index; feeders borrow their bus's colour
        // dimmed. A track that is both (a chain link) shows its own bus colour.
        const auto edges = processor_.routingEdges();
        std::array<int, kNumTracks> busColourIdx{};
        busColourIdx.fill(-1);
        {
            int next = 0;
            for (std::size_t b = 0; b < kNumTracks; ++b)
            {
                bool isBus = false;
                for (std::size_t k = 0; k < kNumTracks; ++k)
                    if (edges[k] == static_cast<int>(b)) { isBus = true; break; }
                if (isBus)
                {
                    busColourIdx[b] = next % static_cast<int>(theme::kRoutingGroup.size());
                    ++next;
                }
            }
        }
        auto groupColourFor = [&](std::size_t i, bool& isBus) -> juce::Colour {
            isBus = false;
            if (busColourIdx[i] >= 0)  // i is itself a bus
            {
                isBus = true;
                return juce::Colour(theme::kRoutingGroup[static_cast<std::size_t>(busColourIdx[i])]);
            }
            const int bus = edges[i];  // i feeds a bus → borrow its colour, dimmed
            if (bus >= 0 && busColourIdx[static_cast<std::size_t>(bus)] >= 0)
                return juce::Colour(theme::kRoutingGroup[
                           static_cast<std::size_t>(busColourIdx[static_cast<std::size_t>(bus)])])
                    .withMultipliedBrightness(0.55f);
            return {};  // ungrouped
        };

        // Per-track VU underlaid behind the (transparent) track-number buttons.
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const int t = static_cast<int>(i);
            const bool gMuted = processor_.getGlobalMute(t);
            const bool pMuted = processor_.getPatternMute(t);
            const bool soloed = soloFlags[i];
            const bool soloEx = anySoloed && !soloed;

            const auto r = trackBtns_[i].getBounds();
            if (r.isEmpty()) continue;

            bool isBus = false;
            const juce::Colour groupCol = groupColourFor(i, isBus);
            const bool hasGroup = !groupCol.isTransparent();

            // Distinct background per silencing source: global mute = red,
            // pattern mute = orange, solo-exclusion = purple, soloed = teal,
            // audible = grey. In the plain audible state the grey is blended
            // toward the routing-group colour so groups read at a glance; a
            // silencing state keeps its dedicated colour (group still shows on
            // the underline below).
            juce::Colour bg = juce::Colour::fromRGB(28, 32, 38);
            if (gMuted) bg = juce::Colour::fromRGB(70, 20, 20);
            else if (pMuted) bg = juce::Colour::fromRGB(80, 50, 16);
            else if (soloEx) bg = juce::Colour::fromRGB(50, 24, 70);
            else if (soloed) bg = juce::Colour::fromRGB(20, 70, 50);
            else if (hasGroup) bg = bg.interpolatedWith(groupCol, 0.25f);  // default state only
            g.setColour(bg);
            g.fillRect(r);

            // MIDI-out tracks meter their velocity-loudness (magenta) rather than
            // audio peak — their machine produces no audio, so trackMeter_ is 0.
            const bool midiOut = processor_.isMidiOutTrack(t);
            const float level = juce::jlimit(0.0f, 1.0f,
                                             midiOut ? midiLevel_[i] : trackMeter_[i]);
            if (level > 0.001f)
            {
                const int fillW = juce::roundToInt(static_cast<float>(r.getWidth()) * level);
                const auto col = midiOut ? midiMeterColour(level) : meterColour(level);
                g.setColour(col.withAlpha(0.55f));
                g.fillRect(r.getX(), r.getY(), fillW, r.getHeight());
            }

            // Level tick (9.31): where this track's fader sits, as opposed to how
            // loud it happens to be playing. The VU fill answers "is it making
            // sound"; the tick answers "how far down did I turn it" -- which used
            // to be answerable only by paging to the AMP section, or by feeling for
            // an invisible drag gesture. Readout only: the MIXER band plays it.
            {
                const int lvlSlot = processor_.levelSlotForTrack(t);
                if (lvlSlot >= 0)
                {
                    const auto spec = processor_.paramSpec(t, lvlSlot);
                    const int tx = meter::levelTickX(processor_.baseParamValue(t, lvlSlot),
                                                     spec.maxValue, r.getWidth());
                    g.setColour(juce::Colour::fromRGBA(235, 240, 245, 170));
                    g.fillRect(r.getX() + tx, r.getY() + 1, 2, r.getHeight() - 2);
                }
            }

            // Routing-group underline (always-on channel) — visible regardless
            // of the mute/solo background so a muted feeder still shows its group.
            if (hasGroup)
            {
                g.setColour(groupCol);
                g.fillRect(r.getX(), r.getBottom() - 3, r.getWidth(), 3);
            }

            // Active-track outline so selection survives the transparent button.
            if (t == keyboardArea_.getActiveTrack())
            {
                g.setColour(juce::Colour::fromRGB(90, 160, 230));
                g.drawRect(r, 2);
            }

            // Machine type badge: abbreviated type in top-right corner for non-sampler tracks.
            {
                const juce::String badge{ processor_.trackBadge(t) };
                if (badge.isNotEmpty())
                {
                    g.setColour(juce::Colour::fromRGB(180, 185, 190).withAlpha(0.85f));
                    g.setFont(9.0f);
                    g.drawText(badge, r.reduced(1).withHeight(10).withTrimmedRight(6),
                               juce::Justification::topRight, false);
                }
            }
        }
    }

    void LockstepEditor::updateTransportGhosting()
    {
        const bool isStandalone =
            (juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone);
        const auto* modeParam =
            processor_.apvts().getRawParameterValue(ParamIDs::syncMode);
        const bool isLocked = modeParam && static_cast<int>(modeParam->load()) == 0;
        const bool ghost = isLocked && !isStandalone;
        transport_.setGhosted(ghost);
    }

    void LockstepEditor::parentHierarchyChanged()
    {
        auto* newTop = getTopLevelComponent();
        if (newTop == keyListenerTarget_)
            return;
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
        keyListenerTarget_ = newTop;
        if (keyListenerTarget_ != nullptr && keyListenerTarget_ != this)
            keyListenerTarget_->addKeyListener(this);

        // D2: wire dirty guard to the standalone window's close-button callback.
        // fileBar_ is only constructed in standalone mode; the cast is also guarded
        // by JucePlugin_Build_Standalone so this block is elided in plugin builds.
#if JucePlugin_Build_Standalone
        if (fileBar_)
        {
            if (auto* w = dynamic_cast<juce::StandaloneFilterWindow*>(newTop))
            {
                // Capture fileBar_ by raw pointer (editor outlives the window).
                auto* fb = fileBar_.get();
                w->onCloseRequested = [this, fb](std::function<void()> doQuit)
                {
                    // Capture-exit dialog first (if recording), then the project
                    // dirty-save guard.
                    captureExitGuard([fb, dq = std::move(doQuit)]() mutable
                    {
                        fb->withDirtyGuard(std::move(dq));
                    });
                };
            }
        }
#endif
    }

    void LockstepEditor::focusLost(FocusChangeType /*cause*/)
    {
        // Release any held mouse state so a focus-loss mid-click doesn't leave
        // a modifier or step stuck (keyboard is reconciled in timerCallback).
        if (const auto entry = pressTracker_.mouseEntry())
        {
            pressTracker_.release(PressTracker::kMouseSource);
            dispatchUp({ ControllerEvent::Type::ButtonUp, entry->button, entry->index, 0 },
                       PressTracker::kMouseSource);
            refreshSurface();
        }
    }

    void LockstepEditor::mouseDown(const juce::MouseEvent& e)
    {
        // Master-VU drag (9.31): the master level is the one level with no cell of
        // its own, so its meter is its fader. The per-track VU drags this replaced
        // are gone -- the MIXER band is where track levels are played.
        masterDrag_ = {};
        if (e.eventComponent == this && masterChromeRegion_.contains(e.getPosition()))
        {
            if (auto* p = processor_.apvts().getParameter(ParamIDs::outputGain))
            {
                if (e.getNumberOfClicks() >= 2)
                {
                    // Double-click = back to unity. A gain you can push is a gain you
                    // need to be able to un-push without hunting for zero.
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1(0.0f));
                    p->endChangeGesture();
                    repaint(masterChromeRegion_);  // chrome only: gain tick follows the reset
                    return;
                }
                masterDrag_.active = true;
                masterDrag_.startDb =
                    processor_.apvts().getRawParameterValue(ParamIDs::outputGain)->load();
                masterDrag_.startY = e.getScreenY();
                p->beginChangeGesture();
                return;
            }
        }

        if (e.eventComponent != &crossfader_ || !e.mods.isRightButtonDown())
            return;
        // Right-click on crossfader: show MIDI-learn / clear menu (DESIGN §17.5).
        const auto existing = [&]() -> std::pair<bool, int> {
            for (const auto& m : processor_.ccMappingTable().mappings())
                if (m.scope == CCScope::Crossfader)
                    return { true, m.ccNumber };
            return { false, -1 };
        }();

        juce::PopupMenu menu;
        if (existing.first)
        {
            menu.addSectionHeader("CC " + juce::String(existing.second) + " mapped (crossfader)");
            menu.addItem(1, "Clear mapping");
        }
        else
        {
            menu.addSectionHeader("Crossfader MIDI Learn:");
            menu.addItem(1, "Map crossfader via MIDI Learn");
        }
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(crossfader_),
            [this, existing](int result) {
                if (result == 0)
                    return;
                if (existing.first)
                {
                    processor_.ccMappingTable().removeMapping(
                        existing.second, CCScope::Crossfader, -1, -1);
                }
                else
                {
                    processor_.startLearn(CCScope::Crossfader, -1, -1);
                }
            });
    }

    void LockstepEditor::mouseDrag(const juce::MouseEvent& e)
    {
        if (!masterDrag_.active) return;
        auto* p = processor_.apvts().getParameter(ParamIDs::outputGain);
        if (p == nullptr) return;

        const auto range = processor_.apvts().getParameterRange(ParamIDs::outputGain);
        const int dy = masterDrag_.startY - e.getScreenY();  // up = louder
        const float db = meter::masterDragDb(masterDrag_.startDb, dy, range.start, range.end);
        p->setValueNotifyingHost(p->convertTo0to1(db));
        repaint(masterChromeRegion_);  // chrome only: gain tick tracks the master drag
    }

    void LockstepEditor::mouseUp(const juce::MouseEvent& /*e*/)
    {
        if (masterDrag_.active)
        {
            if (auto* p = processor_.apvts().getParameter(ParamIDs::outputGain))
                p->endChangeGesture();
        }
        masterDrag_ = {};
    }

    void LockstepEditor::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(20, 22, 26));
        paintMeters(g);  // per-track VU underlaid behind the track buttons
    }

    void LockstepEditor::paintOverChildren(juce::Graphics& g)
    {
        // Persistent per-track state overlays FIRST — they must show in every mode
        // (incl. the unmodified resting state). The held-context preview below
        // early-returns when nothing is held, so these have to precede it.

        // ---- Master section (persistent — must precede the held-context early
        // return, else it vanishes at rest): dB VU meter + capture banner ----
        paintMasterMeter(g);
        paintProjectRail(g);
        paintTransportBand(g);
        paintCaptureStrip(g);

        // 9.30 §42.3: the confirm pop-over. Drawn OVER the children, extending down out
        // of the STATUS lane and across the top of the MZ. It occludes on purpose --
        // while a confirm is pending every key either confirms or cancels it, so nothing
        // under it is a live target, and a destructive decision must never be made by
        // accident because we did not spot the message.
        InspectorBar::paintConfirmPopover(g, confirmPopoverRegion_, inspectorBar_.model());
        // Per-track trig (left, cyan) + MIDI-CC (right, magenta) activity dots.
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const auto r = trackBtns_[i].getBounds();
            if (r.isEmpty()) continue;
            constexpr int d = 5;
            if (trigBlink_[i] > 0.02f)
            {
                // White: high contrast against the green/yellow/red VU bar.
                g.setColour(juce::Colours::white.withAlpha(trigBlink_[i]));
                g.fillEllipse(static_cast<float>(r.getX() + 2),
                              static_cast<float>(r.getY() + 2), d, d);
            }
            // Top-right dot now signals CC activity (MIDI note velocity moved to
            // the full-strip magenta meter behind the button).
            if (ccBlink_[i] > 0.02f)
            {
                g.setColour(juce::Colour::fromRGB(230, 80, 220).withAlpha(ccBlink_[i]));
                g.fillEllipse(static_cast<float>(r.getRight() - 2 - d),
                              static_cast<float>(r.getY() + 2), d, d);
            }
        }

        // ---- Crossfader A/B endpoint labels: inset 25% from each end toward
        // the centre so they sit inside the slider and don't clip adjacent UI.
        {
            static const juce::Colour kMorphMagenta{ 0xffb060d0 };
            const auto fb = crossfader_.getBounds();
            const int labelH = 12;
            const int inset = fb.getHeight() / 4;  // 25% of fader height
            g.setFont(juce::Font(juce::FontOptions(9.0f)).boldened());
            g.setColour(kMorphMagenta.withAlpha(0.9f));
            g.drawText("A", fb.getX(), fb.getY() + inset - labelH / 2, fb.getWidth(), labelH,
                       juce::Justification::centred);
            g.setColour(kMorphMagenta.darker(0.3f).withAlpha(0.9f));
            g.drawText("B", fb.getX(), fb.getBottom() - inset - labelH / 2, fb.getWidth(), labelH,
                       juce::Justification::centred);
        }

        // ---- Deviation badge: an amber corner triangle on every track playing
        // off the scene's diagonal home row (DESIGN §4.7) — persistent in the
        // track / VU row, visible in every mode (no modifier needed).
        {
            const int home = processor_.activeSectionIdx();
            g.setColour(juce::Colour(juce::uint32(0xFFFFC020u)));
            for (std::size_t t = 0; t < kNumTracks; ++t)
            {
                if (!trackBtns_[t].isVisible()) continue;
                const int ti = static_cast<int>(t);
                const int cur = processor_.isTrackDeviated(ti)
                                    ? processor_.deviationPhraseIdxForTrack(ti)
                                    : processor_.activeSectionIdx();
                if (cur == home) continue;
                const auto r = trackBtns_[t].getBounds();
                const float s = 7.0f;
                juce::Path tri;
                tri.addTriangle(static_cast<float>(r.getX()), static_cast<float>(r.getY()),
                                static_cast<float>(r.getX()) + s, static_cast<float>(r.getY()),
                                static_cast<float>(r.getX()), static_cast<float>(r.getY()) + s);
                g.fillPath(tri);
            }
        }

        // (9.30 st.2: the MHZ.2.2 header dashboard and the held-context preview are
        //  GONE — ~320 lines of hand-placed pixels at x 420-640 that duplicated the
        //  inspector's job. The preview narrated the held chord (the inspector's HELD
        //  region) and the active modal (its OVERLAY region), and it painted a hint pill
        //  ON TOP OF the animating mini-sequencer. The badges it carried were real state,
        //  so they moved rather than died: CPY / CK / input-mode now qualify the
        //  inspector's HELD text, where they sit next to the scope they belong to.
        //  Sg:Sc parks here until st.3 gives it a home in the transport+time block.)

        // (Diagnostic meters + activity dots are drawn near the top of this
        // method, in the persistent section — they must precede the early return.)

        if (!isDraggingFiles_)
            return;
        g.setColour(juce::Colour::fromRGB(255, 180, 50).withAlpha(0.12f));
        g.fillAll();
        g.setColour(juce::Colour::fromRGB(255, 180, 50).withAlpha(0.7f));
        g.drawRect(getLocalBounds().reduced(4), 2);
        g.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
        g.drawText("Drop to add to pool",
                   getLocalBounds(),
                   juce::Justification::centred);
    }

    // -------------------------------------------------------------------------
    // FileDragAndDropTarget

    static bool isAudioFile(const juce::String& path)
    {
        const juce::String ext = juce::File(path).getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".ogg";
    }

    bool LockstepEditor::isInterestedInFileDrag(const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (isAudioFile(f)) return true;
        return false;
    }

    void LockstepEditor::fileDragEnter(const juce::StringArray& /*files*/, int /*x*/, int /*y*/)
    {
        isDraggingFiles_ = true;
        repaint();   // chrome only: the file-drag highlight is a window overlay
    }

    void LockstepEditor::fileDragExit(const juce::StringArray& /*files*/)
    {
        isDraggingFiles_ = false;
        repaint();   // chrome only: the file-drag highlight is a window overlay
    }

    void LockstepEditor::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
    {
        isDraggingFiles_ = false;

        const int focus = processor_.focusTrack();

        // Item 6: a drop onto a focused Static (stream) track assigns its streamed
        // source — now a Stream pool entry + the track's sample_id (disk stream, no
        // RAM decode — DESIGN §29.2), not a full pool load.
        if (processor_.isStreamTrack(focus))
        {
            for (const auto& path : files)
            {
                if (!isAudioFile(path)) continue;
                const bool ok = processor_.setStreamFile(focus, path);
                setStatus(ok ? juce::String("Static: ") + juce::File(path).getFileName()
                             : juce::String("Static: could not open file"));
                refreshSurface();
                return;  // one source per Static track
            }
            return;
        }

        // Item 6: a drop onto a focused Flex sampler track (sample_id / slicer
        // sample_id slot) loads into the pool AND assigns the last one to that
        // track's sample slot — so a drag-drop is immediately playable.
        const int sampleSlot = processor_.sampleSlotForTrack(focus);
        int loaded = 0;
        int lastIdx = -1;
        for (const auto& path : files)
        {
            if (!isAudioFile(path)) continue;
            const int idx = processor_.samplePool().load(path);
            if (idx >= 0) { ++loaded; lastIdx = idx; }
        }
        if (loaded > 0 && sampleSlot >= 0 && lastIdx >= 0)
        {
            processor_.writeParam(focus, sampleSlot, static_cast<float>(lastIdx));
            setStatus(juce::String("Loaded ") + juce::String(loaded)
                      + " sample(s); assigned " + processor_.sampleShortName(lastIdx));
        }
        if (loaded > 0)
        {
            poolOverlay_.setVisible(true);
            poolOverlay_.toFront(false);
        }
        refreshSurface();
    }

    void LockstepEditor::refreshMetaBand()
    {
        manipulationZone_.setBand(resolveMetaBand(uiState_), swingScopeFor(uiState_));
    }

    bool LockstepEditor::applyMasterFxPick(int index)
    {
        // 9.24 S12: `index` is the step cell (0..15); the picker is paged and
        // context-filtered, so translate cell -> catalogue index through the model.
        if (index < 0 || index >= 16) return true;
        // 8.26: units 0-1 = master inserts, units 2-3 = send returns.
        const int mUnit = uiState_.masterFxInsertSlot;
        const bool isSend = (mUnit >= 2);
        const int mSlot = isSend ? mUnit - 2 : mUnit;
        const FxPickerCtx ctx = isSend ? FxPickerCtx::MasterSend
                                       : FxPickerCtx::MasterInsert;
        const int cat = fxPickerCellToCatalogue(ctx, uiState_.fxPickerPage, index);
        if (cat < 0) return true;
        const auto info = processor_.availableEffectInfo(cat);
        const std::string curId = isSend ? processor_.masterSendId(mSlot)
                                         : processor_.masterInsertId(mSlot);
        if (info.id == curId)
        {
            // Re-pick the loaded effect → toggle bypass.
            const bool byp = isSend ? processor_.masterSendBypass(mSlot)
                                    : processor_.masterInsertBypass(mSlot);
            if (isSend)
                processor_.setMasterSendBypass(mSlot, !byp);
            else
                processor_.setMasterInsertBypass(mSlot, !byp);
        }
        else
        {
            if (isSend)
            {
                processor_.setMasterSend(mSlot, info.id);
                processor_.setMasterSendBypass(mSlot, false);
                // Item 5: freshly loaded an External send — explain the DAW routing.
                if (info.sendOnly)
                    maybeWarnExternalSend();
            }
            else
            {
                processor_.setMasterInsert(mSlot, info.id);
                processor_.setMasterInsertBypass(mSlot, false);
            }
        }
        uiState_.masterFxPickerOpen = false;
        refreshMetaBand();
        refreshSurface();
        return true;
    }

    void LockstepEditor::maybeWarnExternalSend()
    {
        // Honour the persisted opt-out; also skip if a warning is already showing.
        if (auto* prefs = appProps_.getUserSettings())
            if (!prefs->getBoolValue("warnExternalSend", true))
                return;
        if (externalSendWarn_ != nullptr)
            return;

        externalSendWarn_ = std::make_unique<juce::AlertWindow>(
            "External send routing",
            "\"Send A\"/\"Send B\" are separate plugin output buses. Your host may "
            "also sum them into this track's main output, doubling the signal.\n\n"
            "In the DAW: route each Send output to its destination (typically "
            "PRE-fader on a send/aux track) and mute it on Lockstep's main output.",
            juce::MessageBoxIconType::InfoIcon, this);

        externalSendWarnToggle_ = std::make_unique<juce::ToggleButton>("Don't warn me again");
        externalSendWarnToggle_->setSize(220, 24);
        externalSendWarn_->addCustomComponent(externalSendWarnToggle_.get());
        externalSendWarn_->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        externalSendWarn_->enterModalState(
            true,
            juce::ModalCallbackFunction::create([this](int) {
                if (externalSendWarnToggle_ != nullptr
                    && externalSendWarnToggle_->getToggleState())
                {
                    if (auto* prefs = appProps_.getUserSettings())
                    {
                        prefs->setValue("warnExternalSend", false);
                        prefs->saveIfNeeded();
                    }
                }
                // Destroy the toggle before the window that parents it.
                externalSendWarnToggle_.reset();
                externalSendWarn_.reset();
            }),
            false);
    }

    bool LockstepEditor::applyTrackFxPick(int index)
    {
        // 9.24 S12: `index` is the step cell (0..15); masterOnly/sendOnly effects
        // are compacted out of the track picker, so map cell -> catalogue index.
        if (index < 0 || index >= 16) return true;
        const int at = keyboardArea_.getActiveTrack();
        if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;
        const int cat = fxPickerCellToCatalogue(FxPickerCtx::TrackInsert,
                                                uiState_.fxPickerPage, index);
        if (cat < 0) return true;
        const auto info = processor_.availableEffectInfo(cat);
        const std::string curId = processor_.trackInsertId(at, uiState_.funcFxInsertSlot);
        if (info.id == curId)
        {
            const bool byp = processor_.trackInsertBypass(at, uiState_.funcFxInsertSlot);
            processor_.setTrackInsertBypass(at, uiState_.funcFxInsertSlot, !byp);
        }
        else
        {
            processor_.setTrackInsert(at, uiState_.funcFxInsertSlot, info.id);
            processor_.setTrackInsertBypass(at, uiState_.funcFxInsertSlot, false);
        }
        uiState_.funcFxHeld = false;
        refreshSurface();
        return true;
    }

    // True when the picker catalogue cell at `index` holds the effect currently
    // loaded in the picker's target slot (the "reselect" cell). A tap on it
    // toggles bypass; a long-press removes the effect (see fxPickerStepDown).
    bool LockstepEditor::fxPickerCellIsLoaded(int index, bool master) const
    {
        // 9.24 S12: `index` is the step cell; resolve it to a catalogue entry via
        // the same paged/filtered model the render and apply paths use.
        const int mUnit = uiState_.masterFxInsertSlot;
        const bool isSend = master && (mUnit >= 2);
        const FxPickerCtx ctx = !master     ? FxPickerCtx::TrackInsert
                              : isSend       ? FxPickerCtx::MasterSend
                                             : FxPickerCtx::MasterInsert;
        const int cat = fxPickerCellToCatalogue(ctx, uiState_.fxPickerPage, index);
        if (cat < 0) return false;
        const auto info = processor_.availableEffectInfo(cat);
        if (master)
        {
            const int mSlot = isSend ? mUnit - 2 : mUnit;
            const std::string curId = isSend ? processor_.masterSendId(mSlot)
                                             : processor_.masterInsertId(mSlot);
            return !curId.empty() && info.id == curId;
        }
        const int at = keyboardArea_.getActiveTrack();
        if (at < 0 || at >= static_cast<int>(kNumTracks)) return false;
        const std::string curId = processor_.trackInsertId(at, uiState_.funcFxInsertSlot);
        return !curId.empty() && info.id == curId;
    }

    // Remove (clear) the effect in the picker's currently targeted slot. Used by
    // the long-press-remove gesture on the loaded catalogue cell.
    void LockstepEditor::removeFxPickerSlot(bool master)
    {
        if (master)
        {
            const int mUnit = uiState_.masterFxInsertSlot;
            const bool isSend = (mUnit >= 2);
            const int mSlot = isSend ? mUnit - 2 : mUnit;
            if (isSend) processor_.clearMasterSend(mSlot);
            else        processor_.clearMasterInsert(mSlot);
        }
        else
        {
            const int at = keyboardArea_.getActiveTrack();
            if (at < 0 || at >= static_cast<int>(kNumTracks)) return;
            processor_.clearTrackInsert(at, uiState_.funcFxInsertSlot);
        }
        refreshMetaBand();
        refreshSurface();
    }

    // Step-down entry for the FX picker. On the loaded cell we arm a long-press
    // and defer to key-up (tap = bypass, long-press = remove); every other cell
    // selects its effect immediately. Returns true when the event is consumed.
    bool LockstepEditor::fxPickerStepDown(int index, bool master)
    {
        if (fxPickerCellIsLoaded(index, master))
        {
            gesture_.armLongPress(kFxPickerCellLongPressToken,
                                  juce::Time::getMillisecondCounterHiRes());
            fxPickerRemoveArmed_  = true;
            fxPickerRemoveMaster_ = master;
            return true;  // resolved on key-up in fxPickerStepUp
        }
        return master ? applyMasterFxPick(index) : applyTrackFxPick(index);
    }

    // Step-up resolution for a deferred loaded-cell press. Long-press removes the
    // effect; a tap falls through to the normal reselect (bypass toggle). Returns
    // true when it handled an armed press.
    bool LockstepEditor::fxPickerStepUp(int index)
    {
        if (!fxPickerRemoveArmed_) return false;
        fxPickerRemoveArmed_ = false;
        const bool master = fxPickerRemoveMaster_;
        using LPR = GestureRecognizer::LongPressResult;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (gesture_.checkLongPress(kFxPickerCellLongPressToken, now) == LPR::LongHold)
            removeFxPickerSlot(master);
        else
            (void) (master ? applyMasterFxPick(index) : applyTrackFxPick(index));
        return true;
    }

    void LockstepEditor::openMachineConsole()
    {
        uiState_.machineConsoleOpen = true;
        // 7c: for a Route track, snapshot every track's committed output dest into
        // the scratch buffer so edits can be staged and reverted.
        const int at = keyboardArea_.getActiveTrack();
        if (processor_.machineForTrack(at) != nullptr
            && std::string(processor_.machineForTrack(at)->machineId()) == RouteMachine::kMachineId)
        {
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                uiState_.routeScratch[static_cast<std::size_t>(t)] =
                    processor_.kit(t).channelState.out;
            uiState_.routeConsoleActive = true;
        }
    }

    void LockstepEditor::closeMachineConsole()
    {
        // 7c revert: discard the routing scratch (Confirm applies it separately
        // before closing). 7b base close: clear the open flag.
        uiState_.routeConsoleActive = false;
        uiState_.machineConsoleOpen = false;
    }

    void LockstepEditor::commitRouteConsole()
    {
        // 7c Confirm: apply each staged dest that differs from the committed value
        // through the validated CHANNEL-Out path, then close (scratch discarded).
        if (uiState_.routeConsoleActive)
        {
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                const float staged = uiState_.routeScratch[static_cast<std::size_t>(t)];
                if (std::abs(staged - processor_.kit(t).channelState.out) > 0.5f)
                    processor_.applyTrackOut(t, staged);
            }
        }
        closeMachineConsole();
    }

    void LockstepEditor::cycleRouteConsoleCell(int track)
    {
        // 7c: advance one track's staged dest to the next valid target, wrapping.
        if (!uiState_.routeConsoleActive
            || track < 0 || track >= static_cast<int>(kNumTracks))
            return;
        const auto targets = processor_.validOutTargets(track);
        if (targets.empty()) return;
        const float cur = uiState_.routeScratch[static_cast<std::size_t>(track)];
        int idx = 0;
        for (int k = 0; k < static_cast<int>(targets.size()); ++k)
            if (std::abs(targets[static_cast<std::size_t>(k)] - cur) < 0.5f) { idx = k; break; }
        const int next = (idx + 1) % static_cast<int>(targets.size());
        uiState_.routeScratch[static_cast<std::size_t>(track)] =
            targets[static_cast<std::size_t>(next)];
        refreshSurface();
    }

    void LockstepEditor::openFxSectionPicker(bool master)
    {
        // Slot selection now lives on a *tap* of the FX key while the picker is
        // open (cycleFxPickerSlot). Opening only targets slot 0 on a fresh open;
        // a re-hold while already open leaves the current slot untouched.
        if (master)
        {
            if (!uiState_.masterFxPickerOpen)
            {
                uiState_.masterFxInsertSlot = 0;
                uiState_.fxPickerPage = 0;  // 9.24 S12: fresh open starts on page 1
            }
            uiState_.masterFxPickerOpen = true;
            keyboardArea_.selectMetaSection(LockstepProcessor::kFxSecIdx, /*toggle=*/false);
            refreshMetaBand();
        }
        else
        {
            if (!uiState_.funcFxHeld)
            {
                uiState_.funcFxInsertSlot = 0;
                uiState_.fxPickerPage = 0;
            }
            uiState_.funcFxHeld = true;
        }
        refreshSurface();
    }

    // Tap-to-cycle the FX picker's target slot while the picker is open. Master:
    // units 0-3 (inserts 0-1, sends 2-3). Track: two insert slots (0/1). Keeps
    // the picker open — the caller is responsible for the funcFxHeld survival on
    // the track path (suppressFxPickerClose).
    void LockstepEditor::cycleFxPickerSlot(bool master)
    {
        if (master)
        {
            uiState_.masterFxInsertSlot = (uiState_.masterFxInsertSlot + 1) % 4;
            // Send vs insert have different page counts (External only on sends);
            // clamp the current page into the new context's range.
            const bool isSend = uiState_.masterFxInsertSlot >= 2;
            const int pages = fxPickerPageCount(isSend ? FxPickerCtx::MasterSend
                                                       : FxPickerCtx::MasterInsert);
            uiState_.fxPickerPage = std::min(uiState_.fxPickerPage, pages - 1);
            keyboardArea_.selectMetaSection(LockstepProcessor::kFxSecIdx, /*toggle=*/false);
            refreshMetaBand();
        }
        else
        {
            uiState_.funcFxInsertSlot = 1 - uiState_.funcFxInsertSlot;
        }
        refreshSurface();
    }

    // 9.24 S16: if an FX picker is open on a slot holding the convolution reverb,
    // arm pick-IR mode for that slot and open the pool browser. Returns true (Confirm
    // consumed) when it opened the picker; false so Confirm keeps its other roles.
    bool LockstepEditor::tryOpenIrPicker()
    {
        auto arm = [this](bool master, int track, int slot) {
            irPickTargetMaster_ = master;
            irPickTargetTrack_ = track;
            irPickTargetSlot_ = slot;
            poolOverlay_.setPickIrMode(true);
            poolOverlay_.setVisible(true);
            poolOverlay_.toFront(false);
            setStatus("Pick a sample to use as the convolution IR");
        };
        if (uiState_.masterFxPickerOpen)
        {
            const int unit = uiState_.masterFxInsertSlot;
            const std::string id = (unit >= 2) ? processor_.masterSendId(unit - 2)
                                               : processor_.masterInsertId(unit);
            if (id != "lockstep.conv.v1") return false;
            arm(/*master=*/true, 0, unit);
            return true;
        }
        if (uiState_.funcFxHeld)
        {
            const int track = keyboardArea_.getActiveTrack();
            if (track < 0) return false;
            if (processor_.trackInsertId(track, uiState_.funcFxInsertSlot)
                    != "lockstep.conv.v1")
                return false;
            arm(/*master=*/false, track, uiState_.funcFxInsertSlot);
            return true;
        }
        return false;
    }

    // 9.24 S12: page the open FX picker by `delta` (±1), clamped. Returns true
    // when a picker was open (so Nav is consumed as paging); false otherwise, so
    // the caller falls through to Nav's normal roles.
    bool LockstepEditor::pageFxPicker(int delta)
    {
        const bool master = uiState_.masterFxPickerOpen;
        if (!master && !uiState_.funcFxHeld) return false;
        const int mUnit = uiState_.masterFxInsertSlot;
        const FxPickerCtx ctx = !master        ? FxPickerCtx::TrackInsert
                              : (mUnit >= 2)    ? FxPickerCtx::MasterSend
                                                : FxPickerCtx::MasterInsert;
        const int pages = fxPickerPageCount(ctx);
        uiState_.fxPickerPage =
            std::clamp(uiState_.fxPickerPage + delta, 0, pages - 1);
        refreshSurface();
        return true;
    }

    bool LockstepEditor::pageDeckConsole(int delta)
    {
        const int t = keyboardArea_.getActiveTrack();
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return false;
        // Stage 6d: any multi-sub deck pages — the Loop and the Tape both host the
        // shared TRACKS page.
        if (! processor_.isLooperTrack(t) && ! processor_.isTapeTrack(t)) return false;
        // Only a modifier-free Nav pages; Func/Track/held-step keep their roles.
        if (uiState_.funcHeld || uiState_.trackHeld) return false;
        // Pages available: DECK always, TRACKS when the deck has > 1 sub-track.
        // (MARKS for the Tape face will append here.)
        const int pages = 1 + (processor_.looperSubTrackCount(t) > 1 ? 1 : 0);
        if (pages <= 1)
        {
            // Stage 8 discoverability: a single-sub deck has no TRACKS page, so Nav
            // here would silently do nothing. Point at the way in (raise Sub-tracks
            // on SRC) so the multi-track page is findable. Nav still does its usual job.
            setStatus("Raise Sub-tracks (SRC) to reach the TRACKS page");
            return false;  // nothing to page — let Nav do its usual thing
        }
        uiState_.deckConsolePage =
            ((uiState_.deckConsolePage + delta) % pages + pages) % pages;
        refreshSurface();
        return true;
    }

    bool LockstepEditor::activeTrackContentLocked() const
    {
        const int t = keyboardArea_.getActiveTrack();
        return t >= 0 && t < static_cast<int>(kNumTracks) && processor_.isTrackEmpty(t);
    }

    // Returns the first master unit (0-3) that has an effect loaded, skipping empties.
    // Falls back to 0 so the picker remains reachable even on a fresh project.
    int LockstepEditor::firstLoadedMasterUnit() const noexcept
    {
        for (int u = 0; u < 4; ++u)
        {
            const bool isSend = (u >= 2);
            const bool loaded = isSend ? !processor_.masterSendId(u - 2).empty()
                                       : !processor_.masterInsertId(u).empty();
            if (loaded) return u;
        }
        return 0;
    }

    // Returns the next loaded master unit after `current`, wrapping around and
    // skipping empty units. Returns `current` if no other unit is loaded.
    int LockstepEditor::nextLoadedMasterUnit(int current) const noexcept
    {
        for (int step = 1; step <= 4; ++step)
        {
            const int u = (current + step) % 4;
            const bool isSend = (u >= 2);
            const bool loaded = isSend ? !processor_.masterSendId(u - 2).empty()
                                       : !processor_.masterInsertId(u).empty();
            if (loaded) return u;
        }
        return current;  // no other loaded unit found
    }

    void LockstepEditor::updateSwingQualifier()
    {
        refreshMetaBand();
    }

    void LockstepEditor::clearSwingDismissed()
    {
        uiState_.swingDismissed = false;
        updateSwingQualifier();
    }

    void LockstepEditor::applyEuclidToTrack(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& ph = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        const int len = ph.length;
        if (len <= 0) return;

        const auto vels = euclideanAccents(len,
                                           uiState_.euclidPulses,
                                           uiState_.euclidOffset,
                                           uiState_.euclidAccents);
        for (int si = 0; si < len; ++si)
        {
            auto& s = ph.steps[static_cast<std::size_t>(si)];
            const int v = vels[static_cast<std::size_t>(si)];
            s.trig = (v > 0);
            if (v > 0)
            {
                s.trigOverride.hasVelocity = true;
                s.trigOverride.velocity = v;
            }
            else
            {
                s.trigOverride.hasVelocity = false;
            }
        }
    }

    void LockstepEditor::applyEuclidLive(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& ph = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        const int len = ph.length;
        if (len <= 0) return;

        const auto vels = euclideanAccents(len,
                                           uiState_.euclidPulses,
                                           uiState_.euclidOffset,
                                           uiState_.euclidAccents);
        for (int si = 0; si < len; ++si)
        {
            auto& s = ph.steps[static_cast<std::size_t>(si)];
            const int v = vels[static_cast<std::size_t>(si)];
            s.trig = (v > 0);
            if (v > 0)
            {
                s.trigOverride.hasVelocity = true;
                s.trigOverride.velocity = v;
            }
            else
            {
                s.trigOverride.hasVelocity = false;
            }
        }
    }

    // ── Generator hub entry helpers (9.10) ───────────────────────────────────────
    // Called from the hub Step dispatch (cells 0-2). Each helper is the body that
    // used to live in the legacy Phrase+Fill / Func+MOD / Func+AMP entry blocks.

    void LockstepEditor::enterEuclid(int track)
    {
        if (track < 0) track = 0;
        if (activeTrackContentLocked()) return;
        const auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        int onsets = 0;
        for (int si = 0; si < wt.length; ++si)
            if (wt.steps[static_cast<std::size_t>(si)].trig) ++onsets;
        uiState_.euclidPulses = onsets > 0 ? onsets : 4;
        uiState_.euclidOffset = 0;
        uiState_.euclidAccents = 0;
        uiState_.euclidHeld = true;
        uiState_.masterSection = -1;
        euclidTrack_ = track;
        euclidStashLen_ = wt.length;
        for (int si = 0; si < euclidStashLen_; ++si)
            euclidStash_[static_cast<std::size_t>(si)] = wt.steps[static_cast<std::size_t>(si)];
        applyEuclidLive(track);
        refreshMetaBand();
    }

    void LockstepEditor::restoreEuclidStash()
    {
        if (euclidTrack_ < 0) return;
        auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(euclidTrack_)];
        for (int si = 0; si < euclidStashLen_; ++si)
            wt.steps[static_cast<std::size_t>(si)] =
                euclidStash_[static_cast<std::size_t>(si)];
    }

    void LockstepEditor::cancelEuclid()
    {
        if (!uiState_.euclidHeld) return;
        restoreEuclidStash();
        uiState_.resetEuclid();
        forgetEuclidEditorState();
        refreshMetaBand();
    }

    // ── Melodic generator (10.7) ────────────────────────────────────────────────
    // The print-model twin of the Euclid helpers above: generate a deterministic
    // mono melody from the effective KeySig + the MZ params, write it over the live
    // phrase as ordinary steps, and keep a stash so cancel/escape reverts cleanly.

    void LockstepEditor::applyMelodyLive(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& ph = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        const int len = ph.length;
        if (len <= 0) return;

        MelodyParams p;
        p.density  = uiState_.melodyDensity;
        p.coreBias = uiState_.melodyCore;
        p.contour  = uiState_.melodyContour;
        p.octaves  = uiState_.melodyOctaves;
        p.stepLeap = uiState_.melodyStepLeap;
        // 9.31: SEED is the musical dial, not the whole seed. Salted with WHERE this
        // melody is being generated (track, machine, song, scene, phrase) and with the
        // project's epoch, so SEED 1 on track 3 of this scene is not the same line as
        // SEED 1 on track 1 of the last project you opened. Same place + same SEED +
        // same project still reproduces exactly -- that is what makes SEED playable.
        MelodySeedContext sc;
        sc.track         = track;
        sc.machineIdHash = melodyHashMachineId(processor_.getMachineIdRaw(track));
        sc.song          = processor_.activePieceIdx();
        sc.scene         = processor_.activeSectionIdx();
        sc.phrase        = processor_.arrangement().activePhraseIdx(track);
        sc.projectEpoch  = processor_.projectEpoch();
        sc.seed          = uiState_.melodySeed;
        p.seed     = melodySeedFor(sc);
        p.source   = uiState_.melodySource;

        // KeepRhythm: lock onsets to the pre-entry trigs (the stash, so the live
        // preview overwriting the track doesn't feed back into the onset set).
        std::vector<bool> fixed;
        const std::vector<bool>* fixedPtr = nullptr;
        if (p.source == static_cast<int>(MelodySource::KeepRhythm) && melodicTrack_ == track)
        {
            fixed.assign(static_cast<std::size_t>(len), false);
            const int n = std::min(len, melodyStashLen_);
            for (int si = 0; si < n; ++si)
                fixed[static_cast<std::size_t>(si)] =
                    melodyStash_[static_cast<std::size_t>(si)].trig;
            fixedPtr = &fixed;
        }

        const KeySig key = processor_.effectiveKeySig();
        const int rootMidi = 48 + key.root;   // root pitch class around C3..B3
        const auto notes = generateMelody(key, len, rootMidi, p, fixedPtr);

        for (int si = 0; si < len; ++si)
        {
            auto& s = ph.steps[static_cast<std::size_t>(si)];
            const auto& m = notes[static_cast<std::size_t>(si)];
            s.trig = m.trig;
            if (m.trig)
            {
                s.trigOverride.noteCount = 1;
                s.trigOverride.notes[0] = m.note;
                s.trigOverride.hasGate = (m.gate != MusicalGate::None);
                s.trigOverride.gateValue = m.gate;
            }
            else
            {
                s.trigOverride.noteCount = 0;
                s.trigOverride.hasGate = false;
            }
        }
    }

    void LockstepEditor::applyMelodyToTrack(int track)
    {
        // No structural difference from the live preview — the commit snapshot is
        // taken at the call site (VerbConfirm) before this re-prints over it.
        applyMelodyLive(track);
    }

    void LockstepEditor::enterMelodic(int track)
    {
        if (track < 0) track = 0;
        if (activeTrackContentLocked()) return;
        const auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        uiState_.melodicHeld = true;
        uiState_.masterSection = -1;
        melodicTrack_ = track;
        melodyStashLen_ = wt.length;
        for (int si = 0; si < melodyStashLen_; ++si)
            melodyStash_[static_cast<std::size_t>(si)] = wt.steps[static_cast<std::size_t>(si)];
        applyMelodyLive(track);
        refreshMetaBand();
    }

    void LockstepEditor::restoreMelodyStash()
    {
        if (melodicTrack_ < 0) return;
        auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(melodicTrack_)];
        for (int si = 0; si < melodyStashLen_; ++si)
            wt.steps[static_cast<std::size_t>(si)] =
                melodyStash_[static_cast<std::size_t>(si)];
    }

    void LockstepEditor::cancelMelodic()
    {
        if (!uiState_.melodicHeld) return;
        restoreMelodyStash();
        uiState_.resetMelodic();
        forgetMelodyEditorState();
        refreshMetaBand();
    }

    // ── Harmonic voice-mover (10.8) ─────────────────────────────────────────────
    // The sticky multi-voice twin of the melodic helpers: print the cursor-driven
    // chord progression from the effective KeySig over the live phrase as ordinary
    // chord steps, keeping a stash so cancel/escape reverts cleanly.

    void LockstepEditor::applyHarmonyLive(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& ph = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        const int len = ph.length;
        if (len <= 0) return;

        const KeySig key = processor_.effectiveKeySig();
        const int rootMidi = kHarmonyRootBase + key.root;
        const auto ladder = harmonyLadder(key, rootMidi, kHarmonyOctaves);

        // Bar-aligned placement: one chord per bar of the in-scope time signature
        // (chord k -> bar k downbeat) when the phrase has enough bars; printHarmony
        // falls back to even spacing otherwise. stepsPerBar = round(barPpq / stepPpq).
        const double barPpq = processor_.effectiveTimeSig().barPpq();
        const double stepPpq = subdivisionPpqFromIndex(ph.subdivIndex);
        const int stepsPerBar = (barPpq > 0.0 && stepPpq > 0.0)
            ? std::max(1, static_cast<int>(std::lround(barPpq / stepPpq)))
            : 0;
        const auto steps = printHarmony(uiState_.harmonyProg, ladder, len, stepsPerBar);

        for (int si = 0; si < len; ++si)
        {
            auto& s = ph.steps[static_cast<std::size_t>(si)];
            const auto& h = steps[static_cast<std::size_t>(si)];
            s.trig = h.trig;
            if (h.trig)
            {
                s.trigOverride.noteCount = h.noteCount;
                for (int n = 0; n < h.noteCount && n < kMaxNotesPerStep; ++n)
                    s.trigOverride.notes[static_cast<std::size_t>(n)] =
                        h.notes[static_cast<std::size_t>(n)];
                s.trigOverride.hasGate = (h.gate != MusicalGate::None);
                s.trigOverride.gateValue = h.gate;
            }
            else
            {
                s.trigOverride.noteCount = 0;
                s.trigOverride.hasGate = false;
            }
        }
    }

    void LockstepEditor::applyHarmonyToTrack(int track)
    {
        // No structural difference from the live preview — the commit snapshot is
        // taken at the call site (VerbConfirm) before this re-prints over it.
        applyHarmonyLive(track);
    }

    void LockstepEditor::enterHarmony(int track)
    {
        if (track < 0) track = 0;
        if (activeTrackContentLocked()) return;
        const auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        uiState_.harmonyHeld = true;
        uiState_.masterSection = -1;
        // Seed a fresh progression — an in-key triad to nudge from. The ladder now
        // floors at C2, so seed the bass near C4 (rather than the lowest rung) so
        // the default chord lands in a comfortable register. Seeding slot 0 is
        // enough — growing the progression clones the previous chord.
        uiState_.harmonyProg = HarmonyProgression{};
        {
            const KeySig key = processor_.effectiveKeySig();
            const auto ladder = harmonyLadder(key, kHarmonyRootBase + key.root, kHarmonyOctaves);
            if (!ladder.empty())
            {
                const int ladderMax = static_cast<int>(ladder.size()) - 1;
                int bass = 0;
                for (int i = 0; i <= ladderMax; ++i)
                    if (ladder[static_cast<std::size_t>(i)] <= 60) bass = i;
                // 1-3-5 triad by LADDER RUNG. In a diatonic scale every-other rung
                // (bass+2/bass+4) is the third/fifth. On the chromatic scale a rung
                // is one semitone, so bass+2/bass+4 would give a whole-tone cluster
                // (C-D-E) — seed a real major triad by semitone (+4 major third,
                // +7 perfect fifth) instead.
                const bool chromatic = harmonyScaleSize(key) >= 12;
                const int third = chromatic ? 4 : 2;
                const int fifth = chromatic ? 7 : 4;
                auto& c = uiState_.harmonyProg.chords[0];
                c.voiceCount = 3;
                c.voice = { std::clamp(bass, 0, ladderMax),
                            std::clamp(bass + third, 0, ladderMax),
                            std::clamp(bass + fifth, 0, ladderMax), 0 };
                c.chroma = {};
            }
        }
        harmonyTrack_ = track;
        harmonyStashLen_ = wt.length;
        for (int si = 0; si < harmonyStashLen_; ++si)
            harmonyStash_[static_cast<std::size_t>(si)] = wt.steps[static_cast<std::size_t>(si)];
        applyHarmonyLive(track);
        refreshMetaBand();
    }

    void LockstepEditor::restoreHarmonyStash()
    {
        if (harmonyTrack_ < 0) return;
        auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(harmonyTrack_)];
        for (int si = 0; si < harmonyStashLen_; ++si)
            wt.steps[static_cast<std::size_t>(si)] =
                harmonyStash_[static_cast<std::size_t>(si)];
    }

    void LockstepEditor::cancelHarmony()
    {
        if (!uiState_.harmonyHeld) return;
        restoreHarmonyStash();
        uiState_.resetHarmony();
        forgetHarmonyEditorState();
        refreshMetaBand();
    }

    void LockstepEditor::forgetHarmonyEditorState()
    {
        stopHarmonyAudition();
        harmonyTrack_ = -1;
        harmonyStashLen_ = 0;
    }

    void LockstepEditor::stopHarmonyAudition()
    {
        if (harmonyTrack_ >= 0)
            for (int note : harmonyAuditionNotes_)
                processor_.liveNoteOff(harmonyTrack_, note);
        harmonyAuditionNotes_.clear();
    }

    // 10.10: re-strike the cursor chord immediately on any edit so the ear can
    // lead (preview by slow-turning an encoder). Releases the previous audition
    // voices and sounds the cursor chord through the live-note engine.
    void LockstepEditor::auditionHarmonyCursorChord()
    {
        if (!uiState_.harmonyHeld || harmonyTrack_ < 0) return;
        stopHarmonyAudition();

        const KeySig key = processor_.effectiveKeySig();
        const auto ladder = harmonyLadder(key, kHarmonyRootBase + key.root, kHarmonyOctaves);
        if (ladder.empty()) return;

        const auto& prog = uiState_.harmonyProg;
        const int K = std::clamp(prog.length, 1, kMaxHarmonyChords);
        const int cur = std::clamp(prog.cursor, 0, K - 1);
        const auto& ch = prog.chords[static_cast<std::size_t>(cur)];
        const int vc = std::clamp(ch.voiceCount, 0, kHarmonyVoices);
        for (int v = 0; v < vc; ++v)
        {
            const int note = resolveVoice(ladder, ch.voice[static_cast<std::size_t>(v)],
                                          ch.chroma[static_cast<std::size_t>(v)]);
            if (std::find(harmonyAuditionNotes_.begin(), harmonyAuditionNotes_.end(), note)
                == harmonyAuditionNotes_.end())
            {
                processor_.liveNoteOn(harmonyTrack_, note, 100);
                harmonyAuditionNotes_.push_back(note);
            }
        }
    }

    void LockstepEditor::enterDensitySticky()
    {
        if (uiState_.overlay == Overlay::Density) return;
        uiState_.overlay = Overlay::Density;
        escapeVelSticky();
        escapeOverlay(uiState_, Overlay::Time);
        refreshMetaBand();
    }

    void LockstepEditor::enterVelSticky()
    {
        if (uiState_.overlay == Overlay::Vel) return;
        uiState_.overlay = Overlay::Vel;
        uiState_.velSubPage = velAnyEnabled()
            ? UiState::VelSubPage::Depth : UiState::VelSubPage::Mode;
        escapeDensitySticky();
        escapeOverlay(uiState_, Overlay::Time);
        refreshMetaBand();
    }

    void LockstepEditor::updateFillActivation()
    {
        const bool fillHeld = uiState_.fillHeld;
        const bool funcHeld = uiState_.funcHeld;
        if (fillHeld && funcHeld)
        {
            if (fillLockedTrack_ < 0)
                fillLockedTrack_ = keyboardArea_.getActiveTrack();
            processor_.setFillActive(true, /*allTracks=*/false, fillLockedTrack_);
        }
        else
        {
            fillLockedTrack_ = -1;
            processor_.setFillActive(fillHeld, /*allTracks=*/true, -1);
        }
    }

    // -------------------------------------------------------------------------
    // MHZ.9.4: universal latch escape

    void LockstepEditor::escapeAllLatches()
    {
        using CB = ControllerButton;
        using T = ControllerEvent::Type;

        const auto prevLatch = uiState_.latch;
        uiState_.latch = {};  // clear all latches before calling dispatchUp so guards pass
        escapeDensitySticky();
        escapeOverlay(uiState_, Overlay::Time);

        // For each latched modifier that isn't physically held, do a full release.
        // dispatchUp now checks !uiState_.latch.xxx (already false), so it runs completely.
        if (prevLatch.phrase && !physHeld_.phrase)
            dispatchUp({ T::ButtonUp, CB::PhraseScope });
        if (prevLatch.morph && !physHeld_.morph)
            dispatchUp({ T::ButtonUp, CB::MorphScope });
        if (prevLatch.mute && !physHeld_.mute)
            dispatchUp({ T::ButtonUp, CB::MuteScope });
        if (prevLatch.track && !physHeld_.track)
            dispatchUp({ T::ButtonUp, CB::TrackScope });
        if (prevLatch.scene && !physHeld_.scene)
            dispatchUp({ T::ButtonUp, CB::SceneScope });
        if (prevLatch.song && !physHeld_.song)
            dispatchUp({ T::ButtonUp, CB::SongScope });
        if (prevLatch.fill && !physHeld_.fill)
            dispatchUp({ T::ButtonUp, CB::FillScope });

        // 9.29: a latched COMPOUND scope (Machine / Set) was holding Func virtually.
        // Escape must end that too — the latch is the only thing that was keeping the
        // Func layer up, and it is gone now. Guarded on the physical key so escaping
        // while Func is genuinely down does not yank the layer out from under it.
        if (prevLatch.compound && !physHeld_.func)
        {
            uiState_.funcHeld = false;
            uiState_.machineScopeHeld = false;
            editMode_.onScopeEvent({ T::ButtonUp, CB::Func });
        }

        // Release latched steps (keep them in heldStepKeys_ if still physically held).
        auto& ctx = processor_.editContext();
        const bool hadLatchedSteps = ctx.hasAnyLatchedStep();
        for (const int s : ctx.latchedSteps())
        {
            // Only release from EditContext if the physical key is not held.
            bool physDown = false;
            for (const auto& [code, idx] : heldStepKeys_)
                if (idx == s)
                {
                    physDown = true;
                    break;
                }
            if (!physDown)
                ctx.release(s);
        }
        ctx.clearAllLatched();

        // W7: a latched P-Lock inspector commits its staged removals here — a latched
        // step's own key-up no longer commits (it keeps the inspector open), so the
        // latch-escape (double-tap Func, or entering a new modality) is the commit
        // point. A bare (unlatched) hold still commits on its own step release, so gate
        // this on there having been latched steps.
        if (hadLatchedSteps && uiState_.pLockClearMode
            && uiState_.pLockClearTrack >= 0 && uiState_.pLockClearStep >= 0)
        {
            for (const int slot : uiState_.pLockClearStaged)
            {
                if (slot < 0)
                    processor_.clearTrigOverrideField(uiState_.pLockClearTrack,
                                                      uiState_.pLockClearStep, slot);
                else
                    processor_.clearParam(uiState_.pLockClearTrack,
                                          uiState_.pLockClearStep, slot);
            }
            uiState_.resetPLockClear();
        }

        if (ctx.heldSteps().empty())
        {
            heldStepKeys_.clear();
            uiState_.stepHeld = false;
            editMode_.setTrigHeld(false);
        }

        refreshSurface();
    }

    // -------------------------------------------------------------------------
    // Multi-step hold move + micro nudge (Part 2).

    // Reflect a step-index change (from → to) into the editor's physical-key map so
    // the held key keeps pointing at the moved step for subsequent presses/release.
    static void remapHeldKey(std::vector<std::pair<int, int>>& keys, int from, int to)
    {
        for (auto& [code, idx] : keys)
            if (idx == from) { idx = to; break; }
    }

    void LockstepEditor::openStepInspector(int track, int step)
    {
        uiState_.pLockClearStaged.clear();
        uiState_.pLockClearMode = true;
        uiState_.pLockClearTrack = track;
        uiState_.pLockClearStep = step;
        uiState_.stepMoveActive = false;   // inspector, never the move panel
        setStatus("STEP " + juce::String(step + 1) + " - P-LOCK INSPECTOR");
        refreshMetaBand();
        refreshSurface();
    }

    bool LockstepEditor::moveHeldSteps(int track, int dir)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks) || dir == 0) return false;
        auto& ctx = processor_.editContext();
        const auto held = ctx.heldSteps();  // copy — we mutate ctx while iterating
        if (held.empty()) return false;
        const int len = processor_.sequence().tracks[static_cast<std::size_t>(track)].length;
        if (len <= 1) return false;

        if (held.size() == 1)
        {
            // Single step: keep the existing anchor-restore "carry" model so a step can
            // be moved far while only disturbing anchor↔destination.
            const int from = held.front();
            const int to = from + dir;
            if (to < 0 || to >= len) return false;   // clamp at the ends
            processor_.relocateStepSwap(track, uiState_.stepMoveAnchor, from, to);
            ctx.remapHeldStep(from, to);
            remapHeldKey(heldStepKeys_, from, to);
            uiState_.pLockClearStep = to;            // keep inspector/status in sync
            return true;
        }

        // Block move: shift the whole held set by one as a rigid block, clamped at
        // the boundary. computeBlockMoveSwaps returns the direction-sorted swap
        // order (no adjacent collision) or empty when the block can't move.
        const auto swaps = computeBlockMoveSwaps(held, dir, len);
        if (swaps.empty()) return false;
        for (const auto& [from, to] : swaps)
        {
            processor_.swapSteps(track, from, to);
            ctx.remapHeldStep(from, to);
            remapHeldKey(heldStepKeys_, from, to);
        }
        return true;
    }

    float LockstepEditor::nudgeHeldMicro(int track, float delta)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return 0.0f;
        auto& ctx = processor_.editContext();
        const auto& held = ctx.heldSteps();
        if (held.empty()) return 0.0f;
        auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        for (const int s : held)
        {
            if (s < 0 || s >= static_cast<int>(kMaxStepsPerTrack)) continue;
            auto& step = trk.steps[static_cast<std::size_t>(s)];
            step.microOffset = std::clamp(step.microOffset + delta, -0.5f, 0.5f);
        }
        ctx.markParamWritten();
        return trk.steps[static_cast<std::size_t>(ctx.heldStepIndex())].microOffset;
    }

    // -------------------------------------------------------------------------
    // §39: density-sticky exit SSOT.

    void LockstepEditor::escapeDensitySticky()
    {
        escapeOverlay(uiState_, Overlay::Density);
    }

    bool LockstepEditor::consumeDensityStickyKey(ControllerButton btn, int /*index*/)
    {
        if (!(uiState_.overlay == Overlay::Density))
            return false;

        using CB = ControllerButton;
        if (btn == CB::NavUp || btn == CB::NavDown || btn == CB::NavLeft || btn == CB::NavRight)
        {
            uiState_.densityBank ^= 1;
            refreshMetaBand();
            refreshSurface();
            return true;
        }
        // Section handling moved to handleOverlayEvent in the Section dispatch.
        return false;
    }

    void LockstepEditor::escapeVelSticky()
    {
        escapeOverlay(uiState_, Overlay::Vel);
    }

    // Returns true if any track has velocity overlay enabled (velMode != Off).
    bool LockstepEditor::velAnyEnabled() const
    {
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            if (processor_.kit(t).velMode != VelMode::Off)
                return true;
        return false;
    }

    // Returns the next vel sub-page after `current`, skipping disabled ones.
    // Disabled pages: Depth / Center / Blend are skipped when no track is enabled.
    // Mode is always reachable. If nothing else is enabled, cycles back to Mode.
    UiState::VelSubPage LockstepEditor::nextVelSubPage(UiState::VelSubPage current) const
    {
        using VP = UiState::VelSubPage;
        const bool anyEnabled = velAnyEnabled();
        if (!anyEnabled) return VP::Mode;  // only Mode is reachable when all Off
        switch (current)
        {
            case VP::Depth:  return VP::Center;
            case VP::Center: return VP::Mode;
            case VP::Mode:   return VP::Blend;
            case VP::Blend:  return VP::Depth;
        }
        return VP::Depth;
    }

    bool LockstepEditor::consumeVelStickyKey(ControllerButton btn, int /*index*/)
    {
        if (!(uiState_.overlay == Overlay::Vel)) return false;
        using CB = ControllerButton;
        if (btn == CB::NavUp || btn == CB::NavDown || btn == CB::NavLeft || btn == CB::NavRight)
        {
            uiState_.velBank ^= 1;
            refreshMetaBand();
            refreshSurface();
            return true;
        }
        // Section handling moved to handleOverlayEvent in the Section dispatch.
        return false;
    }

    // 6.4: leave the momentary Cue scope and open the sticky cue console. The
    // physical Func+3 keys may still be down; clearing cueHeld hands the step grid
    // to the console (so a step arms a flip, not an audition). Func+3 release then
    // exits only the (already-inactive) momentary scope, leaving the console open.
    void LockstepEditor::openCueConsole()
    {
        auditionAllOff();
        uiState_.cueHeld = false;
        uiState_.overlay = Overlay::Cue;
        uiState_.cueParamPage = false;
        refreshMetaBand();
        refreshSurface();
    }

    bool LockstepEditor::consumeCueStickyKey(ControllerButton btn, int index)
    {
        if (uiState_.overlay != Overlay::Cue) return false;
        using CB = ControllerButton;
        // Nav toggles between the flip grid and the encoder param page.
        if (btn == CB::NavUp || btn == CB::NavDown || btn == CB::NavLeft || btn == CB::NavRight)
        {
            uiState_.cueParamPage = !uiState_.cueParamPage;
            refreshMetaBand();
            refreshSurface();
            return true;
        }
        // Step keys belong to the console: on the flip page a tap arms that track's
        // quantized cue flip; on the param page (encoders do the editing) steps are
        // swallowed so they never leak into the pattern.
        if (btn == CB::Step)
        {
            if (!uiState_.cueParamPage && index >= 0 && index < static_cast<int>(kNumTracks))
            {
                processor_.queueCueFlip(index);
                refreshSurface();
            }
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // MHZ.9.3: column-exclusivity-aware modifier latch toggle.

    void LockstepEditor::setModifierLatch(ControllerButton cb, bool set)
    {
        using CB = ControllerButton;
        using T = ControllerEvent::Type;

        if (set)
        {
            // Enforce column exclusivity by releasing any existing latch in the same column.
            // Calling dispatchUp (after clearing the latch bool) does the full release with
            // side effects (setControlAllActive, updateFillActivation, etc.).
            const bool isCol1 = (cb == CB::PhraseScope || cb == CB::MorphScope || cb == CB::MuteScope);

            auto releaseOther = [&](bool& latchBool, bool physHeld, CB btn) {
                if (!latchBool || cb == btn) return;
                latchBool = false;
                if (!physHeld)
                    dispatchUp({ T::ButtonUp, btn });
            };

            if (isCol1)
            {
                releaseOther(uiState_.latch.phrase, physHeld_.phrase, CB::PhraseScope);
                releaseOther(uiState_.latch.morph, physHeld_.morph, CB::MorphScope);
                releaseOther(uiState_.latch.mute, physHeld_.mute, CB::MuteScope);
            }
            else
            {
                releaseOther(uiState_.latch.track, physHeld_.track, CB::TrackScope);
                releaseOther(uiState_.latch.scene, physHeld_.scene, CB::SceneScope);
                releaseOther(uiState_.latch.song, physHeld_.song, CB::SongScope);
                releaseOther(uiState_.latch.fill, physHeld_.fill, CB::FillScope);
            }
        }

        // 9.29: latching Track or Song while Func is held latches the COMPOUND scope
        // (Machine / Set) — Func stays virtually held until the scope is unlatched, so
        // the compound survives releasing the key that entered it. Computed here, at
        // the single latch owner, so a latch can never disagree with the scope it is
        // supposed to be holding. Any other latch (or an unlatch) drops it: a compound
        // qualifies exactly one col-2 scope, never two, and never nothing.
        uiState_.latch.compound = compoundLatchFor(cb, uiState_.funcHeld, set);
        if (!uiState_.latch.compound && !physHeld_.func)
        {
            // The compound is gone and Func is not physically down: the virtual Func
            // hold it was maintaining must end with it, or a stale funcHeld would
            // silently re-skin every section key.
            uiState_.funcHeld = false;
            uiState_.machineScopeHeld = false;
            editMode_.onScopeEvent({ T::ButtonUp, CB::Func });
        }

        switch (cb)
        {
            case CB::PhraseScope: uiState_.latch.phrase = set; break;
            case CB::MorphScope:  uiState_.latch.morph = set; break;
            case CB::MuteScope:   uiState_.latch.mute = set; break;
            case CB::TrackScope:  uiState_.latch.track = set; break;
            case CB::SceneScope:  uiState_.latch.scene = set; break;
            case CB::SongScope:   uiState_.latch.song = set; break;
            case CB::FillScope:   uiState_.latch.fill = set; break;
            case CB::Func:
            case CB::CueScope:
            case CB::VerbSnapshot:
            case CB::VerbRecord:
            case CB::VerbPlay:
            case CB::VerbStopLegacy:
            case CB::VerbConfirm:
            case CB::Snapshot:
            case CB::Restore:
            case CB::NavUp:
            case CB::NavLeft:
            case CB::NavDown:
            case CB::NavRight:
            case CB::Section:
            case CB::MetaSection:
            case CB::Step:
            case CB::SelectTrack:
            case CB::ToggleMute:
            case CB::ForkPart:
            case CB::RecordArm:
            case CB::TapTempo:
            case CB::MetronomeToggle:
            case CB::PlayStop:
            case CB::StopReset:
            case CB::VerbClear:
            case CB::VerbDelete:
            case CB::VerbPanic:
            case CB::None:
                break;
        }
    }

    // W7: latch every PHYSICALLY held step so the finger is free (release no longer
    // ends the edit). No trig mutation — a purely virtual hold. Only fires when
    // nothing is latched yet, so a later double-tap-Func still escapes.
    // One implementation, two callers: the Func-down branch in dispatchDown and the
    // StepLatch action's effect (9.12 Stage 6). Returns true if it latched (the
    // caller then consumes the event).
    bool LockstepEditor::latchHeldSteps()
    {
        if (heldStepKeys_.empty() || processor_.editContext().hasAnyLatchedStep())
            return false;

        auto& ctx = processor_.editContext();
        for (const auto& [code, idx] : heldStepKeys_)
            ctx.setLatched(idx);
        setStatus(uiState_.pLockClearMode
            ? "P-LOCK LATCHED - tap cells to clear, double-tap Func to apply"
            : "STEPS LATCHED - edit hands-free, double-tap Func to release");
        refreshSurface();
        return true;
    }

    // 9.12 Stage 7b: route a verb press through the grammar. The table names the
    // action (Copy / Paste / ScopedClear / BakeScene / ...) from the verb button plus
    // the held modifiers, and handleAction delegates to the one verb implementation.
    //
    // Verbs resolve on the FULL held-modifier set — most-specific-wins — which is the
    // exact opposite of the modifier family's bare-row rule (7a). That is not an
    // inconsistency: a modifier press means "enter this scope" whatever else is down,
    // while a verb press means *something different* under a held scope. The two
    // families must not share a routing rule.
    //
    // The Func layer is already applied upstream: every input path runs the event
    // through resolveLayer() first, so Func+Y arrives here as CB::Restore, not as
    // VerbSnapshot-with-Func-held.
    // 9.29: is a HOLD of the Clear key a delete here? Only under a scope that OWNS a
    // deletable entity (Track / Phrase / Scene). The Machine scope is excluded on
    // purpose: Machine+Clear is INIT, and a compound scope cannot be qualified by the
    // key that formed it (Machine IS Func+Track), so its Clear has to be the tap.
    bool LockstepEditor::deleteHoldCapable() const
    {
        using PS = EditMode::PrimaryScope;
        if (uiState_.machineScopeHeld) return false;
        const PS scope = editMode_.primaryScope();
        return scope == PS::Track || scope == PS::Phrase || scope == PS::Scene;
    }

    // Fire the delete (the hold rail of the Clear key). Resolved through the table on
    // the Hold gesture, so WHICH entity dies is the binding's call, not this code's.
    void LockstepEditor::fireDeleteHold()
    {
        const auto& row = resolveBinding(ControllerButton::VerbClear, -1,
                                         heldModsFromUiState(uiState_),
                                         SurfaceLayer::Base, Gesture::Hold);
        if (row.action == ActionId::None) return;
        const ControllerEvent ev{ ControllerEvent::Type::ButtonDown,
                                  ControllerButton::VerbClear, -1, 0 };
        auto ctx = commandContext();
        (void)commandCore_.handleAction(row.action, ev, ctx, *editorEffects_);
        refreshSurface();
    }

    // The Clear key's TAP behaviour — moved verbatim out of dispatchDown when delete
    // took the hold rail (9.29). Two callers now: an immediate press where no delete
    // hold is possible (no scope, Trig, Morph, Machine), and the short-hold resolution
    // on key-up where one was.
    bool LockstepEditor::clearVerbTap(const ControllerEvent& ev)
    {
        using PS = EditMode::PrimaryScope;
        // S3: looper ERASE moved to the always-on console; Track+Clear is the
        // ordinary clipboard/scope verb again.
        // Hold scope + Clear reverts that scope's hierarchical override to inherit
        // (§13 hold-scope+Clear convention). Intercept before cancel-queued-scene
        // and PANIC so that Clear is contextual while a band is open.
        {
            // Swing: hold-scope + Clear zeros the swing delta at the held scope.
            // TIME page: revert is per-control (dial to floor); no Clear chord needed.
            const MetaBand activeBand = resolveMetaBand(uiState_);
            if (activeBand == MetaBand::Swing)
            {
                const int swScope = swingScopeFor(uiState_);
                if (swScope == 1)
                    processor_.setSwingSongAll(0.0f);
                else if (swScope == 2)
                    processor_.setSwingSceneAll(0.0f);
                else if (swScope == 3)
                    processor_.setSwingSongTrack(uiState_.activeTrack, 0.0f);
                refreshMetaBand();
                refreshSurface();
                return true;
            }
        }
        // 9.4 item C: the Scene+O / Phrase+O "cancel queued scene" intercepts that sat
        // here are gone -- cancel is a Cancel-verb gesture now (Func+P), not a Clear one.
        // Removing them lets both keys reach the table: Scene+O is SYNC (verbs::scene) and
        // Phrase+O is CLEAR PHRASE (verbs::phrase's confirm arm, which this intercept had
        // been shadowing into dead code since it was written).
        // Non-trivial scope → grammar verb (Clear scope contents).
        if (editMode_.primaryScope() != PS::None && editMode_.primaryScope() != PS::Func)
            return routeVerb(ev);   // 9.12 st.7b: the table names the action
        // No scope: clear the active P-Lock slot if one is active.
        {
            auto& ctx = processor_.editContext();
            if (ctx.isActiveForEditing() && ctx.activeSlot() >= 0)
            {
                processor_.clearParam(ctx.heldTrackIndex(),
                                      ctx.heldStepIndex(),
                                      ctx.activeSlot());
                ctx.markParamWritten();
            }
        }
        return true;
    }

    // 9.12 Stage 7c: route a nav press through the grammar.
    //
    // Resolved on the ACTIVE LAYER, not on Base — because one nav behaviour genuinely
    // is layer-scoped: inside NoteEdit / CHROMATIC the ←/→ pair shifts the octave
    // instead of paging or rotating. That is what the table's `layer` column is for,
    // and declaring it as rows is what lets the key frame say OCT± while the mode is
    // up. Layer rows are additive, so a miss falls back to Base rather than dropping
    // the key: every other nav gesture means the same thing in every layer.
    // The step grid's active layer, as both the painter and dispatch see it.
    SurfaceLayer LockstepEditor::activeLayer() const
    {
        const int at = keyboardArea_.getActiveTrack();
        const auto inputMode = (at >= 0)
                                   ? uiState_.trackInputMode[static_cast<std::size_t>(at)]
                                   : TrackInputMode::Play;
        const LayerFacts facts{ inputMode, at,
                                at >= 0 && processor_.isLooperTrack(at),
                                processor_.trackConsoleMode(at) };
        return resolveActiveLayer(uiState_, processor_.editContext(), facts);
    }

    // Resolve a key on the ACTIVE layer, falling back to Base. Layer rows are additive:
    // a key means the same thing in every layer unless a layer says otherwise (the
    // octave pair inside NoteEdit/CHROMATIC, QUANT inside the step inspector), so a
    // miss must fall back rather than drop the press.
    bool LockstepEditor::routeLayered(const ControllerEvent& ev)
    {
        const auto layer = activeLayer();
        const auto mods = heldModsFromUiState(uiState_);

        const KeyBinding* row = &resolveBinding(ev.button, ev.index, mods, layer);
        if (row->action == ActionId::None && layer != SurfaceLayer::Base)
            row = &resolveBinding(ev.button, ev.index, mods, SurfaceLayer::Base);
        if (row->action == ActionId::None)
            return true;   // no row: the key is inert here, and swallowing it is the answer

        auto ctx = commandContext();
        (void)commandCore_.handleAction(row->action, ev, ctx, *editorEffects_);
        refreshSurface();
        return true;
    }

    bool LockstepEditor::routeNav(const ControllerEvent& ev) { return routeLayered(ev); }

    // 9.12 st.7e: the P key. Same layered routing — QUANT under a scope OR a held step,
    // the confirm verb bare, cancel under Func.
    bool LockstepEditor::routeConfirm(const ControllerEvent& ev) { return routeLayered(ev); }

    // 9.12 Stage 7d: route a section press through the grammar.
    //
    // Sections are keyed on CB::Section in the table even when the Func layer has
    // renamed the button to CB::MetaSection: the Func rows (kModFunc) ARE the meta
    // hierarchy, so one row set serves both. Passing ev.button would find no rows and
    // silently drop every Func+section press.
    bool LockstepEditor::routeSection(const ControllerEvent& ev)
    {
        const auto& row = resolveBinding(ControllerButton::Section, ev.index,
                                         heldModsFromUiState(uiState_), SurfaceLayer::Base);
        if (row.action == ActionId::None)
            return true;   // no row: inert here, and swallowing is the answer
        auto ctx = commandContext();
        (void)commandCore_.handleAction(row.action, ev, ctx, *editorEffects_);
        refreshSurface();
        return true;
    }

    // Fire whatever the table says a (button, index, gesture) means right now. The
    // gesture axis (tap / hold / double-tap) is resolved by the CALLER — it owns the
    // timing — but WHICH action a gesture carries stays the binding's call, so a hold
    // and a tap on the same key cannot drift apart in two different code paths.
    void LockstepEditor::fireGesture(ControllerButton btn, int index, Gesture g)
    {
        const auto& row = resolveBinding(btn, index, heldModsFromUiState(uiState_),
                                         SurfaceLayer::Base, g);
        if (row.action == ActionId::None) return;
        const ControllerEvent ev{ ControllerEvent::Type::ButtonDown, btn, index, 0 };
        auto ctx = commandContext();
        (void)commandCore_.handleAction(row.action, ev, ctx, *editorEffects_);
        refreshSurface();
    }

    // Fire the section HOLD armed on key-down (the picker rail). The action was decided
    // by the table at arm time and stored, so the mid-hold timer and the key-up fallback
    // cannot disagree about which picker they are opening.
    void LockstepEditor::fireSectionHold()
    {
        if (sectionHoldAction_ == ActionId::None) return;
        const ControllerEvent ev{ ControllerEvent::Type::ButtonDown,
                                  ControllerButton::Section, heldSectionIndex_, 0 };
        auto ctx = commandContext();
        (void)commandCore_.handleAction(sectionHoldAction_, ev, ctx, *editorEffects_);
        refreshSurface();
    }

    bool LockstepEditor::routeVerb(const ControllerEvent& ev)
    {
        const auto& row = resolveBinding(ev.button, ev.index, heldModsFromUiState(uiState_),
                                         SurfaceLayer::Base);
        auto ctx = commandContext();
        const bool handled =
            (row.action != ActionId::None)
                ? commandCore_.handleAction(row.action, ev, ctx, *editorEffects_)
                // No row for this scope+verb: fall back to the scope×verb matrix, so a
                // gap in the table can never silently drop a verb that used to work.
                : commandCore_.handleVerb(editMode_.primaryScope(), ev.button, ctx,
                                          *editorEffects_);
        (void)handled;
        refreshSurface();
        return true;
    }

    // 9.12 Stage 7a: the modifier family's effect. Every line below is MOVED from
    // the dispatchDown branches, not rewritten — the golden net is what proves that.
    // The per-scope differences are real and stay here (Func's escape gesture and
    // step-latch, Track's Control-All, Scene's re-sync-to-scene, Morph's MZ hand-off),
    // because a modifier press is not one uniform thing.
    void LockstepEditor::enterScopeHold(ControllerButton cb)
    {
        using CB = ControllerButton;
        const ControllerEvent ev{ ControllerEvent::Type::ButtonDown, cb, -1, 0 };
        switch (cb)
        {
                case CB::Func:
                    // W7: hold-step + Func latches the held step(s) so the finger is free
                    // (release no longer ends the edit). No trig mutation — a purely
                    // virtual hold. Fires whenever step(s) are PHYSICALLY held and nothing
                    // is latched yet, so a later double-tap-Func still escapes. Decoupled
                    // from the inspector (Part 2): a bare multi-hold latches too, keeping
                    // every held step editable hands-free; the long-press inspector adds
                    // the tap-to-clear slot flow on top.
                    if (latchHeldSteps())
                        return;   // consume: no funcHeld, no Chance band
                    physHeld_.func = true;
                    uiState_.funcHeld = true;
                    // 9.29 / §13.9: Func+Track = the Machine scope. Either press order
                    // enters it (Func over a held Track here; Track under a held Func
                    // in the TrackScope branch) — a compound scope is a state, not a
                    // sequence.
                    uiState_.machineScopeHeld = uiState_.trackHeld;
                    editMode_.onScopeEvent(ev);
                    updateFillActivation();
                    refreshMetaBand();  // 1c: Func held → show Chance band in MZ
                    refreshSurface();
                    // MHZ.9.4: Func never latches; double-tap = universal escape.
                    // Cancels latches and any active overlay (Euclid / sticky modes).
                    {
                        const double now = juce::Time::getMillisecondCounterHiRes();
                        if (gesture_.doubleTap(1000 + static_cast<int>(CB::Func), now))
                        {
                            if (uiState_.latch.any() || processor_.editContext().hasAnyLatchedStep())
                                escapeAllLatches();

                            // 7b: double-tap Func also closes an open machine console
                            // (the confirmed Cancel/close path). 7c hooks revert here.
                            if (uiState_.machineConsoleOpen)
                            {
                                closeMachineConsole();
                                refreshSurface();
                            }

                            // Route through the overlay reducer.  Guard: sticky modes
                            // require no latches (they shouldn't be co-active, but
                            // double-tap can arrive mid-gesture).  Euclid always exits.
                            const Overlay prevOv = activeOverlay(uiState_);
                            const bool canEscape = (prevOv == Overlay::Euclid)
                                || (prevOv == Overlay::Melodic)
                                || (prevOv == Overlay::Harmony)
                                || (!uiState_.latch.any()
                                    && !processor_.editContext().hasAnyLatchedStep());
                            if (canEscape)
                            {
                                const auto r = handleOverlayEvent(uiState_,
                                    { ModeEventKind::DoubleTapFunc });
                                if (r == OverlayResult::Exited)
                                {
                                    // Euclid/Melodic stash restore is editor-owned state
                                    // (the reducer reset the held flag via escapeOverlay).
                                    if (prevOv == Overlay::Euclid && euclidTrack_ >= 0)
                                    {
                                        restoreEuclidStash();
                                        forgetEuclidEditorState();
                                    }
                                    if (prevOv == Overlay::Melodic && melodicTrack_ >= 0)
                                    {
                                        restoreMelodyStash();
                                        forgetMelodyEditorState();
                                    }
                                    if (prevOv == Overlay::Harmony && harmonyTrack_ >= 0)
                                    {
                                        restoreHarmonyStash();
                                        forgetHarmonyEditorState();
                                    }
                                    refreshMetaBand();
                                    refreshSurface();
                                }
                            }
                        }
                    }
                    return;

                case CB::TrackScope:
                    physHeld_.track = true;
                    uiState_.trackHeld = true;
                    // 9.29 / §13.9: Func+Track = the Machine scope (see the Func branch).
                    uiState_.machineScopeHeld = uiState_.funcHeld;
                    processor_.setControlAllActive(true);  // MD.10: active until a track is selected
                    editMode_.onScopeEvent(ev);
                    handleModifierTap(CB::TrackScope, uiState_.latch.track);
                    clearSwingDismissed();
                    refreshSurface();
                    return;

                case CB::PhraseScope:
                    physHeld_.phrase = true;
                    uiState_.phraseScopeHeld = true;
                    uiState_.phraseScopeUsed = false;
                    editMode_.onScopeEvent(ev);
                    // Euclid entry moved to generator hub (9.10): Phrase+Fill no longer enters it.
                    handleModifierTap(CB::PhraseScope, uiState_.latch.phrase);
                    refreshSurface();
                    return;

                case CB::MuteScope:
                    physHeld_.mute = true;
                    uiState_.muteHeld = true;
                    editMode_.onScopeEvent(ev);
                    handleModifierTap(CB::MuteScope, uiState_.latch.mute);
                    refreshSurface();
                    return;

                case CB::FillScope:
                    physHeld_.fill = true;
                    uiState_.fillHeld = true;
                    editMode_.onScopeEvent(ev);
                    // Euclid entry moved to generator hub (9.10): Phrase+Fill no longer enters it.
                    updateFillActivation();
                    handleModifierTap(CB::FillScope, uiState_.latch.fill);
                    refreshSurface();
                    return;

                case CB::CueScope:
                    physHeld_.cue = true;
                    uiState_.cueHeld = true;
                    editMode_.onScopeEvent(ev);
                    refreshSurface();
                    return;

                case CB::MorphScope:
                    physHeld_.morph = true;
                    uiState_.morphHeld = true;
                    manipulationZone_.setMorphHeld(true);
                    editMode_.onScopeEvent(ev);
                    handleModifierTap(CB::MorphScope, uiState_.latch.morph);
                    refreshSurface();
                    return;

                case CB::SongScope:
                    physHeld_.song = true;
                    uiState_.songHeld = true;
                    editMode_.onScopeEvent(ev);
                    handleModifierTap(CB::SongScope, uiState_.latch.song);
                    clearSwingDismissed();
                    refreshSurface();
                    return;

                case CB::SceneScope:
                    physHeld_.scene = true;
                    // Track + Scene: re-sync focused musician to current Scene (Phase 7).
                    if (uiState_.trackHeld)
                    {
                        processor_.resyncTrackToScene(processor_.focusTrack());
                        refreshSurface();
                        return;
                    }
                    uiState_.sceneHeld = true;
                    editMode_.onScopeEvent(ev);
                    handleModifierTap(CB::SceneScope, uiState_.latch.scene);
                    clearSwingDismissed();
                    refreshSurface();
                    return;

            default:
                return;   // not a scope modifier
        }
    }

    // -------------------------------------------------------------------------
    // MHZ.9.x: per-modifier tap router.
    // Single tap on a latched modifier unlatches it; double-tap toggles latch.

    void LockstepEditor::handleModifierTap(ControllerButton cb, bool currentlyLatched)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const bool dbl = gesture_.doubleTap(1000 + static_cast<int>(cb), now);
        if (currentlyLatched && !dbl)
        {
            setModifierLatch(cb, false);
            gesture_.invalidate();  // prevent follow-up read as re-latch
        }
        else if (dbl)
        {
            setModifierLatch(cb, !currentlyLatched);
        }
    }

    // MHZ.9.x: auto-release a transient latch after a terminal action completes.
    // Only releases when the mode is *latched* (not just physically held).

    void LockstepEditor::releaseTransientLatch(ControllerButton cb)
    {
        using T = ControllerEvent::Type;
        bool latched = false;
        bool phys = false;
        if (cb == ControllerButton::TrackScope)
        {
            latched = uiState_.latch.track;
            phys = physHeld_.track;
        }
        else if (cb == ControllerButton::SceneScope)
        {
            latched = uiState_.latch.scene;
            phys = physHeld_.scene;
        }
        else
        {
            return;
        }
        if (!latched) return;
        setModifierLatch(cb, false);
        if (!phys) dispatchUp({ T::ButtonUp, cb });
        refreshSurface();
    }

    // -------------------------------------------------------------------------
    // Key handling (9x4 layout)

    // A section-suite scope (Track/Pattern/Part/Scene/Master) qualifies the next
    // verb. While one is held, bare-Func global ops (Snapshot/Restore) are reserved:
    // Func+scope+verb is that scope's secondary variant — not a global checkpoint.
    static bool sectionSuiteScopeHeld(const UiState& ui) noexcept
    {
        return ui.trackHeld || ui.phraseScopeHeld || ui.sceneHeld || ui.morphHeld || ui.songHeld;
    }

    // ── 5.5 Audition (Cue scope, DESIGN §21) ─────────────────────────────────
    // Cue is a monitor: it fires resolved notes via liveNoteOn/Off and never
    // writes to the pattern. Entered as the Func+3 compound (hardware parity).

    void LockstepEditor::enterCueScope()
    {
        using CB = ControllerButton;
        cueViaFunc_ = true;
        physHeld_.cue = true;
        uiState_.cueHeld = true;
        editMode_.onScopeEvent({ ControllerEvent::Type::ButtonDown, CB::CueScope });
        auditionBaseTrigDown();   // "Cue held, no step" → focused track base trig
        refreshSurface();
    }

    void LockstepEditor::exitCueScope()
    {
        using CB = ControllerButton;
        auditionAllOff();
        cueViaFunc_ = false;
        physHeld_.cue = false;
        uiState_.cueHeld = false;
        editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, CB::CueScope });
        refreshSurface();
    }

    void LockstepEditor::auditionBaseTrigDown()
    {
        const int t = keyboardArea_.getActiveTrack();
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
        auditionBaseTrigOff();  // never stack base monitors
        const auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(t)];
        auditionBaseTrack_ = t;
        auditionBaseNote_ = trk.trigDefaults.note;
        processor_.liveNoteOn(t, auditionBaseNote_, trk.trigDefaults.velocity);
    }

    void LockstepEditor::auditionBaseTrigOff()
    {
        if (auditionBaseTrack_ >= 0 && auditionBaseNote_ >= 0)
            processor_.liveNoteOff(auditionBaseTrack_, auditionBaseNote_);
        auditionBaseTrack_ = -1;
        auditionBaseNote_ = -1;
    }

    void LockstepEditor::auditionStepDown(int stepIdx)
    {
        if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) return;
        // A specific step was chosen — drop the bare base-trig monitor.
        auditionBaseTrigOff();
        const int t = keyboardArea_.getActiveTrack();
        if (t < 0 || t >= static_cast<int>(kNumTracks)) return;
        auditionStepOff(stepIdx);  // release any stale audition on this step
        const auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(t)];
        const auto trig = StateResolver::resolveTrig(trk, stepIdx, false);
        auto& sa = stepAudition_[static_cast<std::size_t>(stepIdx)];
        sa.track = t;
        sa.count = trig.noteCount;
        for (int n = 0; n < trig.noteCount; ++n)
        {
            const auto ni = static_cast<std::size_t>(n);
            const int note = trig.notes[ni];
            const int vel = trig.hasNoteVelocities ? static_cast<int>(trig.velocities[ni])
                                                   : trig.velocity;
            sa.notes[ni] = note;
            processor_.liveNoteOn(t, note, vel);
        }
    }

    void LockstepEditor::auditionStepOff(int stepIdx)
    {
        if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) return;
        auto& sa = stepAudition_[static_cast<std::size_t>(stepIdx)];
        for (int n = 0; n < sa.count; ++n)
            processor_.liveNoteOff(sa.track, sa.notes[static_cast<std::size_t>(n)]);
        sa = StepAudition{};
    }

    void LockstepEditor::auditionAllOff()
    {
        auditionBaseTrigOff();
        for (int i = 0; i < kMaxStepsPerTrack; ++i)
            auditionStepOff(i);
    }

    // dispatchDown — source-agnostic button-down handler fed by both keyboard
    // and mouse.  rawCode is the physical key code (keyboard) or 0 (mouse).
    bool LockstepEditor::dispatchDown(ControllerEvent ev, int rawCode)
    {
        using CB = ControllerButton;

        // 5.5: Func+3 enters the Cue (audition/monitor) scope — a freed compound,
        // not a 9th key (hardware parity; DESIGN §21/§31). Held = cueHeld;
        // releasing the '3' key exits (handled in dispatchUp via cueViaFunc_).
        if (ev.button == CB::TapTempo && uiState_.funcHeld && !cueViaFunc_)
        {
            enterCueScope();
            return true;
        }

        // Generator hub (9.10): intercept bare TapTempo (3-key) before CommandCore.
        // handleTapTempo() is deferred to key-up; a long hold opens the hub instead.
        if (ev.button == CB::TapTempo)
        {
            tapTempoPhysHeld_ = true;
            tapTempoArmMs_ = juce::Time::getMillisecondCounterHiRes();
            return true;
        }

        // 6.4: Cue-held + NavRight opens the sticky cue console (DESIGN §31).
        if (uiState_.cueHeld && uiState_.overlay != Overlay::Cue && ev.button == CB::NavRight)
        {
            openCueConsole();
            return true;
        }
        // While the cue console is open it owns the step grid + Nav paging, before
        // any cueHeld audition / Func-layer step remap can claim them.
        if (uiState_.overlay == Overlay::Cue && consumeCueStickyKey(ev.button, ev.index))
            return true;

        // 5.5: while Cue is held, a step press auditions that step's resolved trig
        // (off-schedule, pattern untouched). Intercept before normal step dispatch.
        if (uiState_.cueHeld && ev.button == CB::Step)
        {
            auditionStepDown(ev.index);
            return true;
        }

        // 6.4: Cue + Mute = toggle the focused track's cue balance (DESIGN §31) —
        // the direct single-track "send to headphones" gesture, a cross-column
        // compound (Cue is the Func+3 scope, Mute the cluster modifier). Distinct
        // from the momentary step audition above. Intercept before MuteScope's
        // normal enterScopeHold so it never opens mute-view.
        if (uiState_.cueHeld && ev.button == CB::MuteScope)
        {
            const int t = keyboardArea_.getActiveTrack();
            if (t >= 0 && t < static_cast<int>(kNumTracks))
            {
                processor_.toggleCueBalance(t);
                setStatus(processor_.getCueBalance(t) > 0.0f ? status::cuedTrack(t)
                                                             : status::uncuedTrack(t));
                refreshSurface();
            }
            cueMuteChordActive_ = true;
            return true;
        }

        // Phase 8.4 command core: try the migrated handlers first; fall through
        // to legacy dispatch for everything that hasn't migrated yet.
        {
            auto ctx = commandContext();
            if (commandCore_.handleDown(ev, ctx, *editorEffects_))
                return true;
        }

        // Transient swing dismissal (C3): any non-swing-scope interaction while the
        // swing band is showing collapses back to machine params so the user can reach
        // sections/verbs/nav/steps without the scope re-routing the whole zone.
        // The scope keys themselves are excluded — they reset dismissed on re-hold.
        if (resolveMetaBand(uiState_) == MetaBand::Swing && ev.button != CB::TrackScope && ev.button != CB::SceneScope && ev.button != CB::SongScope)
        {
            uiState_.swingDismissed = true;
            refreshMetaBand();
        }

        // Foreign-scope discharge: pressing a scope modifier that is "foreign" to the
        // active overlay exits the overlay before the scope runs its normal handler.
        // Per-overlay foreign-scope policy lives in ModeReducer's kOverlays descriptor
        // table — which scopes are "own" vs "foreign" is data, not scattered ifs.
        // Invariant: an active overlay and a foreign-scope-held state are mutually
        // exclusive; resolveMetaBand and resolveActiveLayer can therefore never disagree.
        {
            const auto r = handleOverlayEvent(uiState_,
                { ModeEventKind::ScopePress, -1, ev.button });
            if (r == OverlayResult::Exited)
                refreshMetaBand();
        }

        switch (ev.button)
        {
            // ── 9.12 Stage 7a: the modifier family is grammar-routed ──────────────
            // The eight scope modifiers no longer carry their behaviour inline. The
            // binding table names the action, handleAction dispatches it, and the
            // editor supplies the effect (enterScopeHold) — which is the very code
            // these branches used to run, moved, not rewritten.
            //
            // Resolved on the modifier's BARE row (kModNone), NOT on the held-mod set.
            // A modifier press means "enter this scope" whatever else is down, but
            // most-specific-wins would resolve TrackScope-under-Func to the compound
            // row (OpenMachinePicker) and swallow the hold — the scope would silently
            // never be entered. A compound row says what a PAIR means for the keys it
            // qualifies; it is not the modifier's own action.
            case CB::Func:
            case CB::TrackScope:
            case CB::PhraseScope:
            case CB::MuteScope:
            case CB::FillScope:
            case CB::CueScope:
            case CB::MorphScope:
            case CB::SongScope:
            case CB::SceneScope: {
                // Base layer: the eight modifier rows are declared there, and a
                // modifier's own meaning does not change with the active overlay.
                const auto& row = resolveBinding(ev.button, ev.index, kModNone,
                                                 SurfaceLayer::Base);
                if (row.action != ActionId::None)
                {
                    auto ctx = commandContext();
                    if (commandCore_.handleAction(row.action, ev, ctx, *editorEffects_))
                        return true;
                }
                // CueScope is reserved and has no binding row; it still owes its hold,
                // so fall through to the effect rather than dropping the press.
                enterScopeHold(ev.button);
                return true;
            }

            case ControllerButton::Section: {
                // Euclid is a focused modal generator; selecting a section exits it,
                // reverting the live preview (commit is the explicit P press). Without
                // this the mode lingered after navigating away and a later Confirm
                // would still write the pattern.
                if (uiState_.euclidHeld)
                {
                    cancelEuclid();
                    refreshSurface();
                }
                if (uiState_.melodicHeld)
                {
                    cancelMelodic();
                    refreshSurface();
                }
                if (uiState_.harmonyHeld)
                {
                    cancelHarmony();
                    refreshSurface();
                }

                // Determine whether a section-suite scope modifier is held.
                // Use the canonical kScopePriority ordering (ScopePriority.h SSOT).
                using PS = EditMode::PrimaryScope;
                const PS sectionScope = firstHeldSectionSuiteScope(uiState_);

                // 9.10: Fill+TRIG → RetrigPicker only for slicer tracks (slice-point picker).
                // Non-slicer live stutter removed (NON-GOALS fence #11). Freed slot reserved/inert.
                // Fill+SRC → SoundPool (retained).
                if (uiState_.fillHeld && ev.index == 0)
                {
                    const int slAt = keyboardArea_.getActiveTrack();
                    const auto* slMach = (slAt >= 0) ? processor_.machineForTrack(slAt) : nullptr;
                    const auto* slSliceable = slMach ? dynamic_cast<const ISliceable*>(slMach) : nullptr;
                    if (slSliceable && slSliceable->hasSlices())
                    {
                        uiState_.trigGridMode = TrigGridMode::Retrig;
                        refreshSurface();
                    }
                    return true;
                }
                if (uiState_.fillHeld && ev.index == 1)
                {
                    uiState_.trigGridMode = TrigGridMode::SoundPool;
                    refreshSurface();
                    return true;
                }

                // Route through the overlay reducer.  Handles:
                //   • Consumed: internal section (MOD/AMP) → cycles subpage; return true.
                //   • Exited: any other section → exits the overlay; fall through.
                //   • NotConsumed: no overlay active, or TIME+TRIG (pass-through to
                //     isTimeEntryChord below).
                // NOTE: sticky-mode *entry* (Func+MOD density, Func+AMP vel) is handled
                // in the MetaSection case below, not here. ButtonLayers remaps Section→
                // MetaSection whenever Func is held (kLayerRemaps), so a Func+section
                // press never reaches this Section case.
                {
                    const ScopeCtx octx { velAnyEnabled() };
                    const auto r = handleOverlayEvent(uiState_,
                        { ModeEventKind::SectionPress, ev.index }, octx);
                    if (r == OverlayResult::Consumed) { refreshMetaBand(); refreshSurface(); return true; }
                    if (r == OverlayResult::Exited)   refreshMetaBand();
                }

                // ── 9.12 st.7d: does this key have a HOLD here? ASK THE TABLE ─────
                // The arm used to be two hardcoded blocks — `index == kFxSecIdx &&
                // (scope == Track || scope == Song)`, then `index == kSrcSecIdx && scope
                // == Track` — each restating a rule the binding table already carries.
                // That is the exact duplication this migration exists to remove, and it
                // is why every new picker needed a new if. Now: one query, and a picker
                // is added by adding a ROW.
                //
                // The rule the rows encode (9.14 / §13.9): *<scope> + hold(<section>) =
                // choose what fills that section, at that scope.* Track+FX = track
                // inserts, Song+FX = master, Track+SRC = the machine.
                {
                    const auto& hold = resolveBinding(CB::Section, ev.index,
                                                      heldModsFromUiState(uiState_),
                                                      SurfaceLayer::Base, Gesture::Hold);
                    if (hold.action != ActionId::None)
                    {
                        sectionHoldAction_ = hold.action;
                        sectionHoldFired_ = false;
                        fxSectionPickerWantsMaster_ =
                            (hold.action == ActionId::OpenMasterFxPicker);
                        gesture_.armLongPress(kSectionHoldToken,
                                              juce::Time::getMillisecondCounterHiRes());
                        if (heldSectionRawCode_ < 0)
                        {
                            heldSectionRawCode_ = rawCode;
                            heldSectionIndex_ = ev.index;
                            editMode_.setSectionHeld(true);
                        }
                        return true;  // deferred: the timer fires the hold, key-up the tap
                    }
                }

                // 7b: OnDemand machine console. A bare long-press of the machine's
                // console-owning section key opens/closes the console; a tap still pages
                // that section's params. NOT a table row: which key owns the console is a
                // property of the MACHINE, not of the modifiers, so the table cannot see
                // it. Armed here, resolved on key-up.
                if (sectionScope == PS::None)
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (processor_.trackConsoleMode(at) == ConsoleMode::OnDemand
                        && ev.index == processor_.trackConsoleSection(at))
                    {
                        gesture_.armLongPress(kMachineConsoleLongPressToken,
                                              juce::Time::getMillisecondCounterHiRes());
                        machineConsoleArmed_ = true;
                        if (heldSectionRawCode_ < 0)
                        {
                            heldSectionRawCode_ = rawCode;
                            heldSectionIndex_ = ev.index;
                            editMode_.setSectionHeld(true);
                        }
                        return true;  // action deferred to key-up
                    }
                }

                // The TAP. Section-key hold tracking for Section-scope verb dispatch
                // (MD.3) is bookkeeping, so it is hoisted out of the branches that used
                // to each repeat it.
                if (heldSectionRawCode_ < 0)
                {
                    heldSectionRawCode_ = rawCode;
                    heldSectionIndex_ = ev.index;
                    editMode_.setSectionHeld(true);
                }
                return routeSection(ev);
            }

            case ControllerButton::MetaSection: {
                // The Func layer renames the button (Section → MetaSection) so dispatch
                // can tell the layers apart, but the TABLE keys sections on CB::Section
                // under kModFunc — one key, one row set. routeSection resolves there.
                return routeSection(ev);
            }

            case ControllerButton::Step: {
                // 9.17: Mute + Play + step = relaunch (unmute + phase-reset) on a
                // muted track, or retrigger (phase-reset only) on a playing one.
                // Quantized to the track grid; step double-tap = instant. Handled
                // ahead of the normal mute dispatch.
                if (uiState_.muteHeld && uiState_.relaunchHeld
                    && ev.index >= 0 && ev.index < static_cast<int>(kNumTracks))
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    const bool dbl = gesture_.doubleTap(kRelaunchStepTokenBase + ev.index, now);
                    processor_.queueRelaunch(ev.index, dbl);
                    refreshSurface();
                    return true;
                }

                // 9.10: kRetrigRates used only by slicer preview (live stutter removed).
                // Kept as default-rate lookup; index 4 = /16 default.
                static constexpr std::array<double, 8> kRetrigRates = { {
                    1.0,          // /4   (quarter-note)
                    2.0 / 3.0,    // /4T  (quarter triplet)
                    0.5,          // /8
                    1.0 / 3.0,    // /8T
                    0.25,         // /16  (default)
                    1.0 / 6.0,    // /16T
                    0.125,        // /32
                    1.0 / 12.0,   // /32T
                } };

                // 8.11 A4.3: route overlay step-grid layers through resolveActiveLayer().
                // Behavior fix: MachinePicker (funcTrackHeld) now has correct priority
                // over ChromaticInput/LevelsInput — previously checked after them.
                {
                    const int layerAt = keyboardArea_.getActiveTrack();
                    const auto layerMode = (layerAt >= 0)
                                               ? uiState_.trackInputMode[static_cast<std::size_t>(layerAt)]
                                               : TrackInputMode::Play;
                    const LayerFacts stepFacts{ layerMode, layerAt,
                                                layerAt >= 0 && processor_.isLooperTrack(layerAt),
                                                processor_.trackConsoleMode(layerAt) };
                    const SurfaceLayer layer = resolveActiveLayer(
                        uiState_, processor_.editContext(), stepFacts);

                    // --------------------------------------------------------
                    // 9.10: Generator hub (3-key held ≥350 ms)
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::GeneratorHub)
                    {
                        if (ev.index >= 0 && ev.index <= 4)
                        {
                            switch (ev.index)
                            {
                                case 0: enterEuclid(layerAt < 0 ? 0 : layerAt); break;
                                case 1: enterDensitySticky(); break;
                                case 2: enterVelSticky(); break;
                                case 3: enterMelodic(layerAt < 0 ? 0 : layerAt); break;
                                case 4: enterHarmony(layerAt < 0 ? 0 : layerAt); break;
                                default: break;
                            }
                        }
                        refreshSurface();
                        return true;
                    }

                    // --------------------------------------------------------
                    // S3: Loop console — the step grid is the always-on transport +
                    // performance surface for a focused looper. Top row 0-7 =
                    // transport/length; bottom row 8-15 = performance (beat-repeat S5,
                    // tape FX S6). PLAY/STOP/DUB disambiguate off the loop state since
                    // the underlying Cmd::PlayStop / RecordCycle are toggles.
                    if (layer == SurfaceLayer::LooperConsole)
                    {
                        const int trk = processor_.focusTrack();
                        // 11.8 (§40.5): the TRACKS page — ARM/MUTE/SOLO/SRC per
                        // sub-track. Rows past the count are inert.
                        if (uiState_.deckConsolePage == 1
                            && processor_.looperSubTrackCount(trk) > 1)
                        {
                            const int row = ev.index / 4;   // sub-track
                            const int col = ev.index % 4;
                            if (row < processor_.looperSubTrackCount(trk))
                            {
                                switch (col)
                                {
                                    case 0: processor_.looperToggleSubArmed(trk, row); break;
                                    case 1: processor_.looperToggleSubMute(trk, row);  break;
                                    case 2: processor_.looperToggleSubSolo(trk, row);  break;
                                    // SRC: tap cycles the source; Func+SRC = FIT the
                                    // loaded source to the deck window (§40.3).
                                    default:
                                        if (uiState_.funcHeld)
                                            processor_.fitDeckSubTrack(trk, row);
                                        else
                                            processor_.looperCycleSubSource(trk, row);
                                        break;
                                }
                            }
                            refreshSurface();
                            return true;
                        }
                        const int st = processor_.looperState(trk);  // 0 Idle..5 Armed
                        const double nowMs = juce::Time::getMillisecondCounterHiRes();
                        switch (ev.index)
                        {
                            case 0:  // REC/DUB — tap = record verb (2-tap=now);
                                     // long hold = momentary punch-replace (S4).
                                // Defer the verb to key-up so the same cell can hold.
                                // Evaluate the double-tap at DOWN (records this press
                                // for the next), and arm the long-press.
                                looperReplaceWasDouble_ = gesture_.doubleTap(kLooperRecordToken, nowMs);
                                gesture_.armLongPress(kLooperReplaceToken, nowMs);
                                looperReplaceTrack_ = trk;
                                looperReplaceFired_ = false;
                                break;
                            case 1:  // PLAY — only from Stopped (toggle is state-aware)
                                if (st == 4)
                                    routeLooperVerb(2 /*PlayStop*/,
                                                    gesture_.doubleTap(kLooperPlayToken, nowMs));
                                break;
                            case 2:  // STOP — only while Playing/Overdubbing
                                if (st == 2 || st == 3)
                                    routeLooperVerb(2 /*PlayStop*/,
                                                    gesture_.doubleTap(kLooperPlayToken, nowMs));
                                break;
                            case 3:  routeLooperVerb(3 /*Clear*/); break;   // ERASE
                            case 4:  routeLooperVerb(4 /*Undo*/);  break;   // UNDO
                            case 5:  routeLooperVerb(5 /*Halve*/);  break;  // HALF
                            case 6:  routeLooperVerb(6 /*Double*/); break;  // DBL
                            case 7:  // DUB — explicit overdub toggle while a loop plays
                                if (st == 2 || st == 3) routeLooperVerb(1 /*RecordCycle*/);
                                break;
                            case 8: case 9: case 10: case 11:  // beat-repeat 1/16..1/2 (S5)
                                // Momentary: press captures the grid cell and loops it;
                                // the release (dispatchUp) resyncs. Cell i → rate i-8.
                                processor_.sendLooperPerf(trk, 7 /*BeatRepeat*/, true,
                                                          ev.index - 8);
                                break;
                            case 12: case 13: case 14: case 15:  // tape FX (S6)
                                // Momentary tape FX: cell 12→TapeStop(8) .. 15→Reverse(11).
                                // Release (dispatchUp) resyncs / finalises the graceful stop.
                                processor_.sendLooperPerf(trk, 8 + (ev.index - 12), true);
                                break;
                            default: break;
                        }
                        refreshSurface();
                        return true;
                    }

                    // --------------------------------------------------------
                    // KEY panel (DESIGN §4.10): grid hosts the modifier checkboxes
                    // (cells 0..N-1) and the two symmetric scales (cells 14/15).
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::KeyPanel)
                    {
                        const int modCount = keyModifierCount();
                        if (ev.index >= 0 && ev.index < modCount)
                            keyToggleModifier(uiState_, processor_, ev.index);
                        else if (ev.index == 13)
                            keyToggleSymmetric(uiState_, processor_, ScaleType::Chromatic);
                        else if (ev.index == 14)
                            keyToggleSymmetric(uiState_, processor_, ScaleType::WholeTone);
                        else if (ev.index == 15)
                            keyToggleSymmetric(uiState_, processor_, ScaleType::Diminished);
                        refreshMetaBand();
                        refreshSurface();
                        return true;
                    }

                    // --------------------------------------------------------
                    // 5.7: Retrig overlay (Fill+TRIG held)
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::RetrigPicker)
                    {
                        if (ev.index < 0 || ev.index >= 16) return true;
                        const int at = keyboardArea_.getActiveTrack();
                        if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;

                        const auto* machine = processor_.machineForTrack(at);
                        const auto* sliceable = machine ? dynamic_cast<const ISliceable*>(machine) : nullptr;
                        if (sliceable && sliceable->hasSlices())
                        {
                            const int sliceIdx = ev.index;
                            if (sliceIdx >= sliceable->numSlices()) return true;
                            auto& ctx = processor_.editContext();
                            if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at)
                            {
                                auto& trk = processor_.sequence()
                                                .tracks[static_cast<std::size_t>(at)];
                                for (int heldIdx : ctx.heldSteps())
                                {
                                    if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                                    auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                                    if (s.trigOverride.noteCount == 0)
                                        s.trigOverride.noteCount = 1;
                                    s.trigOverride.notes[0] = sliceIdx;
                                    s.trig = true;
                                }
                                ctx.markParamWritten();
                            }
                            processor_.setRetrigActive(at, true, kRetrigRates[4],
                                                       juce::jlimit(0, 127, sliceIdx));
                            return true;
                        }

                        // 9.10: Non-slicer live stutter removed (fence #11).
                        // Authored ratchet is now set via TRIG meta-band RTG field (slot 5).
                        // RetrigPicker entry is gated to slicer tracks only, so this branch
                        // is only reachable for slicer-less machines (defensive return).
                        return true;
                    }

                    // --------------------------------------------------------
                    // 5.7: SoundPool overlay (Fill+SRC held)
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::SoundPool)
                    {
                        if (ev.index < 0 || ev.index >= 16) return true;
                        const int at = keyboardArea_.getActiveTrack();
                        if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;
                        const int poolSize = processor_.soundPoolSize();
                        if (ev.index >= poolSize) return true;
                        processor_.liveSwapTrackSound(at, ev.index);
                        {
                            auto& ctx = processor_.editContext();
                            if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at)
                            {
                                auto& trk = processor_.sequence()
                                                .tracks[static_cast<std::size_t>(at)];
                                for (int heldIdx : ctx.heldSteps())
                                {
                                    if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                                    auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                                    s.trigOverride.hasSoundId = true;
                                    s.trigOverride.soundId = ev.index;
                                }
                                ctx.markParamWritten();
                            }
                        }
                        return true;
                    }

                    // --------------------------------------------------------
                    // 6.5: Master / Track FX picker step-select (also reachable
                    // from the Track-held SelectTrack case — see applyTrackFxPick).
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::MasterFxPicker)
                        return fxPickerStepDown(ev.index, /*master=*/true);

                    if (layer == SurfaceLayer::TrackFxPicker)
                        return fxPickerStepDown(ev.index, /*master=*/false);

                    // 7c: Route routing-matrix console — a cell is one track; a press
                    // cycles that track's staged output destination.
                    if (layer == SurfaceLayer::MachineConsole)
                    {
                        // 11.4 (§40.5): the Tape console dispatches its own cells;
                        // otherwise this is the Route routing matrix.
                        const int mct = processor_.focusTrack();
                        if (processor_.isTapeTrack(mct))
                        {
                            // Stage 6d: the shared TRACKS page (deckConsolePage 1) —
                            // ARM/MUTE/SOLO/SRC per sub-track, same as the Loop. ARM
                            // is derived from SRC on the Tape (auto-arm), so its cell
                            // is a status light; SRC cycles the source (which arms).
                            if (uiState_.deckConsolePage == 1
                                && processor_.looperSubTrackCount(mct) > 1)
                            {
                                const int row = ev.index / 4;   // sub-track
                                const int col = ev.index % 4;
                                if (row < processor_.looperSubTrackCount(mct))
                                {
                                    switch (col)
                                    {
                                        case 0: break;  // ARM: follows SRC on the Tape
                                        case 1: processor_.looperToggleSubMute(mct, row); break;
                                        case 2: processor_.looperToggleSubSolo(mct, row); break;
                                        default: processor_.looperCycleSubSource(mct, row); break;
                                    }
                                }
                                refreshSurface();
                                return true;
                            }
                            switch (ev.index)
                            {
                                case 0:  // REC / punch (§40.13: a quick second tap
                                         // WHILE recording = retro backfill the
                                         // run-up before the punch-in, not punch-out)
                                {
                                    const double nowMs = juce::Time::getMillisecondCounterHiRes();
                                    const bool dbl = gesture_.doubleTap(kTapeRecordToken, nowMs);
                                    if (dbl && processor_.tapeRecording(mct))
                                    {
                                        const double sr = processor_.getSampleRate();
                                        const double gapMs = std::max(0.0, nowMs - tapeRecLastDownMs_);
                                        const int gap = static_cast<int>(std::lround(
                                            gapMs * 0.001 * (sr > 0.0 ? sr : 44100.0)));
                                        processor_.tapeRetroExtend(mct, gap);
                                        setStatus("Tape: grabbed the run-up (retro punch-in)");
                                    }
                                    else
                                        processor_.tapeApplyVerb(mct, 1);
                                    tapeRecLastDownMs_ = nowMs;
                                    break;
                                }
                                // Stage 4: cells 1/2 (PLAY/STOP) retired — the Tape
                                // follows the main transport, no separate Play/Stop.
                                case 3: processor_.tapeApplyVerb(mct, 3); break;  // CLEAR
                                case 4: processor_.tapeApplyVerb(mct, 4); break;  // UNDO
                                case 8: processor_.dropTapeMarker(mct); break;    // DROP
                                case 9: processor_.tapeCue(mct, -1); break;       // |<
                                case 10: processor_.tapeCue(mct, 0); break;       // CUE nearest
                                case 11: processor_.tapeCue(mct, +1); break;      // >|
                                // §40.2 hold-to-wind: press starts the wind, the
                                // step-release (below) stops it (slews to rest, locates).
                                case 12: processor_.tapeSetScrubRate(mct, -kTapeWindRate); break;  // <<
                                case 13: processor_.tapeSetScrubRate(mct, +kTapeWindRate); break;  // >>
                                default: break;
                            }
                            refreshSurface();
                            return true;
                        }
                        cycleRouteConsoleCell(ev.index);
                        return true;
                    }

                    // --------------------------------------------------------
                    // Machine picker (Func+Track held; §4.7.2)
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::MachinePicker)
                    {
                        const int numMachines = processor_.numAvailableMachines();
                        if (ev.index >= 0 && ev.index < numMachines)
                        {
                            const std::string machineId{
                                processor_.availableMachineInfo(ev.index).id
                            };
                            processor_.setTrackMachine(keyboardArea_.getActiveTrack(), machineId);
                            keyboardArea_.syncToActiveTrack();
                            releaseTransientLatch(CB::TrackScope);
                        }
                        refreshSurface();
                        return true;
                    }
                }

                // ----------------------------------------------------------------
                // 6.5 / 8.26: Animate bypass — FX section key held + step
                // ----------------------------------------------------------------
                if (heldSectionIndex_ == processor_.kFxSecIdx && !uiState_.funcHeld)
                {
                    if (ev.index < 0 || ev.index >= 16) return true;

                    // 8.26: Song+FX focus — target master units (picker must be closed).
                    if (uiState_.masterSection == 5 && !uiState_.masterFxPickerOpen)
                    {
                        // Quadrant: 0-3→FX1, 4-7→FX2, 8-11→SndA, 12-15→SndB.
                        const int unit = ev.index / 4;  // 0-3
                        const bool isInsert = (unit < 2);
                        const int slot = unit % 2;
                        const bool loaded = isInsert
                            ? !processor_.masterInsertId(slot).empty()
                            : !processor_.masterSendId(slot).empty();
                        if (loaded)
                        {
                            if (isInsert)
                                processor_.setMasterInsertBypass(slot, true);
                            else
                                processor_.setMasterSendBypass(slot, true);
                            animateBypassMasterUnit_ = unit;
                        }
                        return true;
                    }

                    const int at = keyboardArea_.getActiveTrack();
                    if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;
                    // Step index selects insert slot: 0-7 → slot 0, 8-15 → slot 1.
                    const int slot = (ev.index >= 8) ? 1 : 0;
                    if (!processor_.trackInsertId(at, slot).empty())
                    {
                        processor_.setTrackInsertBypass(at, slot, true);
                        animateBypassTrack_ = at;
                        animateBypassSlot_ = slot;
                    }
                    return true;
                }

                // MHZ.7.3: CHROMATIC mode — step keys are a piano keyboard.
                // Piano layout via kPianoNoteOffset; dead keys (offset -1) are no-ops.
                // With step held: write noteOverride. Always: trigger live note.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks) && uiState_.trackInputMode[static_cast<std::size_t>(at)] == TrackInputMode::Chromatic)
                    {
                        if (ev.index < 0 || ev.index >= 16) return true;
                        const int semitone = kPianoNoteOffset[static_cast<std::size_t>(ev.index)];
                        if (semitone < 0) return true;  // dead key (D, H, ;)
                        const int note = juce::jlimit(0, 127,
                                                      (uiState_.noteEditOctave + 1) * 12 + semitone);

                        // If a step is held in the EditContext, write noteOverride to it.
                        auto& ctx = processor_.editContext();
                        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at)
                        {
                            auto& trk = processor_.sequence()
                                            .tracks[static_cast<std::size_t>(at)];
                            for (int heldIdx : ctx.heldSteps())
                            {
                                if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                                auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                                if (s.trigOverride.noteCount == 0)
                                    s.trigOverride.noteCount = 1;
                                s.trigOverride.notes[0] = note;
                                s.trig = true;
                            }
                            ctx.markParamWritten();
                        }

                        uiState_.lastPlayedNote[static_cast<std::size_t>(at)] = note;

                        // Poly + velocity: sustain the note until the pad is
                        // released (gate). Push pads supply velocity; QWERTY/mouse
                        // leave ev.velocity 0 → fall back to a default.
                        const int vel = ev.velocity > 0 ? ev.velocity : 100;
                        const auto pad = static_cast<std::size_t>(ev.index);
                        // Release a stale note still parked on this pad (e.g. a
                        // missed note-off) before re-sounding it.
                        if (chromaticHeldNote_[pad] >= 0)
                            processor_.liveNoteOff(chromaticHeldTrack_[pad],
                                                   chromaticHeldNote_[pad]);
                        chromaticHeldNote_[pad] = note;
                        chromaticHeldTrack_[pad] = at;
                        processor_.liveNoteOn(at, note, vel);
                        return true;
                    }
                }

                // MHZ.7.4: LEVELS mode — step cells are 16 velocity buckets.
                // Vel = roundToInt((cellIdx+1)/16 * 127).
                // Always auditions the note at the chosen velocity.
                // Step held → also write velocity override to held step (bypass editorial).
                // No step held + stopped → also set track base velocity.
                // No step held + record-arm + playing → records trig+velocity to nearest step.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks) && uiState_.trackInputMode[static_cast<std::size_t>(at)] == TrackInputMode::Levels)
                    {
                        const int vel = juce::roundToInt((ev.index + 1.0f) / 16.0f * 127.0f);
                        const int pitch = uiState_.lastPlayedNote[static_cast<std::size_t>(at)];
                        const int note = pitch > 0 ? pitch : 60;
                        auto& ctx = processor_.editContext();

                        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at)
                        {
                            // Step held → write velocity override; audition bypasses editorial.
                            auto& trk = processor_.sequence()
                                            .tracks[static_cast<std::size_t>(at)];
                            for (int heldIdx : ctx.heldSteps())
                            {
                                if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                                auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                                s.trigOverride.hasVelocity = true;
                                s.trigOverride.velocity = vel;
                            }
                            ctx.markParamWritten();
                            processor_.triggerNote(at, note, 350, vel, /*bypassEditorial=*/true);
                        }
                        else
                        {
                            // No step held: set base velocity when stopped; always audition.
                            if (!processor_.clock().inPluginPlaying())
                                processor_.sequence()
                                    .tracks[static_cast<std::size_t>(at)]
                                    .trigDefaults.velocity = vel;
                            // Goes through onNoteOn: records if armed+playing; else just plays.
                            processor_.triggerNote(at, note, 350, vel);
                        }

                        refreshSurface();
                        return true;
                    }
                }

                // Master + step: Song (song) select (Phase 7 / DESIGN §16).
                // 9.17: quantized to the launch authority; double-tap = instant.
                if (uiState_.songHeld && !uiState_.morphHeld)
                {
                    if (ev.index >= 0 && ev.index < kNumSongs)
                    {
                        const double now = juce::Time::getMillisecondCounterHiRes();
                        const bool dbl = gesture_.doubleTap(kSongStepTokenBase + ev.index, now);
                        processor_.queueSongSwitch(ev.index, dbl);
                    }
                    refreshSurface();
                    return true;
                }

                // Phrase-length authoring (DESIGN §34.4). The hold re-skins the
                // grid (LengthInRun/Boundary/OutRun in SurfaceModel); the step
                // press sets the length to that absolute (page-aware) index+1.
                //   Phrase + Func + step → focused track's length.
                //   Morph  + Func + step → broadcast: all tracks' length.
                // (Morph is the old "Scene" all-tracks qualifier, renamed in 7.9.)
                // Gated before the bare Phrase/Scene branches so Func qualifies.
                if (uiState_.funcHeld && !uiState_.machineScopeHeld && (uiState_.phraseScopeHeld || uiState_.morphHeld))
                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps + ev.index;
                    const int newLen = absStep + 1;
                    if (uiState_.morphHeld)
                    {
                        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                            processor_.setTrackLength(t, newLen);
                        setStatus(status::lengthAllTracks(newLen));
                    }
                    else
                    {
                        processor_.setTrackLength(keyboardArea_.getActiveTrack(), newLen);
                        setStatus(status::length(newLen));
                    }
                    refreshSurface();
                    return true;
                }

                // Scene + Phrase + step: deviate ALL tracks to phraseIdx.
                // Landing on the diagonal (== current scene index) un-deviates all.
                if (uiState_.sceneHeld && uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                        processor_.deviateAllToPhrase(ev.index);
                    uiState_.phraseScopeUsed = true;
                    refreshSurface();
                    return true;
                }

                // Phrase + step (DESIGN §4.7/§16). Track+Phrase and bare Phrase both
                // deviate the focused track only. 9.17: quantized to the track's
                // launch grid; double-tap = instant.
                if (uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                    {
                        const double now = juce::Time::getMillisecondCounterHiRes();
                        const bool dbl = gesture_.doubleTap(kPhraseStepTokenBase + ev.index, now);
                        processor_.queuePhraseDeviation(keyboardArea_.getActiveTrack(), ev.index, dbl);
                    }
                    uiState_.phraseScopeUsed = true;
                    refreshSurface();
                    return true;
                }

                // Scene + step: scene launch or create-on-empty (DESIGN §16/§23.3).
                //   occupied + no-func + no-mute:  overlay launch (PRINCIPLES §17 — no double-tap floor)
                //   occupied + func:               floor launch unconditionally (Func+Scene+step)
                //   occupied + mute:               reserved for scene-mute (§23.3, no-op)
                //   empty    + no-func + no-mute:  baked-copy create → launch
                //   empty    + func:               baseline-copy create → launch
                //   empty    + mute:               blank scene create → launch
                // The active scene is always treated as occupied (it is live).
                if (uiState_.sceneHeld && !uiState_.machinePickerOpen)
                {
                    if (ev.index >= 0 && ev.index < kScenesPerSong)
                    {
                        const bool funcHeld = uiState_.funcHeld;
                        const bool muteHeld = uiState_.muteHeld;
                        const bool isActive = (ev.index == processor_.activeSectionIdx());
                        const bool occupied = isActive || processor_.sceneSlotOccupied(ev.index);
                        if (occupied && !funcHeld && !muteHeld)
                        {
                            // Always overlay launch — floor is reached only via Func+Scene+step.
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, false);
                            else
                                processor_.setActiveScene(ev.index);
                        }
                        else if (occupied && funcHeld)
                        {
                            // Func + occupied: floor launch.
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, true);
                            else
                                processor_.setActiveSceneToFloor(ev.index);
                        }
                        else if (occupied && muteHeld)
                        {
                            // Mute + occupied: reserved for scene-mute (§23.3).
                        }
                        else if (!occupied && !funcHeld && !muteHeld)
                        {
                            // Empty + bare: baked copy (effective, follows deviations).
                            if (phraseConflictAndConfirm(ev.index, ConfirmKind::CreateScene))
                                break;   // waiting for Yes/No
                            processor_.armUndo(CheckpointScope::Song, 0);
                            processor_.createBakedCopyScene(ev.index);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, false);
                            else
                                processor_.setActiveScene(ev.index);
                            setStatus(status::sceneCreated(ev.index + 1));
                        }
                        else if (!occupied && funcHeld)
                        {
                            // Empty + Func: baseline copy (floor diagonal row, no deviations).
                            if (phraseConflictAndConfirm(ev.index, ConfirmKind::CreateBaselineScene))
                                break;   // waiting for Yes/No
                            processor_.armUndo(CheckpointScope::Song, 0);
                            processor_.createBaselineCopyScene(ev.index);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, false);
                            else
                                processor_.setActiveScene(ev.index);
                            setStatus(status::sceneBaseline(ev.index + 1));
                        }
                        else if (!occupied && muteHeld)
                        {
                            // Empty + Mute: blank scene.
                            processor_.armUndo(CheckpointScope::Song, 0);
                            processor_.createDefaultScene(ev.index);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, false);
                            else
                                processor_.setActiveScene(ev.index);
                            setStatus(status::sceneBlank(ev.index + 1));
                        }
                    }
                    refreshSurface();
                    return true;
                }

                // NoteEdit mode: step keys are a 1-octave chromatic keyboard.
                // Cells 0-11 = semitones C through B; cells 12-15 = unused.
                if (uiState_.noteEditMode)
                {
                    const int semitone = ev.index;
                    if (semitone >= 0 && semitone < 12)
                    {
                        const int absNote = (uiState_.noteEditOctave + 1) * 12 + semitone;
                        const int activeTrack = keyboardArea_.getActiveTrack();
                        auto& trk = processor_.sequence()
                                        .tracks[static_cast<std::size_t>(activeTrack)];
                        for (const int stepIdx : uiState_.noteEditSteps)
                        {
                            if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) continue;
                            auto& s = trk.steps[static_cast<std::size_t>(stepIdx)];
                            auto& staged = uiState_.noteEditStaged[stepIdx];
                            bool inNotes = false;
                            for (int n = 0; n < s.trigOverride.noteCount; ++n)
                                if (s.trigOverride.notes[static_cast<std::size_t>(n)] == absNote)
                                {
                                    inNotes = true;
                                    break;
                                }
                            if (inNotes && staged.count(absNote) == 0)
                                staged.insert(absNote);        // stage for removal
                            else if (inNotes && staged.count(absNote) > 0)
                                staged.erase(absNote);         // cancel removal
                            else if (!inNotes && s.trigOverride.noteCount < kMaxNotesPerStep)
                            {
                                s.trigOverride.notes[static_cast<std::size_t>(s.trigOverride.noteCount++)] = absNote;
                                s.trig = true;
                            }
                        }
                    }
                    refreshSurface();
                    return true;
                }

                // 9.14 Stage 3: StepInspector — a second step press stages/un-stages a P-lock slot.
                // Cell index maps into a packed list of the step's P-locked slots (not by
                // raw slot index). Staged removals are committed on Func release.
                if (uiState_.pLockClearMode)
                {
                    const int cellIdx = ev.index;
                    const int track = uiState_.pLockClearTrack;
                    const int step = uiState_.pLockClearStep;
                    const int numSlots = processor_.numParams(track);
                    if (track >= 0 && step >= 0 && step < kMaxStepsPerTrack)
                    {
                        // Rebuild the packed slot list (same order as the render).
                        const auto& stepData = processor_.sequence()
                                                   .tracks[static_cast<std::size_t>(track)]
                                                   .steps[static_cast<std::size_t>(step)];
                        std::vector<int> lockedSlots;
                        const auto& tov = stepData.trigOverride;
                        if (tov.hasVelocity) lockedSlots.push_back(-2);
                        if (tov.hasGate) lockedSlots.push_back(-3);
                        for (int s = 0; s < numSlots; ++s)
                            if (stepData.overrides.has(s))
                                lockedSlots.push_back(s);

                        if (cellIdx >= 0 && cellIdx < static_cast<int>(lockedSlots.size()))
                        {
                            const int slotIdx = lockedSlots[static_cast<std::size_t>(cellIdx)];
                            if (uiState_.pLockClearStaged.count(slotIdx) > 0)
                                uiState_.pLockClearStaged.erase(slotIdx);  // cancel
                            else
                                uiState_.pLockClearStaged.insert(slotIdx); // stage
                        }
                    }
                    refreshSurface();
                    return true;
                }

                // 5.2: Morph step view — step press toggles A/B pole state.
                // Row 0 (steps 0-7) = A poles, Row 1 (steps 8-15) = B poles.
                // Gate on !funcHeld so Func+step still enters pLockClearMode.
                if (uiState_.morphHeld && !uiState_.funcHeld)
                {
                    const int track = keyboardArea_.getActiveTrack();
                    if (track >= 0)
                    {
                        const int mzLocal = ev.index % 8;
                        const int absSlot = manipulationZone_.slotOffset() + mzLocal;
                        const int pole = (ev.index < 8) ? 0 : 1;
                        const auto key = std::make_pair(track, absSlot);
                        auto& dormant = (pole == 0) ? morphDormantA_ : morphDormantB_;
                        const auto info = processor_.morphWidgetInfo(track, absSlot);
                        const bool isActive = (pole == 0) ? info.inA : info.inB;
                        const bool isDormant = (dormant.count(key) > 0);

                        if (isActive)
                        {
                            // Active → dormant: stash value, remove from overlay.
                            dormant[key] = (pole == 0) ? info.aValue : info.bValue;
                            processor_.removeMorphPole(track, absSlot, pole);
                        }
                        else if (isDormant)
                        {
                            // Dormant → active: restore from stash.
                            processor_.writeMorphPole(track, absSlot, dormant[key], pole);
                            dormant.erase(key);
                        }
                        else
                        {
                            // Dark → active: capture other pole's active value or kit base.
                            float captureVal = processor_.baseParamValue(track, absSlot);
                            if (pole == 0 && info.inB) captureVal = info.bValue;
                            if (pole == 1 && info.inA) captureVal = info.aValue;
                            processor_.writeMorphPole(track, absSlot, captureVal, pole);
                        }

                        // Focus the slot in the MZ so encoders operate on it.
                        processor_.editContext().setActiveSlot(absSlot);
                    }
                    keyboardArea_.setMorphViewState(buildMorphViewState(),
                                                    manipulationZone_.slotOffset(),
                                                    1.0f - processor_.morphFader());
                    refreshSurface();
                    return true;
                }

                // MHZ.9.5: any plain step press while a P-lock latch is active exits
                // the latch (consumed — no trig toggle, no new hold), so you no longer
                // need Func+Func. Checked before the key-repeat guard below so it also
                // fires when you re-press the latched step itself.
                if (processor_.editContext().hasAnyLatchedStep())
                {
                    escapeAllLatches();
                    refreshSurface();
                    return true;
                }

                // Ignore key-repeat (same physical key already in list).
                {
                    bool alreadyHeld = false;
                    for (auto& [code, _] : heldStepKeys_)
                    {
                        if (code == rawCode)
                        {
                            alreadyHeld = true;
                            break;
                        }
                    }
                    if (alreadyHeld) return true;
                }

                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps + ev.index;
                    // Never hold/toggle a cell outside the step array (e.g. a high
                    // page / scrolled-past-end position) — that indexes steps[] OOB.
                    if (absStep < 0 || absStep >= static_cast<int>(kMaxStepsPerTrack))
                        return true;

                    // W7: step latching is now done with hold-step + Func (handled in
                    // the Func key-down case), NOT double-tap. The old double-tap latch
                    // transiently flipped the trig on the first tap's key-up and reverted
                    // it on the second key-down — a live-performance hazard (a stray or
                    // dropped trig if the playhead landed in that window). A plain single
                    // tap now always just toggles the trig, instantly.

                    // Normal (first) press: hold the step.
                    heldStepKeys_.push_back({ rawCode, absStep });
                    uiState_.stepHeld = true;
                    processor_.editContext().hold(keyboardArea_.getActiveTrack(), absStep);
                    editMode_.setTrigHeld(true);
                    // Clear stale trig-toggle tracking on any new step press.
                    lastTrigToggleApplied_ = false;
                    lastTrigToggleStep_ = -1;
                    lastTrigToggleTrack_ = -1;
                    stepInspectorFiredMidHold_ = false;  // re-arm the long-press for this hold

                    // Multi-step hold (Part 2): a bare step-hold builds a pure
                    // multi-step edit context — several steps can be held and edited
                    // at once (Elektron flow). It no longer auto-opens the StepInspector;
                    // that now opens on a long-press of a SINGLE held step (armed below,
                    // fired from the timer). Hold-step + Func still latches (W7, above).
                    uiState_.stepMoveAnchor = absStep;  // home for single-step swap-moves
                    // Fresh hold starts in neither the inspector nor the move panel —
                    // clear leftover move state and re-resolve the MZ band so a prior
                    // Step-Position panel can't linger into this hold.
                    uiState_.stepMoveActive = false;
                    // Arm the long-press → StepInspector gesture. Only a lone held step
                    // (checked at fire time) opens it; a second press cancels the arm.
                    gesture_.armLongPress(absStep, juce::Time::getMillisecondCounterHiRes());
                    refreshMetaBand();

                    refreshSurface();
                }
                return true;
            }

            case ControllerButton::SelectTrack: {
                // When Track is held, QwertyOverlay routes step keys to this case
                // (not the Step case). So the Track-compound gestures must be
                // handled HERE, before the plain track-select fallback below.

                // An open FX picker takes priority over track-select. The picker
                // is entered with Track (track FX) or Song (master FX) held over the
                // FX key, so a following step tap to choose the effect still arrives
                // here while Track is held — without this it fell through to plain
                // track-select and switched the active track instead of picking the
                // effect (edits then landed on the wrong track).
                if (uiState_.masterFxPickerOpen)
                    return fxPickerStepDown(ev.index, /*master=*/true);
                if (uiState_.funcFxHeld)
                    return fxPickerStepDown(ev.index, /*master=*/false);

                // 9.29: machine picker (Track + hold SRC) + step = load the indexed
                // machine on the focused track. Selecting closes the picker, exactly
                // as choosing an effect closes the FX picker — the pick is the verb.
                if (uiState_.machinePickerOpen)
                {
                    if (ev.index >= 0 && ev.index < processor_.numAvailableMachines())
                    {
                        processor_.setTrackMachine(keyboardArea_.getActiveTrack(),
                                                   std::string(processor_.availableMachineInfo(ev.index).id));
                        keyboardArea_.syncToActiveTrack();
                        uiState_.machinePickerOpen = false;
                    }
                    refreshSurface();
                    return true;
                }
                // Track+Phrase+step = sticky per-track deviation for the focused
                // musician (Phrase-alone = unison swap, handled in the Step case).
                // 9.17: quantized to the track's launch grid; double-tap = instant.
                if (uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                    {
                        const double now = juce::Time::getMillisecondCounterHiRes();
                        const bool dbl = gesture_.doubleTap(kPhraseStepTokenBase + ev.index, now);
                        processor_.queuePhraseDeviation(keyboardArea_.getActiveTrack(), ev.index, dbl);
                    }
                    uiState_.phraseScopeUsed = true;
                    refreshSurface();
                    return true;
                }
                // (Track + No delete gesture removed; use Track + Func+O to delete.)
                if (uiState_.trackHeld && processor_.isTrackEmpty(ev.index))
                {
                    // Track+empty step = copy current track's machine+params (no steps),
                    // then select the destination so edits land on the new track.
                    processor_.copyKitTrack(keyboardArea_.getActiveTrack(), ev.index);
                    keyboardArea_.setActiveTrack(ev.index);
                    releaseTransientLatch(CB::TrackScope);
                    refreshSurface();
                    return true;
                }
                keyboardArea_.setActiveTrack(ev.index);
                processor_.setControlAllActive(false);  // specific track chosen; disable control-all
                releaseTransientLatch(CB::TrackScope);
                return true;
            }

            // ── 9.12 Stage 7c: the nav family is grammar-routed ───────────────────
            // Each key kept an if-cascade that re-derived, by hand, the same priority
            // the binding table already encodes (Morph > Phrase > Track > Func > bare).
            // Three rows had silently drifted out of step with those cascades; the table
            // is now the single decider and those rows are corrected.
            //
            // What stays here is everything that is NOT grammar — state the table cannot
            // see, because it is keyed on modifiers and these are not modifiers:
            //   * a sticky overlay consuming the key (Density / Vel),
            //   * an open FX picker or deck console paging with Nav,
            //   * a HELD STEP, where ←/→ move the step and Func+←/→ nudge micro-time.
            // Each is an interception BEFORE the table, exactly as the verb family's
            // scene/phrase intercepts sit before routeVerb (7b).
            case ControllerButton::NavUp:
            case ControllerButton::NavDown: {
                if (consumeDensityStickyKey(ev.button)) return true;
                if (consumeVelStickyKey(ev.button)) return true;
                return routeNav(ev);
            }

            case ControllerButton::NavLeft:
            case ControllerButton::NavRight: {
                const int delta = (ev.button == ControllerButton::NavRight) ? +1 : -1;
                // 9.24 S12: while an FX picker is open, Nav pages the catalogue.
                if (pageFxPicker(delta)) return true;
                if (consumeDensityStickyKey(ev.button)) return true;
                if (consumeVelStickyKey(ev.button)) return true;
                if (pageDeckConsole(delta)) return true;  // 11.8: a looper pages its console

                const int t = keyboardArea_.getActiveTrack();

                // 9.14 Stage 4 / Part 2: hold-step + Func+←/→ = microOffset nudge.
                // Applies to every held step (multi-hold aware).
                if (uiState_.funcHeld && processor_.editContext().isActiveForEditing() && t >= 0)
                {
                    const float micro = nudgeHeldMicro(t, 0.05f * static_cast<float>(delta));
                    uiState_.stepMoveActive = true;  // grid → sequencer, MZ → Step-Position
                    refreshMetaBand();
                    setStatus("micro: " + juce::String(micro, 2));
                    refreshSurface();
                    return true;
                }
                // 9.14 / Part 2: hold-step + ←/→ = move the held step(s). Single step =
                // anchor-carry; multiple = block move (clamped).
                if (!uiState_.funcHeld && processor_.editContext().isActiveForEditing() && t >= 0)
                {
                    if (moveHeldSteps(t, delta))
                    {
                        processor_.editContext().markParamWritten();
                        uiState_.stepMoveActive = true;
                        refreshMetaBand();
                        setStatus(delta > 0 ? "step(s) moved right" : "step(s) moved left");
                        refreshSurface();
                    }
                    return true;  // consume while a step is held (never page-flip)
                }
                return routeNav(ev);
            }

            // MHY.4: right-utility verbs. Without a scope modifier these perform their
            // default transport / confirmation action; with a scope held, EditMode
            // routes them as grammar verbs (CPY / PST / CLR / confirm / cancel).

            case ControllerButton::VerbPlay: {
                using PS = EditMode::PrimaryScope;
                // 9.17: Mute + Play arms the per-track relaunch/retrigger view
                // (Mute+Play+step). Suppress the normal transport toggle while the
                // chord is held; the step press applies the phase-reset.
                if (uiState_.muteHeld)
                {
                    uiState_.relaunchHeld = true;
                    playKeyHeld_ = true;
                    refreshSurface();
                    return true;
                }
                // S3: the looper transport moved OFF the Track+U/I/O verbs onto the
                // always-on console (the step grid). U/I/O on a focused looper are now
                // the ordinary clipboard verbs again (freed for other uses).
                // Scope held → grammar verb (Paste).
                if (editMode_.primaryScope() != PS::None && editMode_.primaryScope() != PS::Func)
                    return routeVerb(ev);   // 9.12 st.7b: the table names the action
                // Func+I (no non-trivial scope) = unqualified paste.
                // Stamps the single captured layer; rejects when type is All.
                if (editMode_.scopeState().func)
                {
                    using CT = ClipboardType;
                    if (clipboard_.type == CT::None)
                    {
                        setStatus(status::nothingCopied());
                    }
                    else if (clipboard_.type == CT::All)
                    {
                        setStatus(status::pastePickScope());
                    }
                    else
                    {
                        // Map clipboard type → matching scope and dispatch paste.
                        static constexpr PS kSynScopes[] = {
                            PS::None,    // None (unreachable here)
                            PS::Trig,    // Step
                            PS::Section, // Section
                            PS::Track,   // Track
                            PS::Phrase,  // Pattern
                            PS::Scene,   // Scene
                            PS::None,    // All (handled above)
                        };
                        const auto ctIdx = static_cast<std::size_t>(clipboard_.type);
                        const PS synScope = (ctIdx < std::size(kSynScopes))
                                                ? kSynScopes[ctIdx] : PS::None;
                        if (synScope != PS::None)
                        {
                            auto ctx = commandContext();
                            (void)commandCore_.handleVerb(synScope, ev.button, ctx, *editorEffects_);
                            refreshSurface();
                        }
                    }
                    return true;
                }
                if (playKeyHeld_) return true;  // ignore key repeat
                playKeyHeld_ = true;

                const double now = juce::Time::getMillisecondCounterHiRes();
                // Layered stop by Play tap-count (DESIGN): 1 = play/graceful-stop
                // toggle, 2 = track cut (sends+master ring), 3 = master cut (dead).
                // All hold phase; rewind is decoupled onto hold-Record. Immediate-
                // then-upgrade: tap 1 acts now, taps 2/3 escalate the silence.
                const int taps = gesture_.playTapCount(now);
                if (taps >= 3)
                    processor_.transportMasterCut();
                else if (taps == 2)
                    processor_.transportTrackCut();
                else
                    processor_.transportPlay();
                return true;
            }

            case ControllerButton::VerbClear: {
                // 9.29: DELETE is a HOLD of Clear under a deletable scope (Track /
                // Phrase / Scene). Tap still clears that scope's contents. Armed here
                // and resolved on key-up (the timer fires it mid-hold so the picker
                // appears while the key is still down), which is the same shape the FX
                // and machine section pickers use. The Machine scope is excluded: under
                // Func+Track, Clear is INIT and stays a tap.
                if (deleteHoldCapable())
                {
                    gesture_.armLongPress(kDeleteHoldToken,
                                          juce::Time::getMillisecondCounterHiRes());
                    clearHoldArmed_ = true;
                    clearHoldFired_ = false;
                    return true;   // deferred
                }
                return clearVerbTap(ev);
            }

            // (the Clear key's TAP behaviour moved verbatim into clearVerbTap, below)

            // ControllerButton::VerbDelete — migrated to CommandCore::handleDown (8.24 Stage 7)
            // ControllerButton::VerbPanic — migrated to CommandCore::handleDown (8.11 A4.6)

            case ControllerButton::VerbRecord: {
                using PS = EditMode::PrimaryScope;
                // S3: looper REC moved to the always-on console; Track+Record is the
                // ordinary clipboard/scope verb again.
                // Func+Song+Record = CAPTURE (the tape-deck cell, global scope).
                // All capture gestures live on this cell's own timeline: arm the
                // long-press and defer tap/double-tap/long-press resolution to
                // key-up (mid-hold long-press fires from the timer).
                if (editMode_.scopeState().func && uiState_.songHeld)
                {
                    gesture_.armLongPress(kCaptureToken,
                                          juce::Time::getMillisecondCounterHiRes());
                    captureCellHeld_ = true;
                    captureLongPressFired_ = false;
                    return true;  // action deferred to key-up
                }
                // Scene + Record: bake live deviations into home-row phrase content.
                // Destructive — requires Yes/No confirmation.
                // Func+Scene+Record is "copy scene" — falls through to handleVerb.
                if (uiState_.sceneHeld && !editMode_.scopeState().func)
                {
                    const int nd = processor_.countDeviatedTracks();
                    if (nd == 0)
                    {
                        setStatus(status::noDeviationsToBake());
                        return true;
                    }
                    uiState_.confirm = { ConfirmKind::BakeScene, -1 };
                    setStatus(status::confirmBake(nd, processor_.activeSectionIdx()));
                    refreshSurface();
                    return true;
                }
                // Scope held → grammar verb (e.g. copy).  No scope → arm recording.
                if (editMode_.primaryScope() != PS::None && editMode_.primaryScope() != PS::Func)
                    return routeVerb(ev);   // 9.12 st.7b: the table names the action
                // Func+U with no non-trivial scope = omni copy (capture all layers).
                if (editMode_.scopeState().func && editMode_.primaryScope() == PS::Func)
                {
                    captureScene();           // fills scene layer
                    {
                        const int t = keyboardArea_.getActiveTrack();
                        if (t >= 0 && t < static_cast<int>(kNumTracks))
                            clipboard_.clipTrack =
                                processor_.sequence().tracks[static_cast<std::size_t>(t)];
                    }
                    clipboard_.clipSequence = processor_.sequence();
                    clipboard_.type = ClipboardType::All;
                    setStatus(status::capturedAll());
                    return true;
                }
                // Bare Record: hold = transport reset (rewind), tap = record-arm,
                // double-tap = overdub. Reset must not fire on press (that would make
                // record-arm the accidental default of a hold), and firing on release
                // is fine for a reset (unlike Play, which stays press-triggered). So
                // arm the long-press and defer tap/double-tap/reset resolution to
                // key-up (mirrors the CAPTURE cell); the timer may fire the reset
                // mid-hold.
                gesture_.armLongPress(kTransportResetToken,
                                      juce::Time::getMillisecondCounterHiRes());
                recordResetHeld_ = true;
                recordResetFired_ = false;
                return true;
            }

            case ControllerButton::VerbSnapshot: {
                // 5.5: Euclid modal armed → Y is inert (commit is on bare P).
                if (uiState_.euclidHeld)
                    return true;
                // 9.4 item C: the Scene+Y → SYNC intercept that sat here is gone. SYNC is
                // Scene+O now (verbs::scene), so Y is the snapshot verb in every scope with
                // no exception carved out of it -- and Scene finally gets a mark of its own.
                // 9.4 item B: EVERY scope routes, including no-scope. Y used to fork here
                // -- a scope routed to the table while a bare Y snapshotted the Song
                // inline -- so the Song snapshot had a second implementation that
                // verbs::noScope shadowed but never ran, and no status message reached
                // the player. One verb, one path, one place that decides the scope.
                // (Func+Y cannot arrive: ButtonLayers remaps it to CB::Restore upstream.)
                return routeVerb(ev);   // 9.12 st.7b: the table names the action
            }

            case ControllerButton::VerbConfirm: {
                const bool funcHeld = editMode_.scopeState().func;

                // 9.24 S16: Confirm on a convolution FX-picker slot opens the pool
                // browser in pick-IR mode for that slot.
                if (tryOpenIrPicker()) return true;

                // 7c: Route routing console open → bare P = commit staged routes;
                // Func+P = cancel (revert). Both close the console.
                if (uiState_.routeConsoleActive)
                {
                    if (funcHeld) { closeMachineConsole(); setStatus("ROUTING cancelled"); }
                    else          { commitRouteConsole();  setStatus("ROUTING committed"); }
                    refreshSurface();
                    return true;
                }

                // 5.5: Euclid modal armed → bare P = commit; Func+P = cancel.
                if (uiState_.euclidHeld)
                {
                    // Both paths revert the live preview first; commit then snapshots
                    // the (pre-Euclid) phrase and re-applies the pattern over it.
                    restoreEuclidStash();
                    if (!funcHeld)
                    {
                        const auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(euclidTrack_)];
                        if (wt.length > 0)
                            processor_.armUndo(CheckpointScope::Phrase, euclidTrack_);
                        applyEuclidToTrack(euclidTrack_);
                        setStatus("EUCLID committed");
                    }
                    uiState_.resetEuclid();
                    forgetEuclidEditorState();
                    refreshMetaBand();
                    refreshSurface();
                    return true;
                }

                // 10.7: Melodic generator armed → bare P = print; Func+P = cancel.
                if (uiState_.melodicHeld)
                {
                    restoreMelodyStash();
                    if (!funcHeld)
                    {
                        const auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(melodicTrack_)];
                        if (wt.length > 0)
                            processor_.armUndo(CheckpointScope::Phrase, melodicTrack_);
                        applyMelodyToTrack(melodicTrack_);
                        setStatus("MELODY printed");
                    }
                    uiState_.resetMelodic();
                    forgetMelodyEditorState();
                    refreshMetaBand();
                    refreshSurface();
                    return true;
                }

                // 10.8: Harmonic voice-mover armed → bare P = print; Func+P = cancel.
                if (uiState_.harmonyHeld)
                {
                    restoreHarmonyStash();
                    if (!funcHeld)
                    {
                        const auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(harmonyTrack_)];
                        if (wt.length > 0)
                            processor_.armUndo(CheckpointScope::Phrase, harmonyTrack_);
                        applyHarmonyToTrack(harmonyTrack_);
                        setStatus("CHORD printed");
                    }
                    uiState_.resetHarmony();
                    forgetHarmonyEditorState();
                    refreshMetaBand();
                    refreshSurface();
                    return true;
                }

                // 9.4 item C: CANCEL a queued scene launch. This used to live on the Clear
                // key (Scene+O / Phrase+O), where it shadowed two real verbs -- Scene's SYNC
                // and Phrase's CLEAR PHRASE both dispatched to nothing because this intercept
                // ran first. A queued launch is a PENDING action, and Cancel (Func+P) is the
                // grammar's verb for pending actions -- the same seat that cancels the route
                // console, Euclid, and the generators just above. Scoped to the two modifiers
                // that used to carry it, so the fingering is one key longer, not relocated.
                if (funcHeld && (uiState_.sceneHeld || uiState_.phraseScopeHeld)
                    && processor_.hasQueuedScene())
                {
                    processor_.cancelQueuedScene();
                    setStatus(status::cancelled());
                    if (uiState_.phraseScopeHeld) uiState_.phraseScopeUsed = true;
                    refreshSurface();
                    return true;
                }

                // Both primary P (Yes/confirm) and Func+P (No/cancel) arrive here as VerbConfirm.
                // Distinguish by whether Func is held.
                // Note: when confirm is pending, CommandCore::handleDown intercepts VerbConfirm
                // before this point and calls executeConfirm / status::cancelled() directly.

                // 9.12 st.7e: everything below the modal intercepts is grammar. The table
                // decides: scope+P = QUANT, bare P = the confirm verb, Func+P = cancel.
                // (A PENDING confirm never reaches here — CommandCore::handleDown
                // intercepts VerbConfirm far upstream and executes or cancels it.)
                //
                // Resolved on the ACTIVE LAYER, because the step-scoped quantize is a
                // layer, not a modifier: while a step is held the grid is the inspector,
                // and P means "quantize the held step(s)".
                return routeConfirm(ev);
            }

            case ControllerButton::Snapshot:
                // Reserved while a section-suite scope is held (see helper above):
                // Func+scope+Yes is that scope's secondary, not a global snapshot.
                if (sectionSuiteScopeHeld(uiState_)) return true;
                {
                    int ckTrk = 0;
                    processor_.snapshot(ckScope(ckTrk), ckTrk);
                }
                refreshSurface();
                return true;
            case ControllerButton::Restore:
                // Func+Y = Restore. Resolve on key-up (tap = pop one, hold = jump to floor).
                if (sectionSuiteScopeHeld(uiState_)) return true;
                gesture_.armLongPress(kRestoreLongPressToken,
                                      juce::Time::getMillisecondCounterHiRes());
                return true;

            // ControllerButton::PlayStop — migrated to CommandCore::handleDown (8.4h)
            case ControllerButton::StopReset:
                if (uiState_.noteEditMode)  // Func+E = NavLeft: octave down
                {
                    uiState_.noteEditOctave = std::max(uiState_.noteEditOctave - 1, 0);
                    refreshSurface();
                    return true;
                }
                // Mode-aware (v27): standalone/Auto → stop + reset (re-anchor to
                // step 0 next start); hosted-Locked → park the plugin.
                processor_.transportStopReset();
                return true;
            case ControllerButton::RecordArm: {
                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = gesture_.doubleTap(
                    1000 + static_cast<int>(ControllerButton::RecordArm), now);
                if (isDouble)
                {
                    processor_.clock().setRecordArmed(true);
                    processor_.clock().setOverdubArmed(true);
                }
                else
                {
                    processor_.clock().setOverdubArmed(false);
                    processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                }
                return true;
            }

            // 8.11 A4.1: mute/solo cluster migrated to KeyBindings dispatch.
            case ControllerButton::ToggleMute: {
                const int trackIdx = ev.index;
                if (trackIdx < 0 || trackIdx >= static_cast<int>(kNumTracks))
                    return true;
                const int at = keyboardArea_.getActiveTrack();
                const auto inputMode = (at >= 0)
                                           ? uiState_.trackInputMode[static_cast<std::size_t>(at)]
                                           : TrackInputMode::Play;
                const LayerFacts facts{ inputMode, at,
                                        at >= 0 && processor_.isLooperTrack(at),
                                        processor_.trackConsoleMode(at) };
                const auto layer = resolveActiveLayer(uiState_, processor_.editContext(), facts);
                const auto heldMods = heldModsFromUiState(uiState_);
                const auto& binding = resolveBinding(ev.button, trackIdx, heldMods, layer);
                if (binding.action != ActionId::None)
                {
                    auto ctx = commandContext();
                    return commandCore_.handleAction(binding.action, ev, ctx, *editorEffects_);
                }
                return true;
            }

            // ControllerButton::MetronomeToggle — migrated to CommandCore::handleDown (8.4h)
            // ControllerButton::TapTempo — migrated to CommandCore::handleDown (8.11 A4.6)

            case ControllerButton::None:
                return false;

            default:
                return false;
        }
        return false;
    }

    bool LockstepEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
    {
        const int rawCode = key.getKeyCode();
        const int uCode = (rawCode >= 'a' && rawCode <= 'z')
                              ? rawCode - ('a' - 'A')
                              : rawCode;

        // Suppress OS key-repeat: if we already saw this key go down, ignore.
        if (!heldKeys_.insert(uCode).second)
            return true;

        if (QwertyOverlay::isEdgeKey(uCode))
            return true;

        const auto ev = qwerty_.resolve(uCode,
                                        uiState_.funcHeld,
                                        uiState_.trackHeld,
                                        uiState_.muteHeld);

        pressTracker_.press(uCode, ev.button, ev.index);
        return dispatchDown(ev, uCode);
    }

    // dispatchUp — source-agnostic button-up handler.
    // rawCode: physical key code (keyboard) or 0 (mouse / no key).
    void LockstepEditor::dispatchUp(ControllerEvent ev, int rawCode)
    {
        using CB = ControllerButton;
        using T = ControllerEvent::Type;

        // 6.4: swallow the Mute key-up that completed a Cue+Mute cue toggle, so it
        // does not fall through to the normal mute-view up handler (DESIGN §31).
        if (ev.button == CB::MuteScope && cueMuteChordActive_)
        {
            cueMuteChordActive_ = false;
            return;
        }

        // 5.5: Cue scope (Func+3) releases. The '3' key-up exits the scope; a
        // step-up ends that step's audition. Intercepted before any other handler
        // so neither the command core nor legacy dispatch consumes them.
        if (cueViaFunc_)
        {
            if (ev.button == CB::Step) { auditionStepOff(ev.index); return; }
            if (ev.button == CB::TapTempo) { exitCueScope(); return; }
        }

        // Phase 8.4 command core: try migrated handlers first.
        {
            auto ctx = commandContext();
            if (commandCore_.handleUp(ev, ctx, *editorEffects_))
                return;
        }

        switch (ev.button)
        {
            case CB::Func:
                physHeld_.func = false;
                // MD.7/MD.8: apply deferred pattern mute toggles atomically on Func release.
                for (const int t : deferredPatternMutes_)
                    processor_.togglePatternMute(t);
                deferredPatternMutes_.clear();
                uiState_.pendingPatternMuteToggle.fill(false);

                // 9.29: a LATCHED compound scope (Machine / Set) survives this release.
                // Func is the only modifier that cannot latch on its own, so the latch
                // on Track/Song carries it: while latch.compound is set, funcHeld stays
                // virtually true and the release ends here. Everything below — exiting
                // the Func re-skins, dropping the scope, restoring the MZ band — is the
                // teardown of a Func layer that is, in this case, still standing.
                if (uiState_.latch.compound)
                {
                    refreshSurface();
                    break;
                }
                uiState_.funcHeld = false;
                // Commit staged edits before exiting (these call processor_ and cannot
                // live inside the pure exitFuncReskin helper).
                // NoteEdit: apply staged note removals.
                if (uiState_.noteEditMode)
                {
                    const int activeTrack = keyboardArea_.getActiveTrack();
                    auto& trk = processor_.sequence()
                                    .tracks[static_cast<std::size_t>(activeTrack)];
                    for (auto& [stepIdx, staged] : uiState_.noteEditStaged)
                    {
                        if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) continue;
                        auto& s = trk.steps[static_cast<std::size_t>(stepIdx)];
                        int newCount = 0;
                        std::array<int, kMaxNotesPerStep> kept{};
                        for (int n = 0; n < s.trigOverride.noteCount; ++n)
                            if (staged.count(s.trigOverride.notes[static_cast<std::size_t>(n)]) == 0)
                                kept[static_cast<std::size_t>(newCount++)] = s.trigOverride.notes[static_cast<std::size_t>(n)];
                        s.trigOverride.noteCount = newCount;
                        for (int n = 0; n < newCount; ++n)
                            s.trigOverride.notes[static_cast<std::size_t>(n)] = kept[static_cast<std::size_t>(n)];
                    }
                }
                // PLockClear: apply staged P-Lock slot removals.
                if (uiState_.pLockClearMode)
                {
                    for (const int slot : uiState_.pLockClearStaged)
                    {
                        if (slot < 0)
                            processor_.clearTrigOverrideField(uiState_.pLockClearTrack,
                                                              uiState_.pLockClearStep,
                                                              -(slot + 1));
                        else
                            processor_.clearParam(uiState_.pLockClearTrack,
                                                  uiState_.pLockClearStep, slot);
                    }
                }
                // Stage 4: unified exit for all Func-layer modes (NoteEdit, PLockClear,
                // MachinePicker, TrackFxPicker, MasterFxPicker). Each reset is safe
                // when its mode is not active so a single call covers all cases.
                exitFuncReskin(uiState_);
                uiState_.machineScopeHeld = false;   // 9.29: half the compound left
                refreshMetaBand();  // 1c: Func released → restore normal MZ band
                editMode_.onScopeEvent({ T::ButtonUp, CB::Func });
                updateFillActivation();
                refreshSurface();
                break;

            // Scope modifier releases: CommandCore::handleUp clears xxxHeld and fires
            // editMode_.onScopeEvent + repaint if not latched (8.11 A4.2). Per-scope
            // unique side effects run here when xxxHeld was cleared (i.e. !xxxHeld).
            case CB::TrackScope:
                physHeld_.track = false;
                // 9.29: half the compound left — unless the compound itself is latched,
                // in which case both halves are virtually held and the scope stands.
                if (!uiState_.latch.compound)
                    uiState_.machineScopeHeld = false;
                if (!uiState_.trackHeld)  // cleared by handleUp → not latched
                {
                    processor_.setControlAllActive(false);  // MD.10
                    clearSwingDismissed();
                }
                break;

            case CB::PhraseScope:
                physHeld_.phrase = false;
                if (!uiState_.phraseScopeHeld)
                {
                    // 5.5: euclid modal stays armed after key release — do not commit here.
                    // Yes commits; No cancels; double-tap Func escapes.
                    uiState_.phraseScopeUsed = false;
                }
                break;

            case CB::SceneScope:
                physHeld_.scene = false;
                if (!uiState_.sceneHeld)
                {
                    clearSwingDismissed();
                }
                break;

            case CB::MuteScope:
                physHeld_.mute = false;
                // No unique side effects beyond what handleUp already does.
                break;

            case CB::FillScope:
                physHeld_.fill = false;
                if (!uiState_.fillHeld)
                {
                    // 5.5: euclid modal stays armed after key release — do not commit here.
                    // Yes commits; No cancels; double-tap Func escapes.
                    updateFillActivation();
                    // 5.7: release any momentary trig-grid overlay (Retrig / SoundPool).
                    if (uiState_.trigGridMode == TrigGridMode::SoundPool)
                    {
                        const int at = keyboardArea_.getActiveTrack();
                        if (at >= 0 && at < static_cast<int>(kNumTracks))
                            processor_.clearLiveSwap(at);
                    }
                    else if (uiState_.trigGridMode == TrigGridMode::Retrig)
                    {
                        processor_.setRetrigActive(0, false);
                    }
                    uiState_.trigGridMode = TrigGridMode::Default;
                }
                break;

            case CB::CueScope:
                physHeld_.cue = false;
                // Cue is not latchable; handleUp always clears it + fires scope event.
                break;

            case CB::MorphScope:
                physHeld_.morph = false;
                uiState_.morphNavQualifier = 0;
                manipulationZone_.setMorphQualifier(0);
                if (!uiState_.morphHeld)
                {
                    morphDormantA_.clear();
                    morphDormantB_.clear();
                }
                manipulationZone_.setMorphHeld(uiState_.morphHeld);
                break;

            case CB::SongScope:
                physHeld_.song = false;
                uiState_.masterFxPickerOpen = false;
                if (!uiState_.songHeld)
                {
                    clearSwingDismissed();
                }
                break;

            case CB::Section:
            case CB::MetaSection: {
                // ── 9.12 st.7d: resolve the section tap-vs-hold ───────────────────
                // One block for every picker. The action was decided BY THE TABLE at arm
                // time (sectionHoldAction_), so the three ways a hold can end — fired by
                // the timer, released just past threshold, released early (a tap) —
                // cannot disagree about which picker they were opening.
                bool suppressFxPickerClose = false;
                if (sectionHoldAction_ != ActionId::None)
                {
                    using LPR = GestureRecognizer::LongPressResult;
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    const bool trackFx = (sectionHoldAction_ == ActionId::OpenTrackFxPicker);

                    if (sectionHoldFired_)
                    {
                        // Picker already opened during the hold (timer path). Consume the
                        // arm and keep it open; the TRACK FX picker (funcFxHeld) must
                        // survive the funcFxHeld=false cleanup below — but only if it is
                        // still open: choosing an effect while holding closes it, and the
                        // release must not re-open it.
                        gesture_.cancelLongPress();
                        if (trackFx && uiState_.funcFxHeld)
                            suppressFxPickerClose = true;
                    }
                    else switch (gesture_.checkLongPress(kSectionHoldToken, now))
                    {
                        case LPR::ShortHold:
                            // A TAP on a key that also carries a hold. For the FX key the
                            // tap cycles the picker's target slot while it is open (the old
                            // re-hold-to-cycle was the pain point); otherwise it is an
                            // ordinary section nav, which is the grammar's own tap row.
                            if (fxSectionPickerWantsMaster_ && uiState_.masterFxPickerOpen)
                            {
                                cycleFxPickerSlot(/*master=*/true);
                            }
                            else if (trackFx && uiState_.funcFxHeld)
                            {
                                cycleFxPickerSlot(/*master=*/false);
                                suppressFxPickerClose = true;   // keep it open past cleanup
                            }
                            else if (fxSectionPickerWantsMaster_)
                            {
                                if (uiState_.masterSection == LockstepProcessor::kFxSecIdx)
                                    uiState_.masterFxInsertSlot =
                                        nextLoadedMasterUnit(uiState_.masterFxInsertSlot);
                                else
                                    uiState_.masterFxInsertSlot = firstLoadedMasterUnit();
                                keyboardArea_.selectMetaSection(LockstepProcessor::kFxSecIdx,
                                                                /*toggle=*/false);
                                refreshMetaBand();
                            }
                            else
                            {
                                // The tap row, through the grammar (7d): Track+tap(SRC)
                                // pages SRC, Track+tap(FX) pages the FX params.
                                const ControllerEvent tap{ ControllerEvent::Type::ButtonDown,
                                                           CB::Section, heldSectionIndex_, 0 };
                                routeSection(tap);
                            }
                            break;
                        case LPR::LongHold:
                            // Released just past the threshold, before the timer ticked.
                            fireSectionHold();
                            if (trackFx)
                                suppressFxPickerClose = true;   // keep funcFxHeld past cleanup
                            break;
                        case LPR::NotArmed:
                            break;
                    }
                    sectionHoldAction_ = ActionId::None;
                    sectionHoldFired_ = false;
                }

                // 7b: resolve a deferred machine-console section press.
                if (machineConsoleArmed_)
                {
                    using LPR = GestureRecognizer::LongPressResult;
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (gesture_.checkLongPress(kMachineConsoleLongPressToken, now) == LPR::LongHold)
                    {
                        if (uiState_.machineConsoleOpen)
                            closeMachineConsole();            // re-hold closes (revert)
                        else
                            openMachineConsole();             // snapshot scratch + open
                        refreshSurface();
                    }
                    else if (heldSectionIndex_ >= 0)
                    {
                        keyboardArea_.selectSection(heldSectionIndex_);  // tap → page params
                    }
                    machineConsoleArmed_ = false;
                }
                heldSectionRawCode_ = -1;
                heldSectionIndex_ = -1;
                uiState_.funcSrcHeld = false;
                if (!uiState_.funcHeld)
                    uiState_.funcFxHeld = false;
                editMode_.setSectionHeld(false);
                // Set after cleanup so the cleanup's funcFxHeld=false doesn't undo it.
                if (suppressFxPickerClose)
                    uiState_.funcFxHeld = true;
                break;
            }

            case CB::VerbPlay:
                playKeyHeld_ = false;
                // 9.17: releasing Play exits the Mute+Play relaunch chord.
                if (uiState_.relaunchHeld)
                {
                    uiState_.relaunchHeld = false;
                    refreshSurface();
                }
                break;

            case CB::Step: {
                // FX picker: resolve a deferred loaded-cell press (tap = bypass,
                // long-press = remove). Sticky picker path (funcFxHeld / master).
                if (fxPickerStepUp(ev.index))
                    break;

                // S4: looper console cell 0 (REC/DUB) resolves on release — a long
                // hold was punch-replace (end it now); a short tap is the record verb
                // (double-tap captured at key-down = record/punch now). Always fires
                // on release so a replace can never stick.
                if (ev.index == 0 && looperReplaceTrack_ >= 0)
                {
                    const int trk = looperReplaceTrack_;
                    looperReplaceTrack_ = -1;
                    if (looperReplaceFired_)
                    {
                        processor_.sendLooperPerf(trk, 12 /*ReplacePunch*/, false);
                        looperReplaceFired_ = false;
                    }
                    else if (processor_.isLooperTrack(trk))
                    {
                        routeLooperVerb(1 /*RecordCycle*/, looperReplaceWasDouble_);
                    }
                    refreshSurface();
                    break;
                }

                // S5/S6: looper performance cells are momentary — a step-release on a
                // console cell (8-11 beat-repeat, 12-15 tape FX) must always end the
                // effect, even if an overlay opened while held, so it can never stick.
                if (ev.index >= 8 && ev.index <= 15)
                {
                    const int trk = processor_.focusTrack();
                    if (processor_.isLooperTrack(trk))
                    {
                        if (ev.index <= 11)
                            processor_.sendLooperPerf(trk, 7 /*BeatRepeat*/, false, ev.index - 8);
                        else
                            processor_.sendLooperPerf(trk, 8 + (ev.index - 12), false);
                    }
                    // §40.2 hold-to-wind: releasing a Tape FF/RW cell ends the wind
                    // (the machine slews to rest, then the transport settles). Always
                    // fires on release so a wind can never stick.
                    else if (processor_.isTapeTrack(trk) && uiState_.deckConsolePage == 0
                             && (ev.index == 12 || ev.index == 13))
                        processor_.tapeSetScrubRate(trk, 0.0);
                }

                // CHROMATIC gate: a pad-release always ends the note it sounded —
                // before any mode-specific handling, and regardless of the current
                // mode/octave/track (we release the exact note we stored on press),
                // so notes can never hang. Poly: each pad releases independently.
                if (ev.index >= 0 && ev.index < 16)
                {
                    const auto pad = static_cast<std::size_t>(ev.index);
                    if (chromaticHeldNote_[pad] >= 0)
                    {
                        processor_.liveNoteOff(chromaticHeldTrack_[pad],
                                               chromaticHeldNote_[pad]);
                        chromaticHeldNote_[pad] = -1;
                    }
                }

                // 6.5 / 8.26 Animate bypass restore: step-up ends the momentary bypass.
                if (animateBypassTrack_ >= 0)
                {
                    processor_.setTrackInsertBypass(animateBypassTrack_, animateBypassSlot_, false);
                    animateBypassTrack_ = -1;
                    animateBypassSlot_ = -1;
                }
                if (animateBypassMasterUnit_ >= 0)
                {
                    const int unit = animateBypassMasterUnit_;
                    const bool isInsert = (unit < 2);
                    const int slot = unit % 2;
                    if (isInsert)
                        processor_.setMasterInsertBypass(slot, false);
                    else
                        processor_.setMasterSendBypass(slot, false);
                    animateBypassMasterUnit_ = -1;
                }

                // 9.14 Stage 3: NoteEdit entered via inspector (SRC tap while step held).
                // On step release, check if this is the original held step — if so,
                // commit the staged notes and exit NoteEdit. Chromatic key releases
                // (notes being released) are not in heldStepKeys_ and fall through.
                if (uiState_.noteEditMode)
                {
                    bool isOriginalStep = false;
                    for (const auto& [code, _] : heldStepKeys_)
                    {
                        if (code == rawCode) { isOriginalStep = true; break; }
                    }
                    if (!isOriginalStep)
                    {
                        refreshSurface();
                        break;
                    }
                    // Original step releasing — commit staged notes.
                    for (auto& [stepIdx, staged] : uiState_.noteEditStaged)
                    {
                        if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) continue;
                        const int tk = processor_.editContext().heldTrackIndex();
                        if (tk < 0) continue;
                        auto& s = processor_.sequence()
                                      .tracks[static_cast<std::size_t>(tk)]
                                      .steps[static_cast<std::size_t>(stepIdx)];
                        for (const int note : staged)
                        {
                            bool found = false;
                            for (int n = 0; n < s.trigOverride.noteCount; ++n)
                            {
                                if (s.trigOverride.notes[static_cast<std::size_t>(n)] == note)
                                    { found = true; break; }
                            }
                            if (found)
                            {
                                // Remove the note.
                                int nc = s.trigOverride.noteCount;
                                for (int n = 0; n < nc; ++n)
                                {
                                    if (s.trigOverride.notes[static_cast<std::size_t>(n)] == note)
                                    {
                                        s.trigOverride.notes[static_cast<std::size_t>(n)] =
                                            s.trigOverride.notes[static_cast<std::size_t>(nc - 1)];
                                        s.trigOverride.noteCount = nc - 1;
                                        break;
                                    }
                                }
                            }
                            else
                            {
                                // Add the note.
                                if (s.trigOverride.noteCount < static_cast<int>(
                                        std::size(s.trigOverride.notes)))
                                {
                                    s.trigOverride.notes[static_cast<std::size_t>(
                                        s.trigOverride.noteCount++)] = note;
                                }
                            }
                        }
                    }
                    uiState_.resetNoteEdit();
                    processor_.editContext().markParamWritten();  // suppress trig toggle
                    // Fall through to normal step release logic (with paramWrote set).
                }

                // CHROMATIC/LEVELS mode: key-up clears the pressed highlight.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks))
                    {
                        const auto m = uiState_.trackInputMode[static_cast<std::size_t>(at)];
                        if (m == TrackInputMode::Chromatic || m == TrackInputMode::Levels)
                        {
                            refreshSurface();
                            break;
                        }
                    }
                }

                // Normal step release: look up absStep by rawCode and commit.
                for (int i = static_cast<int>(heldStepKeys_.size()) - 1; i >= 0; --i)
                {
                    auto [code, stepIdx] = heldStepKeys_[static_cast<std::size_t>(i)];
                    if (code == rawCode)
                    {
                        auto& ctx = processor_.editContext();

                        // MHZ.9.5: latched step — keep it in the edit context, just remove
                        // the physical key entry. Suppress trig toggle for the latch.
                        if (ctx.isLatched(stepIdx))
                        {
                            heldStepKeys_.erase(heldStepKeys_.begin() + i);
                            break;
                        }

                        const int track = ctx.heldTrackIndex();

                        // 9.14 Stage 3: commit StepInspector staged P-lock removals on step release.
                        if (uiState_.pLockClearMode && uiState_.pLockClearStep == stepIdx
                            && uiState_.pLockClearTrack == track
                            && !uiState_.pLockClearStaged.empty())
                        {
                            for (const int slot : uiState_.pLockClearStaged)
                            {
                                if (slot < 0)
                                    processor_.clearTrigOverrideField(track, stepIdx, slot);
                                else
                                    processor_.clearParam(track, stepIdx, slot);
                            }
                            ctx.markParamWritten();  // suppress trig toggle
                        }
                        uiState_.resetPLockClear();  // also clears stepMoveActive
                        refreshMetaBand();  // drop the Step-Position panel on release

                        const bool paramWrote = ctx.wasParamWritten();
                        ctx.release(stepIdx);
                        // MHZ.3.1: next press starts a fresh chord capture.
                        if (track >= 0)
                            processor_.cancelChordCapture(track, stepIdx);
                        if (!paramWrote && track >= 0 && stepIdx >= 0
                            && !processor_.isTrackEmpty(track))
                        {
                            auto& s = processor_.sequence()
                                          .tracks[static_cast<std::size_t>(track)]
                                          .steps[static_cast<std::size_t>(stepIdx)];
                            if (!processor_.trackSequencesTrigs(track))
                            {
                                // 7a: control-only machine (Route). The grid is a
                                // bank of lock-only P-Lock anchors — a step press
                                // toggles lock-only, never a note trig. Trig+step
                                // and plain press behave identically here.
                                s.lockOnly = !s.lockOnly;
                                if (s.lockOnly) s.trig = false;
                            }
                            else if (heldSectionIndex_ == IMachine::kTrigSecIdx)
                            {
                                // 5.6 Trig+step: cycle the tri-state off → note →
                                // lock-only → off (DESIGN §30). lock-only keeps the
                                // step's P-Locks but emits no note. On a Record
                                // track a trig is a capture trigger, so lock-only is
                                // meaningless and skipped: off → note → off.
                                if (processor_.isRecorderTrack(track))
                                {
                                    s.trig = !s.trig;
                                    s.lockOnly = false;
                                }
                                else if (s.trig)       { s.trig = false; s.lockOnly = true; }
                                else if (s.lockOnly)   { s.lockOnly = false; }
                                else                   { s.trig = true; }
                            }
                            else if (uiState_.fillHeld)
                            {
                                // Cycle fill trig state: Inherit → On → Off → Inherit.
                                using FTS = FillTrigState;
                                switch (s.fillTrigState)
                                {
                                    case FTS::Inherit: s.fillTrigState = FTS::On; break;
                                    case FTS::On:      s.fillTrigState = FTS::Off; break;
                                    case FTS::Off:     s.fillTrigState = FTS::Inherit; break;
                                }
                            }
                            else
                            {
                                s.trig = !s.trig;
                                // MHZ.9.5: record for possible revert if double-tap follows.
                                lastTrigToggleStep_ = stepIdx;
                                lastTrigToggleTrack_ = track;
                                lastTrigToggleApplied_ = true;
                            }
                        }
                        heldStepKeys_.erase(heldStepKeys_.begin() + i);
                        break;
                    }
                }
                // MHZ.9.5: clear stepHeld/trigHeld only when no physical or latched steps remain.
                if (processor_.editContext().heldSteps().empty())
                {
                    uiState_.stepHeld = false;
                    editMode_.setTrigHeld(false);
                    stepInspectorFiredMidHold_ = false;   // hold ended — re-arm next time
                    gesture_.cancelLongPress();           // drop any pending step long-press
                }
                refreshSurface();
                break;
            }

            case CB::VerbSnapshot:
                break;  // Y = Snapshot; no held-state to clear.

            case CB::Restore: {
                // Func+Y is remapped to CB::Restore upstream (ButtonLayers), so the
                // keyboard reaches restore HERE and never through AId::VerbRestore --
                // which is why this branch has to be the effects call and not a second
                // copy of it. It used to be a copy, and the copy dropped restoreOne()'s
                // bool on the floor: 9.4 item A's "NOTHING TO RESTORE" was live on the
                // table path and unreachable from the keys the player actually presses.
                const double now = juce::Time::getMillisecondCounterHiRes();
                using LPR = GestureRecognizer::LongPressResult;
                switch (gesture_.checkLongPress(kRestoreLongPressToken, now))
                {
                    case LPR::LongHold:  editorEffects_->restoreFloor(); break;
                    case LPR::ShortHold: editorEffects_->restorePop();   break;
                    case LPR::NotArmed:  break;
                }
                break;
            }

            case CB::NavUp:
            case CB::NavDown:
                uiState_.morphNavQualifier = 0;
                manipulationZone_.setMorphQualifier(0);
                manipulationZone_.setMorphHeld(uiState_.morphHeld);
                break;

            case CB::VerbRecord: {
                using LPR = GestureRecognizer::LongPressResult;
                const double now = juce::Time::getMillisecondCounterHiRes();
                // CAPTURE cell resolution (tape deck).
                if (captureCellHeld_)
                {
                    captureCellHeld_ = false;
                    const LPR lp = gesture_.checkLongPress(kCaptureToken, now);
                    if (captureLongPressFired_)
                    {
                        // Long-press already serviced mid-hold (timer path). Consume.
                        captureLongPressFired_ = false;
                    }
                    else if (lp == LPR::LongHold)
                    {
                        // Released just past the threshold before the timer ticked.
                        runCaptureOut(captureController_.onLongPress());
                    }
                    else  // ShortHold / NotArmed → a tap; double-tap refines it.
                    {
                        const bool dbl = gesture_.doubleTap(kCaptureToken, now);
                        const bool playing = processor_.clock().inPluginPlaying();
                        runCaptureOut(dbl ? captureController_.onDoubleTap()
                                          : captureController_.onTap(playing));
                    }
                    break;
                }
                // Bare Record: hold = transport reset, tap = record-arm, double-tap
                // = overdub. Deferred from key-down so a hold doesn't also arm record.
                if (recordResetHeld_)
                {
                    recordResetHeld_ = false;
                    const LPR lp = gesture_.checkLongPress(kTransportResetToken, now);
                    if (recordResetFired_)
                    {
                        recordResetFired_ = false;  // reset already fired mid-hold; consume
                    }
                    else if (lp == LPR::LongHold)
                    {
                        processor_.transportStopReset();  // released just past threshold
                        setStatus(status::transportReset());
                    }
                    else  // tap → record-arm; double-tap → overdub
                    {
                        const bool isDouble = gesture_.doubleTap(
                            1000 + static_cast<int>(ControllerButton::VerbRecord), now);
                        if (isDouble)
                        {
                            processor_.clock().setRecordArmed(true);
                            processor_.clock().setOverdubArmed(true);
                        }
                        else
                        {
                            processor_.clock().setOverdubArmed(false);
                            processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                        }
                    }
                    break;
                }
                break;
            }

            // 9.29: the Clear key resolves tap-vs-hold on release. A hold that already
            // fired mid-timer just consumes its arm; a short hold runs the tap
            // behaviour it deferred; a long hold that beat the timer fires here.
            case CB::VerbClear: {
                fxPickerStepUp(ev.index);   // (kept from the group below)
                if (clearHoldArmed_)
                {
                    using LPR = GestureRecognizer::LongPressResult;
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (clearHoldFired_)
                    {
                        gesture_.cancelLongPress();
                    }
                    else switch (gesture_.checkLongPress(kDeleteHoldToken, now))
                    {
                        case LPR::ShortHold: {
                            const ControllerEvent down{ ControllerEvent::Type::ButtonDown,
                                                        CB::VerbClear, ev.index, 0 };
                            (void)clearVerbTap(down);
                            break;
                        }
                        case LPR::LongHold:
                            fireDeleteHold();
                            break;
                        case LPR::NotArmed:
                            break;
                    }
                    clearHoldArmed_ = false;
                    clearHoldFired_ = false;
                }
                break;
            }

            // These buttons act on key-down; their key-up is a no-op. They must NOT
            // fall through into the TapTempo body below — doing so made releasing any
            // of them (notably SelectTrack) register a tap-tempo tap, so changing
            // tracks set the BPM from the inter-change interval.
            case CB::VerbStopLegacy:
            case CB::VerbDelete:
            case CB::VerbPanic:
            case CB::Snapshot:
            case CB::NavLeft:
            case CB::NavRight:
            case CB::SelectTrack:
                // FX picker: resolve a deferred loaded-cell press when the whole
                // gesture ran with Track held (step keys route here, not to Step).
                fxPickerStepUp(ev.index);
                break;

            case CB::ToggleMute:
            case CB::ForkPart:
            case CB::RecordArm:
                break;

            case CB::TapTempo:
                // 9.12 st.7f: TAP resolves on RELEASE, because the same key's HOLD opens
                // the generator hub — firing the tempo on press would make every hub
                // entry also nudge the tempo. Releasing out of the hub closes it and
                // taps nothing; a short press is the tap row.
                tapTempoPhysHeld_ = false;
                if (uiState_.generatorHubHeld)
                    uiState_.resetGeneratorHub();
                else
                    fireGesture(CB::TapTempo, -1, Gesture::Tap);
                refreshSurface();
                break;

            case CB::MetronomeToggle:
            case CB::PlayStop:
            case CB::StopReset:
            case CB::None:
                break;

            default:
                break;
        }
    }

    bool LockstepEditor::keyStateChanged(bool isKeyDown, juce::Component*)
    {
        // Purge released keys from the repeat-suppression set.
        std::erase_if(heldKeys_, [](int code) {
            return !juce::KeyPress::isKeyCurrentlyDown(code);
        });

        // Diff PressTracker against physical key state; synthesize ButtonUp for
        // any keyboard entry that is no longer down and dispatch release logic.
        bool handled = false;
        std::vector<std::pair<int, ControllerEvent>> toRelease;
        pressTracker_.forEachReleasedKeyboard([&](int src, ControllerButton btn, int idx) {
            toRelease.push_back({ src,
                                  { ControllerEvent::Type::ButtonUp, btn, idx, 0 } });
        });
        for (auto& [src, relEv] : toRelease)
        {
            pressTracker_.release(src);
            dispatchUp(relEv, src);
            handled = true;
        }

        // Repaint on every physical key transition — press AND release — so that
        // *every* key flashes when struck, including disabled section keys and the
        // off-grid edge keys (` Tab Caps Shift - = [ ] '), which dispatch no bound
        // event and would otherwise never trigger a repaint. keyStateChanged fires
        // for modifier keys too (Shift), which never reach keyPressed. The paint
        // path reads live physical state, so this is order-independent with
        // keyPressed. (Orientation aid: PRINCIPLES §8 — no silent keys.)
        juce::ignoreUnused(isKeyDown);
        refreshSurface();

        return handled;
    }

    // -------------------------------------------------------------------------
    // Mouse button routing (mirror of keyPressed / keyStateChanged for click input)

    void LockstepEditor::handleTapTempo()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        int kept = 0;
        for (int i = 0; i < tapCount_; ++i)
        {
            if (now - tapTimes_[static_cast<std::size_t>(i)] <= kTapWindowMs)
                tapTimes_[static_cast<std::size_t>(kept++)] = tapTimes_[static_cast<std::size_t>(i)];
        }
        tapCount_ = kept;

        if (tapCount_ >= kTapMaxCount)
        {
            for (int i = 1; i < kTapMaxCount; ++i)
                tapTimes_[static_cast<std::size_t>(i - 1)] = tapTimes_[static_cast<std::size_t>(i)];
            tapCount_ = kTapMaxCount - 1;
        }
        tapTimes_[static_cast<std::size_t>(tapCount_++)] = now;

        if (tapCount_ >= 2)
        {
            const double spanMs = tapTimes_[static_cast<std::size_t>(tapCount_ - 1)] - tapTimes_[0];
            const double intervals = static_cast<double>(tapCount_ - 1);
            const double bpm = (60000.0 * intervals) / spanMs;
            if (bpm >= kTapMinBpm && bpm <= kTapMaxBpm)
            {
                // Item 10: land the tap at the scope the performer is hearing.
                // A held scope modifier wins; otherwise the deepest scope that
                // already overrides tempo (else global).
                const int scope = resolveTapTempoScope(
                    uiState_, processor_.song().hasTempo, processor_.section().hasTempo);
                const double globalBpm = processor_.clock().bpm();
                if (scope == 2 && globalBpm > 0.0)  // Song
                {
                    processor_.song().hasTempo = true;
                    processor_.song().tempoRatio = bpm / globalBpm;
                }
                else if (scope == 3)  // Scene
                {
                    const auto& sg = processor_.song();
                    const double songBpm = globalBpm * (sg.hasTempo ? sg.tempoRatio : 1.0);
                    if (songBpm > 0.0)
                    {
                        processor_.section().hasTempo = true;
                        processor_.section().tempoRatio = bpm / songBpm;
                    }
                }
                else  // scope == 1 — global/Set
                {
                    processor_.clock().setLocalBpm(bpm);
                }
                refreshSurface();
            }
        }
    }

    void LockstepEditor::applyDisplayMode(GridDisplayMode mode)
    {
        gridMode_ = mode;
        keyboardArea_.setDisplayMode(mode);

        static constexpr const char* kModeLabels[] = { "STG", "ORL", "CLN" };
        displayModeBtn_.setButtonText(kModeLabels[static_cast<int>(mode)]);

        if (auto* prefs = appProps_.getUserSettings())
        {
            prefs->setValue("gridMode", static_cast<int>(mode));
            prefs->saveIfNeeded();
        }
    }

    void LockstepEditor::resized()
    {
        auto bounds = getLocalBounds();

        // (9.30 st.5: the full-width master strip that used to sit here is GONE. The
        //  meter is now a vertical column beside the crossfader — see the MZ block
        //  below, which is where masterChromeRegion_ is computed.)

        // ── 9.30 st.4: THE PROJECT RAIL (cold) ────────────────────────────────
        // One row, and it is the top row because it is the row you look at LEAST: file
        // ops, the sound library, and the per-session plumbing (sync mode, channel mode,
        // grid display). The old header mixed these with Play/Rec — three usage
        // frequencies in one strip, which is how a header becomes a junk drawer.
        {
            auto rail = bounds.removeFromTop(kProjectRailH);
            projectRailRegion_ = rail;

            if (fileBar_)
                fileBar_->setBounds(rail.removeFromLeft(210).reduced(2, 1));

            // Right-hand end: the library, then the session plumbing.
            poolBtn_.setBounds(ChromeLookAndFeel::cell(rail.removeFromRight(80)));
            soundBankBtn_.setBounds(ChromeLookAndFeel::cell(rail.removeFromRight(60)));
            displayModeBtn_.setBounds(ChromeLookAndFeel::cell(rail.removeFromRight(46)));
            channelModeBox_.setBounds(ChromeLookAndFeel::cell(rail.removeFromRight(90)));
            syncModeBox_.setBounds(ChromeLookAndFeel::cell(rail.removeFromRight(80)));

            // What is left in the middle is where the controller-connection indicator
            // is painted (paintProjectRail) — the bottom banner's job, brought up into
            // the row that is actually about "what am I plugged into".
            controllerIndicatorRegion_ = rail.reduced(6, 3);
        }

        // ── 9.30 st.3: THE TRANSPORT + TIME BLOCK (hot) ───────────────────────
        // One band answering "where am I, and what is playing" in a single glance.
        // Time used to be shredded across three non-adjacent places: the transport
        // buttons here, the BPM readout in an "info row" below, and the timeline ruler
        // below THAT — with a file bar in between. They are one concern; they are now
        // one block, with the ruler fused directly beneath it.
        {
            auto band = bounds.removeFromTop(kTransportBandH);
            transportBandRegion_ = band;

            transport_.setBounds(band.removeFromLeft(200).reduced(4));

            // Capture / rec-arm indicator, hard right: the highest-stakes status in the
            // core CUJ (live set -> stems). It used to be a banner on the master VU
            // strip — peripheral vision, on the one element the eye treats as decoration.
            captureIndicatorRegion_ = band.removeFromRight(150).reduced(4, 6);

            // Sg:Sc identity, then the time readout (BPM · time-sig · key-sig) fill the
            // middle. Both are painted (paintTransportBand), not components: they are
            // readouts, and a Label per readout was how the old header grew a dashboard.
            songSceneRegion_ = band.removeFromRight(90).reduced(4, 6);
            tempoReadout_.setBounds(band.reduced(8, 4));
        }

        // S8 (§40.6): the timeline strip is PERMANENT (display-only chrome, fence #5) —
        // the window carries a fixed extra 22 px for it. FUSED to the transport band
        // above it (9.30 st.3): the ruler is the same question those buttons answer.
        timelineStrip_.setVisible(true);
        timelineStrip_.setBounds(bounds.removeFromTop(22));  // matches the +22 window

        // 9.30 st.1: the inspector moves DIRECTLY ABOVE THE MZ, because it is the MZ's
        // caption — its EDIT region says where the knobs are about to write, and its
        // STATUS lane says what the last key did. Both belong beside the hands, not up
        // in the chrome. Row 1 = the four regions; row 2 = the full-width STATUS lane.
        inspectorRow_ = bounds.removeFromTop(InspectorBar::kHeight);
        inspectorBar_.setBounds(inspectorRow_);
        bounds.removeFromTop(2);

        // MHX.5: encoder band (MZ 4x2) + vertical crossfader to its right.
        static constexpr int kMZHeight = 160; // MHX 4x2 MZ (two rows of 4 slots)
        static constexpr int kFaderW = 28;  // crossfader strip width
        static constexpr int kTrackRowH = 26;  // track-number + VU row
        static constexpr int kMsRowH = 22;  // mute/solo row
        {
            auto mzStrip = bounds.removeFromTop(kMZHeight).reduced(8, 4);
            // 9.30 st.5: the MZ's right flank is the OUTPUT COLUMN, read top-to-bottom
            // as the signal flows: sculpt (MZ) -> blend (crossfader) -> level (meter).
            masterChromeRegion_ = mzStrip.removeFromRight(kMasterMeterW).reduced(1, 0);
            auto faderArea = mzStrip.removeFromRight(kFaderW).reduced(2, 0);
            crossfader_.setBounds(faderArea);
            manipulationZone_.setBounds(mzStrip);
        }

        // 9.30 §42.3: the confirm pop-over grows DOWNWARD out of the STATUS lane at
        // double height, over the MZ's top edge. Cached here so the repaint is scoped
        // and the paint has one source of truth for where it goes.
        //
        // THE OCCLUSION RULE: this zone may only ever cover DISPLAY, never an
        // interactive control. It is safe today because a pending confirm makes every
        // key a confirm-or-cancel (§13.2), so nothing beneath it is a live target. If a
        // future layout puts a control here, the CONTROL moves — the pop-over does not
        // shrink, or a destructive prompt becomes missable again.
        confirmPopoverRegion_ = { inspectorRow_.getX(),
                                  inspectorRow_.getBottom() - InspectorBar::kStatusLaneH,
                                  inspectorRow_.getWidth(),
                                  InspectorBar::kStatusLaneH * 3 };

        // Remaining region, laid out top->bottom: track row, mute/solo row,
        // section bar, function bar, step grid.
        {
            // Cache the track/VU-row chrome band (full width, un-reduced) for the
            // scoped meter/blink repaint.
            trackRowChromeRegion_ = { bounds.getX(), bounds.getY(), bounds.getWidth(), kTrackRowH };
            auto trackRow = bounds.removeFromTop(kTrackRowH).reduced(8, 2);
            // Page toggle sits at the left edge; the 8 visible track buttons fill the rest.
            static constexpr int kPageBtnW = 36;
            trackPageBtn_.setBounds(trackRow.removeFromLeft(kPageBtnW).reduced(1, 1));
            const int pageStart = trackPage_ * 8;
            const int colW = trackRow.getWidth() / 8;
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                const bool visible = (static_cast<int>(i) >= pageStart && static_cast<int>(i) < pageStart + 8);
                trackBtns_[i].setVisible(visible);
                if (visible)
                    trackBtns_[i].setBounds(trackRow.removeFromLeft(colW).reduced(1, 1));
                else
                    trackBtns_[i].setBounds({});
            }
        }
        {
            auto msRow = bounds.removeFromTop(kMsRowH).reduced(8, 2);
            // Reserve the same width as the page toggle button above.
            static constexpr int kPageBtnW = 36;
            msRow.removeFromLeft(kPageBtnW);
            const int pageStart = trackPage_ * 8;
            const int colW = msRow.getWidth() / 8;
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                const bool visible = (static_cast<int>(i) >= pageStart && static_cast<int>(i) < pageStart + 8);
                muteBtns_[i].setVisible(visible);
                soloBtns_[i].setVisible(visible);
                if (visible)
                {
                    auto col = msRow.removeFromLeft(colW);
                    auto mute = col.removeFromLeft(col.getWidth() / 2);
                    muteBtns_[i].setBounds(mute.reduced(1, 1));
                    soloBtns_[i].setBounds(col.reduced(1, 1));
                }
                else
                {
                    muteBtns_[i].setBounds({});
                    soloBtns_[i].setBounds({});
                }
            }
        }

        // KeyboardArea owns all four button rows (section bar, function bar, two step rows)
        // and the nav row. Give it the remaining space; it handles the internal layout.
        keyboardArea_.setBounds(bounds);

        greyoutLayer_.setBounds(getLocalBounds());

        poolOverlay_.setBounds(manipulationZone_.getBounds()
                                   .withBottom(keyboardArea_.getY() + keyboardArea_.stepRowsLocalY()));

        soundBankOverlay_.setBounds(manipulationZone_.getBounds()
                                        .withBottom(keyboardArea_.getY() + keyboardArea_.stepRowsLocalY()));
    }

    // -------------------------------------------------------------------------
    // Clipboard helpers

    void LockstepEditor::captureScene()
    {
        auto& cl = clipboard_;
        cl.scene.floor = processor_.section();
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            cl.scene.phrases[static_cast<std::size_t>(t)] = processor_.activePhrase(t);
        cl.type = ClipboardType::Scene;
    }

    bool LockstepEditor::phraseConflictAndConfirm(int phraseSlot, ConfirmKind kind)
    {
        // No-op skip: re-stamping identical content never needs a prompt.
        if (kind == ConfirmKind::CreateScene && processor_.phraseRowMatchesActiveContent(phraseSlot))
            return false;

        bool slotHasContent = false;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            if (processor_.song().tracks[static_cast<std::size_t>(t)].phrases[static_cast<std::size_t>(phraseSlot)].initialised)
            {
                slotHasContent = true;
                break;
            }
        }
        if (!slotHasContent)
            return false;   // clean row — no conflict

        uiState_.confirm = { kind, phraseSlot };
        const int freeSlot = processor_.firstFreePhraseSlot();
        juce::String msg = "Overwrite phrase row " + juce::String(phraseSlot) + "?";
        if (freeSlot >= 0) msg += "  free:S" + juce::String(freeSlot + 1);
        msg += "  P=CONFIRM  Func+P=CANCEL";
        setStatus(msg);
        refreshSurface();   // uiState_.confirm arms the CONFIRM cell — a surface change
        return true;    // conflict raised — caller must wait for Yes/No
    }

    // -------------------------------------------------------------------------
    // Transient status line

    void LockstepEditor::setStatus(const juce::String& msg)
    {
        statusMessage_ = msg;
        statusSetMs_ = juce::Time::getMillisecondCounter();
        repaint();   // chrome only: the transient status line carries no cell state
    }

    void LockstepEditor::routeLooperVerb(int cmd, bool immediate)
    {
        const int track = processor_.focusTrack();
        processor_.sendLooperCommand(track, cmd, immediate);
        // Echo the action; the command applies on the next audio block, so the
        // precise resolved state is shown by the surface refresh that follows.
        const char* verb = (cmd == 1) ? "Rec/Overdub"
                          : (cmd == 2) ? "Play/Stop"
                          : (cmd == 3) ? "Clear"
                          : (cmd == 5) ? "Halve"
                          : (cmd == 6) ? "Double" : "Undo";
        setStatus(juce::String("Loop: ") + verb + (immediate ? " (now)" : ""));
        refreshSurface();
    }

    // (9.30 st.1: paintStatus is GONE. The toast and the missing-samples banner were
    //  two renderers in the nav strip, each deciding for itself when to show -- the
    //  banner literally said "shown only when no toast is up". They are now two KINDS
    //  in one lane, and the precedence between them is a property of the taxonomy
    //  (§42.2), not of whichever painter happened to run first. setStatus still records
    //  the toast; the model reads it.)

    // -------------------------------------------------------------------------
    // Checkpoint scope helper

    CheckpointScope LockstepEditor::ckScope(int& outTrack) const
    {
        using PS = EditMode::PrimaryScope;
        outTrack = keyboardArea_.getActiveTrack();
        switch (editMode_.primaryScope())
        {
            case PS::Track:  return CheckpointScope::Track;
            case PS::Scene:  return CheckpointScope::Scene;
            case PS::Phrase: return CheckpointScope::Phrase;
            default:         return CheckpointScope::Song;
        }
    }

    // -------------------------------------------------------------------------
    // Layer context + controller surface sink construction

    LayerContext LockstepEditor::layerContext() const noexcept
    {
        return {
            uiState_.funcHeld,
            uiState_.trackHeld || uiState_.latch.track,
            uiState_.muteHeld || uiState_.latch.mute
        };
    }

    CommandContext LockstepEditor::commandContext()
    {
        // ProcessorCatalog is lightweight — safe to construct per-call.
        static ProcessorCatalog catalog{ processor_ };
        return {
            processor_.arrangement(),
            processor_.sequence(),
            processor_.editContext(),
            editMode_,
            uiState_,
            clipboard_,
            processor_.project().soundPool,
            catalog
        };
    }

    // Maps a relative encoder delta (|rawDelta| up to 7) to a new value for a
    // discrete stepped field. One value per device delta unit (single detent = ±1,
    // fast flick = device-accelerated delta, capped at ±7).
    // intervals = number of discrete steps (value-count - 1).
    static float applyDiscreteEncoderDelta(float minV, float maxV, int intervals,
                                           float cur, int rawDelta, bool wrap = false)
    {
        if (intervals <= 0 || rawDelta == 0) return cur;
        const int unit = juce::jlimit(-7, 7, rawDelta);
        const float quantum = (maxV - minV) / static_cast<float>(intervals);
        const int curIdx = juce::roundToInt((cur - minV) / quantum);
        int newIdx;
        if (wrap)
        {
            // intervals steps span (intervals + 1) discrete positions; wrap the
            // index across that inclusive range so turning past either end cycles
            // (item 9: CUR advances from the last chord back to the first).
            const int span = intervals + 1;
            newIdx = (((curIdx + unit) % span) + span) % span;
        }
        else
            newIdx = juce::jlimit(0, intervals, curIdx + unit);
        return minV + static_cast<float>(newIdx) * quantum;
    }

    ControllerEventSink LockstepEditor::buildControllerSink()
    {
        ControllerEventSink sink;

        sink.emitEvent = [this](ControllerEvent ev) {
            // Apply all layer remaps (kLayerRemaps: Track > Mute > Func priority).
            // Previously only Step→SelectTrack/ToggleMute were handled here; now
            // resolveLayer() also closes the Section→MetaSection gap for controllers.
            if (ev.type != ControllerEvent::Type::EncoderDelta)
                ev = resolveLayer(ev, layerContext());

            if (ev.type == ControllerEvent::Type::ButtonDown)
            {
                pressTracker_.press(PressTracker::kControllerSource, ev.button, ev.index);
                dispatchDown(ev, PressTracker::kControllerSource);
            }
            else if (ev.type == ControllerEvent::Type::ButtonUp)
            {
                pressTracker_.release(PressTracker::kControllerSource);
                dispatchUp(ev, PressTracker::kControllerSource);
            }
            // Mirror the mouse/keyboard paths: repaint so a controller press/release
            // updates the on-screen highlight immediately (else a released key's
            // highlight lingers until the next unrelated repaint).
            refreshSurface();
        };

        sink.applyParamDelta = [this](int mzSlot, int rawDelta) {
            const int track = keyboardArea_.getActiveTrack();

            // Meta band takes priority: encoder edits the shown band, not machine params.
            const MetaBand band = resolveMetaBand(uiState_);
            if (band != MetaBand::None)
            {
                // Stub tracks: block track-scoped band writes; allow global/song-level.
                if (activeTrackContentLocked())
                {
                    const bool isTrackBand = (band == MetaBand::Cond
                        || band == MetaBand::Trig || band == MetaBand::Divider
                        || band == MetaBand::PhraseLen || band == MetaBand::Euclidean
                        || band == MetaBand::DensityMode
                        || band == MetaBand::DensitySelection
                        || (band == MetaBand::Density && !densityEditsMaster(uiState_)));
                    if (isTrackBand)
                    {
                        setStatus("EMPTY -- Func+Track to add a machine");
                        return;
                    }
                }
                // Density + Song held → adjust master density as a relative delta.
                if (band == MetaBand::Density && densityEditsMaster(uiState_))
                {
                    processor_.setMasterDensity(juce::jlimit(-1.0f, 1.0f,
                        processor_.masterDensity() + static_cast<float>(rawDelta) / 128.0f));
                    refreshMetaBand();
                    return;
                }
                const int swScope = swingScopeFor(uiState_);
                const auto views = buildMetaBand(band, swScope, processor_, track,
                                                 processor_.editContext(), uiState_);
                if (mzSlot < 0 || mzSlot >= 8) return;
                // Harmony + Func held: chromatic (borrowed-tone) nudge, mirroring
                // the mouse path so encoder and mouse agree. Per-voice on a voice
                // cell; whole-chord on MOVE. Each detent = one semitone. Bypasses
                // writeMetaField, which owns the diatonic rung write.
                if (band == MetaBand::Harmony && uiState_.funcHeld
                    && (mzSlot < kHarmonyVoices || mzSlot == 6))
                {
                    if (mzSlot < kHarmonyVoices)
                        nudgeHarmonyChroma(processor_, uiState_, mzSlot, rawDelta);
                    else
                        nudgeHarmonyChromaAll(processor_, uiState_, rawDelta);
                    if (uiState_.harmonyHeld && harmonyTrack_ >= 0)
                    {
                        applyHarmonyLive(harmonyTrack_);
                        auditionHarmonyCursorChord();
                    }
                    refreshMetaBand();
                    refreshSurface();
                    return;
                }
                const auto& v = views[static_cast<std::size_t>(mzSlot)];
                if (!v.writable) return;
                const float range = v.maxValue - v.minValue;
                if (range <= 0.0f) return;
                float newVal;
                if (v.stepped)
                {
                    const int intervals = juce::roundToInt(v.maxValue - v.minValue);
                    newVal = applyDiscreteEncoderDelta(v.minValue, v.maxValue, intervals,
                                                      v.value, rawDelta, v.wrap);
                }
                else
                {
                    const float norm = juce::jlimit(0.0f, 1.0f, (v.value - v.minValue) / range);
                    const float newNorm = juce::jlimit(0.0f, 1.0f,
                                                       norm + static_cast<float>(rawDelta) / 128.0f);
                    newVal = v.minValue + newNorm * range;
                }
                writeMetaField(band, swScope, mzSlot, newVal,
                               processor_, track, processor_.editContext(), uiState_);
                if (band == MetaBand::Euclidean && uiState_.euclidHeld && euclidTrack_ >= 0)
                    applyEuclidLive(euclidTrack_);
                if (band == MetaBand::Melodic && uiState_.melodicHeld && melodicTrack_ >= 0)
                    applyMelodyLive(melodicTrack_);
                if (band == MetaBand::Harmony && uiState_.harmonyHeld && harmonyTrack_ >= 0)
                {
                    applyHarmonyLive(harmonyTrack_);
                    auditionHarmonyCursorChord();
                }
                // Rebuild the band so stepped value-text (mode name, scale type,
                // root) reflects the new value, and repaint the grid (KEY panel
                // cells track the edited key). The KeyboardArea timer only
                // repaints on playhead movement otherwise.
                refreshMetaBand();
                refreshSurface();
                return;
            }

            // Stub track: no real machine params.
            if (activeTrackContentLocked())
            {
                setStatus("EMPTY -- Func+Track to add a machine");
                return;
            }

            const int absSlot = manipulationZone_.slotOffset() + mzSlot;
            if (absSlot >= processor_.numParams(track)) return;

            const auto spec = processor_.paramSpec(track, absSlot);

            // Out / input_source are FILTERED candidate rotaries: the mouse path
            // (ManipulationZone) maps a rotary index onto validOutTargets /
            // validInputSources. The encoder must step the SAME candidate list,
            // not the raw encoding range — otherwise valid bus targets / tap
            // sources are unreachable by encoder (only Off/Master resolve). This
            // is the encoder-side mirror of the ManipulationZone special-case.
            // Routing is not morphable, so it precedes the morph handling below.
            {
                const juce::String sid { spec.id };
                const bool isOut   = (sid == kOutSlotId);
                const bool isInSrc = (sid == kInputSourceSlotId);
                if (isOut || isInSrc)
                {
                    const auto cands = isOut ? processor_.validOutTargets(track)
                                             : processor_.validInputSources(track);
                    if (!cands.empty())
                    {
                        const float curEnc = processor_.baseParamValue(track, absSlot);
                        int idx = 0;
                        for (std::size_t c = 0; c < cands.size(); ++c)
                            if (std::lround(cands[c]) == std::lround(curEnc))
                            { idx = static_cast<int>(c); break; }
                        idx = juce::jlimit(0, static_cast<int>(cands.size()) - 1,
                                           idx + rawDelta);
                        processor_.writeParam(track, absSlot,
                                              cands[static_cast<std::size_t>(idx)]);
                        processor_.editContext().markParamWritten();
                        return;
                    }
                }
            }

            const float range = spec.maxValue - spec.minValue;
            if (range <= 0.0f) return;

            // Compute step count for discrete params; 0 = continuous.
            const int intervals = spec.isStepped
                ? (spec.valueLabels.size() > 0
                       ? static_cast<int>(spec.valueLabels.size()) - 1
                       : juce::roundToInt(spec.maxValue - spec.minValue))
                : 0;

            // Returns the new value for a given current value, respecting discrete stepping.
            auto stepVal = [&](float c) -> float {
                if (intervals > 0)
                    return applyDiscreteEncoderDelta(spec.minValue, spec.maxValue,
                                                     intervals, c, rawDelta);
                const float n = juce::jlimit(0.0f, 1.0f, (c - spec.minValue) / range);
                return spec.minValue + juce::jlimit(0.0f, 1.0f,
                                                    n + static_cast<float>(rawDelta) / 128.0f) * range;
            };

            const auto& ec = processor_.editContext();
            const bool stepHeld = ec.isActiveForEditing() && ec.heldTrackIndex() == track;

            // Morph-held + no step held → write morph overlay (DESIGN §17.3).
            // With ^/v qualifier: write directly to one pole (absolute delta).
            if (uiState_.morphHeld && !stepHeld)
            {
                if (uiState_.morphNavQualifier != 0)
                {
                    const int pole = uiState_.morphNavQualifier - 1;  // 0=A, 1=B
                    const auto info = processor_.morphWidgetInfo(track, absSlot);
                    const float curPole = (pole == 0)
                                              ? (info.inA ? info.aValue : processor_.baseParamValue(track, absSlot))
                                              : (info.inB ? info.bValue : processor_.baseParamValue(track, absSlot));
                    processor_.writeMorphPole(track, absSlot,
                                              juce::jlimit(spec.minValue, spec.maxValue,
                                                           stepVal(curPole)), pole);
                }
                else
                {
                    const float base = processor_.baseParamValue(track, absSlot);
                    const float deltaAbs = stepVal(base) - base;
                    processor_.writeMorph(track, absSlot, deltaAbs, processor_.morphFader());
                }
                return;
            }

            // Read the OEB-resolved current value (Override-ELSE-Base) so that
            // encoder deltas accumulate correctly when P-lock editing is active.
            float cur = processor_.baseParamValue(track, absSlot);
            if (stepHeld)
            {
                const int heldStep = ec.heldStepIndex();
                if (heldStep >= 0)
                {
                    const auto& s = processor_.sequence()
                                        .tracks[static_cast<std::size_t>(track)]
                                        .steps[static_cast<std::size_t>(heldStep)];
                    cur = s.overrides.get(absSlot, cur);
                }
            }

            const float newVal = stepVal(cur);

            // Auto-morph-aware: bare encoder follows morph state of the slot.
            // No-morph → kit base (as before). One pole set → write that pole
            // directly (fader irrelevant). Both set → proportional split.
            if (!stepHeld)
            {
                const auto mInfo = processor_.morphWidgetInfo(track, absSlot);
                if (mInfo.exists)
                {
                    if (mInfo.inA && !mInfo.inB)
                    {
                        processor_.writeMorphPole(track, absSlot,
                                                  juce::jlimit(spec.minValue, spec.maxValue,
                                                               stepVal(mInfo.aValue)), 0);
                    }
                    else if (!mInfo.inA && mInfo.inB)
                    {
                        processor_.writeMorphPole(track, absSlot,
                                                  juce::jlimit(spec.minValue, spec.maxValue,
                                                               stepVal(mInfo.bValue)), 1);
                    }
                    else
                    {
                        const float base = processor_.baseParamValue(track, absSlot);
                        const float deltaAbs = stepVal(base) - base;
                        processor_.writeMorph(track, absSlot, deltaAbs, processor_.morphFader());
                    }
                    processor_.editContext().markParamWritten();
                    return;
                }
            }

            processor_.writeParam(track, absSlot, newVal);
            processor_.editContext().markParamWritten();
        };

        sink.resetSlot = [this](int mzSlot) {
            const int track = keyboardArea_.getActiveTrack();
            const int absSlot = manipulationZone_.slotOffset() + mzSlot;
            if (absSlot >= processor_.numParams(track)) return;

            auto& ctx = processor_.editContext();
            const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;

            // Morph+Stop: bake the current fader-blended value into kit base,
            // then erase morph data (default = bake).
            // Func+Morph+Stop: revert to kit base without baking (erases only).
            if (uiState_.morphHeld && !stepHeld)
            {
                if (uiState_.funcHeld)
                    processor_.removeMorph(track, absSlot);   // revert
                else
                    processor_.bakeMorph(track, absSlot);     // bake (default)
                return;
            }

            if (ctx.isActiveForEditing())
            {
                processor_.clearParam(ctx.heldTrackIndex(), ctx.heldStepIndex(), absSlot);
            }
            else
            {
                const auto spec = processor_.paramSpec(track, absSlot);
                processor_.writeParam(track, absSlot, spec.defaultValue);
            }
            ctx.markParamWritten();
        };

        sink.setCrossfader = [this](float normValue) {
            // setValue triggers onValueChange which applies the inversion.
            crossfader_.setValue(static_cast<double>(normValue), juce::sendNotificationAsync);
        };

        sink.applyGlobalDelta = [this](GlobalTarget target, int rawDelta) {
            switch (target)
            {
                case GlobalTarget::Tempo: {
                    const double cur = processor_.clock().localBpm();
                    processor_.clock().setLocalBpm(
                        std::clamp(cur + static_cast<double>(rawDelta) * 0.5,
                                   20.0, 300.0));
                    break;
                }
                case GlobalTarget::Master: {
                    auto* p = processor_.apvts().getParameter(ParamIDs::outputGain);
                    if (p)
                    {
                        const float cur = p->getValue();  // normalised 0..1
                        const float newVal = juce::jlimit(0.0f, 1.0f,
                                                          cur + static_cast<float>(rawDelta) / 128.0f);
                        p->setValueNotifyingHost(newVal);
                    }
                    break;
                }
                case GlobalTarget::Swing: {
                    // Song-all swing: encoder delta at ~1% per tick.
                    const float cur = processor_.swingSongAll();
                    const float newVal = std::clamp(cur + static_cast<float>(rawDelta) / 100.0f,
                                                    -0.5f, 0.5f);
                    processor_.setSwingSongAll(newVal);
                    break;
                }
            }
        };

        return sink;
    }

    // -------------------------------------------------------------------------
    // buildMorphViewState — builds per-slot A/B pole states from live morph data
    // and in-memory dormant maps, for the 8 currently visible MZ slots.
    MorphViewState LockstepEditor::buildMorphViewState() const
    {
        MorphViewState mv;
        if (!uiState_.morphHeld) return mv;

        const int track = keyboardArea_.getActiveTrack();
        if (track < 0) return mv;

        const int slotOff = manipulationZone_.slotOffset();
        using MPS = MorphViewState::PoleState;

        for (int i = 0; i < 8; ++i)
        {
            const int absSlot = slotOff + i;
            auto& s = mv.slots[static_cast<std::size_t>(i)];
            const auto key = std::make_pair(track, absSlot);
            const auto info = processor_.morphWidgetInfo(track, absSlot);

            s.a = info.inA ? MPS::Active
                           : (morphDormantA_.count(key) > 0 ? MPS::Dormant : MPS::Dark);
            s.b = info.inB ? MPS::Active
                           : (morphDormantB_.count(key) > 0 ? MPS::Dormant : MPS::Dark);
        }
        return mv;
    }
}
