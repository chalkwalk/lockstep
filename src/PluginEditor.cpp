#include "PluginEditor.h"
#include "ParameterIDs.h"

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

        setSize(880, 480);
        setWantsKeyboardFocus(true);
    }

    LockstepEditor::~LockstepEditor()
    {
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
                return true;

            case ControllerButton::TrackScope:
                uiState_.trackHeld = true;
                editMode_.onScopeEvent(ev);
                return true;

            case ControllerButton::MuteScope:
                uiState_.muteHeld = true;
                editMode_.onScopeEvent(ev);
                return true;

            case ControllerButton::FillScope:
                uiState_.fillHeld = true;
                editMode_.onScopeEvent(ev);
                return true;

            case ControllerButton::Section:
                sectionBar_.selectSection(ev.index);
                return true;

            case ControllerButton::MetaSection:
                sectionBar_.selectMetaSection(ev.index);
                return true;

            case ControllerButton::Step:
            {
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
                stepGrid_.nextPage();
                return true;

            case ControllerButton::PlayStop:
                processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                return true;

            case ControllerButton::StopReset:
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

            // Checkpoint verbs.
            case ControllerButton::Yes:
            case ControllerButton::No:
                editMode_.onVerb(ev.button);
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

            // Mute toggle (Mute+step) — reserved for MD.
            case ControllerButton::ToggleMute:
                return true;

            case ControllerButton::TapTempo:
            case ControllerButton::PatternScope:
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
            uiState_.funcHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::Func });
            sectionBar_.repaint();
            functionBar_.repaint();
            handled = true;
        }

        if (!isKeyDown && uiState_.trackHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('Q')))
        {
            uiState_.trackHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::TrackScope });
            handled = true;
        }

        if (!isKeyDown && uiState_.muteHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('A')))
        {
            uiState_.muteHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::MuteScope });
            handled = true;
        }

        if (!isKeyDown && uiState_.fillHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('Z')))
        {
            uiState_.fillHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::FillScope });
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
            case PS::Trig:
            {
                const auto& ctx = processor_.editContext();
                if (!ctx.isActiveForEditing()) break;
                const int track = ctx.heldTrackIndex();
                const int step  = ctx.heldStepIndex();

                if (verb == CB::VerbStop)
                {
                    // Clear all P-Locks and trig overrides on the held step.
                    processor_.clearStepLocks(track, step);
                }
                else if (verb == CB::VerbRecord)
                {
                    // Stub: copy step into clipboard (MD implements full copy).
                    clipboardType_ = ClipboardType::Step;
                }
                else if (verb == CB::VerbPlay)
                {
                    // Stub: paste clipboard onto step (MD implements full paste).
                }
                break;
            }

            case PS::Track:
            {
                if (verb == CB::VerbRecord)      { clipboardType_ = ClipboardType::Track; }
                else if (verb == CB::VerbPlay)   { /* stub: paste track */ }
                else if (verb == CB::VerbStop)   { /* stub: clear track */ }
                break;
            }

            case PS::Pattern:
            {
                if (verb == CB::VerbRecord)      { clipboardType_ = ClipboardType::Pattern; }
                else if (verb == CB::VerbPlay)   { /* stub: paste pattern */ }
                else if (verb == CB::VerbStop)   { /* stub: clear pattern */ }
                break;
            }

            case PS::Section:
            {
                if (verb == CB::VerbRecord)      { clipboardType_ = ClipboardType::Section; }
                else if (verb == CB::VerbPlay)   { /* stub: paste section */ }
                else if (verb == CB::VerbStop)   { /* stub: clear section */ }
                break;
            }

            case PS::Func:
            {
                // Func + Yes/No = checkpoint push/pop (MD implements real stack).
                if (verb == CB::Yes)
                {
                    checkpointDepth_ = std::min(checkpointDepth_ + 1, 8);
                }
                else if (verb == CB::No)
                {
                    checkpointDepth_ = std::max(checkpointDepth_ - 1, 0);
                }
                break;
            }

            case PS::Mute:
            case PS::Fill:
                // Verb with mute/fill scope reserved for MD.
                break;

            case PS::None:
            {
                // VerbStop with no scope: clear the active-slot P-Lock (legacy behaviour).
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
    }
}
