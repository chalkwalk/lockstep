#include "MachineSelectOverlay.h"
#include "../PluginProcessor.h"

namespace lockstep
{
    MachineSelectOverlay::MachineSelectOverlay(LockstepProcessor& processor)
        : processor_(processor)
    {
        model_.owner = this;
        listBox_.setModel(&model_);
        listBox_.setRowHeight(24);
        listBox_.setWantsKeyboardFocus(false);
        addAndMakeVisible(listBox_);

        closeBtn_.setWantsKeyboardFocus(false);
        closeBtn_.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(closeBtn_);

        for (int i = 0; i < static_cast<int>(partBtns_.size()); ++i)
        {
            auto& btn = partBtns_[static_cast<std::size_t>(i)];
            btn.setButtonText("Part " + juce::String(i + 1));
            btn.setWantsKeyboardFocus(false);
            btn.onClick = [this, i] {
                processor_.setActivePatternPart(i);
                refresh();
            };
            addAndMakeVisible(btn);
        }
    }

    void MachineSelectOverlay::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(28, 32, 40));
        g.setColour(juce::Colour::fromRGB(60, 70, 90));
        g.drawRect(getLocalBounds(), 1);
        g.setColour(juce::Colour::fromRGB(180, 200, 220));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("Machine Select", getLocalBounds().withHeight(20),
                   juce::Justification::centred);
    }

    void MachineSelectOverlay::resized()
    {
        auto r = getLocalBounds().reduced(4);
        r.removeFromTop(20);

        auto topRow = r.removeFromTop(24);
        closeBtn_.setBounds(topRow.removeFromRight(24).reduced(2, 1));

        // Part selector row
        auto partRow = r.removeFromTop(26);
        const int partW = partRow.getWidth() / static_cast<int>(partBtns_.size());
        for (auto& btn : partBtns_)
            btn.setBounds(partRow.removeFromLeft(partW).reduced(2, 1));

        listBox_.setBounds(r.reduced(0, 2));
    }

    void MachineSelectOverlay::visibilityChanged()
    {
        if (isVisible()) refresh();
    }

    void MachineSelectOverlay::refresh()
    {
        // Highlight the active Part button.
        const int activePart = processor_.activePatternPartRef();
        for (int i = 0; i < static_cast<int>(partBtns_.size()); ++i)
        {
            auto& btn = partBtns_[static_cast<std::size_t>(i)];
            btn.setToggleState(i == activePart, juce::dontSendNotification);
        }

        listBox_.updateContent();
        listBox_.repaint();
    }

    void MachineSelectOverlay::onMachineRowClicked(int row)
    {
        const int track = getActiveTrack ? getActiveTrack() : 0;
        const auto info = processor_.availableMachineInfo(row);
        if (info.id && info.id[0] != '\0')
        {
            processor_.setTrackMachine(track, info.id);
            if (onClose) onClose();
        }
    }

    // -------------------------------------------------------------------------
    // ListBoxModel

    int MachineSelectOverlay::Model::getNumRows()
    {
        return owner ? owner->processor_.numAvailableMachines() : 0;
    }

    void MachineSelectOverlay::Model::paintListBoxItem(int row, juce::Graphics& g,
                                                        int w, int h, bool selected)
    {
        if (!owner) return;
        const auto info = owner->processor_.availableMachineInfo(row);
        if (!info.id) return;

        const int track      = owner->getActiveTrack ? owner->getActiveTrack() : 0;
        const auto currentId = owner->processor_.getMachineId(track);
        const bool isCurrent = (currentId == juce::String(info.id));

        if (isCurrent)
            g.fillAll(juce::Colour::fromRGB(40, 80, 60));
        else if (selected)
            g.fillAll(juce::Colour::fromRGB(50, 70, 100));
        else if (row % 2 == 0)
            g.fillAll(juce::Colour::fromRGB(32, 36, 44));
        else
            g.fillAll(juce::Colour::fromRGB(36, 40, 50));

        g.setColour(isCurrent ? juce::Colour::fromRGB(120, 220, 160)
                              : juce::Colour::fromRGB(180, 200, 220));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(juce::String(info.displayName),
                   juce::Rectangle<int>(8, 0, w - 8, h),
                   juce::Justification::centredLeft, true);
    }

    void MachineSelectOverlay::Model::listBoxItemClicked(int row,
                                                          const juce::MouseEvent&)
    {
        if (owner) owner->onMachineRowClicked(row);
    }
}
