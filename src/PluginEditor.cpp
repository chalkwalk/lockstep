#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "command/StatusText.h"
#include "core/Euclidean.h"
#include "core/TrackInputMode.h"
#include "io/TrigGridMode.h"
#include "machine/IMachine.h"
#include "machine/ISliceable.h"
#include "machine/SamplerMachine.h"
#include "ui/MetaBand.h"
#include "ui/ScopedSectionMatrix.h"
#include "ui/SurfaceModel.h"
#include <algorithm>

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

        void status(const juce::String& msg) override       { ed.setStatus(msg); }
        void requestRepaint() override                      { ed.repaint(); }
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

        void sceneFloorPaste() override
        {
            auto& cl  = ed.clipboard_;
            auto& dst = ed.processor_.section();
            dst.activeMask  = cl.scene.floor.activeMask;
            dst.coreTime    = cl.scene.floor.coreTime;
            dst.morphA      = cl.scene.floor.morphA;
            dst.morphB      = cl.scene.floor.morphB;
            dst.initialised = true;
            ed.setStatus(status::pastedSceneFloor());
        }

        void sceneFullPaste(int destSlot) override
        {
            auto& cl = ed.clipboard_;
            if (ed.phraseConflictAndConfirm(destSlot, PendingConfirm::PasteScene))
                return;  // waiting for Yes/No
            ed.processor_.snapshot(CheckpointScope::Song, 0);
            auto& dst = ed.processor_.section();
            dst.activeMask  = cl.scene.floor.activeMask;
            dst.coreTime    = cl.scene.floor.coreTime;
            dst.morphA      = cl.scene.floor.morphA;
            dst.morphB      = cl.scene.floor.morphB;
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
    };

    // Narrow IMachineCatalog adapter — forwards to LockstepProcessor.
    // Defined here so test targets can supply their own fixture.
    struct ProcessorCatalog final : IMachineCatalog
    {
        LockstepProcessor& p;
        explicit ProcessorCatalog(LockstepProcessor& proc) : p(proc) {}

        [[nodiscard]] int         numParams (int track)           const override
            { return p.numParams(track); }
        [[nodiscard]] ParamSpec   paramSpec (int track, int slot) const override
            { return p.paramSpec(track, slot); }
        [[nodiscard]] SectionInfo section   (int track, int idx)  const override
            { return p.section(track, idx); }
        [[nodiscard]] const char* machineId (int track)           const override
            { return p.getMachineIdRaw(track); }
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
          soundBankOverlay_(proc),
          editorEffects_(std::make_unique<EditorEffects>(*this))
    {
        // Load persisted display mode.
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "Lockstep";
            o.filenameSuffix      = ".xml";
            o.folderName          = "Lockstep";
            o.osxLibrarySubFolder = "Application Support";
            appProps_.setStorageParameters(o);
        }
        if (auto* prefs = appProps_.getUserSettings())
            gridMode_ = static_cast<GridDisplayMode>(
                prefs->getIntValue("gridMode", static_cast<int>(GridDisplayMode::Ortholinear)));
        applyDisplayMode(gridMode_);
        addAndMakeVisible(transport_);

        if (juce::PluginHostType::getPluginLoadedAs()
                == juce::AudioProcessor::wrapperType_Standalone)
        {
            tempoBar_ = std::make_unique<StandaloneTempoBar>(proc.clock());
            addAndMakeVisible(tempoBar_.get());
        }

        // Sync mode ComboBox + APVTS attachment
        syncModeBox_.addItem("Locked", 1);
        syncModeBox_.addItem("Auto",   2);
        syncModeBox_.setWantsKeyboardFocus(false);
        addAndMakeVisible(syncModeBox_);
        syncModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                proc.apvts(), ParamIDs::syncMode, syncModeBox_);

        // Channel mode ComboBox + APVTS attachment
        channelModeBox_.addItem("Omni",      1);
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
        keyboardArea_.onSectionChanged = [this](int /*section*/, int /*page*/, int firstSlot)
        {
            manipulationZone_.setSlotOffset(firstSlot);
        };
        keyboardArea_.onMetaSectionChanged = [this](int metaSection)
        {
            // Navigating to any meta section other than FX/Global (5 or -1) clears
            // the sticky master FX band so the newly-selected section wins.
            // Navigating to a different meta section closes the FX picker.
            if (metaSection != 5 && metaSection != -1)
                uiState_.masterFxPickerOpen = false;
            refreshMetaBand();
        };

        // Track page toggle: flips between tracks 1-8 and 9-16.
        trackPageBtn_.setWantsKeyboardFocus(false);
        trackPageBtn_.onClick = [this]
        {
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
            trackBtns_[ti].setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
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

        keyboardArea_.onActiveTrackChanged = [this](int newTrack)
        {
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
        displayModeBtn_.onClick = [this]
        {
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
        crossfader_.onValueChange = [this]
        {
            // Slider is inverted: top (1.0) = A (f=0), bottom (0.0) = B (f=1).
            processor_.setMorphFader(1.0f - static_cast<float>(crossfader_.getValue()));
        };
        crossfader_.addMouseListener(this, false);
        addAndMakeVisible(crossfader_);

        addAndMakeVisible(manipulationZone_);
        addAndMakeVisible(keyboardArea_);

        greyoutLayer_.onPaint = [this](juce::Graphics& g)
        {
            const juce::Colour emptyGrey { juce::uint32(0x66444444u) };
            g.setColour(emptyGrey);

            // (A) per-empty-track strip = number button ∪ mute ∪ solo.
            for (std::size_t t = 0; t < kNumTracks; ++t)
            {
                if (!trackBtns_[t].isVisible()) continue;
                if (!processor_.isTrackEmpty(static_cast<int>(t))) continue;
                g.fillRect(trackBtns_[t].getBounds()
                               .getUnion(muteBtns_[t].getBounds())
                               .getUnion(soloBtns_[t].getBounds()));
            }

            // (B) focused track empty → grey the edit area (not the strip rows).
            const int at = keyboardArea_.getActiveTrack();
            if (at >= 0 && at < static_cast<int>(kNumTracks)
                && processor_.isTrackEmpty(at))
            {
                g.fillRect(manipulationZone_.getBounds().getUnion(crossfader_.getBounds()));
                g.fillRect(keyboardArea_.getBounds());
            }
        };
        addAndMakeVisible(greyoutLayer_);

        poolBtn_.setWantsKeyboardFocus(false);
        poolBtn_.onClick = [this]
        {
            poolOverlay_.setVisible(!poolOverlay_.isVisible());
            if (poolOverlay_.isVisible())
                poolOverlay_.toFront(false);
        };
        addAndMakeVisible(poolBtn_);

        poolOverlay_.onClose = [this] { poolOverlay_.setVisible(false); };
        poolOverlay_.getActiveTrack = [this]() { return keyboardArea_.getActiveTrack(); };
        addChildComponent(poolOverlay_);

        soundBankBtn_.setWantsKeyboardFocus(false);
        soundBankBtn_.onClick = [this]
        {
            soundBankOverlay_.setVisible(!soundBankOverlay_.isVisible());
            if (soundBankOverlay_.isVisible())
                soundBankOverlay_.toFront(false);
        };
        addAndMakeVisible(soundBankBtn_);

        soundBankOverlay_.onClose = [this] { soundBankOverlay_.setVisible(false); };
        soundBankOverlay_.getActiveTrack = [this]() { return keyboardArea_.getActiveTrack(); };
        addChildComponent(soundBankOverlay_);

        manipulationZone_.setUiState(&uiState_);
        manipulationZone_.onOpenPoolManager = [this]
        {
            poolOverlay_.setVisible(true);
            poolOverlay_.toFront(false);
        };

        // Wire verb dispatch to this editor's handler.
        editMode_.onVerbDispatched = [this](EditMode::PrimaryScope scope,
                                             ControllerButton verb)
        {
            dispatchVerb(scope, verb);
        };

        // Wire mouse button events from KeyboardArea through the unified dispatch.
        keyboardArea_.onButtonDown = [this](ControllerEvent ev)
        {
            pressTracker_.press(PressTracker::kMouseSource, ev.button, ev.index);
            dispatchDown(ev, PressTracker::kMouseSource);
            keyboardArea_.repaint();
        };
        keyboardArea_.onButtonUp = [this](ControllerEvent ev)
        {
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
        push1Surface_  = std::make_unique<Push1Surface>();

        controllerPorts_.onStateChange = [this](bool open)
        {
            setStatus(open ? "Controller: X-Touch Mini connected"
                           : "Controller: X-Touch Mini disconnected");
        };

        push1Ports_.onStateChange = [this](bool open)
        {
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
            if (paramID == juce::String(ParamIDs::trackMute(i))
                || paramID == juce::String(ParamIDs::trackSolo(i)))
            {
                juce::MessageManager::callAsync([this] { repaint(); });
                return;
            }
        }
    }

    void LockstepEditor::timerCallback()
    {
        // Peak meters: fast attack, slow ballistic decay. Activity blinks: a
        // pulse from the audio thread snaps to 1.0, then decays each tick.
        // Only repaint if any value actually changed; floor tiny values to zero
        // so decay terminates and the repaint loop stops when transport is idle.
        static constexpr float kMeterFloor = 0.001f;
        static constexpr float kBlinkFloor = 0.005f;
        bool dirty = false;

        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const float peak     = processor_.trackPeak(static_cast<int>(i));
            const float newMeter = std::max(peak, trackMeter_[i] * 0.80f);
            const float floored  = (newMeter < kMeterFloor) ? 0.0f : newMeter;
            if (std::abs(floored - trackMeter_[i]) > 0.0f) { trackMeter_[i] = floored; dirty = true; }

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

        const float newMaster = std::max(processor_.masterPeak(), masterMeter_ * 0.80f);
        const float flooredMaster = (newMaster < kMeterFloor) ? 0.0f : newMaster;
        if (std::abs(flooredMaster - masterMeter_) > 0.0f) { masterMeter_ = flooredMaster; dirty = true; }

        // Transport state change: repaint so the PLAY/PAUSE label updates promptly.
        const bool nowPlaying = processor_.clock().inPluginPlaying();
        if (nowPlaying != lastPlayingState_) { lastPlayingState_ = nowPlaying; dirty = true; }

        // Morph fader: detect on-screen crossfader moves so controller surfaces update.
        const float curMorphFader = processor_.morphFader();
        if (curMorphFader != lastMorphFader_) { lastMorphFader_ = curMorphFader; dirty = true; }

        // Push the morph view state to KeyboardArea so its paint() gets current slot states.
        keyboardArea_.setMorphViewState(buildMorphViewState(),
                                        manipulationZone_.slotOffset(),
                                        1.0f - processor_.morphFader());

        if (dirty) repaint();

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
        if ((xTouchSurface_ && controllerPorts_.isOpen())
            || (push1Surface_ && push1Ports_.isOpen()))
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
        if (level < 0.5f)  return juce::Colour::fromRGB(60, 200, 90);
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
                if (p->load() >= 0.5f) { anySoloed = true; break; }

        // Per-track VU underlaid behind the (transparent) track-number buttons.
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const int  t       = static_cast<int>(i);
            const bool gMuted  = processor_.getGlobalMute(t);
            const bool pMuted  = processor_.getPatternMute(t);
            const auto* sp     = processor_.apvts().getRawParameterValue(ParamIDs::trackSolo(t));
            const bool soloed  = sp && sp->load() >= 0.5f;
            const bool soloEx  = anySoloed && !soloed;

            const auto r = trackBtns_[i].getBounds();
            if (r.isEmpty()) continue;

            // Distinct background per silencing source: global mute = red,
            // pattern mute = orange, solo-exclusion = purple, soloed = teal,
            // audible = grey.
            juce::Colour bg = juce::Colour::fromRGB(28, 32, 38);
            if      (gMuted) bg = juce::Colour::fromRGB(70, 20, 20);
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
                const juce::String badge { processor_.trackBadge(t) };
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
            (juce::PluginHostType::getPluginLoadedAs()
             == juce::AudioProcessor::wrapperType_Standalone);
        const auto* modeParam =
            processor_.apvts().getRawParameterValue(ParamIDs::syncMode);
        const bool isLocked = modeParam && static_cast<int>(modeParam->load()) == 0;
        const bool ghost    = isLocked && !isStandalone;
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
            [this, existing](int result)
            {
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
            static const juce::Colour kMorphMagenta { 0xffb060d0 };
            const auto fb       = crossfader_.getBounds();
            const int  labelH   = 12;
            const int  inset    = fb.getHeight() / 4;  // 25% of fader height
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
                const int ti  = static_cast<int>(t);
                const int cur = processor_.isTrackDeviated(ti)
                    ? processor_.deviationPhraseIdxForTrack(ti)
                    : processor_.activeSectionIdx();
                if (cur == home) continue;
                const auto r = trackBtns_[t].getBounds();
                const float s = 7.0f;
                juce::Path tri;
                tri.addTriangle(static_cast<float>(r.getX()),     static_cast<float>(r.getY()),
                                static_cast<float>(r.getX()) + s,  static_cast<float>(r.getY()),
                                static_cast<float>(r.getX()),      static_cast<float>(r.getY()) + s);
                g.fillPath(tri);
            }
        }

        // ---- MHZ.2.2: top-bar dashboard (free space between left controls and right buttons) ----
        // Left zone (~420..640): Bank/Pattern/Part identity + state badges (CK, CHN, QUE, SHR, CPY).
        // Right zone (~640..800): Held-context preview derived from modifier cluster state.
        {
            static constexpr int kBadgeH   = 16;
            static constexpr int kGap       = 3;
            static constexpr int kDashStartX = 420;   // right edge of left controls
            static constexpr int kRightBtnX  = 800;   // left edge of the three right buttons
            static constexpr int kSplitX     = 640;   // dashboard/preview divider
            const int by = (36 - kBadgeH) / 2;

            g.setFont(juce::Font(juce::FontOptions(10.0f)));

            // ---- Left dashboard ----
            {
                // Song / Scene identity pill.
                const int sg = processor_.activePieceIdx() + 1;
                const int sc = processor_.activeSectionIdx() + 1;
                const juce::String identity = "Sg:" + juce::String(sg)
                                            + "  Sc:" + juce::String(sc);
                {
                    const auto r = juce::Rectangle<int>(kDashStartX, by, 120, kBadgeH);
                    g.setColour(juce::Colour(0xFF262830u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colour(0xFFBBCCDDu));
                    g.drawText(identity, r, juce::Justification::centred);
                }

                int bx = kDashStartX + 120 + kGap;

                // Clipboard badge.
                const char* cbLabel = nullptr;
                switch (clipboard_.type)
                {
                    case ClipboardType::None:    break;
                    case ClipboardType::Step:    cbLabel = "CPY:STP"; break;
                    case ClipboardType::Section: cbLabel = "CPY:SEC"; break;
                    case ClipboardType::Track:   cbLabel = "CPY:TRK"; break;
                    case ClipboardType::Pattern: cbLabel = "CPY:PHR"; break;
                    case ClipboardType::Scene:   cbLabel = "CPY:SCN"; break;
                    case ClipboardType::All:     cbLabel = "CPY:ALL"; break;
                }
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
                        case TrackInputMode::Play:      break;  // no badge
                        case TrackInputMode::Chromatic: modeLabel = "CHROM"; modeCol = juce::Colour(0xFF4090E0u); break;
                        case TrackInputMode::Levels:    modeLabel = "LEVLS"; modeCol = juce::Colour(0xFFE07030u); break;
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
                // MHZ.3.5: Func+Part = machine picker — show dedicated hint.
                if (ui.funcTrackHeld)
                {
                    ctx = "FUNC + MACH  |  press step to select machine";
                }
                // MHZ.3.4: P-lock clear mode.
                else if (ui.pLockClearMode)
                {
                    ctx = "FUNC + STEP " + juce::String(ui.pLockClearStep + 1)
                          + "  |  press cell to clear P-Lock slot";
                }
                else
                {
                    // Step held (no modifier) → P-Lock edit mode.
                    const auto& ec = processor_.editContext();
                    if (ui.stepHeld && ec.isActiveForEditing())
                    {
                        const int stepNum = ec.heldStepIndex() + 1;
                        const int cnt     = static_cast<int>(ec.heldSteps().size());
                        ctx = cnt > 1
                            ? juce::String(cnt) + " STEPS  |  turn knob to P-Lock"
                            : "STEP " + juce::String(stepNum) + "  |  turn knob to P-Lock";
                    }
                    // Primary scope token. MHZ.9.7: show mode-cycle hint when Track+Control-All.
                    else if (ui.trackHeld && processor_.controlAllActive())
                    {
                        ctx = juce::String(u8"TRACK  |  ↑↓ cycle PLAY/CHROM/LEVLS");
                    }
                    else if (ui.trackHeld)        ctx = "TRACK " + juce::String(keyboardArea_.getActiveTrack() + 1);
                    else if (ui.phraseScopeHeld) ctx = "PHRASE";
                    else if (ui.sceneHeld)         ctx = "SCENE";
                    else if (ui.morphHeld)        ctx = "MORPH";
                    else if (ui.songHeld)       ctx = "SONG";
                    else if (ui.muteHeld)         ctx = "MUTE";
                    else if (ui.fillHeld)         ctx = "FILL";
                    else if (ui.funcHeld)         ctx = "FUNC";
                }

                if (ctx.isEmpty()) return;   // nothing held — preview is blank

                // Qualify with Func if held alongside another modifier (normal path only).
                if (!ui.funcTrackHeld && !ui.pLockClearMode
                    && ui.funcHeld && ctx != "FUNC")
                    ctx = "FUNC + " + ctx;

                // Active section suffix.
                const int activeTrack = keyboardArea_.getActiveTrack();
                if (activeTrack >= 0)
                {
                    const int sec = ui.trackSection[static_cast<std::size_t>(activeTrack)];
                    if (sec >= 0 && sec < IMachine::kMaxSections)
                        ctx += juce::String("  |  ") + juce::String(IMachine::kCanonicalSectionNames[static_cast<std::size_t>(sec)]);
                }

                const int previewW = kRightBtnX - kSplitX - kGap;
                const auto r = juce::Rectangle<int>(kSplitX, by, previewW, kBadgeH);
                g.setColour(juce::Colour(0xFF1E2028u));
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(juce::Colour(0xFFDDEEFFu));
                g.drawText(ctx, r.reduced(4, 0), juce::Justification::centredLeft, true);

                // Transient CPC status overlays the context-HUD for ~1.5s.
                paintStatus(g, r);
            }
        }

        // ---- Diagnostic meters drawn over children ----
        // Master output meter: a thin bar along the very top edge.
        {
            const float level = juce::jlimit(0.0f, 1.0f, masterMeter_);
            const int   w     = juce::roundToInt(static_cast<float>(getWidth()) * level);
            g.setColour(juce::Colour::fromRGB(30, 34, 40));
            g.fillRect(0, 0, getWidth(), 3);
            if (w > 0)
            {
                g.setColour(meterColour(level));
                g.fillRect(0, 0, w, 3);
            }
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
        return ext == ".wav" || ext == ".aiff" || ext == ".aif"
            || ext == ".flac" || ext == ".ogg";
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

    void LockstepEditor::updateSwingQualifier()
    {
        refreshMetaBand();
    }

    void LockstepEditor::applyEuclidToTrack(int track)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
        auto& ph = processor_.activePhrase(track);
        const int len = ph.length;
        if (len <= 0) return;

        // Checkpoint only if the phrase already has trigs (preserves undo).
        bool hasTrigs = false;
        for (int si = 0; si < len; ++si)
            if (ph.steps[static_cast<std::size_t>(si)].trig) { hasTrigs = true; break; }
        if (hasTrigs)
            processor_.snapshot(CheckpointScope::Track, track);

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
                s.trigOverride.velocity    = v;
            }
            else
            {
                s.trigOverride.hasVelocity = false;
            }
        }
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
        using T  = ControllerEvent::Type;

        const auto prevLatch = uiState_.latch;
        uiState_.latch = {};  // clear all latches before calling dispatchUp so guards pass

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
                if (idx == s) { physDown = true; break; }
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
        keyboardArea_.repaint();
        repaint();
    }

    // -------------------------------------------------------------------------
    // MHZ.9.3: column-exclusivity-aware modifier latch toggle.

    void LockstepEditor::setModifierLatch(ControllerButton cb, bool set)
    {
        using CB = ControllerButton;
        using T  = ControllerEvent::Type;

        if (set)
        {
            // Enforce column exclusivity by releasing any existing latch in the same column.
            // Calling dispatchUp (after clearing the latch bool) does the full release with
            // side effects (setControlAllActive, updateFillActivation, etc.).
            const bool isCol1 = (cb == CB::PhraseScope || cb == CB::MorphScope
                                 || cb == CB::MuteScope);

            auto releaseOther = [&](bool& latchBool, bool physHeld, CB btn) {
                if (!latchBool || cb == btn) return;
                latchBool = false;
                if (!physHeld)
                    dispatchUp({ T::ButtonUp, btn });
            };

            if (isCol1)
            {
                releaseOther(uiState_.latch.phrase, physHeld_.phrase, CB::PhraseScope);
                releaseOther(uiState_.latch.morph,   physHeld_.morph,   CB::MorphScope);
                releaseOther(uiState_.latch.mute,    physHeld_.mute,    CB::MuteScope);
            }
            else
            {
                releaseOther(uiState_.latch.track,  physHeld_.track,  CB::TrackScope);
                releaseOther(uiState_.latch.scene,   physHeld_.scene,   CB::SceneScope);
                releaseOther(uiState_.latch.song, physHeld_.song, CB::SongScope);
                releaseOther(uiState_.latch.fill,   physHeld_.fill,   CB::FillScope);
            }
        }

        switch (cb)
        {
            case CB::PhraseScope: uiState_.latch.phrase = set; break;
            case CB::MorphScope:   uiState_.latch.morph   = set; break;
            case CB::MuteScope:    uiState_.latch.mute    = set; break;
            case CB::TrackScope:   uiState_.latch.track   = set; break;
            case CB::SceneScope:    uiState_.latch.scene    = set; break;
            case CB::SongScope:  uiState_.latch.song  = set; break;
            case CB::FillScope:    uiState_.latch.fill    = set; break;
            case CB::Func:
            case CB::CueScope:
            case CB::VerbYes:
            case CB::VerbRecord:
            case CB::VerbPlay:
            case CB::VerbStop:
            case CB::VerbNo:
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
        const bool dbl = doubleTap_.recordAndCheck(1000 + static_cast<int>(cb), now);
        if (currentlyLatched && !dbl)
        {
            setModifierLatch(cb, false);
            doubleTap_.invalidate();  // prevent follow-up read as re-latch
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
        bool phys    = false;
        if (cb == ControllerButton::TrackScope)
        {
            latched = uiState_.latch.track; phys = physHeld_.track;
        }
        else if (cb == ControllerButton::SceneScope)
        {
            latched = uiState_.latch.scene; phys = physHeld_.scene;
        }
        else
        {
            return;
        }
        if (!latched) return;
        setModifierLatch(cb, false);
        if (!phys) dispatchUp({ T::ButtonUp, cb });
        keyboardArea_.repaint();
        repaint();
    }

    // -------------------------------------------------------------------------
    // Key handling (9x4 layout)

    // A section-suite scope (Track/Pattern/Part/Scene/Master) qualifies the next
    // verb. While one is held, bare-Func global ops (Snapshot/Restore) are reserved:
    // Func+scope+verb is that scope's secondary variant — not a global checkpoint.
    static bool sectionSuiteScopeHeld(const UiState& ui) noexcept
    {
        return ui.trackHeld || ui.phraseScopeHeld || ui.sceneHeld
            || ui.morphHeld || ui.songHeld;
    }

    // dispatchDown — source-agnostic button-down handler fed by both keyboard
    // and mouse.  rawCode is the physical key code (keyboard) or 0 (mouse).
    bool LockstepEditor::dispatchDown(ControllerEvent ev, int rawCode)
    {
        using CB = ControllerButton;

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
        if (resolveMetaBand(uiState_) == MetaBand::Swing
            && ev.button != CB::TrackScope
            && ev.button != CB::SceneScope
            && ev.button != CB::SongScope)
        {
            uiState_.swingDismissed = true;
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
                keyboardArea_.repaint();
                repaint();
                // MHZ.9.4: Func never latches; double-tap = universal escape (only if latches engaged).
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::Func), now)
                        && (uiState_.latch.any() || processor_.editContext().hasAnyLatchedStep()))
                    {
                        escapeAllLatches();
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
                uiState_.swingDismissed = false;
                updateSwingQualifier();
                repaint();
                return true;

            case CB::PhraseScope:
                physHeld_.phrase = true;
                uiState_.phraseScopeHeld = true;
                uiState_.phraseScopeUsed = false;
                editMode_.onScopeEvent(ev);
                // 5.5: Fill+Phrase chord → enter Euclidean generator mode.
                if (uiState_.fillHeld && !uiState_.euclidHeld)
                {
                    const int at = keyboardArea_.getActiveTrack();
                    const auto& ph = processor_.activePhrase(at < 0 ? 0 : at);
                    int onsets = 0;
                    for (int si = 0; si < ph.length; ++si)
                        if (ph.steps[static_cast<std::size_t>(si)].trig) ++onsets;
                    uiState_.euclidPulses  = onsets > 0 ? onsets : 4;
                    uiState_.euclidOffset  = 0;
                    uiState_.euclidAccents = 0;
                    uiState_.euclidHeld    = true;
                    refreshMetaBand();
                }
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
                // 5.5: Phrase+Fill chord → enter Euclidean generator mode.
                if (uiState_.phraseScopeHeld && !uiState_.euclidHeld)
                {
                    const int at = keyboardArea_.getActiveTrack();
                    const auto& ph = processor_.activePhrase(at < 0 ? 0 : at);
                    int onsets = 0;
                    for (int si = 0; si < ph.length; ++si)
                        if (ph.steps[static_cast<std::size_t>(si)].trig) ++onsets;
                    uiState_.euclidPulses  = onsets > 0 ? onsets : 4;
                    uiState_.euclidOffset  = 0;
                    uiState_.euclidAccents = 0;
                    uiState_.euclidHeld    = true;
                    refreshMetaBand();
                }
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
                uiState_.swingDismissed = false;
                updateSwingQualifier();
                repaint();
                return true;

            case CB::SceneScope:
                physHeld_.scene = true;
                // Track + Scene: re-sync focused musician to current Scene (Phase 7).
                if (uiState_.trackHeld)
                {
                    processor_.resyncTrackToScene(processor_.focusTrack());
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
                uiState_.sceneHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::SceneScope, uiState_.latch.scene);
                uiState_.swingDismissed = false;
                updateSwingQualifier();
                keyboardArea_.repaint();
                repaint();
                return true;

            case ControllerButton::Section:
            {
                // Determine whether a section-suite scope modifier is held.
                using PS = EditMode::PrimaryScope;
                PS sectionScope = PS::None;
                if      (uiState_.trackHeld)        sectionScope = PS::Track;
                else if (uiState_.phraseScopeHeld) sectionScope = PS::Phrase;
                else if (uiState_.sceneHeld)         sectionScope = PS::Scene;
                else if (uiState_.morphHeld)        sectionScope = PS::Morph;
                else if (uiState_.songHeld)       sectionScope = PS::Song;

                // 5.7: Fill+TRIG → Retrig overlay; Fill+SRC → SoundPool overlay.
                // These are momentary: the overlay clears when Fill releases.
                if (uiState_.fillHeld && ev.index == 0)
                {
                    uiState_.trigGridMode = TrigGridMode::Retrig;
                    repaint();
                    return true;
                }
                if (uiState_.fillHeld && ev.index == 1)
                {
                    uiState_.trigGridMode = TrigGridMode::SoundPool;
                    repaint();
                    return true;
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
                        // Song+FX: master insert params (or global transport if none loaded).
                        // Re-press while already at section 5 cycles the master insert slot.
                        if (uiState_.masterSection == 5)
                            uiState_.masterFxInsertSlot = 1 - uiState_.masterFxInsertSlot;
                        else
                            uiState_.masterFxInsertSlot = 0;
                        keyboardArea_.selectMetaSection(5);
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
                    heldSectionIndex_   = ev.index;
                    editMode_.setSectionHeld(true);
                }
                return true;
            }

            case ControllerButton::MetaSection:
                // 6.5: Func+Song+FX → open master FX picker on the step grid.
                // Also navigates to Song+FX meta section so MZ shows master insert params.
                // Re-pressing while picker is open cycles the targeted slot (0↔1).
                if (uiState_.funcHeld && uiState_.songHeld && ev.index == processor_.kFxSecIdx)
                {
                    if (uiState_.masterFxPickerOpen)
                        uiState_.masterFxInsertSlot = 1 - uiState_.masterFxInsertSlot;
                    else
                        uiState_.masterFxInsertSlot = 0;
                    uiState_.masterFxPickerOpen = true;
                    // Navigate to Song+FX meta section so MZ shows master FX params.
                    keyboardArea_.selectMetaSection(processor_.kFxSecIdx);
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

            case ControllerButton::Step:
            {
                // ----------------------------------------------------------------
                // 5.7: Retrig overlay (Fill+TRIG held)
                // ----------------------------------------------------------------
                // PPQ per repetition for each grid cell. 8 rates (cells 0-7),
                // cells 8-15 are dark/ignored.
                static constexpr std::array<double, 8> kRetrigRates = {{
                    1.0,          // /4   (quarter-note)
                    2.0 / 3.0,    // /4T  (quarter triplet)
                    0.5,          // /8
                    1.0 / 3.0,    // /8T
                    0.25,         // /16  (default)
                    1.0 / 6.0,    // /16T
                    0.125,        // /32
                    1.0 / 12.0,   // /32T
                }};

                if (uiState_.trigGridMode == TrigGridMode::Retrig)
                {
                    if (ev.index < 0 || ev.index >= 16) return true;
                    const int at = keyboardArea_.getActiveTrack();
                    if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;

                    // Slice sub-mode: if the machine is ISliceable, cells address slices.
                    const auto* machine = processor_.machineForTrack(at);
                    const auto* sliceable = machine ? dynamic_cast<const ISliceable*>(machine) : nullptr;
                    if (sliceable && sliceable->hasSlices())
                    {
                        // Cell → slice index. Write to held steps; outside range = no-op.
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
                        // Live audition: play the slice.
                        processor_.setRetrigActive(at, true, kRetrigRates[4],
                                                   juce::jlimit(0, 127, sliceIdx));
                        return true;
                    }

                    // Rate sub-mode: cells 0-7 select retrig rates; 8-15 ignored.
                    if (ev.index >= static_cast<int>(kRetrigRates.size())) return true;
                    const double rate = kRetrigRates[static_cast<std::size_t>(ev.index)];

                    // Resolve the track's primary note for the stutter.
                    int retrigNote = uiState_.lastPlayedNote[static_cast<std::size_t>(at)];
                    if (retrigNote <= 0)
                    {
                        // Fall back to the active step's note.
                        const auto& trk = processor_.sequence()
                                              .tracks[static_cast<std::size_t>(at)];
                        const auto& ctx = processor_.editContext();
                        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at
                            && !ctx.heldSteps().empty())
                        {
                            const int si = ctx.heldSteps().front();
                            if (si >= 0 && si < kMaxStepsPerTrack)
                            {
                                const auto& ov = trk.steps[static_cast<std::size_t>(si)]
                                                     .trigOverride;
                                retrigNote = (ov.noteCount > 0) ? ov.notes[0] : 60;
                            }
                        }
                        if (retrigNote <= 0) retrigNote = 60;
                    }

                    // Live stutter.
                    processor_.setRetrigActive(at, true, rate, retrigNote);

                    // If a step is held, bake the rate as a per-step P-Lock.
                    auto& ctx = processor_.editContext();
                    if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at)
                    {
                        auto& trk = processor_.sequence()
                                        .tracks[static_cast<std::size_t>(at)];
                        for (int heldIdx : ctx.heldSteps())
                        {
                            if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                            auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                            s.trigOverride.hasRetrig  = true;
                            s.trigOverride.retrigRate = rate;
                        }
                        ctx.markParamWritten();
                    }
                    return true;
                }

                // ----------------------------------------------------------------
                // 5.7: SoundPool overlay (Fill+SRC held)
                // ----------------------------------------------------------------
                if (uiState_.trigGridMode == TrigGridMode::SoundPool)
                {
                    if (ev.index < 0 || ev.index >= 16) return true;
                    const int at = keyboardArea_.getActiveTrack();
                    if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;
                    const int poolSize = processor_.soundPoolSize();
                    if (ev.index >= poolSize) return true;

                    // Live audition.
                    processor_.liveSwapTrackSound(at, ev.index);

                    // If record-armed and a step is held, bake as a sound_id P-Lock.
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
                            s.trigOverride.soundId    = ev.index;
                        }
                        ctx.markParamWritten();
                    }
                    return true;
                }

                // ----------------------------------------------------------------
                // 6.5: Master FX picker (Func+Song+FX held, picker overlay open)
                // ----------------------------------------------------------------
                if (uiState_.masterFxPickerOpen)
                {
                    if (ev.index < 0 || ev.index >= 16) return true;
                    if (ev.index >= processor_.numAvailableEffects()) return true;
                    const auto info = processor_.availableEffectInfo(ev.index);
                    processor_.setMasterInsert(uiState_.masterFxInsertSlot, info.id);
                    uiState_.masterFxPickerOpen = false;  // close picker; masterFxHeld stays
                    refreshMetaBand();
                    repaint();
                    return true;
                }

                // ----------------------------------------------------------------
                // 6.5: FX insert picker (Func+FX held)
                // ----------------------------------------------------------------
                if (uiState_.funcFxHeld)
                {
                    if (ev.index < 0 || ev.index >= 16) return true;
                    const int at = keyboardArea_.getActiveTrack();
                    if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;
                    if (ev.index >= processor_.numAvailableEffects()) return true;
                    const auto info = processor_.availableEffectInfo(ev.index);
                    const std::string curId = processor_.trackInsertId(at, uiState_.funcFxInsertSlot);
                    if (info.id == curId)
                    {
                        // Re-pressing the loaded effect toggles bypass.
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

                // ----------------------------------------------------------------
                // 6.5: Animate bypass — FX section key held + step
                // ----------------------------------------------------------------
                if (heldSectionIndex_ == processor_.kFxSecIdx && !uiState_.funcHeld)
                {
                    if (ev.index < 0 || ev.index >= 16) return true;
                    const int at = keyboardArea_.getActiveTrack();
                    if (at < 0 || at >= static_cast<int>(kNumTracks)) return true;
                    // Step index selects insert slot: 0-7 → slot 0, 8-15 → slot 1.
                    const int slot = (ev.index >= 8) ? 1 : 0;
                    if (!processor_.trackInsertId(at, slot).empty())
                    {
                        processor_.setTrackInsertBypass(at, slot, true);
                        animateBypassTrack_ = at;
                        animateBypassSlot_  = slot;
                    }
                    return true;
                }

                // MHZ.7.3: CHROMATIC mode — step keys are a piano keyboard.
                // Piano layout via kPianoNoteOffset; dead keys (offset -1) are no-ops.
                // With step held: write noteOverride. Always: trigger live note.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks)
                        && uiState_.trackInputMode[static_cast<std::size_t>(at)] == TrackInputMode::Chromatic
                        )
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
                                s.trig                  = true;
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
                        chromaticHeldNote_[pad]  = note;
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
                    if (at >= 0 && at < static_cast<int>(kNumTracks)
                        && uiState_.trackInputMode[static_cast<std::size_t>(at)] == TrackInputMode::Levels
                        )
                    {
                        const int vel = juce::roundToInt((ev.index + 1.0f) / 16.0f * 127.0f);
                        const int pitch = uiState_.lastPlayedNote[static_cast<std::size_t>(at)];
                        const int note  = pitch > 0 ? pitch : 60;
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
                                s.trigOverride.velocity    = vel;
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
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // Phrase-length authoring (DESIGN §34.4). The hold re-skins the
                // grid (LengthInRun/Boundary/OutRun in SurfaceModel); the step
                // press sets the length to that absolute (page-aware) index+1.
                //   Phrase + Func + step → focused track's length.
                //   Morph  + Func + step → broadcast: all tracks' length.
                // (Morph is the old "Scene" all-tracks qualifier, renamed in 7.9.)
                // Gated before the bare Phrase/Scene branches so Func qualifies.
                if (uiState_.funcHeld && !uiState_.funcTrackHeld
                    && (uiState_.phraseScopeHeld || uiState_.morphHeld))
                {
                    const int absStep = keyboardArea_.currentPage()
                                            * KeyboardArea::kPageSteps + ev.index;
                    const int newLen  = absStep + 1;
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
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }

                // Scene + Phrase + step: deviate ALL tracks to phraseIdx.
                // Landing on the diagonal (== current scene index) un-deviates all.
                if (uiState_.sceneHeld && uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                        processor_.deviateAllToPhrase(ev.index);
                    uiState_.phraseScopeUsed = true;
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // Phrase + step (DESIGN §4.7/§16). Track+Phrase and bare Phrase both
                // deviate the focused track only.
                if (uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                        processor_.swapPhraseForTrack(keyboardArea_.getActiveTrack(), ev.index);
                    uiState_.phraseScopeUsed = true;
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // Scene + step: scene launch or create-on-empty (DESIGN §16/§23.3).
                //   occupied + no-func + no-mute:  single-tap=overlay, double-tap=floor
                //   occupied + func:               floor launch unconditionally
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
                        const bool occupied = isActive
                                              || processor_.sceneSlotOccupied(ev.index);
                        if (occupied && !funcHeld && !muteHeld)
                        {
                            // Single-tap = overlay, double-tap = floor.
                            const double now = juce::Time::getMillisecondCounterHiRes();
                            const bool toFloor =
                                doubleTap_.recordAndCheck(3000 + ev.index, now);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, toFloor);
                            else if (toFloor)
                                processor_.setActiveSceneToFloor(ev.index);
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
                            if (phraseConflictAndConfirm(ev.index,
                                                         PendingConfirm::CreateScene))
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
                            if (phraseConflictAndConfirm(ev.index,
                                                         PendingConfirm::CreateBaselineScene))
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
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // Func+Track (machine/Kit picker) + step: assign machine by index (§4.7.2).
                if (uiState_.funcTrackHeld)
                {
                    const int numMachines = processor_.numAvailableMachines();
                    if (ev.index >= 0 && ev.index < numMachines)
                    {
                        const std::string machineId {
                            processor_.availableMachineInfo(ev.index).id };
                        processor_.setTrackMachine(keyboardArea_.getActiveTrack(), machineId);
                        keyboardArea_.syncToActiveTrack();
                        releaseTransientLatch(CB::TrackScope);
                    }
                    keyboardArea_.repaint();
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
                                if (s.trigOverride.notes[static_cast<std::size_t>(n)] == absNote) { inNotes = true; break; }
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
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps
                                        + ev.index;
                    uiState_.noteEditSteps = { absStep };
                    keyboardArea_.repaint();
                    return true;
                }

                // MHZ.3.4: P-Lock clear mode — a second step press stages/un-stages a slot.
                // Cell index maps into a packed list of the step's P-locked slots (not by
                // raw slot index). Staged removals are committed on Func release.
                if (uiState_.pLockClearMode)
                {
                    const int cellIdx  = ev.index;
                    const int track    = uiState_.pLockClearTrack;
                    const int step     = uiState_.pLockClearStep;
                    const int numSlots = processor_.numParams(track);
                    if (track >= 0 && step >= 0 && step < kMaxStepsPerTrack)
                    {
                        // Rebuild the packed slot list (same order as the render).
                        const auto& stepData = processor_.sequence()
                            .tracks[static_cast<std::size_t>(track)]
                            .steps[static_cast<std::size_t>(step)];
                        std::vector<int> lockedSlots;
                        const auto& tov = stepData.trigOverride;
                        if (tov.hasVelocity)   lockedSlots.push_back(-2);
                        if (tov.hasGate)       lockedSlots.push_back(-3);
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
                        const int pole    = (ev.index < 8) ? 0 : 1;
                        const auto key    = std::make_pair(track, absSlot);
                        auto& dormant     = (pole == 0) ? morphDormantA_ : morphDormantB_;
                        const auto info   = processor_.morphWidgetInfo(track, absSlot);
                        const bool isActive  = (pole == 0) ? info.inA : info.inB;
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
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }

                // MHZ.3.4: Func + step (no existing step held) → enter P-Lock clear mode.
                // MHZ.9.5: use ctx.heldSteps().empty() so latched steps keep the edit context alive.
                if (uiState_.funcHeld && processor_.editContext().heldSteps().empty())
                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps
                                        + ev.index;
                    uiState_.pLockClearStaged.clear();  // fresh session
                    uiState_.pLockClearMode  = true;
                    uiState_.pLockClearTrack = keyboardArea_.getActiveTrack();
                    uiState_.pLockClearStep  = absStep;
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
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }

                // Ignore key-repeat (same physical key already in list).
                {
                    bool alreadyHeld = false;
                    for (auto& [code, _] : heldStepKeys_)
                    {
                        if (code == rawCode) { alreadyHeld = true; break; }
                    }
                    if (alreadyHeld) return true;
                }

                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps
                                        + ev.index;
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    const bool isDouble = doubleTap_.recordAndCheck(absStep, now);

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
                        if (lastTrigToggleApplied_
                            && lastTrigToggleStep_  == absStep
                            && lastTrigToggleTrack_ == ctx.heldTrackIndex())
                        {
                            auto& s = processor_.sequence()
                                .tracks[static_cast<std::size_t>(lastTrigToggleTrack_)]
                                .steps[static_cast<std::size_t>(lastTrigToggleStep_)];
                            s.trig = !s.trig;  // undo the first-tap's toggle
                        }
                        lastTrigToggleApplied_ = false;
                        lastTrigToggleStep_    = -1;
                        lastTrigToggleTrack_   = -1;
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
                    lastTrigToggleStep_    = -1;
                    lastTrigToggleTrack_   = -1;
                    repaint();
                }
                return true;
            }

            case ControllerButton::SelectTrack:
            {
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
                    repaint();
                    keyboardArea_.repaint();
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
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }
                keyboardArea_.setActiveTrack(ev.index);
                processor_.setControlAllActive(false);  // specific track chosen; disable control-all
                releaseTransientLatch(CB::TrackScope);
                return true;
            }

            case ControllerButton::NavUp:
            {
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
                if (uiState_.trackHeld && processor_.controlAllActive()
                    && t >= 0 && t < static_cast<int>(kNumTracks))
                {
                    auto& mode = uiState_.trackInputMode[static_cast<std::size_t>(t)];
                    switch (mode)
                    {
                        case TrackInputMode::Play:      mode = TrackInputMode::Levels;     break;
                        case TrackInputMode::Chromatic: mode = TrackInputMode::Play;       break;
                        case TrackInputMode::Levels:    mode = TrackInputMode::Chromatic;  break;
                        default: break;
                    }
                    escapeAllLatches();  // entering new modality exits current latch
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
                // Normal: next higher track number.
                keyboardArea_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1, t + 1));
                return true;
            }

            case ControllerButton::NavDown:
            {
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
                if (uiState_.trackHeld && processor_.controlAllActive()
                    && t >= 0 && t < static_cast<int>(kNumTracks))
                {
                    auto& mode = uiState_.trackInputMode[static_cast<std::size_t>(t)];
                    switch (mode)
                    {
                        case TrackInputMode::Play:      mode = TrackInputMode::Chromatic;  break;
                        case TrackInputMode::Chromatic: mode = TrackInputMode::Levels;     break;
                        case TrackInputMode::Levels:    mode = TrackInputMode::Play;       break;
                        default: break;
                    }
                    escapeAllLatches();  // entering new modality exits current latch
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
                // Normal: previous (lower) track number.
                keyboardArea_.setActiveTrack(std::max(0, t - 1));
                return true;
            }

            case ControllerButton::NavLeft:
            {
                // Note-edit mode and CHROMATIC mode both use NavLeft/Right for octave shift.
                const int tl = keyboardArea_.getActiveTrack();
                const bool chromL = tl >= 0 && tl < static_cast<int>(kNumTracks)
                    && uiState_.trackInputMode[static_cast<std::size_t>(tl)] == TrackInputMode::Chromatic;
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

            case ControllerButton::NavRight:
            {
                const int tr = keyboardArea_.getActiveTrack();
                const bool chromR = tr >= 0 && tr < static_cast<int>(kNumTracks)
                    && uiState_.trackInputMode[static_cast<std::size_t>(tr)] == TrackInputMode::Chromatic;
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
                // DESIGN §34.4: at the last in-length page, a single NavRight is a
                // no-op (clamped); a double-tap unlocks one empty page past the
                // end so a longer length can be set out there.
                if (keyboardArea_.currentPage() >= keyboardArea_.numPages() - 1)
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(4000, now))
                        keyboardArea_.unlockScrollPastEnd();
                }
                keyboardArea_.nextPage();
                return true;
            }

            // MHY.4: right-utility verbs. Without a scope modifier these perform their
            // default transport / confirmation action; with a scope held, EditMode
            // routes them as grammar verbs (CPY / PST / CLR / confirm / cancel).

            case ControllerButton::VerbPlay:
            {
                using PS = EditMode::PrimaryScope;
                // Scope held → grammar verb (Paste).
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
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
                        PS synScope = PS::None;
                        switch (clipboard_.type)
                        {
                            case CT::None:    break;
                            case CT::Step:    synScope = PS::Trig;    break;
                            case CT::Section: synScope = PS::Section; break;
                            case CT::Track:   synScope = PS::Track;   break;
                            case CT::Pattern: synScope = PS::Phrase;  break;
                            case CT::Scene:   synScope = PS::Scene;   break;
                            case CT::All:     break;  // handled above
                        }
                        if (synScope != PS::None)
                            dispatchVerb(synScope, ev.button);
                    }
                    return true;
                }
                if (playKeyHeld_) return true;  // ignore key repeat
                playKeyHeld_ = true;

                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = (now - lastPlayPressTime_) < kDoublePressMsThreshold;
                lastPlayPressTime_ = now;

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

            case ControllerButton::VerbClear:
            {
                using PS = EditMode::PrimaryScope;
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
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
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

            case ControllerButton::VerbDelete:
            {
                using PS = EditMode::PrimaryScope;
                // Build a description of the entity to delete based on current scope.
                juce::String entityName;
                switch (editMode_.primaryScope())
                {
                    case PS::Track:
                        entityName = "Track " + juce::String(keyboardArea_.getActiveTrack() + 1);
                        break;
                    case PS::Phrase:
                        entityName = "Phrase";
                        break;
                    case PS::Scene:
                        entityName = "Part";
                        break;
                    default:
                        return true;  // No operand — inert.
                }
                pendingConfirm_ = PendingConfirm::Delete;
                setStatus(status::confirmDelete(entityName));
                keyboardArea_.repaint();
                return true;
            }

            case ControllerButton::VerbPanic:
                processor_.requestPanic();
                return true;

            case ControllerButton::VerbRecord:
            {
                using PS = EditMode::PrimaryScope;
                // Scene + Record: bake live deviations into home-row phrase content.
                // Destructive — requires Yes/No confirmation.
                // Func+Scene+Record is "copy scene" — falls through to dispatchVerb.
                if (uiState_.sceneHeld && !editMode_.scopeState().func)
                {
                    const int nd = processor_.countDeviatedTracks();
                    if (nd == 0)
                    {
                        setStatus(status::noDeviationsToBake());
                        return true;
                    }
                    pendingConfirm_ = PendingConfirm::BakeScene;
                    {
                        juce::String msg = "Bake " + juce::String(nd) + " track(s) onto row "
                            + juce::String(processor_.activeSectionIdx()) + "?  P=Yes  Func+P=No";
                        setStatus(msg);
                    }
                    repaint();
                    return true;
                }
                // Scope held → grammar verb (e.g. copy).  No scope → arm recording.
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                // Func+U with no non-trivial scope = omni copy (capture all layers).
                if (editMode_.scopeState().func
                    && editMode_.primaryScope() == PS::Func)
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
                    const bool isDouble = doubleTap_.recordAndCheck(
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

            case ControllerButton::VerbYes:
            {
                using PS = EditMode::PrimaryScope;
                // Y = Snapshot. Under scene scope → re-sync all to scene (scope-specific snapshot).
                if (uiState_.sceneHeld)
                {
                    processor_.resyncAllToScene();
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }
                // Non-trivial scope → scope-specific snapshot via dispatchVerb.
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
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

            case ControllerButton::VerbNo:
            {
                using PS = EditMode::PrimaryScope;
                const bool funcHeld = editMode_.scopeState().func;

                // Both primary P (Yes/confirm) and Func+P (No/cancel) arrive here as VerbNo.
                // Distinguish by whether Func is held.

                // Pending-confirm: P = execute, Func+P = cancel.
                if (pendingConfirm_ != PendingConfirm::None)
                {
                    if (!funcHeld)
                    {
                        if (pendingConfirm_ == PendingConfirm::BakeScene)
                        {
                            processor_.snapshot(CheckpointScope::Song, 0);
                            processor_.bakeSceneState();
                            setStatus(status::baked());
                            pendingConfirm_ = PendingConfirm::None;
                            repaint();
                            keyboardArea_.repaint();
                            return true;
                        }
                        if (pendingConfirm_ == PendingConfirm::CreateScene)
                        {
                            const int tgt = pendingTarget_;
                            processor_.snapshot(CheckpointScope::Song, 0);
                            processor_.createBakedCopyScene(tgt);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(tgt, false);
                            else
                                processor_.setActiveScene(tgt);
                            setStatus(status::sceneCreated(tgt + 1));
                            pendingConfirm_ = PendingConfirm::None;
                            repaint();
                            keyboardArea_.repaint();
                            return true;
                        }
                        if (pendingConfirm_ == PendingConfirm::CreateBaselineScene)
                        {
                            const int tgt = pendingTarget_;
                            processor_.snapshot(CheckpointScope::Song, 0);
                            processor_.createBaselineCopyScene(tgt);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(tgt, false);
                            else
                                processor_.setActiveScene(tgt);
                            setStatus(status::sceneBaseline(tgt + 1));
                            pendingConfirm_ = PendingConfirm::None;
                            repaint();
                            keyboardArea_.repaint();
                            return true;
                        }
                        if (pendingConfirm_ == PendingConfirm::PasteScene)
                        {
                            const int destG = pendingTarget_;
                            processor_.snapshot(CheckpointScope::Song, 0);
                            auto& dst = processor_.section();
                            dst.activeMask  = clipboard_.scene.floor.activeMask;
                            dst.coreTime    = clipboard_.scene.floor.coreTime;
                            dst.morphA      = clipboard_.scene.floor.morphA;
                            dst.morphB      = clipboard_.scene.floor.morphB;
                            dst.initialised = true;
                            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                            {
                                auto& ph =
                                    processor_.song().tracks[static_cast<std::size_t>(t)]
                                        .phrases[static_cast<std::size_t>(destG)];
                                ph = clipboard_.scene.phrases[static_cast<std::size_t>(t)];
                                ph.initialised = true;
                            }
                            processor_.refreshWorkingFromModel();
                            setStatus(status::pastedScene());
                            pendingConfirm_ = PendingConfirm::None;
                            repaint();
                            keyboardArea_.repaint();
                            return true;
                        }
                        // Execute the pending delete against the current scope.
                        // Snapshot first so the delete is undoable via Restore.
                        switch (editMode_.primaryScope())
                        {
                            case PS::Track:
                            {
                                const int t = keyboardArea_.getActiveTrack();
                                if (t >= 0 && t < static_cast<int>(kNumTracks))
                                {
                                    processor_.snapshot(CheckpointScope::Track, t);
                                    processor_.deleteTrack(t);
                                    setStatus(status::deletedTrack(t));
                                }
                                break;
                            }
                            case PS::Phrase:
                            {
                                int ckTrk = 0;
                                processor_.snapshot(ckScope(ckTrk), ckTrk);
                                for (auto& trk : processor_.sequence().tracks)
                                {
                                    for (auto& s : trk.steps)
                                    {
                                        s.trig              = false;
                                        s.condition         = TrigCondition{};
                                        s.overrides         = PLock{};
                                        s.trigOverride      = TrigOverride{};
                                        s.fillTrigState     = FillTrigState::Off;
                                        s.fillOverrides     = PLock{};
                                        s.fillTrigOverride  = TrigOverride{};
                                    }
                                }
                                setStatus(status::deletedPhrase());
                                break;
                            }
                            case PS::Scene:
                            {
                                int ckTrk = 0;
                                processor_.snapshot(ckScope(ckTrk), ckTrk);
                                processor_.deletePart();
                                releaseTransientLatch(CB::SceneScope);
                                setStatus(status::deletedPart());
                                break;
                            }
                            default:
                                break;
                        }
                    }
                    else
                    {
                        setStatus(status::cancelled());
                    }
                    pendingConfirm_ = PendingConfirm::None;
                    repaint();
                    return true;
                }

                // No pending confirm. Bare Yes (no Func) = Quantize or snapshot/confirm verb.
                if (!funcHeld)
                {
                    // Quantize verb (DESIGN §19.3): scope + No zeros microOffset values.
                    // Trig (held steps) → those steps; Track → whole track; Phrase → all tracks.
                    // Bare No (no scope) falls through to snapshot/confirm.
                    const auto qScope     = editMode_.primaryScope();
                    const auto& qCtx      = processor_.editContext();
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
                    editMode_.onVerb(ev.button);
                    return true;
                }

                // Func+P = No/cancel. No scope → scope-aware Restore (resolved on key-up).
                if (editMode_.primaryScope() == PS::None
                    || editMode_.primaryScope() == PS::Func)
                {
                    restoreActive_    = true;
                    restoreKeyDownMs_ = juce::Time::getMillisecondCounterHiRes();
                    return true;
                }
                editMode_.onVerb(ev.button);
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
                // Resolve on key-up (tap = pop one, hold = jump to floor).
                if (sectionSuiteScopeHeld(uiState_)) return true;
                restoreKeyDownMs_ = juce::Time::getMillisecondCounterHiRes();
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
            case ControllerButton::RecordArm:
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = doubleTap_.recordAndCheck(
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
            case ControllerButton::ToggleMute:
            {
                const int trackIdx = ev.index;
                if (trackIdx < 0 || trackIdx >= static_cast<int>(kNumTracks))
                    return true;
                const int at = keyboardArea_.getActiveTrack();
                const auto inputMode = (at >= 0)
                    ? uiState_.trackInputMode[static_cast<std::size_t>(at)]
                    : TrackInputMode::Play;
                const LayerFacts facts { inputMode, at };
                const auto layer    = resolveActiveLayer(uiState_, processor_.editContext(), facts);
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

            case ControllerButton::TapTempo:
                handleTapTempo();
                return true;

            case ControllerButton::None:
                return false;

            default:
                return false;
        }
    }

    bool LockstepEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
    {
        const int rawCode = key.getKeyCode();
        const int uCode   = (rawCode >= 'a' && rawCode <= 'z')
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
        using T  = ControllerEvent::Type;

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
                // NoteEdit: Func release commits staged note removals, then exits mode.
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
                    uiState_.noteEditMode = false;
                    uiState_.noteEditSteps.clear();
                    uiState_.noteEditStaged.clear();
                }
                uiState_.funcSrcHeld = false;
                // MHZ.3.4: Func release commits staged P-Lock clears, then exits mode.
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
                    uiState_.pLockClearStaged.clear();
                }
                uiState_.pLockClearMode  = false;
                uiState_.pLockClearTrack = -1;
                uiState_.pLockClearStep  = -1;
                // MHZ.3.5: Func release exits machine picker mode.
                uiState_.funcTrackHeld = false;
                // 6.5: Func release closes both pickers; meta section view stays.
                uiState_.funcFxHeld         = false;
                uiState_.masterFxPickerOpen = false;
                refreshMetaBand();  // 1c: Func released → restore normal MZ band
                editMode_.onScopeEvent({ T::ButtonUp, CB::Func });
                updateFillActivation();
                keyboardArea_.repaint();
                repaint();
                break;

            case CB::TrackScope:
                physHeld_.track = false;
                uiState_.funcTrackHeld = false;  // exit machine/Kit picker on Track release
                if (!uiState_.latch.track)
                {
                    uiState_.trackHeld = false;
                    uiState_.swingDismissed = false;  // re-arm for next hold
                    processor_.setControlAllActive(false);  // MD.10
                    editMode_.onScopeEvent({ T::ButtonUp, CB::TrackScope });
                    updateSwingQualifier();
                    repaint();
                }
                break;

            case CB::PhraseScope:
                physHeld_.phrase = false;
                if (!uiState_.latch.phrase)
                {
                    // 5.5: if we were in Euclidean mode, commit the pattern now.
                    if (uiState_.euclidHeld)
                    {
                        applyEuclidToTrack(keyboardArea_.getActiveTrack());
                        uiState_.euclidHeld = false;
                        refreshMetaBand();
                    }
                    uiState_.phraseScopeHeld = false;
                    uiState_.phraseScopeUsed = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::PhraseScope });
                    repaint();
                }
                break;

            case CB::SceneScope:
                physHeld_.scene = false;
                if (!uiState_.latch.scene)
                {
                    uiState_.sceneHeld = false;
                    uiState_.swingDismissed = false;  // re-arm for next hold
                    editMode_.onScopeEvent({ T::ButtonUp, CB::SceneScope });
                    updateSwingQualifier();
                    repaint();
                }
                break;

            case CB::MuteScope:
                physHeld_.mute = false;
                if (!uiState_.latch.mute)
                {
                    uiState_.muteHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::MuteScope });
                    repaint();
                }
                break;

            case CB::FillScope:
                physHeld_.fill = false;
                if (!uiState_.latch.fill)
                {
                    // 5.5: if we were in Euclidean mode, commit the pattern now.
                    if (uiState_.euclidHeld)
                    {
                        applyEuclidToTrack(keyboardArea_.getActiveTrack());
                        uiState_.euclidHeld = false;
                        refreshMetaBand();
                    }
                    uiState_.fillHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::FillScope });
                    updateFillActivation();
                    // 5.7: release any momentary trig-grid overlay (Retrig / SoundPool).
                    if (uiState_.trigGridMode == TrigGridMode::SoundPool)
                    {
                        // Restore the track's saved sound (exit audition mode).
                        const int at = keyboardArea_.getActiveTrack();
                        if (at >= 0 && at < static_cast<int>(kNumTracks))
                            processor_.clearLiveSwap(at);
                    }
                    else if (uiState_.trigGridMode == TrigGridMode::Retrig)
                    {
                        processor_.setRetrigActive(0, false);  // stop any live retrig stutter
                    }
                    uiState_.trigGridMode = TrigGridMode::Default;
                    repaint();
                }
                break;

            case CB::CueScope:
                physHeld_.cue = false;
                // Cue is not latchable (reserved for MU); always release.
                uiState_.cueHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::CueScope });
                repaint();
                break;

            case CB::MorphScope:
                physHeld_.morph = false;
                uiState_.morphNavQualifier = 0;
                manipulationZone_.setMorphQualifier(0);
                if (!uiState_.latch.morph)
                {
                    uiState_.morphHeld = false;
                    morphDormantA_.clear();
                    morphDormantB_.clear();
                    editMode_.onScopeEvent({ T::ButtonUp, CB::MorphScope });
                    repaint();
                }
                manipulationZone_.setMorphHeld(uiState_.morphHeld);
                break;

            case CB::SongScope:
                physHeld_.song = false;
                uiState_.masterFxPickerOpen = false;  // close picker on Song release; band stays
                if (!uiState_.latch.song)
                {
                    uiState_.songHeld = false;
                    uiState_.swingDismissed = false;  // re-arm for next hold
                    editMode_.onScopeEvent({ T::ButtonUp, CB::SongScope });
                    updateSwingQualifier();
                    refreshMetaBand();
                    repaint();
                }
                break;

            case CB::Section:
            case CB::MetaSection:
                heldSectionRawCode_ = -1;
                heldSectionIndex_   = -1;
                uiState_.funcSrcHeld  = false;
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

            case CB::Step:
            {
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

                // 6.5 Animate bypass restore: step-up ends the momentary bypass.
                if (animateBypassTrack_ >= 0)
                {
                    processor_.setTrackInsertBypass(animateBypassTrack_, animateBypassSlot_, false);
                    animateBypassTrack_ = -1;
                    animateBypassSlot_  = -1;
                }

                // Func+Src+step: step release while funcSrcHeld → enter NoteEdit mode.
                if (uiState_.funcSrcHeld && !uiState_.noteEditMode
                    && !uiState_.noteEditSteps.empty())
                {
                    uiState_.noteEditMode = true;
                    uiState_.noteEditStaged.clear();

                    // Auto-set the view octave to match the step's existing notes so
                    // cells are immediately live without needing a NavUp/Down first.
                    if (!uiState_.noteEditSteps.empty())
                    {
                        const int firstStep = *uiState_.noteEditSteps.begin();
                        const int track     = processor_.editContext().heldTrackIndex();
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

                    keyboardArea_.repaint();
                    repaint();
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

                        const int  track      = ctx.heldTrackIndex();
                        const bool paramWrote = ctx.wasParamWritten();
                        ctx.release(stepIdx);
                        // MHZ.3.1: next press starts a fresh chord capture.
                        if (track >= 0)
                            processor_.cancelChordCapture(track, stepIdx);
                        if (!paramWrote && track >= 0 && stepIdx >= 0)
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
                                    case FTS::Inherit: s.fillTrigState = FTS::On;      break;
                                    case FTS::On:      s.fillTrigState = FTS::Off;     break;
                                    case FTS::Off:     s.fillTrigState = FTS::Inherit; break;
                                }
                            }
                            else
                            {
                                s.trig = !s.trig;
                                // MHZ.9.5: record for possible revert if double-tap follows.
                                lastTrigToggleStep_    = stepIdx;
                                lastTrigToggleTrack_   = track;
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

            case CB::VerbYes:
                break;  // Y = Snapshot; no held-state to clear.

            case CB::Restore:
            {
                if (!restoreActive_) break;
                const double held = juce::Time::getMillisecondCounterHiRes() - restoreKeyDownMs_;
                restoreActive_ = false;
                int ckTrk = 0;
                const CheckpointScope scp = ckScope(ckTrk);
                if (held >= kHoldRestoreMs)
                    processor_.restoreToFloor(scp, ckTrk);
                else
                    processor_.restoreOne(scp, ckTrk);
                repaint();
                break;
            }

            case CB::VerbNo:
            {
                // Func+P "Restore" path (recorded press time in dispatchDown).
                if (restoreActive_)
                {
                    const double held = juce::Time::getMillisecondCounterHiRes() - restoreKeyDownMs_;
                    restoreActive_ = false;
                    int ckTrk = 0;
                    const CheckpointScope scp = ckScope(ckTrk);
                    if (held >= kHoldRestoreMs)
                        processor_.restoreToFloor(scp, ckTrk);
                    else
                        processor_.restoreOne(scp, ckTrk);
                    repaint();
                }
                break;
            }

            case CB::NavUp:
            case CB::NavDown:
                uiState_.morphNavQualifier = 0;
                manipulationZone_.setMorphQualifier(0);
                manipulationZone_.setMorphHeld(uiState_.morphHeld);
                break;

            case CB::VerbRecord:
            case CB::VerbStop:
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
            case CB::TapTempo:
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
        pressTracker_.forEachReleasedKeyboard([&](int src, ControllerButton btn, int idx)
        {
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
            const double spanMs    = tapTimes_[static_cast<std::size_t>(tapCount_ - 1)] - tapTimes_[0];
            const double intervals = static_cast<double>(tapCount_ - 1);
            const double bpm       = (60000.0 * intervals) / spanMs;
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

        // Tempo bar + Manipulation Zone are anchored to the top at fixed heights;
        // the key rows below fill the remaining space, so growing the window makes
        // the (QWERTY-emulating) buttons taller/squarer while the MZ stays put.
        if (tempoBar_)
            tempoBar_->setBounds(bounds.removeFromTop(28).reduced(8, 2));
        bounds.removeFromTop(2);

        // MHX.5: encoder band (MZ 4x2) + vertical crossfader to its right.
        static constexpr int kMZHeight     = 160; // MHX 4x2 MZ (two rows of 4 slots)
        static constexpr int kFaderW       = 28;  // crossfader strip width
        static constexpr int kTrackRowH    = 26;  // track-number + VU row
        static constexpr int kMsRowH       = 22;  // mute/solo row
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
                const bool visible = (static_cast<int>(i) >= pageStart
                                   && static_cast<int>(i) < pageStart + 8);
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
                const bool visible = (static_cast<int>(i) >= pageStart
                                   && static_cast<int>(i) < pageStart + 8);
                muteBtns_[i].setVisible(visible);
                soloBtns_[i].setVisible(visible);
                if (visible)
                {
                    auto col  = msRow.removeFromLeft(colW);
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

    bool LockstepEditor::phraseConflictAndConfirm(int phraseSlot, PendingConfirm action)
    {
        // No-op skip: re-stamping identical content never needs a prompt.
        if (action == PendingConfirm::CreateScene
            && processor_.phraseRowMatchesActiveContent(phraseSlot))
            return false;

        bool slotHasContent = false;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            if (processor_.song().tracks[static_cast<std::size_t>(t)]
                    .phrases[static_cast<std::size_t>(phraseSlot)].initialised)
            {
                slotHasContent = true;
                break;
            }
        }
        if (!slotHasContent)
            return false;   // clean row — no conflict

        pendingConfirm_ = action;
        pendingTarget_  = phraseSlot;
        const int freeSlot = processor_.firstFreePhraseSlot();
        juce::String msg = "Overwrite phrase row " + juce::String(phraseSlot) + "?";
        if (freeSlot >= 0) msg += "  free:S" + juce::String(freeSlot + 1);
        msg += "  P=Yes  Func+P=No";
        setStatus(msg);
        repaint();
        return true;    // conflict raised — caller must wait for Yes/No
    }

    // -------------------------------------------------------------------------
    // Transient status line

    void LockstepEditor::setStatus(const juce::String& msg)
    {
        statusMessage_ = msg;
        statusSetMs_   = juce::Time::getMillisecondCounter();
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
    // Verb dispatch (MB.3)

    void LockstepEditor::dispatchVerb(EditMode::PrimaryScope scope, ControllerButton verb)
    {
        using PS = EditMode::PrimaryScope;
        using CB = ControllerButton;

        // Phase 8.4 command core: try migrated verb handlers first.
        {
            auto ctx = commandContext();
            if (commandCore_.handleVerb(scope, verb, ctx, *editorEffects_))
                return;
        }

        switch (scope)
        {
            // -----------------------------------------------------------------------
            // MD.2  Step copy / paste / clear
            // PS::Trig — migrated to CommandCore / VerbCommands.cpp (8.4b)

            // PS::Section — migrated to CommandCore / VerbCommands.cpp (8.4h)

            // PS::Track, PS::Phrase — migrated to CommandCore / VerbCommands.cpp (8.4c)

            // PS::Scene, PS::Song, PS::None, PS::Morph, PS::Func/Mute/Fill/Cue —
            // migrated to CommandCore / VerbCommands.cpp (8.4d–e)

            default:
                break;
        }

        // Chrome must repaint after any verb that may change clipboard or checkpoint state.
        repaint();
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
            uiState_.trackHeld  || uiState_.latch.track,
            uiState_.muteHeld   || uiState_.latch.mute
        };
    }

    CommandContext LockstepEditor::commandContext()
    {
        // ProcessorCatalog is lightweight — safe to construct per-call.
        static ProcessorCatalog catalog { processor_ };
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

    ControllerEventSink LockstepEditor::buildControllerSink()
    {
        ControllerEventSink sink;

        sink.emitEvent = [this](ControllerEvent ev)
        {
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

        sink.applyParamDelta = [this](int mzSlot, int rawDelta)
        {
            const int track = keyboardArea_.getActiveTrack();

            // Meta band takes priority: encoder edits the shown band, not machine params.
            const MetaBand band = resolveMetaBand(uiState_);
            if (band != MetaBand::None)
            {
                const int  swScope = swingScopeFor(uiState_);
                const auto views   = buildMetaBand(band, swScope, processor_, track,
                                                   processor_.editContext(), uiState_);
                if (mzSlot < 0 || mzSlot >= 8) return;
                const auto& v = views[static_cast<std::size_t>(mzSlot)];
                if (!v.writable) return;
                const float range = v.maxValue - v.minValue;
                if (range <= 0.0f) return;
                const float norm    = juce::jlimit(0.0f, 1.0f, (v.value - v.minValue) / range);
                const float newNorm = juce::jlimit(0.0f, 1.0f,
                                                   norm + static_cast<float>(rawDelta) / 128.0f);
                writeMetaField(band, swScope, mzSlot, v.minValue + newNorm * range,
                               processor_, track, processor_.editContext(), uiState_);
                return;
            }

            const int absSlot = manipulationZone_.slotOffset() + mzSlot;
            if (absSlot >= processor_.numParams(track)) return;

            const auto  spec  = processor_.paramSpec(track, absSlot);
            const float range = spec.maxValue - spec.minValue;
            if (range <= 0.0f) return;

            const auto& ec       = processor_.editContext();
            const bool  stepHeld = ec.isActiveForEditing()
                                   && ec.heldTrackIndex() == track;

            // Morph-held + no step held → write morph overlay (DESIGN §17.3).
            // With ^/v qualifier: write directly to one pole (absolute delta).
            if (uiState_.morphHeld && !stepHeld)
            {
                const float deltaAbs = static_cast<float>(rawDelta) / 128.0f * range;
                if (uiState_.morphNavQualifier != 0)
                {
                    const int pole = uiState_.morphNavQualifier - 1;  // 0=A, 1=B
                    const auto info = processor_.morphWidgetInfo(track, absSlot);
                    const float curPole = (pole == 0)
                        ? (info.inA ? info.aValue : processor_.baseParamValue(track, absSlot))
                        : (info.inB ? info.bValue : processor_.baseParamValue(track, absSlot));
                    processor_.writeMorphPole(track, absSlot,
                        juce::jlimit(spec.minValue, spec.maxValue, curPole + deltaAbs), pole);
                }
                else
                {
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

            const float norm    = juce::jlimit(0.0f, 1.0f, (cur - spec.minValue) / range);
            const float newNorm = juce::jlimit(0.0f, 1.0f,
                                               norm + static_cast<float>(rawDelta) / 128.0f);
            const float newVal  = spec.minValue + newNorm * range;

            // Auto-morph-aware: bare encoder follows morph state of the slot.
            // No-morph → kit base (as before). One pole set → write that pole
            // directly (fader irrelevant). Both set → proportional split.
            if (!stepHeld)
            {
                const auto mInfo = processor_.morphWidgetInfo(track, absSlot);
                if (mInfo.exists)
                {
                    const float deltaAbs = static_cast<float>(rawDelta) / 128.0f * range;
                    if (mInfo.inA && !mInfo.inB)
                    {
                        const float curA = mInfo.aValue;
                        processor_.writeMorphPole(track, absSlot,
                            juce::jlimit(spec.minValue, spec.maxValue, curA + deltaAbs), 0);
                    }
                    else if (!mInfo.inA && mInfo.inB)
                    {
                        const float curB = mInfo.bValue;
                        processor_.writeMorphPole(track, absSlot,
                            juce::jlimit(spec.minValue, spec.maxValue, curB + deltaAbs), 1);
                    }
                    else
                    {
                        processor_.writeMorph(track, absSlot, deltaAbs, processor_.morphFader());
                    }
                    processor_.editContext().markParamWritten();
                    return;
                }
            }

            processor_.writeParam(track, absSlot, newVal);
            processor_.editContext().markParamWritten();
        };

        sink.resetSlot = [this](int mzSlot)
        {
            const int track   = keyboardArea_.getActiveTrack();
            const int absSlot = manipulationZone_.slotOffset() + mzSlot;
            if (absSlot >= processor_.numParams(track)) return;

            auto& ctx      = processor_.editContext();
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

        sink.setCrossfader = [this](float normValue)
        {
            // setValue triggers onValueChange which applies the inversion.
            crossfader_.setValue(static_cast<double>(normValue), juce::sendNotificationAsync);
        };

        sink.applyGlobalDelta = [this](GlobalTarget target, int rawDelta)
        {
            switch (target)
            {
                case GlobalTarget::Tempo:
                {
                    const double cur = processor_.clock().localBpm();
                    processor_.clock().setLocalBpm(
                        std::clamp(cur + static_cast<double>(rawDelta) * 0.5,
                                   20.0, 300.0));
                    break;
                }
                case GlobalTarget::Master:
                {
                    auto* p = processor_.apvts().getParameter(ParamIDs::outputGain);
                    if (p)
                    {
                        const float cur    = p->getValue();  // normalised 0..1
                        const float newVal = juce::jlimit(0.0f, 1.0f,
                                                          cur + static_cast<float>(rawDelta) / 128.0f);
                        p->setValueNotifyingHost(newVal);
                    }
                    break;
                }
                case GlobalTarget::Swing:
                {
                    // Song-all swing: encoder delta at ~1% per tick.
                    const float cur    = processor_.swingSongAll();
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
