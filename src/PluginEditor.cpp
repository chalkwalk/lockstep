#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "command/ScopePriority.h"
#include "command/StatusText.h"
#include "core/Euclidean.h"
#include "core/MetricGrid.h"
#include "core/TrackInputMode.h"
#include "io/TrigGridMode.h"
#include "machine/IMachine.h"
#include "machine/ISliceable.h"
#include "machine/SamplerMachine.h"
#include "ui/KeyLabel.h"
#include "ui/MetaBand.h"
#include "ui/ScopedSectionMatrix.h"
#include "ui/SurfaceModel.h"
#include "ui/mode/ModeReducer.h"
#include <algorithm>

// D2: dirty-guard hook requires access to StandaloneFilterWindow (standalone target only).
#if JucePlugin_Build_Standalone
  #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace lockstep
{
    // -------------------------------------------------------------------------
    // Command core seam (Phase 8.4)
    // Defined here (before constructor) so the nested struct is complete when
    // make_unique<EditorEffects> is called in the member-initialiser list.

    struct LockstepEditor::EditorEffects final : CommandEffects
    {
        LockstepEditor& ed;
        explicit EditorEffects(LockstepEditor& e) : ed(e) {}

        void status(const juce::String& msg) override { ed.setStatus(msg); }
        void requestRepaint() override { ed.repaint(); }
        void transport(TransportAction a) override
        {
            using A = TransportAction;
            auto& clk = ed.processor_.clock();
            switch (a)
            {
                case A::Play:
                    clk.setInPluginPlaying(!clk.inPluginPlaying());
                    break;
                case A::Pause:
                    clk.setInPluginPlaying(false);
                    break;
                case A::StopReset:
                    clk.setInPluginPlaying(false);
                    clk.resetPhase();
                    ed.processor_.requestFreshStart();
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
            ed.repaint();
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
                    ed.uiState_.funcTrackHeld = (param != 0);
                    ed.repaint();
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
        void globalMuteToggle(int track) override
        {
            ed.processor_.toggleGlobalMute(track);
            ed.repaint();
        }
        void soloToggle(int track) override
        {
            ed.processor_.toggleSolo(track);
            ed.repaint();
        }
        void sceneMuteToggle(int track) override
        {
            ed.processor_.togglePatternMute(track);
            ed.repaint();
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
            ed.repaint();
        }
        void toggleCapture() override
        {
            if (ed.processor_.isCapturing())
            {
                const juce::RelativeTime dur = ed.processor_.stopCapture();
                const juce::File f = ed.processor_.captureFile();
                const int totalSec = static_cast<int>(dur.inSeconds());
                const juce::String durStr = juce::String(totalSec / 60)
                    + ":" + juce::String(totalSec % 60).paddedLeft('0', 2);
                ed.setStatus(status::captureDisarmed(durStr, f.getFileName()));
            }
            else
            {
                if (ed.processor_.startCapture())
                {
                    ed.setStatus(status::captureArmed(
                        ed.processor_.captureFile().getFileName()));
                }
                else
                {
                    ed.setStatus(status::captureFailed());
                }
            }
            ed.repaint();
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
            ed.processor_.snapshot(CheckpointScope::Song, 0);
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
                ed.processor_.snapshot(CheckpointScope::Song, 0);
                ed.processor_.bakeSceneState();
                ed.setStatus(status::baked());
            }
            else if (kind == ConfirmKind::CreateScene)
            {
                ed.processor_.snapshot(CheckpointScope::Song, 0);
                ed.processor_.createBakedCopyScene(target);
                if (ed.processor_.clock().inPluginPlaying())
                    ed.processor_.queueScene(target, false);
                else
                    ed.processor_.setActiveScene(target);
                ed.setStatus(status::sceneCreated(target + 1));
            }
            else if (kind == ConfirmKind::CreateBaselineScene)
            {
                ed.processor_.snapshot(CheckpointScope::Song, 0);
                ed.processor_.createBaselineCopyScene(target);
                if (ed.processor_.clock().inPluginPlaying())
                    ed.processor_.queueScene(target, false);
                else
                    ed.processor_.setActiveScene(target);
                ed.setStatus(status::sceneBaseline(target + 1));
            }
            else if (kind == ConfirmKind::PasteScene)
            {
                ed.processor_.snapshot(CheckpointScope::Song, 0);
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
                    ed.processor_.snapshot(CheckpointScope::Track, target);
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
                    ed.processor_.snapshot(CheckpointScope::Track, trk >= 0 ? trk : 0);
                    ed.processor_.deletePhraseSlot(trk >= 0 ? trk : 0, target);
                    ed.setStatus(status::deletedPhrase());
                }
                else
                {
                    // Legacy fallback: old-style global phrase wipe (no slot selected).
                    int ckTrk = 0;
                    ed.processor_.snapshot(ed.ckScope(ckTrk), ckTrk);
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
                    ed.processor_.snapshot(ed.ckScope(ckTrk), ckTrk);
                    ed.processor_.deleteSceneSlot(target);
                    ed.releaseTransientLatch(ControllerButton::SceneScope);
                    ed.setStatus(status::deletedPart());
                }
                else
                {
                    // Legacy fallback (should not happen with picker flow).
                    int ckTrk = 0;
                    ed.processor_.snapshot(ed.ckScope(ckTrk), ckTrk);
                    ed.processor_.deletePart();
                    ed.releaseTransientLatch(ControllerButton::SceneScope);
                    ed.setStatus(status::deletedPart());
                }
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

        tempoReadout_.setJustificationType(juce::Justification::centredLeft);
        tempoReadout_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(tempoReadout_);
        addAndMakeVisible(inspectorBar_);

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
        }
        updateTransportGhosting();

        // Wire section-change callbacks -> update ManipulationZone.
        keyboardArea_.onSectionChanged = [this](int section, int page, int firstSlot) {
            manipulationZone_.setSlotOffset(firstSlot);
            const int track = keyboardArea_.getActiveTrack();
            if (track >= 0)
            {
                const auto info = processor_.section(track, section);
                manipulationZone_.setNormalTitle(info.label, page, info.pageCount);
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
            repaint();
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
            repaint();
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
            // Transparent background so the underlaid per-track VU meter (drawn
            // behind in paint()) shows through; the number paints on top.
            trackBtns_[ti].setColour(juce::TextButton::buttonColourId,
                                     juce::Colours::transparentBlack);
            trackBtns_[ti].setColour(juce::TextButton::buttonOnColourId,
                                     juce::Colours::transparentBlack);
            trackBtns_[ti].setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            trackBtns_[ti].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            trackBtns_[ti].addMouseListener(this, false);  // meter drag
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
        manipulationZone_.onEuclidParamChanged = [this] {
            if (uiState_.euclidHeld && euclidTrack_ >= 0)
            {
                applyEuclidLive(euclidTrack_);
                repaint();
                // Explicitly repaint the grid: the KeyboardArea timer only repaints
                // on playhead movement, so when stopped the editor repaint() alone
                // left the live rhythm invisible until transport started.
                keyboardArea_.repaint();
            }
        };
        // Wire mouse button events from KeyboardArea through the unified dispatch.
        keyboardArea_.onButtonDown = [this](ControllerEvent ev) {
            pressTracker_.press(PressTracker::kMouseSource, ev.button, ev.index);
            dispatchDown(ev, PressTracker::kMouseSource);
            keyboardArea_.repaint();
        };
        keyboardArea_.onButtonUp = [this](ControllerEvent ev) {
            pressTracker_.release(PressTracker::kMouseSource);
            dispatchUp(ev, PressTracker::kMouseSource);
            keyboardArea_.repaint();
        };

        // Give KeyboardArea a pointer to the shared PressTracker so its paint
        // methods can query pressed state from both keyboard and mouse sources.
        keyboardArea_.setPressTracker(&pressTracker_);


        setSize(990, 604);  // MHX: taller for 4x2 MZ encoder band; +8 for timeline nav row
        setWantsKeyboardFocus(true);

        // Controller surfaces (DESIGN §35).
        xTouchSurface_ = std::make_unique<XTouchMiniSurface>();
        push1Surface_ = std::make_unique<Push1Surface>();

        controllerPorts_.onStateChange = [this](bool open) {
            setStatus(open ? "Controller: X-Touch Mini connected"
                           : "Controller: X-Touch Mini disconnected");
        };

        push1Ports_.onStateChange = [this](bool open) {
            setStatus(open ? "Controller: Ableton Push 1 connected"
                           : "Controller: Ableton Push 1 disconnected");
        };

        startTimerHz(30);  // diagnostic VU meters / activity blinks
    }

    LockstepEditor::~LockstepEditor()
    {
        processor_.apvts().removeParameterListener(ParamIDs::syncMode, this);
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            processor_.apvts().removeParameterListener(ParamIDs::trackMute(i), this);
            processor_.apvts().removeParameterListener(ParamIDs::trackSolo(i), this);
        }
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
    }

    void LockstepEditor::parameterChanged(const juce::String& paramID, float /*newValue*/)
    {
        if (paramID == ParamIDs::syncMode)
        {
            juce::MessageManager::callAsync([this] { updateTransportGhosting(); });
            return;
        }
        // Mute or solo changed (via track-bar buttons, host automation, or MIDI
        // learn): repaint so the VU meter colour updates immediately.
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            if (paramID == juce::String(ParamIDs::trackMute(i)) || paramID == juce::String(ParamIDs::trackSolo(i)))
            {
                juce::MessageManager::callAsync([this] { repaint(); });
                return;
            }
        }
    }

    void LockstepEditor::timerCallback()
    {
        // Generator hub (9.10): promote a held 3-key to the hub picker after 350 ms.
        if (tapTempoPhysHeld_ && !uiState_.generatorHubHeld)
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (nowMs - tapTempoArmMs_ >= GestureRecognizer::kLongPressMs)
            {
                uiState_.generatorHubHeld = true;
                repaint();
            }
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

            if (processor_.takeMidiPulse(static_cast<int>(i)) > 0.5f)
            {
                midiBlink_[i] = 1.0f;
                dirty = true;
            }
            else if (midiBlink_[i] > 0.0f)
            {
                midiBlink_[i] = (midiBlink_[i] > kBlinkFloor) ? midiBlink_[i] * 0.70f : 0.0f;
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

        // Transport state change: repaint so the PLAY/PAUSE label updates promptly.
        const bool nowPlaying = processor_.clock().inPluginPlaying();
        if (nowPlaying != lastPlayingState_)
        {
            lastPlayingState_ = nowPlaying;
            dirty = true;
        }

        // Morph fader: detect on-screen crossfader moves so controller surfaces update.
        const float curMorphFader = processor_.morphFader();
        if (curMorphFader != lastMorphFader_)
        {
            lastMorphFader_ = curMorphFader;
            dirty = true;
        }

        // Push the morph view state to KeyboardArea so its paint() gets current slot states.
        keyboardArea_.setMorphViewState(buildMorphViewState(),
                                        manipulationZone_.slotOffset(),
                                        1.0f - processor_.morphFader());

        if (dirty) repaint();

        // Update the scope-coloured tempo + time-sig readout.
        {
            const auto effTs = processor_.effectiveTimeSig();
            const double effBpm = processor_.effectiveBpm();
            const juce::String readout =
                juce::String(static_cast<int>(std::round(effBpm))) + " BPM  "
                + juce::String(effTs.numerator) + "/" + juce::String(effTs.denominator);
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

        // 9.11: refresh inspector bar with current context.
        inspectorBar_.setModel(buildInspectorModel(
            uiState_, processor_.editContext(), processor_,
            lastFocusedButton_, lastFocusedIndex_));

        // Controller: drain MIDI FIFO → surface.onInput(), then render feedback LEDs.
        // drain() is called unconditionally every tick (never gated on dirty) because:
        //   1. Input (encoder turns, button presses) must be processed even when the
        //      software is idle — gating on dirty breaks encoders when nothing else
        //      is changing.
        //   2. render() diffs against a shadow cache and emits MIDI only for changed
        //      cells, so calling it every 30 Hz is safe and was the design intent.
        //   3. Mode transitions (holding Track, switching sections, etc.) call repaint()
        //      directly without touching the dirty flag here, so dirty is not a reliable
        //      signal for "controller state may have changed".
        if ((xTouchSurface_ && controllerPorts_.isOpen()) || (push1Surface_ && push1Ports_.isOpen()))
        {
            auto sink = buildControllerSink();
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
            if (xTouchSurface_ && controllerPorts_.isOpen())
                controllerPorts_.drain(*xTouchSurface_, sink, model);
            if (push1Surface_ && push1Ports_.isOpen())
                push1Ports_.drain(*push1Surface_, sink, model);
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
            repaint();
        }
    }

    // Maps a linear meter level [0,1] to a green→yellow→red colour.
    static juce::Colour meterColour(float level)
    {
        if (level < 0.5f) return juce::Colour::fromRGB(60, 200, 90);
        if (level < 0.85f) return juce::Colour::fromRGB(220, 200, 60);
        return juce::Colour::fromRGB(230, 80, 60);
    }

    void LockstepEditor::paintMeters(juce::Graphics& g)
    {
        // Determine which tracks are silenced (muted or solo-excluded) so the VU
        // can flag them — a silenced track's machine is skipped entirely, which
        // is a common cause of "no sound" confusion.
        bool anySoloed = false;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            if (auto* p = processor_.apvts().getRawParameterValue(ParamIDs::trackSolo(t)))
                if (p->load() >= 0.5f)
                {
                    anySoloed = true;
                    break;
                }

        // Per-track VU underlaid behind the (transparent) track-number buttons.
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const int t = static_cast<int>(i);
            const bool gMuted = processor_.getGlobalMute(t);
            const bool pMuted = processor_.getPatternMute(t);
            const auto* sp = processor_.apvts().getRawParameterValue(ParamIDs::trackSolo(t));
            const bool soloed = sp && sp->load() >= 0.5f;
            const bool soloEx = anySoloed && !soloed;

            const auto r = trackBtns_[i].getBounds();
            if (r.isEmpty()) continue;

            // Distinct background per silencing source: global mute = red,
            // pattern mute = orange, solo-exclusion = purple, soloed = teal,
            // audible = grey.
            juce::Colour bg = juce::Colour::fromRGB(28, 32, 38);
            if (gMuted) bg = juce::Colour::fromRGB(70, 20, 20);
            else if (pMuted) bg = juce::Colour::fromRGB(80, 50, 16);
            else if (soloEx) bg = juce::Colour::fromRGB(50, 24, 70);
            else if (soloed) bg = juce::Colour::fromRGB(20, 70, 50);
            g.setColour(bg);
            g.fillRect(r);

            const float level = juce::jlimit(0.0f, 1.0f, trackMeter_[i]);
            if (level > 0.001f)
            {
                const int fillW = juce::roundToInt(static_cast<float>(r.getWidth()) * level);
                g.setColour(meterColour(level).withAlpha(0.55f));
                g.fillRect(r.getX(), r.getY(), fillW, r.getHeight());
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
                w->onCloseRequested = [fb](std::function<void()> doQuit)
                {
                    fb->withDirtyGuard(std::move(doQuit));
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
            keyboardArea_.repaint();
        }
    }

    void LockstepEditor::mouseDown(const juce::MouseEvent& e)
    {
        // Meter drag: vertical drag on a track button sets AMP level (left) or sendA (right).
        meterDrag_ = {};
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            if (e.eventComponent != &trackBtns_[static_cast<std::size_t>(i)])
                continue;
            const juce::String paramId = e.mods.isRightButtonDown()
                                             ? "lockstep.amp.sendA"
                                             : "lockstep.amp.level";
            const int slot = processor_.slotForId(i, paramId);
            if (slot < 0) break;
            const auto spec = processor_.paramSpec(i, slot);
            meterDrag_.track = i;
            meterDrag_.paramSlot = slot;
            meterDrag_.startValue = processor_.baseParamValue(i, slot);
            meterDrag_.paramMax = spec.maxValue > spec.minValue ? spec.maxValue : 1.0f;
            meterDrag_.startY = e.getScreenY();
            break;
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
        if (meterDrag_.track < 0) return;
        const float kDragScale = 200.0f;  // pixels for full range
        const int dy = meterDrag_.startY - e.getScreenY();
        const float delta = static_cast<float>(dy) / kDragScale * meterDrag_.paramMax;
        const float newVal = juce::jlimit(0.0f, meterDrag_.paramMax,
                                          meterDrag_.startValue + delta);
        processor_.writeParam(meterDrag_.track, meterDrag_.paramSlot, newVal);
    }

    void LockstepEditor::mouseUp(const juce::MouseEvent& /*e*/)
    {
        meterDrag_ = {};
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

        // ---- MHZ.2.2: top-bar dashboard (free space between left controls and right buttons) ----
        // STATE only: Song/Scene identity + persistent badges (CK, CHN, QUE, SHR, CPY, mode).
        // Gesture context lives exclusively in the bottom nav strip pill (single source of truth).
        {
            static constexpr int kBadgeH = 16;
            static constexpr int kGap = 3;
            static constexpr int kDashStartX = 420;   // right edge of left controls
            static constexpr int kSplitX = 640;   // right boundary of the state dashboard
            const int by = (36 - kBadgeH) / 2;

            g.setFont(juce::Font(juce::FontOptions(10.0f)));

            // ---- Left dashboard ----
            {
                // Song / Scene identity pill.
                const int sg = processor_.activePieceIdx() + 1;
                const int sc = processor_.activeSectionIdx() + 1;
                const juce::String identity = "Sg:" + juce::String(sg) + "  Sc:" + juce::String(sc);
                {
                    const auto r = juce::Rectangle<int>(kDashStartX, by, 120, kBadgeH);
                    g.setColour(juce::Colour(0xFF262830u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colour(0xFFBBCCDDu));
                    g.drawText(identity, r, juce::Justification::centred);
                }

                int bx = kDashStartX + 120 + kGap;

                // Clipboard badge.
                static constexpr const char* kCbLabels[] = {
                    nullptr,     // None
                    "CPY:STP",   // Step
                    "CPY:SEC",   // Section
                    "CPY:TRK",   // Track
                    "CPY:PHR",   // Pattern
                    "CPY:SCN",   // Scene
                    "CPY:ALL",   // All
                };
                const auto ctIdx = static_cast<std::size_t>(clipboard_.type);
                const char* cbLabel = (ctIdx < std::size(kCbLabels)) ? kCbLabels[ctIdx] : nullptr;
                if (cbLabel != nullptr && bx + 56 < kSplitX)
                {
                    const auto r = juce::Rectangle<int>(bx, by, 56, kBadgeH);
                    g.setColour(juce::Colour(0xFF50B0C8u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::black);
                    g.drawText(cbLabel, r, juce::Justification::centred);
                    bx += 56 + kGap;
                }

                // CK:N checkpoint badge — depth of the currently-held scope's stack.
                int ckTrack = 0;
                const CheckpointScope ckScp = ckScope(ckTrack);
                const int checkpointDepth = processor_.checkpointDepth(ckScp, ckTrack);
                if (checkpointDepth > 0 && bx + 38 < kSplitX)
                {
                    const auto r = juce::Rectangle<int>(bx, by, 38, kBadgeH);
                    g.setColour(juce::Colour(0xFF40A080u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText("CK:" + juce::String(checkpointDepth), r, juce::Justification::centred);
                    bx += 38 + kGap;
                }


                // MHZ.7.1: per-track input-mode badge — shown when focused track is not in PLAY mode.
                {
                    const int track = keyboardArea_.getActiveTrack();
                    const auto mode = (track >= 0 && track < static_cast<int>(kNumTracks))
                                          ? uiState_.trackInputMode[static_cast<std::size_t>(track)]
                                          : TrackInputMode::Play;
                    const char* modeLabel = nullptr;
                    juce::Colour modeCol;
                    switch (mode)
                    {
                        case TrackInputMode::Play: break;  // no badge
                        case TrackInputMode::Chromatic:
                            modeLabel = "CHROM";
                            modeCol = juce::Colour(0xFF4090E0u);
                            break;
                        case TrackInputMode::Levels:
                            modeLabel = "LEVLS";
                            modeCol = juce::Colour(0xFFE07030u);
                            break;
                    }
                    if (modeLabel != nullptr && bx + 46 < kSplitX)
                    {
                        const auto r = juce::Rectangle<int>(bx, by, 46, kBadgeH);
                        g.setColour(modeCol);
                        g.fillRoundedRectangle(r.toFloat(), 3.0f);
                        g.setColour(juce::Colours::white);
                        g.drawText(modeLabel, r, juce::Justification::centred);
                    }
                }
            }

            // ---- Right held-context preview ----
            // Compose a short description of the currently held modifier cluster.
            {
                juce::String ctx;
                const auto& ui = uiState_;
                // 5.5: Euclidean modal armed — show commit/cancel banner.
                if (ui.euclidHeld)
                {
                    ctx = "EUCLID  pulses / offset / accent  |  P = commit  Func+P = cancel";
                }
                // MHZ.3.5: Func+Part = machine picker — show dedicated hint.
                else if (ui.funcTrackHeld)
                {
                    ctx = "FUNC + MACH  |  press step to select machine";
                }
                // MHZ.3.4: P-lock clear mode.
                else if (ui.pLockClearMode)
                {
                    ctx = "FUNC + STEP " + juce::String(ui.pLockClearStep + 1) + "  |  press cell to clear P-Lock slot";
                }
                // Sticky DENSITY mode context.
                else if ((ui.overlay == Overlay::Density))
                {
                    using SP = UiState::DensitySubPage;
                    if (ui.densitySubPage == SP::Musicality)
                        ctx = "DENSITY  Musicality  |  turn = Unif / Mix / Metric  MOD = selection";
                    else if (ui.densitySubPage == SP::Selection)
                        ctx = "DENSITY  Selection  |  turn = Scrub / Re-roll / Exempt  MOD = amount";
                    else
                        ctx = "DENSITY  Amount  |  nav = bank  Song = master  MOD = musicality";
                }
                // Sticky VEL sticky mode context.
                else if ((ui.overlay == Overlay::Vel))
                {
                    using VP = UiState::VelSubPage;
                    if (ui.velSubPage == VP::Center)
                        ctx = "VEL CENTER  |  turn = 1-127  AMP = mode  sec key = exit";
                    else if (ui.velSubPage == VP::Mode)
                        ctx = "VEL MODE  |  turn = Off / Bar  AMP = blend  sec key = exit";
                    else if (ui.velSubPage == VP::Blend)
                        ctx = "VEL BLEND  |  turn = Repl / Mix  AMP = depth  sec key = exit";
                    else
                        ctx = "VEL DEPTH  |  nav = bank  AMP = center  sec key = exit";
                }
                else
                {
                    // Step held (no modifier) → P-Lock edit mode.
                    const auto& ec = processor_.editContext();
                    if (ui.stepHeld && ec.isActiveForEditing())
                    {
                        const int stepNum = ec.heldStepIndex() + 1;
                        const int cnt = static_cast<int>(ec.heldSteps().size());
                        ctx = cnt > 1
                                  ? juce::String(cnt) + " STEPS  |  knob = P-Lock"
                                  : "STEP " + juce::String(stepNum) + "  |  knob = P-Lock";
                        if (!ui.funcHeld)
                        {
                            const int t = ec.heldTrackIndex();
                            if (t >= 0 && t < static_cast<int>(kNumTracks))
                            {
                                const auto& trk = processor_.sequence()
                                                      .tracks[static_cast<std::size_t>(t)];
                                // QUANT granularity (Trig scope = the held steps): surface
                                // P's target when any held step actually carries a microOffset.
                                bool anyOffset = false;
                                for (int si : ec.heldSteps())
                                    if (si >= 0 && si < kMaxStepsPerTrack
                                        && trk.steps[static_cast<std::size_t>(si)].microOffset != 0.0f)
                                    {
                                        anyOffset = true;
                                        break;
                                    }
                                if (anyOffset)
                                    ctx += cnt == 1
                                               ? "  |  P = QUANT step"
                                               : "  |  P = QUANT " + juce::String(cnt) + " steps";

                                // Content-aware clear hints on single-step holds.
                                if (cnt == 1)
                                {
                                    const int si = ec.heldStepIndex();
                                    if (si >= 0 && si < kMaxStepsPerTrack)
                                    {
                                        const auto& s = trk.steps[static_cast<std::size_t>(si)];
                                        if (!s.overrides.empty())
                                            ctx += "  |  Func+Clear = wipe P-Locks";
                                        if (s.trigOverride.noteCount > 0)
                                            ctx += "  |  SRC+Clear = clear notes";
                                    }
                                }
                            }
                        }
                    }
                    // Primary scope token. MHZ.9.7: show mode-cycle hint when Track+Control-All.
                    else if (ui.trackHeld && processor_.controlAllActive())
                    {
                        ctx = juce::String(u8"TRACK  |  ↑↓ cycle PLAY/CHROM/LEVLS");
                    }
                    else if (ui.trackHeld)
                    {
                        const int t = keyboardArea_.getActiveTrack();
                        ctx = "TRACK " + juce::String(t + 1);
                        // QUANT granularity: Track scope quantizes the whole focused track.
                        if (t >= 0 && t < static_cast<int>(kNumTracks))
                        {
                            const auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(t)];
                            bool anyOffset = false;
                            for (const auto& s : trk.steps)
                                if (s.microOffset != 0.0f) { anyOffset = true; break; }
                            if (anyOffset)
                                ctx += "  |  P = QUANT track";
                        }
                    }
                    else if (ui.phraseScopeHeld)
                    {
                        ctx = "PHRASE";
                        // QUANT granularity: Phrase scope quantizes every track.
                        bool anyOffset = false;
                        for (const auto& trk : processor_.sequence().tracks)
                        {
                            for (const auto& s : trk.steps)
                                if (s.microOffset != 0.0f) { anyOffset = true; break; }
                            if (anyOffset) break;
                        }
                        if (anyOffset)
                            ctx += "  |  P = QUANT all tracks";
                    }
                    else if (ui.sceneHeld) ctx = "SCENE";
                    else if (ui.morphHeld) ctx = "MORPH";
                    else if (ui.songHeld) ctx = "SONG";
                    else if (ui.muteHeld) ctx = "MUTE";
                    else if (ui.fillHeld) ctx = "FILL";
                    else if (ui.funcHeld)
                    {
                        const MetaBand activeBand = resolveMetaBand(ui);
                        if (activeBand == MetaBand::Density && ui.songHeld)
                            ctx = "DENSITY  master overlay";
                        else if (activeBand == MetaBand::Density)
                            ctx = "DENSITY  per-track  (Func+MOD to pin)";
                        else
                            ctx = "FUNC";
                    }
                }

                // Transient CPC status always uses the nav lane — flash even without active gesture.
                {
                    const auto navLocal = keyboardArea_.navAreaBounds();
                    const auto navInEditor = navLocal.translated(
                        keyboardArea_.getX(), keyboardArea_.getY());
                    paintStatus(g, navInEditor);
                }

                if (ctx.isEmpty()) return;   // nothing held — preview is blank

                // Qualify with Func if held alongside another modifier (normal path only).
                if (!ui.funcTrackHeld && !ui.pLockClearMode && ui.funcHeld && ctx != "FUNC")
                    ctx = "FUNC + " + ctx;

                // Active section suffix — master-aware: show master unit name in master mode.
                const int activeTrack = keyboardArea_.getActiveTrack();
                if (ui.masterSection == 5)
                {
                    // Song+FX meta: show which master unit is focused.
                    static const char* kUnitSuffixes[4] = {
                        "FX: Insert 1", "FX: Insert 2",
                        "FX: Send A",   "FX: Send B"
                    };
                    const int u = juce::jlimit(0, 3, ui.masterFxInsertSlot);
                    ctx += juce::String("  |  ") + juce::String(kUnitSuffixes[u]);
                }
                else if (ui.masterSection >= 0)
                {
                    // Other meta sections (Cond, Trig, Divider, PhraseLen) — meta name.
                    static const char* kMetaSuffixes[] = { "Cond", "Trig", "", "Div", "Len", "" };
                    if (ui.masterSection < 6)
                        ctx += juce::String("  |  ") + juce::String(kMetaSuffixes[ui.masterSection]);
                }
                else if (activeTrack >= 0)
                {
                    // Normal track mode: show the current track section name.
                    const int sec = ui.trackSection[static_cast<std::size_t>(activeTrack)];
                    if (sec >= 0 && sec < IMachine::kMaxSections)
                        ctx += juce::String("  |  ") + juce::String(IMachine::kCanonicalSectionNames[static_cast<std::size_t>(sec)]);
                }

                // Hint pill floating over the mini-sequencer strip.
                // Bottom nav strip is the single CONTEXT lane: shows the active gesture
                // banner when idle; pickers draw their own banner (navStripOverlayActive).
                // Suppressed when an overlay (FX picker, note edit, machine picker, etc.)
                // is already drawing its own banner in the nav strip area.
                if (!keyboardArea_.navStripOverlayActive())
                {
                    const auto navLocal = keyboardArea_.navAreaBounds();
                    const auto navInEditor = navLocal.translated(
                        keyboardArea_.getX(), keyboardArea_.getY());
                    const int pillH = 18;
                    const int pillW = juce::jmin(360, navInEditor.getWidth() - 16);
                    const auto pill = juce::Rectangle<int>(
                        navInEditor.getCentreX() - pillW / 2,
                        navInEditor.getCentreY() - pillH / 2,
                        pillW, pillH);
                    g.setColour(juce::Colour(0xCC1E2028u));
                    g.fillRoundedRectangle(pill.toFloat(), 8.0f);
                    g.setColour(juce::Colour(0xFFCCDDEEu));
                    g.setFont(juce::Font(juce::FontOptions(11.0f)));
                    g.drawText(ctx, pill.reduced(6, 0), juce::Justification::centred, true);
                }
            }
        }

        // ---- Diagnostic meters drawn over children ----
        // Stereo master output meter: two stacked 3px bars (L top, R below).
        {
            const int fullW = getWidth();
            const float levelL = juce::jlimit(0.0f, 1.0f, masterMeter_);
            const float levelR = juce::jlimit(0.0f, 1.0f, masterMeterR_);
            const int wL = juce::roundToInt(static_cast<float>(fullW) * levelL);
            const int wR = juce::roundToInt(static_cast<float>(fullW) * levelR);
            g.setColour(juce::Colour::fromRGB(30, 34, 40));
            g.fillRect(0, 0, fullW, 6);
            if (wL > 0) { g.setColour(meterColour(levelL)); g.fillRect(0, 0, wL, 3); }
            if (wR > 0) { g.setColour(meterColour(levelR)); g.fillRect(0, 3, wR, 3); }
        }
        // Per-track trig (left, cyan) + MIDI-in (right, magenta) activity dots.
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
            if (midiBlink_[i] > 0.02f)
            {
                g.setColour(juce::Colour::fromRGB(230, 80, 220).withAlpha(midiBlink_[i]));
                g.fillEllipse(static_cast<float>(r.getRight() - 2 - d),
                              static_cast<float>(r.getY() + 2), d, d);
            }
        }

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
        repaint();
    }

    void LockstepEditor::fileDragExit(const juce::StringArray& /*files*/)
    {
        isDraggingFiles_ = false;
        repaint();
    }

    void LockstepEditor::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
    {
        isDraggingFiles_ = false;
        int loaded = 0;
        for (const auto& path : files)
        {
            if (!isAudioFile(path)) continue;
            if (processor_.samplePool().load(path) >= 0)
                ++loaded;
        }
        if (loaded > 0)
        {
            poolOverlay_.setVisible(true);
            poolOverlay_.toFront(false);
        }
        repaint();
    }

    void LockstepEditor::refreshMetaBand()
    {
        manipulationZone_.setBand(resolveMetaBand(uiState_), swingScopeFor(uiState_));
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

    void LockstepEditor::cancelEuclid()
    {
        if (!uiState_.euclidHeld) return;
        if (euclidTrack_ >= 0)
        {
            auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(euclidTrack_)];
            for (int si = 0; si < euclidStashLen_; ++si)
                wt.steps[static_cast<std::size_t>(si)] =
                    euclidStash_[static_cast<std::size_t>(si)];
        }
        uiState_.resetEuclid();
        euclidTrack_ = -1;
        euclidStashLen_ = 0;
        refreshMetaBand();
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

        // Release latched steps (keep them in heldStepKeys_ if still physically held).
        auto& ctx = processor_.editContext();
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

        if (ctx.heldSteps().empty())
        {
            heldStepKeys_.clear();
            uiState_.stepHeld = false;
            editMode_.setTrigHeld(false);
        }

        uiState_.latch.anySteps = ctx.hasAnyLatchedStep();
        refreshSurface();
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
            repaint();
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
            repaint();
            return true;
        }
        // Section handling moved to handleOverlayEvent in the Section dispatch.
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

    // dispatchDown — source-agnostic button-down handler fed by both keyboard
    // and mouse.  rawCode is the physical key code (keyboard) or 0 (mouse).
    bool LockstepEditor::dispatchDown(ControllerEvent ev, int rawCode)
    {
        using CB = ControllerButton;

        // Generator hub (9.10): intercept bare TapTempo (3-key) before CommandCore.
        // handleTapTempo() is deferred to key-up; a long hold opens the hub instead.
        if (ev.button == CB::TapTempo)
        {
            tapTempoPhysHeld_ = true;
            tapTempoArmMs_ = juce::Time::getMillisecondCounterHiRes();
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
            case CB::Func:
                uiState_.funcHeld = true;
                // Func+Track is the machine/Kit picker gesture (§4.7.2) — entering
                // the compound re-skins the step grid to machine names directly.
                uiState_.funcTrackHeld = uiState_.trackHeld;
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

                        // Route through the overlay reducer.  Guard: sticky modes
                        // require no latches (they shouldn't be co-active, but
                        // double-tap can arrive mid-gesture).  Euclid always exits.
                        const Overlay prevOv = activeOverlay(uiState_);
                        const bool canEscape = (prevOv == Overlay::Euclid)
                            || (!uiState_.latch.any()
                                && !processor_.editContext().hasAnyLatchedStep());
                        if (canEscape)
                        {
                            const auto r = handleOverlayEvent(uiState_,
                                { ModeEventKind::DoubleTapFunc });
                            if (r == OverlayResult::Exited)
                            {
                                // Euclid stash restore is editor-owned state.
                                if (prevOv == Overlay::Euclid && euclidTrack_ >= 0)
                                {
                                    auto& wt = processor_.sequence().tracks[
                                        static_cast<std::size_t>(euclidTrack_)];
                                    for (int si = 0; si < euclidStashLen_; ++si)
                                        wt.steps[static_cast<std::size_t>(si)] =
                                            euclidStash_[static_cast<std::size_t>(si)];
                                    euclidTrack_ = -1;
                                    euclidStashLen_ = 0;
                                }
                                refreshMetaBand();
                                repaint();
                            }
                        }
                    }
                }
                return true;

            case CB::TrackScope:
                physHeld_.track = true;
                uiState_.trackHeld = true;
                // Func+Track = machine/Kit picker (§4.7.2): arm when Track pressed with Func held.
                uiState_.funcTrackHeld = uiState_.funcHeld;
                processor_.setControlAllActive(true);  // MD.10: active until a track is selected
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::TrackScope, uiState_.latch.track);
                clearSwingDismissed();
                repaint();
                return true;

            case CB::PhraseScope:
                physHeld_.phrase = true;
                uiState_.phraseScopeHeld = true;
                uiState_.phraseScopeUsed = false;
                editMode_.onScopeEvent(ev);
                // Euclid entry moved to generator hub (9.10): Phrase+Fill no longer enters it.
                handleModifierTap(CB::PhraseScope, uiState_.latch.phrase);
                repaint();
                return true;

            case CB::MuteScope:
                physHeld_.mute = true;
                uiState_.muteHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::MuteScope, uiState_.latch.mute);
                repaint();
                return true;

            case CB::FillScope:
                physHeld_.fill = true;
                uiState_.fillHeld = true;
                editMode_.onScopeEvent(ev);
                // Euclid entry moved to generator hub (9.10): Phrase+Fill no longer enters it.
                updateFillActivation();
                handleModifierTap(CB::FillScope, uiState_.latch.fill);
                repaint();
                return true;

            case CB::CueScope:
                physHeld_.cue = true;
                uiState_.cueHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case CB::MorphScope:
                physHeld_.morph = true;
                uiState_.morphHeld = true;
                manipulationZone_.setMorphHeld(true);
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::MorphScope, uiState_.latch.morph);
                repaint();
                return true;

            case CB::SongScope:
                physHeld_.song = true;
                uiState_.songHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::SongScope, uiState_.latch.song);
                clearSwingDismissed();
                repaint();
                return true;

            case CB::SceneScope:
                physHeld_.scene = true;
                // Track + Scene: re-sync focused musician to current Scene (Phase 7).
                if (uiState_.trackHeld)
                {
                    processor_.resyncTrackToScene(processor_.focusTrack());
                    refreshSurface();
                    return true;
                }
                uiState_.sceneHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::SceneScope, uiState_.latch.scene);
                clearSwingDismissed();
                refreshSurface();
                return true;

            case ControllerButton::Section: {
                // Euclid is a focused modal generator; selecting a section exits it,
                // reverting the live preview (commit is the explicit P press). Without
                // this the mode lingered after navigating away and a later Confirm
                // would still write the pattern.
                if (uiState_.euclidHeld)
                {
                    cancelEuclid();
                    keyboardArea_.repaint();
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
                        repaint();
                    }
                    return true;
                }
                if (uiState_.fillHeld && ev.index == 1)
                {
                    uiState_.trigGridMode = TrigGridMode::SoundPool;
                    repaint();
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
                    if (r == OverlayResult::Consumed) { refreshMetaBand(); repaint(); return true; }
                    if (r == OverlayResult::Exited)   refreshMetaBand();
                }

                if (sectionScope != PS::None)
                {
                    // Dim under this scope — no content, block entirely.
                    if (!scopedCell(sectionScope, ev.index).hasContent) return true;

                    // Scope-specific dispatch for cells whose content is implemented.
                    if (sectionScope == PS::Phrase && ev.index == 0)
                    {
                        // Phrase+LEN: phrase length (PHRASELEN meta, index 4).
                        keyboardArea_.selectMetaSection(4);
                        return true;
                    }
                    if (sectionScope == PS::Track && ev.index == 0)
                    {
                        // Track+DIV: kit divider (DIV meta, index 3).
                        keyboardArea_.selectMetaSection(3);
                        return true;
                    }
                    if (sectionScope == PS::Song && ev.index == 5)
                    {
                        // Song+FX: master insert / send params.
                        // Re-press while already at section 5 cycles through loaded units
                        // (skips empty); entering for the first time lands on the first
                        // loaded unit (or Insert1 if none). Never toggles out of master mode.
                        if (uiState_.masterSection == 5)
                            uiState_.masterFxInsertSlot = nextLoadedMasterUnit(
                                uiState_.masterFxInsertSlot);
                        else
                            uiState_.masterFxInsertSlot = firstLoadedMasterUnit();
                        keyboardArea_.selectMetaSection(5, /*toggle=*/false);
                        refreshMetaBand();
                        return true;
                    }
                    if (isTimeEntryChord(sectionScope, ev.index))
                    {
                        // Song+TRIG or Scene+TRIG: toggle TIME sticky mode (DESIGN §4.8).
                        applyTimeEntry(uiState_);
                        refreshMetaBand();
                        return true;
                    }
                    // All other non-dim scope cells fall through to the machine's own
                    // section (e.g. Track+FLTR → section 2 = post-machine FLTR block).
                }

                // KeyboardArea gates on machine slot availability.
                keyboardArea_.selectSection(ev.index);
                // Track section key hold for Section-scope verb dispatch (MD.3).
                if (heldSectionRawCode_ < 0)
                {
                    heldSectionRawCode_ = rawCode;
                    heldSectionIndex_ = ev.index;
                    editMode_.setSectionHeld(true);
                }
                return true;
            }

            case ControllerButton::MetaSection:
                // 6.5: Func+Song+FX → open master FX picker on the step grid.
                // Also navigates to Song+FX meta section so MZ shows master insert params.
                // Re-pressing while picker is open cycles all 4 slots (to load into empties too).
                if (uiState_.funcHeld && uiState_.songHeld && ev.index == processor_.kFxSecIdx)
                {
                    if (uiState_.masterFxPickerOpen)
                        uiState_.masterFxInsertSlot = (uiState_.masterFxInsertSlot + 1) % 4;
                    else
                        uiState_.masterFxInsertSlot = 0;
                    uiState_.masterFxPickerOpen = true;
                    // Navigate to Song+FX meta section so MZ shows master FX params.
                    keyboardArea_.selectMetaSection(processor_.kFxSecIdx, /*toggle=*/false);
                    refreshMetaBand();
                    repaint();
                    return true;
                }
                // 6.5: Func+FX → enter effect picker (step grid re-skins to catalogue).
                // Re-pressing FX while picker is active cycles the targeted insert slot.
                // (Func+section routes to MetaSection via QwertyOverlay kFunc table.)
                if (uiState_.funcHeld && ev.index == processor_.kFxSecIdx)
                {
                    if (uiState_.funcFxHeld)
                        uiState_.funcFxInsertSlot = 1 - uiState_.funcFxInsertSlot;  // cycle 0↔1
                    else
                        uiState_.funcFxInsertSlot = 0;
                    uiState_.funcFxHeld = true;
                    repaint();
                    return true;
                }
                // Density (Func+MOD) and Vel (Func+AMP) entry moved to generator hub (9.10).
                // Func-row secondaries are COND (TRIG) and NOTE (SRC) only; other
                // sections have no Func secondary (FILTER/FX metas relocated to
                // Track+TRIG / Song+FX). Those cells dim under Func — ignore the
                // press so Func doesn't silently open a meta the row hides.
                if (KeyboardArea::isReservedMeta(ev.index))
                    return true;
                if (uiState_.funcHeld && ev.index == 1)
                    uiState_.funcSrcHeld = true;  // Func+Src(NOTE) — enables note-edit gesture
                keyboardArea_.selectMetaSection(ev.index);
                return true;

            case ControllerButton::Step: {
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
                    const LayerFacts stepFacts{ layerMode, layerAt };
                    const SurfaceLayer layer = resolveActiveLayer(
                        uiState_, processor_.editContext(), stepFacts);

                    // --------------------------------------------------------
                    // 9.10: Generator hub (3-key held ≥350 ms)
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::GeneratorHub)
                    {
                        if (ev.index >= 0 && ev.index <= 2)
                        {
                            switch (ev.index)
                            {
                                case 0: enterEuclid(layerAt < 0 ? 0 : layerAt); break;
                                case 1: enterDensitySticky(); break;
                                case 2: enterVelSticky(); break;
                                default: break;
                            }
                        }
                        repaint();
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
                    // 6.5: Master FX picker (Func+Song+FX held)
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::MasterFxPicker)
                    {
                        if (ev.index < 0 || ev.index >= 16) return true;
                        if (ev.index >= processor_.numAvailableEffects()) return true;
                        const auto info = processor_.availableEffectInfo(ev.index);
                        // 8.26: units 0-1 = master inserts, units 2-3 = send returns.
                        const int mUnit = uiState_.masterFxInsertSlot;
                        const bool isSend = (mUnit >= 2);
                        const int mSlot = isSend ? mUnit - 2 : mUnit;
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
                            }
                            else
                            {
                                processor_.setMasterInsert(mSlot, info.id);
                                processor_.setMasterInsertBypass(mSlot, false);
                            }
                        }
                        uiState_.masterFxPickerOpen = false;
                        refreshMetaBand();
                        repaint();
                        return true;
                    }

                    // --------------------------------------------------------
                    // 6.5: FX insert picker (Func+FX held)
                    // --------------------------------------------------------
                    if (layer == SurfaceLayer::TrackFxPicker)
                    {
                        if (ev.index < 0 || ev.index >= 16) return true;
                        const int at = keyboardArea_.getActiveTrack();
                        if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;
                        if (ev.index >= processor_.numAvailableEffects()) return true;
                        const auto info = processor_.availableEffectInfo(ev.index);
                        // 8.26: masterOnly effects cannot be placed in track inserts.
                        if (info.masterOnly) return true;
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
                        repaint();
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
                        keyboardArea_.repaint();
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

                        keyboardArea_.repaint();
                        return true;
                    }
                }

                // Master + step: Song (song) select (Phase 7 / DESIGN §16).
                if (uiState_.songHeld && !uiState_.morphHeld)
                {
                    if (ev.index >= 0 && ev.index < kNumSongs)
                        processor_.setActiveSong(ev.index);
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
                if (uiState_.funcHeld && !uiState_.funcTrackHeld && (uiState_.phraseScopeHeld || uiState_.morphHeld))
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
                // deviate the focused track only.
                if (uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                        processor_.swapPhraseForTrack(keyboardArea_.getActiveTrack(), ev.index);
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
                if (uiState_.sceneHeld && !uiState_.funcTrackHeld)
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
                            processor_.snapshot(CheckpointScope::Song, 0);
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
                            processor_.snapshot(CheckpointScope::Song, 0);
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
                            processor_.snapshot(CheckpointScope::Song, 0);
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
                    keyboardArea_.repaint();
                    return true;
                }

                // Func+Src+step: tentative note-edit entry.
                // Suppress pLockClearMode; NoteEdit mode activates on step key release.
                if (uiState_.funcSrcHeld && !uiState_.noteEditMode)
                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps + ev.index;
                    uiState_.noteEditSteps = { absStep };
                    keyboardArea_.repaint();
                    return true;
                }

                // MHZ.3.4: P-Lock clear mode — a second step press stages/un-stages a slot.
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
                    keyboardArea_.repaint();
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

                // MHZ.3.4: Func + step (no existing step held) → enter P-Lock clear mode.
                // MHZ.9.5: use ctx.heldSteps().empty() so latched steps keep the edit context alive.
                if (uiState_.funcHeld && processor_.editContext().heldSteps().empty())
                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps + ev.index;
                    uiState_.pLockClearStaged.clear();  // fresh session
                    uiState_.pLockClearMode = true;
                    uiState_.pLockClearTrack = keyboardArea_.getActiveTrack();
                    uiState_.pLockClearStep = absStep;
                    keyboardArea_.repaint();
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
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    const bool isDouble = gesture_.doubleTap(absStep, now);

                    // MHZ.9.5: double-tap on a step = virtual-hold (latch operand).
                    // On the 2nd key-down, re-hold the step and mark it latched; also
                    // revert the first key-up's trig toggle (if it happened) so the
                    // net effect is zero trig changes.
                    if (isDouble)
                    {
                        auto& ctx = processor_.editContext();
                        ctx.hold(keyboardArea_.getActiveTrack(), absStep);
                        ctx.setLatched(absStep);
                        uiState_.latch.anySteps = ctx.hasAnyLatchedStep();
                        heldStepKeys_.push_back({ rawCode, absStep });
                        uiState_.stepHeld = true;
                        editMode_.setTrigHeld(true);

                        // Revert the first key-up trig flip if it happened on this step.
                        if (lastTrigToggleApplied_ && lastTrigToggleStep_ == absStep && lastTrigToggleTrack_ == ctx.heldTrackIndex())
                        {
                            auto& s = processor_.sequence()
                                          .tracks[static_cast<std::size_t>(lastTrigToggleTrack_)]
                                          .steps[static_cast<std::size_t>(lastTrigToggleStep_)];
                            s.trig = !s.trig;  // undo the first-tap's toggle
                        }
                        lastTrigToggleApplied_ = false;
                        lastTrigToggleStep_ = -1;
                        lastTrigToggleTrack_ = -1;
                        repaint();
                        return true;
                    }

                    // Normal (first) press: hold the step.
                    heldStepKeys_.push_back({ rawCode, absStep });
                    uiState_.stepHeld = true;
                    processor_.editContext().hold(keyboardArea_.getActiveTrack(), absStep);
                    editMode_.setTrigHeld(true);
                    // Clear stale trig-toggle tracking on any new step press.
                    lastTrigToggleApplied_ = false;
                    lastTrigToggleStep_ = -1;
                    lastTrigToggleTrack_ = -1;
                    repaint();
                }
                return true;
            }

            case ControllerButton::SelectTrack: {
                // When Track is held, QwertyOverlay routes step keys to this case
                // (not the Step case). So the Track-compound gestures must be
                // handled HERE, before the plain track-select fallback below.

                // Func+Track (machine/Kit picker) + step = assign the indexed
                // machine to the focused track (§4.7.2).
                if (uiState_.funcTrackHeld)
                {
                    if (ev.index >= 0 && ev.index < processor_.numAvailableMachines())
                    {
                        processor_.setTrackMachine(keyboardArea_.getActiveTrack(),
                                                   std::string(processor_.availableMachineInfo(ev.index).id));
                        keyboardArea_.syncToActiveTrack();
                    }
                    keyboardArea_.repaint();
                    return true;
                }
                // Track+Phrase+step = sticky per-track deviation for the focused
                // musician (Phrase-alone = unison swap, handled in the Step case).
                if (uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                        processor_.swapPhraseForTrack(keyboardArea_.getActiveTrack(), ev.index);
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

            case ControllerButton::NavUp: {
                if (consumeDensityStickyKey(CB::NavUp)) return true;
                if (consumeVelStickyKey(CB::NavUp)) return true;
                const int t = keyboardArea_.getActiveTrack();
                // Morph+^ = force A-pole edits while ^ is held (DESIGN §17.3).
                if (uiState_.morphHeld)
                {
                    uiState_.morphNavQualifier = 1;
                    manipulationZone_.setMorphQualifier(1);
                    manipulationZone_.setMorphHeld(uiState_.morphHeld);
                    repaint();
                    return true;
                }
                // Func+↑ = double the focused track's pattern length.
                if (uiState_.funcHeld && !uiState_.trackHeld)
                {
                    if (t >= 0 && t < static_cast<int>(kNumTracks))
                        processor_.doubleTrackLength(t);
                    keyboardArea_.repaint();
                    return true;
                }
                // MHZ.9.7: Track (no specific track selected) + NavUp → cycle input mode upward.
                if (uiState_.trackHeld && processor_.controlAllActive() && t >= 0 && t < static_cast<int>(kNumTracks))
                {
                    auto& mode = uiState_.trackInputMode[static_cast<std::size_t>(t)];
                    switch (mode)
                    {
                        case TrackInputMode::Play:      mode = TrackInputMode::Levels; break;
                        case TrackInputMode::Chromatic: mode = TrackInputMode::Play; break;
                        case TrackInputMode::Levels:    mode = TrackInputMode::Chromatic; break;
                        default:                        break;
                    }
                    escapeAllLatches();  // entering new modality exits current latch
                    refreshSurface();
                    return true;
                }
                // Normal: next higher track number.
                keyboardArea_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1, t + 1));
                return true;
            }

            case ControllerButton::NavDown: {
                if (consumeDensityStickyKey(CB::NavDown)) return true;
                if (consumeVelStickyKey(CB::NavDown)) return true;
                const int t = keyboardArea_.getActiveTrack();
                // Morph+v = force B-pole edits while v is held (DESIGN §17.3).
                if (uiState_.morphHeld)
                {
                    uiState_.morphNavQualifier = 2;
                    manipulationZone_.setMorphQualifier(2);
                    manipulationZone_.setMorphHeld(uiState_.morphHeld);
                    repaint();
                    return true;
                }
                // Func+↓ = halve the focused track's pattern length.
                if (uiState_.funcHeld && !uiState_.trackHeld)
                {
                    if (t >= 0 && t < static_cast<int>(kNumTracks))
                        processor_.halveTrackLength(t);
                    keyboardArea_.repaint();
                    return true;
                }
                // MHZ.9.7: Track (no specific track selected) + NavDown → cycle input mode downward.
                if (uiState_.trackHeld && processor_.controlAllActive() && t >= 0 && t < static_cast<int>(kNumTracks))
                {
                    auto& mode = uiState_.trackInputMode[static_cast<std::size_t>(t)];
                    switch (mode)
                    {
                        case TrackInputMode::Play:      mode = TrackInputMode::Chromatic; break;
                        case TrackInputMode::Chromatic: mode = TrackInputMode::Levels; break;
                        case TrackInputMode::Levels:    mode = TrackInputMode::Play; break;
                        default:                        break;
                    }
                    escapeAllLatches();  // entering new modality exits current latch
                    refreshSurface();
                    return true;
                }
                // Normal: previous (lower) track number.
                keyboardArea_.setActiveTrack(std::max(0, t - 1));
                return true;
            }

            case ControllerButton::NavLeft: {
                if (consumeDensityStickyKey(CB::NavLeft)) return true;
                if (consumeVelStickyKey(CB::NavLeft)) return true;
                // Note-edit mode and CHROMATIC mode both use NavLeft/Right for octave shift.
                const int tl = keyboardArea_.getActiveTrack();
                const bool chromL = tl >= 0 && tl < static_cast<int>(kNumTracks) && uiState_.trackInputMode[static_cast<std::size_t>(tl)] == TrackInputMode::Chromatic;
                // Func+← = rotate the focused track's sequence one step left.
                // In note-edit or Chromatic mode, Func+← keeps its octave-shift role.
                if (uiState_.funcHeld && !uiState_.noteEditMode && !chromL)
                {
                    if (tl >= 0 && tl < static_cast<int>(kNumTracks))
                        processor_.rotateTrackSteps(tl, -1);
                    keyboardArea_.repaint();
                    return true;
                }
                if (uiState_.noteEditMode || chromL)
                {
                    uiState_.noteEditOctave = std::max(uiState_.noteEditOctave - 1, 0);
                    keyboardArea_.repaint();
                    return true;
                }
                keyboardArea_.prevPage();
                return true;
            }

            case ControllerButton::NavRight: {
                if (consumeDensityStickyKey(CB::NavRight)) return true;
                if (consumeVelStickyKey(CB::NavRight)) return true;
                const int tr = keyboardArea_.getActiveTrack();
                const bool chromR = tr >= 0 && tr < static_cast<int>(kNumTracks) && uiState_.trackInputMode[static_cast<std::size_t>(tr)] == TrackInputMode::Chromatic;
                // Func+→ = rotate the focused track's sequence one step right.
                // In note-edit or Chromatic mode, Func+→ keeps its octave-shift role.
                if (uiState_.funcHeld && !uiState_.noteEditMode && !chromR)
                {
                    if (tr >= 0 && tr < static_cast<int>(kNumTracks))
                        processor_.rotateTrackSteps(tr, +1);
                    keyboardArea_.repaint();
                    return true;
                }
                if (uiState_.noteEditMode || chromR)
                {
                    uiState_.noteEditOctave = std::min(uiState_.noteEditOctave + 1, 8);
                    keyboardArea_.repaint();
                    return true;
                }
                // DESIGN §34.4 / PRINCIPLES §17 (nav reveal/unlock family): at the last
                // in-length page, a single NavRight is a no-op (clamped); a double-tap
                // unlocks one empty page past the end so a longer length can be set.
                if (keyboardArea_.currentPage() >= keyboardArea_.numPages() - 1)
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (gesture_.doubleTap(GestureRecognizer::kNavRightUnlock, now))
                    {
                        keyboardArea_.unlockScrollPastEnd();
                        setStatus(status::scrolledPastEnd());
                    }
                }
                keyboardArea_.nextPage();
                return true;
            }

            // MHY.4: right-utility verbs. Without a scope modifier these perform their
            // default transport / confirmation action; with a scope held, EditMode
            // routes them as grammar verbs (CPY / PST / CLR / confirm / cancel).

            case ControllerButton::VerbPlay: {
                using PS = EditMode::PrimaryScope;
                // Scope held → grammar verb (Paste).
                if (editMode_.primaryScope() != PS::None && editMode_.primaryScope() != PS::Func)
                {
                    auto ctx = commandContext();
                    (void)commandCore_.handleVerb(editMode_.primaryScope(), ev.button, ctx, *editorEffects_);
                    repaint();
                    return true;
                }
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
                            repaint();
                        }
                    }
                    return true;
                }
                if (playKeyHeld_) return true;  // ignore key repeat
                playKeyHeld_ = true;

                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = gesture_.playDoubleTap(now);

                if (isDouble)
                {
                    // Double-tap = stop + reset: next start re-anchors to step 0.
                    processor_.clock().setInPluginPlaying(false);
                    processor_.clock().resetPhase();
                    processor_.requestFreshStart();
                }
                else
                {
                    // Single tap toggles play/pause; resume continues in phase.
                    processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                }
                return true;
            }

            case ControllerButton::VerbClear: {
                using PS = EditMode::PrimaryScope;
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
                        repaint();
                        return true;
                    }
                }
                // Scene scope held → cancel queued scene.
                if (uiState_.sceneHeld)
                {
                    processor_.cancelQueuedScene();
                    repaint();
                    return true;
                }
                // Phrase scope → cancel queued scene.
                if (uiState_.phraseScopeHeld)
                {
                    processor_.cancelQueuedScene();
                    uiState_.phraseScopeUsed = true;
                    repaint();
                    return true;
                }
                // Non-trivial scope → grammar verb (Clear scope contents).
                if (editMode_.primaryScope() != PS::None && editMode_.primaryScope() != PS::Func)
                {
                    auto ctx = commandContext();
                    (void)commandCore_.handleVerb(editMode_.primaryScope(), ev.button, ctx, *editorEffects_);
                    repaint();
                    return true;
                }
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

            // ControllerButton::VerbDelete — migrated to CommandCore::handleDown (8.24 Stage 7)
            // ControllerButton::VerbPanic — migrated to CommandCore::handleDown (8.11 A4.6)

            case ControllerButton::VerbRecord: {
                using PS = EditMode::PrimaryScope;
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
                    repaint();
                    return true;
                }
                // Scope held → grammar verb (e.g. copy).  No scope → arm recording.
                if (editMode_.primaryScope() != PS::None && editMode_.primaryScope() != PS::Func)
                {
                    auto ctx = commandContext();
                    (void)commandCore_.handleVerb(editMode_.primaryScope(), ev.button, ctx, *editorEffects_);
                    repaint();
                    return true;
                }
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
                // Double-tap = overdub record; single tap = plain (overwrite) record.
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
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
                return true;
            }

            case ControllerButton::VerbSnapshot: {
                using PS = EditMode::PrimaryScope;
                // 5.5: Euclid modal armed → Y is inert (commit is on bare P).
                if (uiState_.euclidHeld)
                    return true;
                // Y = Snapshot. Under scene scope → re-sync all to scene (scope-specific snapshot).
                if (uiState_.sceneHeld)
                {
                    processor_.resyncAllToScene();
                    refreshSurface();
                    return true;
                }
                // Non-trivial scope → scope-specific snapshot.
                if (editMode_.primaryScope() != PS::None && editMode_.primaryScope() != PS::Func)
                {
                    auto ctx = commandContext();
                    (void)commandCore_.handleVerb(editMode_.primaryScope(), ev.button, ctx, *editorEffects_);
                    repaint();
                    return true;
                }
                // No scope → Song-scope snapshot.
                {
                    int ckTrk = 0;
                    processor_.snapshot(ckScope(ckTrk), ckTrk);
                }
                repaint();
                return true;
            }

            case ControllerButton::VerbConfirm: {
                using PS = EditMode::PrimaryScope;
                const bool funcHeld = editMode_.scopeState().func;

                // 5.5: Euclid modal armed → bare P = commit; Func+P = cancel.
                if (uiState_.euclidHeld)
                {
                    auto& wt = processor_.sequence().tracks[static_cast<std::size_t>(euclidTrack_)];
                    if (!funcHeld)
                    {
                        // Commit: restore stash first (snapshot captures pre-Euclid state),
                        // then apply the Euclid pattern.
                        for (int si = 0; si < euclidStashLen_; ++si)
                            wt.steps[static_cast<std::size_t>(si)] =
                                euclidStash_[static_cast<std::size_t>(si)];
                        if (wt.length > 0)
                            processor_.snapshot(CheckpointScope::Phrase, euclidTrack_);
                        applyEuclidToTrack(euclidTrack_);
                        setStatus("EUCLID committed");
                    }
                    else
                    {
                        // Cancel: restore stashed phrase.
                        for (int si = 0; si < euclidStashLen_; ++si)
                            wt.steps[static_cast<std::size_t>(si)] =
                                euclidStash_[static_cast<std::size_t>(si)];
                    }
                    uiState_.resetEuclid();
                    euclidTrack_ = -1;
                    euclidStashLen_ = 0;
                    refreshMetaBand();
                    repaint();
                    return true;
                }

                // Both primary P (Yes/confirm) and Func+P (No/cancel) arrive here as VerbConfirm.
                // Distinguish by whether Func is held.
                // Note: when confirm is pending, CommandCore::handleDown intercepts VerbConfirm
                // before this point and calls executeConfirm / status::cancelled() directly.

                // No pending confirm. Bare Yes (no Func) = Quantize or snapshot/confirm verb.
                if (!funcHeld)
                {
                    // Quantize verb (DESIGN §19.3): scope + No zeros microOffset values.
                    // Trig (held steps) → those steps; Track → whole track; Phrase → all tracks.
                    // Bare No (no scope) falls through to snapshot/confirm.
                    const auto qScope = editMode_.primaryScope();
                    const auto& qCtx = processor_.editContext();
                    const auto& heldSteps = qCtx.heldSteps();

                    if (!heldSteps.empty())
                    {
                        const int t = keyboardArea_.getActiveTrack();
                        if (t >= 0 && t < static_cast<int>(kNumTracks))
                        {
                            processor_.snapshot(CheckpointScope::Track, t);
                            auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(t)];
                            for (int si : heldSteps)
                                if (si >= 0 && si < kMaxStepsPerTrack)
                                    trk.steps[static_cast<std::size_t>(si)].microOffset = 0.0f;
                            setStatus(status::quantized());
                            keyboardArea_.repaint();
                            return true;
                        }
                    }
                    else if (qScope == PS::Track)
                    {
                        const int t = keyboardArea_.getActiveTrack();
                        if (t >= 0 && t < static_cast<int>(kNumTracks))
                        {
                            processor_.snapshot(CheckpointScope::Track, t);
                            for (auto& s : processor_.sequence().tracks[static_cast<std::size_t>(t)].steps)
                                s.microOffset = 0.0f;
                            setStatus(status::quantized());
                            keyboardArea_.repaint();
                            return true;
                        }
                    }
                    else if (qScope == PS::Phrase)
                    {
                        int ckTrk = 0;
                        processor_.snapshot(ckScope(ckTrk), ckTrk);
                        for (auto& trk : processor_.sequence().tracks)
                            for (auto& s : trk.steps)
                                s.microOffset = 0.0f;
                        setStatus(status::quantized());
                        keyboardArea_.repaint();
                        return true;
                    }

                    // Bare No (no scope held): snapshot/confirm verb.
                    keyboardArea_.repaint();
                    {
                        auto ctx = commandContext();
                        (void)commandCore_.handleVerb(editMode_.primaryScope(), ev.button, ctx, *editorEffects_);
                    }
                    repaint();
                    return true;
                }

                // Func+P = Cancel. A pending prompt is intercepted earlier by CommandCore;
                // with nothing pending, cancel is a no-op. Restore lives solely on Func+Y —
                // the legacy Func+P restore overload was removed (DESIGN §13.6).
                {
                    auto ctx = commandContext();
                    (void)commandCore_.handleVerb(editMode_.primaryScope(), ev.button, ctx, *editorEffects_);
                }
                return true;
            }

            case ControllerButton::Snapshot:
                // Reserved while a section-suite scope is held (see helper above):
                // Func+scope+Yes is that scope's secondary, not a global snapshot.
                if (sectionSuiteScopeHeld(uiState_)) return true;
                {
                    int ckTrk = 0;
                    processor_.snapshot(ckScope(ckTrk), ckTrk);
                }
                repaint();
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
                    keyboardArea_.repaint();
                    return true;
                }
                processor_.clock().setInPluginPlaying(false);
                processor_.clock().resetPhase();
                processor_.requestFreshStart();
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
                const LayerFacts facts{ inputMode, at };
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

        // Phase 8.4 command core: try migrated handlers first.
        {
            auto ctx = commandContext();
            if (commandCore_.handleUp(ev, ctx, *editorEffects_))
                return;
        }

        switch (ev.button)
        {
            case CB::Func:
                // MD.7/MD.8: apply deferred pattern mute toggles atomically on Func release.
                for (const int t : deferredPatternMutes_)
                    processor_.togglePatternMute(t);
                deferredPatternMutes_.clear();
                uiState_.pendingPatternMuteToggle.fill(false);
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
                uiState_.funcTrackHeld = false;
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
            case CB::MetaSection:
                heldSectionRawCode_ = -1;
                heldSectionIndex_ = -1;
                uiState_.funcSrcHeld = false;
                // 6.5: keep FX picker alive while Func is still held so the user
                // can re-press FX to cycle the insert slot without losing the overlay.
                // funcFxHeld is cleared on Func release (line ~2888).
                if (!uiState_.funcHeld)
                    uiState_.funcFxHeld = false;
                editMode_.setSectionHeld(false);
                break;

            case CB::VerbPlay:
                playKeyHeld_ = false;
                break;

            case CB::Step: {
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

                // Func+Src+step: step release while funcSrcHeld → enter NoteEdit mode.
                if (uiState_.funcSrcHeld && !uiState_.noteEditMode && !uiState_.noteEditSteps.empty())
                {
                    uiState_.noteEditMode = true;
                    uiState_.noteEditStaged.clear();

                    // Auto-set the view octave to match the step's existing notes so
                    // cells are immediately live without needing a NavUp/Down first.
                    if (!uiState_.noteEditSteps.empty())
                    {
                        const int firstStep = *uiState_.noteEditSteps.begin();
                        const int track = processor_.editContext().heldTrackIndex();
                        if (track >= 0 && firstStep >= 0)
                        {
                            const auto& s = processor_.sequence()
                                                .tracks[static_cast<std::size_t>(track)]
                                                .steps[static_cast<std::size_t>(firstStep)];
                            if (s.trigOverride.noteCount > 0)
                            {
                                // Use the octave of the first note on the step.
                                const int noteVal = s.trigOverride.notes[0];
                                uiState_.noteEditOctave = noteVal / 12 - 1;
                            }
                        }
                    }

                    // Restore MZ to machine params — dismiss the TRIG meta section
                    // that Func+Trig brought up, so the user returns to where they were.
                    uiState_.masterSection = -1;
                    refreshMetaBand();

                    refreshSurface();
                    break;
                }

                // NoteEdit mode: key-up on a chromatic cell; no further processing needed.
                if (uiState_.noteEditMode)
                {
                    keyboardArea_.repaint();
                    break;
                }

                // CHROMATIC/LEVELS mode: key-up clears the pressed highlight.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks))
                    {
                        const auto m = uiState_.trackInputMode[static_cast<std::size_t>(at)];
                        if (m == TrackInputMode::Chromatic || m == TrackInputMode::Levels)
                        {
                            keyboardArea_.repaint();
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
                            if (uiState_.fillHeld)
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
                }
                repaint();
                break;
            }

            case CB::VerbSnapshot:
                break;  // Y = Snapshot; no held-state to clear.

            case CB::Restore: {
                const double now = juce::Time::getMillisecondCounterHiRes();
                int ckTrk = 0;
                const CheckpointScope scp = ckScope(ckTrk);
                using LPR = GestureRecognizer::LongPressResult;
                switch (gesture_.checkLongPress(kRestoreLongPressToken, now))
                {
                    case LPR::LongHold:  processor_.restoreToFloor(scp, ckTrk); repaint(); break;
                    case LPR::ShortHold: processor_.restoreOne(scp, ckTrk);     repaint(); break;
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

            // These buttons act on key-down; their key-up is a no-op. They must NOT
            // fall through into the TapTempo body below — doing so made releasing any
            // of them (notably SelectTrack) register a tap-tempo tap, so changing
            // tracks set the BPM from the inter-change interval.
            case CB::VerbRecord:
            case CB::VerbStopLegacy:
            case CB::VerbClear:
            case CB::VerbDelete:
            case CB::VerbPanic:
            case CB::Snapshot:
            case CB::NavLeft:
            case CB::NavRight:
            case CB::SelectTrack:
            case CB::ToggleMute:
            case CB::ForkPart:
            case CB::RecordArm:
                break;

            case CB::TapTempo:
                tapTempoPhysHeld_ = false;
                if (uiState_.generatorHubHeld)
                    uiState_.resetGeneratorHub();
                else
                    handleTapTempo();
                repaint();
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
        keyboardArea_.repaint();

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
                processor_.clock().setLocalBpm(bpm);
                repaint();
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

        // Header row: transport | sync mode box | channel mode | display mode | pool button
        auto header = bounds.removeFromTop(36);
        transport_.setBounds(header.removeFromLeft(200).reduced(4));
        syncModeBox_.setBounds(header.removeFromLeft(80).reduced(4));
        channelModeBox_.setBounds(header.removeFromLeft(90).reduced(4));
        displayModeBtn_.setBounds(header.removeFromLeft(46).reduced(4));
        poolBtn_.setBounds(header.removeFromRight(80).reduced(4));
        soundBankBtn_.setBounds(header.removeFromRight(60).reduced(4));

        // 9.11: consolidated info row — tempo/time-sig + project file bar on one line.
        // Frees the ~26px that used to be the separate fileBar row for the inspector.
        {
            auto infoRow = bounds.removeFromTop(28);
            // Standalone fileBar sits on the right side of the row; tempo fills left.
            if (fileBar_)
            {
                fileBar_->setBounds(infoRow.removeFromRight(210).reduced(2, 1));
                tempoReadout_.setBounds(infoRow.reduced(8, 2));
            }
            else
            {
                tempoReadout_.setBounds(infoRow.reduced(8, 2));
            }
        }
        // Inspector bar row — always-on 4-region context strip.
        inspectorRow_ = bounds.removeFromTop(26);
        inspectorBar_.setBounds(inspectorRow_);
        bounds.removeFromTop(2);

        // MHX.5: encoder band (MZ 4x2) + vertical crossfader to its right.
        static constexpr int kMZHeight = 160; // MHX 4x2 MZ (two rows of 4 slots)
        static constexpr int kFaderW = 28;  // crossfader strip width
        static constexpr int kTrackRowH = 26;  // track-number + VU row
        static constexpr int kMsRowH = 22;  // mute/solo row
        {
            auto mzStrip = bounds.removeFromTop(kMZHeight).reduced(8, 4);
            auto faderArea = mzStrip.removeFromRight(kFaderW).reduced(2, 0);
            crossfader_.setBounds(faderArea);
            manipulationZone_.setBounds(mzStrip);
        }

        // Remaining region, laid out top->bottom: track row, mute/solo row,
        // section bar, function bar, step grid.
        {
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
        repaint();
        return true;    // conflict raised — caller must wait for Yes/No
    }

    // -------------------------------------------------------------------------
    // Transient status line

    void LockstepEditor::setStatus(const juce::String& msg)
    {
        statusMessage_ = msg;
        statusSetMs_ = juce::Time::getMillisecondCounter();
        repaint();
    }

    void LockstepEditor::paintStatus(juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (statusMessage_.isEmpty()) return;
        const auto elapsed = juce::Time::getMillisecondCounter() - statusSetMs_;
        if (elapsed > kStatusDurationMs) return;
        const float alpha = juce::jlimit(0.0f, 1.0f,
                                         1.0f - static_cast<float>(elapsed) / static_cast<float>(kStatusDurationMs));
        g.setColour(juce::Colour(0xFF1E2028u).withAlpha(alpha));
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFF80FFB0u).withAlpha(alpha));
        g.drawText(statusMessage_, area.reduced(4, 0), juce::Justification::centredLeft, true);
    }


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
                                           float cur, int rawDelta)
    {
        if (intervals <= 0 || rawDelta == 0) return cur;
        const int unit = juce::jlimit(-7, 7, rawDelta);
        const float quantum = (maxV - minV) / static_cast<float>(intervals);
        const int curIdx = juce::roundToInt((cur - minV) / quantum);
        const int newIdx = juce::jlimit(0, intervals, curIdx + unit);
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
            keyboardArea_.repaint();
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
                const auto& v = views[static_cast<std::size_t>(mzSlot)];
                if (!v.writable) return;
                const float range = v.maxValue - v.minValue;
                if (range <= 0.0f) return;
                float newVal;
                if (v.stepped)
                {
                    const int intervals = juce::roundToInt(v.maxValue - v.minValue);
                    newVal = applyDiscreteEncoderDelta(v.minValue, v.maxValue, intervals,
                                                      v.value, rawDelta);
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
                {
                    applyEuclidLive(euclidTrack_);
                    // Repaint the grid so the live rhythm shows when stopped too — the
                    // KeyboardArea timer only repaints on playhead movement, so without
                    // this the pattern only appeared while transport was running.
                    keyboardArea_.repaint();
                }
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
