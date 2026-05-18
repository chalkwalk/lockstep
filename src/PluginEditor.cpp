#include "PluginEditor.h"
#include "ParameterIDs.h"
#include <algorithm>

namespace lockstep
{
    LockstepEditor::LockstepEditor(LockstepProcessor& proc)
        : juce::AudioProcessorEditor(&proc),
          processor_(proc),
          transport_(proc.clock()),
          stepGrid_(proc, uiState_),
          manipulationZone_(proc, stepGrid_),
          poolOverlay_(proc),
          sectionBar_(proc, stepGrid_, uiState_),
          functionBar_(proc, uiState_)
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
        updateTransportGhosting();

        // Wire section-change callbacks -> update ManipulationZone.
        sectionBar_.onSectionChanged = [this](int /*section*/, int /*page*/, int firstSlot)
        {
            manipulationZone_.setSlotOffset(firstSlot);
        };
        sectionBar_.onMetaSectionChanged = [this](int metaSection)
        {
            manipulationZone_.setMetaSection(metaSection);
        };

        // Track header: selector + mute/solo.
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            const auto ti = static_cast<std::size_t>(i);

            trackBtns_[ti].setButtonText(juce::String(i + 1));
            trackBtns_[ti].setClickingTogglesState(false);
            trackBtns_[ti].setWantsKeyboardFocus(false);
            trackBtns_[ti].onClick = [this, i] { stepGrid_.setActiveTrack(i); };
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

        stepGrid_.onActiveTrackChanged = [this](int newTrack)
        {
            for (auto& b : trackBtns_)
                b.setToggleState(false, juce::dontSendNotification);
            trackBtns_[static_cast<std::size_t>(newTrack)].setToggleState(
                true, juce::dontSendNotification);
            sectionBar_.syncToActiveTrack();
        };

        stepGrid_.onDisplayModeChanged = [this](GridDisplayMode mode)
        {
            applyDisplayMode(mode);
        };

        displayModeBtn_.setWantsKeyboardFocus(false);
        displayModeBtn_.onClick = [this]
        {
            applyDisplayMode(static_cast<GridDisplayMode>(
                (static_cast<int>(gridMode_) + 1) % 3));
        };
        addAndMakeVisible(displayModeBtn_);
        addAndMakeVisible(manipulationZone_);
        addAndMakeVisible(sectionBar_);
        addAndMakeVisible(functionBar_);
        addAndMakeVisible(stepGrid_);
        addAndMakeVisible(keyboard_);

        poolBtn_.setWantsKeyboardFocus(false);
        poolBtn_.onClick = [this]
        {
            poolOverlay_.setVisible(!poolOverlay_.isVisible());
            if (poolOverlay_.isVisible())
                poolOverlay_.toFront(false);
        };
        addAndMakeVisible(poolBtn_);

        poolOverlay_.onClose = [this] { poolOverlay_.setVisible(false); };
        poolOverlay_.getActiveTrack = [this]() { return stepGrid_.getActiveTrack(); };
        addChildComponent(poolOverlay_);

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

        // Repaint chrome when a queued pattern switch fires.
        proc.onActivePatternChanged = [this] { repaint(); };

        setSize(990, 480);
        setWantsKeyboardFocus(true);
    }

    LockstepEditor::~LockstepEditor()
    {
        processor_.onActivePatternChanged = nullptr;
        processor_.apvts().removeParameterListener(ParamIDs::syncMode, this);
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
    }

    void LockstepEditor::parameterChanged(const juce::String& paramID, float /*newValue*/)
    {
        if (paramID == ParamIDs::syncMode)
            juce::MessageManager::callAsync([this] { updateTransportGhosting(); });
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

    void LockstepEditor::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(20, 22, 26));
        juce::ignoreUnused(processor_);
    }

    void LockstepEditor::paintOverChildren(juce::Graphics& g)
    {
        // ---- Scope chrome: badge row in the free space of the header row ----
        {
            const auto& sc = editMode_.scopeState();
            // Scope badges: (label, active?)
            struct Badge { const char* label; bool active; juce::Colour onColour; };
            const Badge scopes[] = {
                { "FNC", sc.func,    juce::Colour(0xFF6090C0u) },
                { "TRK", sc.track,   juce::Colour(0xFF50C060u) },
                { "PAT", sc.pattern, juce::Colour(0xFFC09030u) },
                { "MUT", sc.mute,    juce::Colour(0xFFC05050u) },
                { "FIL", sc.fill,    juce::Colour(0xFFB060C0u) },
            };

            // Clipboard badge
            const char* cbLabel = nullptr;
            juce::Colour cbColour{ 0xFFFFFFFFu };
            switch (clipboard_.type)
            {
                case ClipboardType::None:    break;
                case ClipboardType::Step:    cbLabel = "CPY:STP"; cbColour = juce::Colour(0xFF50B0C8u); break;
                case ClipboardType::Section: cbLabel = "CPY:SEC"; cbColour = juce::Colour(0xFF50B0C8u); break;
                case ClipboardType::Track:   cbLabel = "CPY:TRK"; cbColour = juce::Colour(0xFF50B0C8u); break;
                case ClipboardType::Pattern: cbLabel = "CPY:PAT"; cbColour = juce::Colour(0xFF50B0C8u); break;
            }

            g.setFont(juce::Font(juce::FontOptions(10.0f)));

            // Position: right-of-centre in the 36px header strip
            static constexpr int kBadgeH = 16;
            static constexpr int kBadgeW = 32;
            static constexpr int kGap    = 3;
            int bx = 420;
            const int by = (36 - kBadgeH) / 2;

            for (const auto& b : scopes)
            {
                const auto r = juce::Rectangle<int>(bx, by, kBadgeW, kBadgeH);
                g.setColour(b.active ? b.onColour : juce::Colour(0xFF303035u));
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(b.active ? juce::Colours::white
                                     : juce::Colour(0xFF606070u));
                g.drawText(b.label, r, juce::Justification::centred);
                bx += kBadgeW + kGap;
            }

            if (cbLabel != nullptr)
            {
                const auto r = juce::Rectangle<int>(bx, by, 56, kBadgeH);
                g.setColour(cbColour);
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(juce::Colours::black);
                g.drawText(cbLabel, r, juce::Justification::centred);
                bx += 56 + kGap;
            }

            if (checkpointDepth_ > 0)
            {
                const juce::String ckLabel = "CK:" + juce::String(checkpointDepth_);
                const auto r = juce::Rectangle<int>(bx, by, 38, kBadgeH);
                g.setColour(juce::Colour(0xFF40A080u));
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.drawText(ckLabel, r, juce::Justification::centred);
                bx += 38 + kGap;
            }

            // Queued pattern switch badge: shown while a pattern switch is pending.
            if (processor_.hasQueuedPattern())
            {
                const int qBank = processor_.queuedPatternBankIdx();
                const int qPat  = processor_.queuedPatternPatIdx();
                const juce::String quLabel = "QUE:" + juce::String(qBank + 1)
                                             + "." + juce::String(qPat + 1);
                const auto r = juce::Rectangle<int>(bx, by, 52, kBadgeH);
                g.setColour(juce::Colour(0xFFC08020u));
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.drawText(quLabel, r, juce::Justification::centred);
                bx += 52 + kGap;
            }

            // Part-sharing badge: shown when the active Part is shared by multiple patterns.
            {
                const int shareCount = processor_.activePartShareCount();
                if (shareCount > 1)
                {
                    const juce::String shrLabel = "SHR:" + juce::String(shareCount);
                    const auto r = juce::Rectangle<int>(bx, by, 40, kBadgeH);
                    g.setColour(juce::Colour(0xFF9040C0u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText(shrLabel, r, juce::Justification::centred);
                    bx += 40 + kGap;
                }
            }

            // Chain badge: shown when the chain queue has entries.
            {
                const int chainLen = processor_.chainLength();
                if (chainLen > 0)
                {
                    const bool looping = processor_.chainLoopEnabled();
                    const juce::String chnLabel = juce::String(looping ? "CHN:" : "CHN1:")
                                                  + juce::String(chainLen);
                    const int badgeW = looping ? 40 : 50;
                    const auto r = juce::Rectangle<int>(bx, by, badgeW, kBadgeH);
                    g.setColour(juce::Colour(0xFF20A0C0u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText(chnLabel, r, juce::Justification::centred);
                }
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

    // -------------------------------------------------------------------------
    // Key handling (9x4 layout)

    bool LockstepEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
    {
        const int rawCode = key.getKeyCode();
        const int uCode   = (rawCode >= 'a' && rawCode <= 'z')
                                ? rawCode - ('a' - 'A')
                                : rawCode;

        const auto ev = qwerty_.resolve(uCode,
                                        uiState_.funcHeld,
                                        uiState_.trackHeld,
                                        uiState_.muteHeld);

        switch (ev.button)
        {
            case ControllerButton::Func:
                uiState_.funcHeld = true;
                editMode_.onScopeEvent(ev);
                sectionBar_.repaint();
                functionBar_.repaint();
                repaint();
                return true;

            case ControllerButton::TrackScope:
                uiState_.trackHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::MuteScope:
                uiState_.muteHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::FillScope:
                uiState_.fillHeld = true;
                processor_.setFillActive(true);
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::Section:
                sectionBar_.selectSection(ev.index);
                // Track section key hold for Section-scope verb dispatch (MD.3).
                if (heldSectionRawCode_ < 0)
                {
                    heldSectionRawCode_ = rawCode;
                    editMode_.setSectionHeld(true);
                }
                return true;

            case ControllerButton::MetaSection:
                sectionBar_.selectMetaSection(ev.index);
                return true;

            case ControllerButton::Step:
            {
                // PatternScope + step: first step press queues a direct switch and
                // clears any existing chain; subsequent step presses (while still
                // holding PatternScope) append to the chain.
                if (uiState_.patternScopeHeld)
                {
                    const int bank = processor_.activeBankIdx();
                    if (!uiState_.patternScopeUsed)
                    {
                        processor_.clearChain();
                        processor_.queuePattern(bank, ev.index);
                    }
                    else
                    {
                        processor_.appendToChain(bank, ev.index);
                    }
                    uiState_.patternScopeUsed = true;
                    repaint();
                    return true;
                }

                // Ignore key-repeat (same physical key already in list).
                bool alreadyHeld = false;
                for (auto& [code, _] : heldStepKeys_)
                {
                    if (code == rawCode) { alreadyHeld = true; break; }
                }
                if (!alreadyHeld)
                {
                    const int absStep = stepGrid_.currentPage() * StepGrid::kPageSteps
                                        + ev.index;
                    heldStepKeys_.push_back({ rawCode, absStep });
                    processor_.editContext().hold(stepGrid_.getActiveTrack(), absStep);
                    editMode_.setTrigHeld(true);
                }
                return true;
            }

            case ControllerButton::SelectTrack:
                stepGrid_.setActiveTrack(ev.index);
                return true;

            case ControllerButton::NavUp:
                stepGrid_.setActiveTrack(std::max(0, stepGrid_.getActiveTrack() - 1));
                return true;

            case ControllerButton::NavDown:
                stepGrid_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1,
                             stepGrid_.getActiveTrack() + 1));
                return true;

            case ControllerButton::NavLeft:
                stepGrid_.prevPage();
                return true;

            case ControllerButton::NavRight:
                if (uiState_.patternScopeHeld)
                {
                    // PatternScope + NavRight (Func+2 + R): toggle chain loop mode.
                    processor_.setChainLoopEnabled(!processor_.chainLoopEnabled());
                    uiState_.patternScopeUsed = true;
                    repaint();
                    return true;
                }
                stepGrid_.nextPage();
                return true;

            case ControllerButton::PlayStop:
            {
                if (playKeyHeld_) return true;  // ignore key repeat
                playKeyHeld_ = true;

                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = (now - lastPlayPressTime_) < kDoublePressMsThreshold;
                lastPlayPressTime_ = now;

                if (isDouble)
                {
                    processor_.clock().setInPluginPlaying(false);
                    processor_.clock().resetPhase();
                }
                else
                {
                    processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                }
                return true;
            }

            case ControllerButton::StopReset:
                if (uiState_.patternScopeHeld)
                {
                    processor_.cancelQueuedPattern();
                    uiState_.patternScopeUsed = true;
                    repaint();
                    return true;
                }
                processor_.clock().setInPluginPlaying(false);
                processor_.clock().resetPhase();
                return true;

            case ControllerButton::VerbStop:
                editMode_.onVerb(ev.button);
                return true;

            case ControllerButton::RecordArm:
                processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                return true;

            // Scope verbs dispatched through EditMode.
            case ControllerButton::VerbRecord:
            case ControllerButton::VerbPlay:
                editMode_.onVerb(ev.button);
                return true;

            case ControllerButton::Snapshot:
                checkpointDepth_ = std::min(checkpointDepth_ + 1, 8);
                repaint();
                return true;
            case ControllerButton::Restore:
                checkpointDepth_ = std::max(checkpointDepth_ - 1, 0);
                repaint();
                return true;

            // Trig grid mode selection (Func+Y/U/I). Pressing the active mode
            // a second time resets to Default (toggle behaviour).
            case ControllerButton::TrigModeKeyboard:
            {
                const auto next = (uiState_.trigGridMode == TrigGridMode::Keyboard)
                                  ? TrigGridMode::Default : TrigGridMode::Keyboard;
                uiState_.trigGridMode = next;
                stepGrid_.repaint();
                return true;
            }
            case ControllerButton::TrigModeRetrig:
            {
                const auto next = (uiState_.trigGridMode == TrigGridMode::Retrig)
                                  ? TrigGridMode::Default : TrigGridMode::Retrig;
                uiState_.trigGridMode = next;
                stepGrid_.repaint();
                return true;
            }
            case ControllerButton::TrigModeSoundPool:
            {
                const auto next = (uiState_.trigGridMode == TrigGridMode::SoundPool)
                                  ? TrigGridMode::Default : TrigGridMode::SoundPool;
                uiState_.trigGridMode = next;
                stepGrid_.repaint();
                return true;
            }

            // MD.6/MD.7: Mute toggle.
            // A+step (no Func) → immediate global mute toggle.
            // Func+A+step → deferred pattern mute (applied atomically on Func release).
            case ControllerButton::ToggleMute:
            {
                const int trackIdx = ev.index;
                if (trackIdx < 0 || trackIdx >= static_cast<int>(kNumTracks))
                    return true;
                if (uiState_.funcHeld)
                {
                    // Deferred pattern mute (MD.7 + MD.8).
                    deferredPatternMutes_.push_back(trackIdx);
                }
                else
                {
                    // Immediate global mute (MD.6).
                    processor_.toggleGlobalMute(trackIdx);
                }
                repaint();
                return true;
            }

            case ControllerButton::ForkPart:
                processor_.forkActivePart();
                repaint();
                return true;

            case ControllerButton::MetronomeToggle:
                processor_.clock().setMetronomeEnabled(!processor_.clock().isMetronomeEnabled());
                return true;

            case ControllerButton::TapTempo:
                return true;

            case ControllerButton::PatternScope:
                uiState_.patternScopeHeld = true;
                uiState_.patternScopeUsed = false;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            default:
                return false;
        }
    }

    bool LockstepEditor::keyStateChanged(bool isKeyDown, juce::Component*)
    {
        bool handled = false;

        // Left-column modifier key releases.
        if (!isKeyDown && uiState_.funcHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('1')))
        {
            // MD.7/MD.8: apply deferred pattern mute toggles atomically.
            for (const int t : deferredPatternMutes_)
                processor_.togglePatternMute(t);
            deferredPatternMutes_.clear();

            uiState_.funcHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::Func });
            sectionBar_.repaint();
            functionBar_.repaint();
            repaint();
            handled = true;
        }

        if (!isKeyDown && uiState_.trackHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('Q')))
        {
            uiState_.trackHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::TrackScope });
            repaint();
            handled = true;
        }

        if (!isKeyDown && uiState_.muteHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('A')))
        {
            uiState_.muteHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::MuteScope });
            repaint();
            handled = true;
        }

        if (!isKeyDown && uiState_.fillHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('Z')))
        {
            uiState_.fillHeld = false;
            processor_.setFillActive(false);
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::FillScope });
            repaint();
            handled = true;
        }

        // PatternScope (Func+2) release: if no step was queued, fire Snapshot instead.
        if (!isKeyDown && uiState_.patternScopeHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('2')))
        {
            if (!uiState_.patternScopeUsed)
                checkpointDepth_ = std::min(checkpointDepth_ + 1, 8);
            uiState_.patternScopeHeld = false;
            uiState_.patternScopeUsed = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::PatternScope });
            repaint();
            handled = true;
        }

        // Section key release: clear section-held scope.
        if (heldSectionRawCode_ >= 0
            && !juce::KeyPress::isKeyCurrentlyDown(heldSectionRawCode_))
        {
            heldSectionRawCode_ = -1;
            editMode_.setSectionHeld(false);
            handled = true;
        }

        if (!isKeyDown && playKeyHeld_
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('T')))
        {
            playKeyHeld_ = false;
            handled = true;
        }

        // Step key releases: for each held step whose physical key is no longer down,
        // release it from the edit context and optionally toggle its trig.
        for (int i = static_cast<int>(heldStepKeys_.size()) - 1; i >= 0; --i)
        {
            auto [code, stepIdx] = heldStepKeys_[static_cast<std::size_t>(i)];
            if (!juce::KeyPress::isKeyCurrentlyDown(code))
            {
                auto& ctx = processor_.editContext();
                const int  track          = ctx.heldTrackIndex();
                const bool paramWasWritten = ctx.wasParamWritten();

                processor_.editContext().release(stepIdx);

                if (!paramWasWritten && track >= 0 && stepIdx >= 0)
                {
                    auto& s = processor_.sequence()
                        .tracks[static_cast<std::size_t>(track)]
                        .steps[static_cast<std::size_t>(stepIdx)];
                    s.trig = !s.trig;
                }

                heldStepKeys_.erase(heldStepKeys_.begin() + i);
                handled = true;
            }
        }
        if (handled && heldStepKeys_.empty())
        {
            editMode_.setTrigHeld(false);
        }

        return handled;
    }

    void LockstepEditor::applyDisplayMode(GridDisplayMode mode)
    {
        gridMode_ = mode;
        stepGrid_.setDisplayMode(mode);
        functionBar_.setDisplayMode(mode);
        sectionBar_.setDisplayMode(mode);

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

        static constexpr int kKeyRowAlloc = 44;
        static constexpr int kStepGridH   = 2 * (kKeyRowAlloc - 4) + 26 + 8;

        keyboard_.setBounds(bounds.removeFromBottom(72).reduced(8, 4));
        stepGrid_.setBounds(bounds.removeFromBottom(kStepGridH).reduced(8, 4));
        if (gridMode_ == GridDisplayMode::Clean)
            bounds.removeFromBottom(kClnRowGap);
        functionBar_.setBounds(bounds.removeFromBottom(kKeyRowAlloc).reduced(8, 2));
        sectionBar_.setBounds(bounds.removeFromBottom(kKeyRowAlloc).reduced(8, 2));
        {
            auto msRow = bounds.removeFromBottom(22).reduced(8, 2);
            const int colW = msRow.getWidth() / static_cast<int>(kNumTracks);
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                auto col  = msRow.removeFromLeft(colW);
                auto mute = col.removeFromLeft(col.getWidth() / 2);
                muteBtns_[i].setBounds(mute.reduced(1, 1));
                soloBtns_[i].setBounds(col.reduced(1, 1));
            }
        }
        {
            auto trackRow = bounds.removeFromBottom(22).reduced(8, 2);
            const int colW = trackRow.getWidth() / static_cast<int>(kNumTracks);
            for (std::size_t i = 0; i < kNumTracks; ++i)
                trackBtns_[i].setBounds(trackRow.removeFromLeft(colW).reduced(1, 1));
        }

        if (tempoBar_)
            tempoBar_->setBounds(bounds.removeFromTop(28).reduced(8, 2));
        bounds.removeFromTop(2);
        manipulationZone_.setBounds(bounds.reduced(8, 4));
        poolOverlay_.setBounds(manipulationZone_.getBounds()
            .withBottom(stepGrid_.getBounds().getY()));
    }

    // -------------------------------------------------------------------------
    // Verb dispatch (MB.3)

    void LockstepEditor::dispatchVerb(EditMode::PrimaryScope scope, ControllerButton verb)
    {
        using PS = EditMode::PrimaryScope;
        using CB = ControllerButton;

        switch (scope)
        {
            // -----------------------------------------------------------------------
            // MD.2  Step copy / paste / clear
            // -----------------------------------------------------------------------
            case PS::Trig:
            {
                const auto& ctx = processor_.editContext();
                if (!ctx.isActiveForEditing()) break;
                const int track = ctx.heldTrackIndex();
                auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(track)];

                if (verb == CB::VerbRecord)
                {
                    const auto& held = ctx.heldSteps();
                    if (held.empty()) break;
                    int anchor = *std::min_element(held.begin(), held.end());
                    clipboard_.stepEntries.clear();
                    for (int idx : held)
                    {
                        if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                        clipboard_.stepEntries.push_back(
                            { idx - anchor, trk.steps[static_cast<std::size_t>(idx)] });
                    }
                    std::sort(clipboard_.stepEntries.begin(), clipboard_.stepEntries.end(),
                              [](const StepClipEntry& a, const StepClipEntry& b)
                              { return a.relOffset < b.relOffset; });
                    clipboard_.type = ClipboardType::Step;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Step) break;
                    const int anchor  = ctx.heldStepIndex();
                    const int trkLen  = trk.length;
                    for (const auto& entry : clipboard_.stepEntries)
                    {
                        int dst = (anchor + entry.relOffset);
                        dst = ((dst % trkLen) + trkLen) % trkLen;
                        trk.steps[static_cast<std::size_t>(dst)] = entry.data;
                    }
                }
                else if (verb == CB::VerbStop)
                {
                    for (int idx : ctx.heldSteps())
                    {
                        processor_.clearStepLocks(track, idx);
                        auto& s = trk.steps[static_cast<std::size_t>(idx)];
                        s.trig      = false;
                        s.condition = TrigCondition{};
                    }
                }
                break;
            }

            // -----------------------------------------------------------------------
            // MD.3  Section copy / paste / clear
            // -----------------------------------------------------------------------
            case PS::Section:
            {
                const int activeTrack = stepGrid_.getActiveTrack();
                const int secIdx      = uiState_.trackSection[static_cast<std::size_t>(activeTrack)];
                auto& trk = processor_.sequence()
                                .tracks[static_cast<std::size_t>(activeTrack)];
                const int trkLen    = trk.length;
                const int numSlots  = processor_.numParams(activeTrack);

                if (verb == CB::VerbRecord)
                {
                    clipboard_.sectionSlots.clear();
                    clipboard_.sectionTrackLength = trkLen;
                    for (int sl = 0; sl < numSlots; ++sl)
                    {
                        if (processor_.paramSpec(activeTrack, sl).sectionIndex != secIdx)
                            continue;
                        SectionClipSlot entry;
                        entry.slot = sl;
                        entry.perStep.reserve(static_cast<std::size_t>(trkLen));
                        for (int st = 0; st < trkLen; ++st)
                        {
                            const auto& plock = trk.steps[static_cast<std::size_t>(st)].overrides;
                            const bool  has   = plock.has(sl);
                            entry.perStep.push_back({ has, has ? plock.get(sl, 0.0f) : 0.0f });
                        }
                        clipboard_.sectionSlots.push_back(std::move(entry));
                    }
                    clipboard_.type = ClipboardType::Section;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Section) break;
                    for (const auto& entry : clipboard_.sectionSlots)
                    {
                        const int steps = std::min(static_cast<int>(entry.perStep.size()), trkLen);
                        for (int st = 0; st < steps; ++st)
                        {
                            auto& plock = trk.steps[static_cast<std::size_t>(st)].overrides;
                            if (entry.perStep[static_cast<std::size_t>(st)].first)
                                plock.set(entry.slot, entry.perStep[static_cast<std::size_t>(st)].second);
                            else
                                plock.clear(entry.slot);
                        }
                    }
                }
                else if (verb == CB::VerbStop)
                {
                    for (int sl = 0; sl < numSlots; ++sl)
                    {
                        if (processor_.paramSpec(activeTrack, sl).sectionIndex != secIdx)
                            continue;
                        for (int st = 0; st < trkLen; ++st)
                            trk.steps[static_cast<std::size_t>(st)].overrides.clear(sl);
                    }
                }
                break;
            }

            // -----------------------------------------------------------------------
            // MD.4  Track copy / paste / clear
            // -----------------------------------------------------------------------
            case PS::Track:
            {
                const int activeTrack = stepGrid_.getActiveTrack();
                auto& trk = processor_.sequence()
                                .tracks[static_cast<std::size_t>(activeTrack)];

                if (verb == CB::VerbRecord)
                {
                    clipboard_.clipTrack = trk;
                    clipboard_.type      = ClipboardType::Track;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Track) break;
                    trk = clipboard_.clipTrack;
                }
                else if (verb == CB::VerbStop)
                {
                    // Reset all steps; preserve length, divider, and base params.
                    for (auto& s : trk.steps)
                    {
                        s.trig       = false;
                        s.condition  = TrigCondition{};
                        s.overrides  = PLock{};
                        s.trigOverride = TrigOverride{};
                    }
                }
                break;
            }

            // -----------------------------------------------------------------------
            // MD.5  Pattern copy / paste / clear  (Pattern+Record = copy, not fork)
            // -----------------------------------------------------------------------
            case PS::Pattern:
            {
                auto& pat = processor_.activePattern();

                if (verb == CB::VerbRecord)
                {
                    clipboard_.clipSequence    = pat.sequence;
                    clipboard_.clipPatternMutes = pat.patternMutes;
                    clipboard_.type            = ClipboardType::Pattern;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Pattern) break;
                    pat.sequence     = clipboard_.clipSequence;
                    pat.patternMutes = clipboard_.clipPatternMutes;
                }
                else if (verb == CB::VerbStop)
                {
                    for (auto& trk : pat.sequence.tracks)
                    {
                        for (auto& s : trk.steps)
                        {
                            s.trig       = false;
                            s.condition  = TrigCondition{};
                            s.overrides  = PLock{};
                            s.trigOverride = TrigOverride{};
                        }
                    }
                    pat.patternMutes.fill(false);
                }
                break;
            }

            case PS::Func:
            case PS::Mute:
            case PS::Fill:
                break;

            case PS::None:
            {
                // VerbStop with no scope: clear the active-slot P-Lock.
                if (verb == CB::VerbStop)
                {
                    const auto& ctx = processor_.editContext();
                    if (ctx.isActiveForEditing() && ctx.activeSlot() >= 0)
                        processor_.clearParam(ctx.heldTrackIndex(),
                                              ctx.heldStepIndex(),
                                              ctx.activeSlot());
                }
                break;
            }

            default:
                break;
        }

        // Chrome must repaint after any verb that may change clipboard or checkpoint state.
        repaint();
    }
}
