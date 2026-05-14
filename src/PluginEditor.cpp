#include "PluginEditor.h"
#include "ParameterIDs.h"

namespace lockstep
{
    LockstepEditor::LockstepEditor(LockstepProcessor& proc)
        : juce::AudioProcessorEditor(&proc),
          processor_(proc),
          transport_(proc.clock()),
          stepGrid_(proc),
          manipulationZone_(proc, stepGrid_),
          sectionBar_(proc, stepGrid_, uiState_)
    {
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

        // Wire section-change callbacks → update ManipulationZone.
        sectionBar_.onSectionChanged = [this](int /*section*/, int /*page*/, int firstSlot)
        {
            manipulationZone_.setSlotOffset(firstSlot);
        };
        sectionBar_.onMetaSectionChanged = [this](int metaSection)
        {
            manipulationZone_.setMetaSection(metaSection);
        };

        // When the active track changes, repaint the SectionBar (labels/state change
        // per track) and re-sync the MZ slot offset to the new track's active section.
        stepGrid_.onActiveTrackChanged = [this](int /*newTrack*/)
        {
            sectionBar_.syncToActiveTrack();
        };

        addAndMakeVisible(manipulationZone_);
        addAndMakeVisible(sectionBar_);
        addAndMakeVisible(stepGrid_);
        addAndMakeVisible(keyboard_);
        addAndMakeVisible(loadButton_);

        loadButton_.onClick = [this]
        {
            fileChooser_ = std::make_unique<juce::FileChooser>(
                "Load Sample",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif;*.flac;*.ogg");

            fileChooser_->launchAsync(
                juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    const auto results = fc.getResults();
                    if (results.isEmpty())
                        return;

                    const int idx = processor_.samplePool().load(results[0].getFullPathName());
                    if (idx >= 0)
                    {
                        const int n = processor_.samplePool().size();
                        sampleStatus_ = juce::String(n) + " sample"
                            + (n == 1 ? "" : "s") + " loaded  |  last: "
                            + results[0].getFileName();
                    }
                    else
                    {
                        sampleStatus_ = "Load failed: " + results[0].getFileName();
                    }
                    repaint();
                });
        };

        setSize(720, 440);
        setWantsKeyboardFocus(true);
        // Key listener is registered on the top-level window in
        // parentHierarchyChanged(), not here, so focus changes among child
        // components cannot interrupt key-up routing.
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

        // When switching from Locked+hosted to Auto, wire up a Play click
        // that first resets phase then enables in-plugin playback — this
        // makes Auto mode start from step 0, not from wherever the DAW is.
        // (InPluginTransport already calls setInPluginPlaying; no extra wiring needed.)
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

        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(juce::Colour::fromRGB(140, 160, 180));
        g.drawText(sampleStatus_,
                   getLocalBounds().removeFromTop(36).reduced(12, 0),
                   juce::Justification::centredLeft);
        juce::ignoreUnused(processor_);
    }

    bool LockstepEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
    {
        const int rawCode = key.getKeyCode();
        const int code = (rawCode >= 'a' && rawCode <= 'z')
                             ? rawCode - ('a' - 'A')
                             : rawCode;

        const auto mapping = qwerty_.resolve(code, uiState_.shiftHeld);

        switch (mapping.action)
        {
            case QwertyOverlay::Action::Shift:
                uiState_.shiftHeld = true;
                sectionBar_.repaint();
                return true;

            case QwertyOverlay::Action::SelectSection:
                sectionBar_.selectSection(mapping.stepIndex);
                return true;

            case QwertyOverlay::Action::SelectMetaSection:
                sectionBar_.selectMetaSection(mapping.stepIndex);
                return true;

            case QwertyOverlay::Action::Step:
                if (heldStepKey_ != rawCode)
                {
                    // First press (not a key-repeat): engage hold. Trig toggle
                    // happens on release, unless a P-Lock is applied during hold.
                    heldStepKey_ = rawCode;
                    const int absStep = stepGrid_.currentPage() * StepGrid::kPageSteps
                                        + mapping.stepIndex;
                    processor_.editContext().hold(stepGrid_.getActiveTrack(), absStep);
                }
                return true;

            case QwertyOverlay::Action::NavLeft:
                stepGrid_.prevPage();
                return true;

            case QwertyOverlay::Action::NavRight:
                stepGrid_.nextPage();
                return true;

            case QwertyOverlay::Action::SelectTrack:
                stepGrid_.setActiveTrack(mapping.stepIndex);
                return true;

            case QwertyOverlay::Action::NavUp:
                stepGrid_.setActiveTrack(std::max(0, stepGrid_.getActiveTrack() - 1));
                return true;

            case QwertyOverlay::Action::NavDown:
                stepGrid_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1,
                             stepGrid_.getActiveTrack() + 1));
                return true;

            case QwertyOverlay::Action::PlayStop:
                processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                return true;

            case QwertyOverlay::Action::Clear:
            {
                const auto& ctx = processor_.editContext();
                if (ctx.isActiveForEditing() && ctx.activeSlot() >= 0)
                    processor_.clearParam(ctx.heldTrackIndex(),
                                          ctx.heldStepIndex(),
                                          ctx.activeSlot());
                return true;
            }

            // Reserved: implementations land in later milestones.
            case QwertyOverlay::Action::RecordArm:
            case QwertyOverlay::Action::TapTempo:
            case QwertyOverlay::Action::Copy:
            case QwertyOverlay::Action::Paste:
                return true;

            case QwertyOverlay::Action::None:
            default:
                return false;
        }
    }

    bool LockstepEditor::keyStateChanged(bool isKeyDown, juce::Component*)
    {
        // Track shift-key release (key '1').
        if (!isKeyDown && uiState_.shiftHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('1')))
        {
            uiState_.shiftHeld = false;
            sectionBar_.repaint();
        }

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
            return true;
        }
        return false;
    }

    void LockstepEditor::resized()
    {
        auto bounds = getLocalBounds();

        // Header row: transport | sync mode box | [status text area] | load button
        auto header = bounds.removeFromTop(36);
        transport_.setBounds(header.removeFromLeft(108).reduced(4));
        syncModeBox_.setBounds(header.removeFromLeft(80).reduced(4));
        channelModeBox_.setBounds(header.removeFromLeft(90).reduced(4));
        loadButton_.setBounds(header.removeFromRight(160).reduced(4));
        // Remaining header area is drawn as status text in paint()

        // Optional standalone tempo bar directly below the header
        if (tempoBar_)
            tempoBar_->setBounds(bounds.removeFromTop(28).reduced(8, 2));

        bounds.removeFromTop(4);  // small gap

        // Encoder strip (ManipulationZone) at the top, mirroring the hardware encoder row.
        manipulationZone_.setBounds(bounds.removeFromTop(96).reduced(8, 4));

        // Section bar below the encoders.
        sectionBar_.setBounds(bounds.removeFromTop(48).reduced(8, 2));

        keyboard_.setBounds(bounds.removeFromBottom(72).reduced(8, 4));
        stepGrid_.setBounds(bounds.reduced(8, 4));
    }
}
