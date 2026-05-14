#include "SamplePoolOverlay.h"
#include "../PluginProcessor.h"
#include <algorithm>

namespace lockstep
{
    SamplePoolOverlay::SamplePoolOverlay(LockstepProcessor& processor)
        : processor_(processor)
    {
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colour::fromRGB(22, 26, 32));
        list_.setColour(juce::ListBox::outlineColourId,    juce::Colour::fromRGB(55, 65, 80));
        list_.setOutlineThickness(1);
        list_.setRowHeight(22);
        list_.setWantsKeyboardFocus(false);
        addAndMakeVisible(list_);

        loadBtn_.setWantsKeyboardFocus(false);
        loadBtn_.onClick = [this]
        {
            fileChooser_ = std::make_unique<juce::FileChooser>(
                "Load Sample(s)",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif;*.flac;*.ogg");
            fileChooser_->launchAsync(
                juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::canSelectMultipleItems,
                [this](const juce::FileChooser& fc)
                {
                    for (const auto& f : fc.getResults())
                        processor_.samplePool().load(f.getFullPathName());
                    list_.updateContent();
                });
        };
        addAndMakeVisible(loadBtn_);

        removeBtn_.setWantsKeyboardFocus(false);
        removeBtn_.onClick = [this]
        {
            const int row = list_.getSelectedRow();
            if (row < 0 || row >= processor_.samplePool().size())
                return;
            processor_.removeSample(row);
            list_.updateContent();
            list_.selectRow(std::max(0, row - 1));
        };
        addAndMakeVisible(removeBtn_);

        upBtn_.setWantsKeyboardFocus(false);
        upBtn_.onClick = [this]
        {
            const int row = list_.getSelectedRow();
            if (row <= 0 || row >= processor_.samplePool().size())
                return;
            processor_.swapSamples(row, row - 1);
            list_.updateContent();
            list_.selectRow(row - 1);
        };
        addAndMakeVisible(upBtn_);

        downBtn_.setWantsKeyboardFocus(false);
        downBtn_.onClick = [this]
        {
            const int row = list_.getSelectedRow();
            if (row < 0 || row >= processor_.samplePool().size() - 1)
                return;
            processor_.swapSamples(row, row + 1);
            list_.updateContent();
            list_.selectRow(row + 1);
        };
        addAndMakeVisible(downBtn_);

        closeBtn_.setWantsKeyboardFocus(false);
        closeBtn_.onClick = [this]
        {
            if (onClose) onClose();
        };
        addAndMakeVisible(closeBtn_);

        startTimerHz(10);
    }

    SamplePoolOverlay::~SamplePoolOverlay()
    {
        stopTimer();
    }

    void SamplePoolOverlay::timerCallback()
    {
        list_.updateContent();
    }

    int SamplePoolOverlay::getNumRows()
    {
        return processor_.samplePool().size();
    }

    void SamplePoolOverlay::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                             int width, int height, bool rowIsSelected)
    {
        if (rowIsSelected)
        {
            g.setColour(juce::Colour::fromRGB(50, 80, 120));
            g.fillAll();
        }

        const auto* sample = processor_.samplePool().get(rowNumber);
        if (sample == nullptr)
            return;

        const juce::File f(juce::String(sample->ref.path));
        const juce::String index   = juce::String(rowNumber) + ".  ";
        const juce::String name    = f.getFileNameWithoutExtension();
        const juce::String dirHint = f.getParentDirectory().getFileName();

        const int textY = (height - 13) / 2;

        g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        g.setColour(juce::Colours::white);
        g.drawText(index + name,
                   juce::Rectangle<int>(6, textY, width - 12, 14),
                   juce::Justification::centredLeft);

        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(juce::Colour::fromRGB(120, 140, 160));
        g.drawText(dirHint,
                   juce::Rectangle<int>(6, textY, width - 12, 14),
                   juce::Justification::centredRight);
    }

    void SamplePoolOverlay::listBoxItemClicked(int rowNumber, const juce::MouseEvent& /*e*/)
    {
        if (rowNumber < 0 || rowNumber >= processor_.samplePool().size())
            return;
        const int track = getActiveTrack ? getActiveTrack() : 0;
        processor_.triggerPreview(rowNumber, std::max(0, track));
    }

    void SamplePoolOverlay::listBoxItemDoubleClicked(int rowNumber, const juce::MouseEvent& /*e*/)
    {
        if (rowNumber < 0 || rowNumber >= processor_.samplePool().size())
            return;
        const int track = getActiveTrack ? getActiveTrack() : 0;
        if (track < 0) return;
        const int sampleSlot = processor_.slotForId(track, "sample_id");
        if (sampleSlot < 0) return;
        processor_.writeParam(track, sampleSlot, static_cast<float>(rowNumber));
    }

    void SamplePoolOverlay::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colour::fromRGB(18, 22, 28).withAlpha(0.97f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        g.setColour(juce::Colour::fromRGB(255, 180, 50));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.5f);

        auto titleArea = getLocalBounds().removeFromTop(24).reduced(8, 0);
        g.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
        g.setColour(juce::Colour::fromRGB(255, 180, 50));
        g.drawText("SAMPLE POOL", titleArea, juce::Justification::centredLeft);

        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(juce::Colour::fromRGB(120, 140, 160));
        g.drawText("click: preview    dbl-click: assign to track  (p-lock if step held)",
                   titleArea, juce::Justification::centredRight);
    }

    void SamplePoolOverlay::resized()
    {
        auto bounds = getLocalBounds().reduced(6);

        // Title strip (painted in paint(), just consume the space)
        bounds.removeFromTop(22);

        // Bottom button row
        auto btnRow = bounds.removeFromBottom(26);
        closeBtn_.setBounds(btnRow.removeFromRight(26).reduced(1));
        btnRow.removeFromRight(4);
        downBtn_.setBounds(btnRow.removeFromRight(26).reduced(1));
        upBtn_.setBounds(btnRow.removeFromRight(26).reduced(1));
        btnRow.removeFromRight(4);
        removeBtn_.setBounds(btnRow.removeFromRight(60).reduced(1));
        loadBtn_.setBounds(btnRow.removeFromLeft(70).reduced(1));

        bounds.removeFromBottom(4);
        list_.setBounds(bounds);
    }
}
