#include "PluginEditor.h"

namespace lockstep
{
    LockstepEditor::LockstepEditor(LockstepProcessor& proc)
        : juce::AudioProcessorEditor(&proc),
          processor_(proc),
          stepGrid_(proc),
          manipulationZone_(proc, stepGrid_)
    {
        addAndMakeVisible(pageBar_);
        addAndMakeVisible(manipulationZone_);
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
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
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
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(18.0f)));
        auto header = getLocalBounds().removeFromTop(36).reduced(12, 0);
        g.drawText("Lockstep - Skeleton", header, juce::Justification::centredLeft);

        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(juce::Colour::fromRGB(140, 160, 180));
        g.drawText(sampleStatus_, getLocalBounds().removeFromTop(56).removeFromBottom(20).reduced(12, 0),
                   juce::Justification::centredLeft);
        juce::ignoreUnused(processor_);
    }

    bool LockstepEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
    {
        const int rawCode = key.getKeyCode();
        const int code = (rawCode >= 'a' && rawCode <= 'z')
                             ? rawCode - ('a' - 'A')
                             : rawCode;

        const auto mapping = qwerty_.resolve(code);
        if (mapping.action == QwertyOverlay::Action::Step)
        {
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
        }
        if (mapping.action == QwertyOverlay::Action::Clear)
        {
            const auto& ctx = processor_.editContext();
            if (ctx.isActiveForEditing() && ctx.activeSlot() >= 0)
                processor_.clearParam(ctx.heldTrackIndex(),
                                      ctx.heldStepIndex(),
                                      ctx.activeSlot());
            return true;
        }

        return false;
    }

    bool LockstepEditor::keyStateChanged(bool isKeyDown, juce::Component*)
    {
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
        auto header = bounds.removeFromTop(36);
        header.removeFromLeft(200);
        loadButton_.setBounds(header.reduced(4));
        bounds.removeFromTop(20);                                      // status line

        pageBar_.setBounds(bounds.removeFromTop(40).reduced(8, 4));
        keyboard_.setBounds(bounds.removeFromBottom(72).reduced(8, 4));
        stepGrid_.setBounds(bounds.removeFromBottom(180).reduced(8, 4));
        manipulationZone_.setBounds(bounds.reduced(8, 4));
    }
}
