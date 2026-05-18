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

        // Wire verb dispatch callback (real handlers land in MB.3 and later milestones).
        editMode_.onVerbDispatched = [](EditMode::PrimaryScope /*scope*/,
                                        ControllerButton /*verb*/) {};

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
                if (heldStepKey_ != rawCode)
                {
                    heldStepKey_ = rawCode;
                    const int absStep = stepGrid_.currentPage() * StepGrid::kPageSteps
                                        + ev.index;
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
            {
                const auto& ctx = processor_.editContext();
                if (ctx.isActiveForEditing() && ctx.activeSlot() >= 0)
                    processor_.clearParam(ctx.heldTrackIndex(),
                                          ctx.heldStepIndex(),
                                          ctx.activeSlot());
                return true;
            }

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

            // Trig grid modes — reserved for MB.5.
            case ControllerButton::TrigModeKeyboard:
            case ControllerButton::TrigModeRetrig:
            case ControllerButton::TrigModeSoundPool:
                return true;

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

        // Step key release: toggle trig or commit P-Lock hold.
        if (!isKeyDown && heldStepKey_ != -1
            && !juce::KeyPress::isKeyCurrentlyDown(heldStepKey_))
        {
            const auto& ctx = processor_.editContext();
            const int track = ctx.heldTrackIndex();
            const int step  = ctx.heldStepIndex();
            const bool shouldToggle = !ctx.wasParamWritten();

            processor_.editContext().release();

            if (shouldToggle && track >= 0 && step >= 0)
            {
                auto& s = processor_.sequence()
                    .tracks[static_cast<std::size_t>(track)]
                    .steps[static_cast<std::size_t>(step)];
                s.trig = !s.trig;
            }

            heldStepKey_ = -1;
            editMode_.setTrigHeld(false);
            handled = true;
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
}
