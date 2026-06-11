#include "SoundBankOverlay.h"
#include "../PluginProcessor.h"

namespace lockstep
{
    SoundBankOverlay::SoundBankOverlay(LockstepProcessor& processor)
        : processor_(processor)
    {
        model_.owner = this;
        listBox_.setModel(&model_);
        listBox_.setRowHeight(22);
        listBox_.setWantsKeyboardFocus(false);
        addAndMakeVisible(listBox_);

        saveBtn_.setWantsKeyboardFocus(false);
        saveBtn_.onClick = [this] { onSaveClicked(); };
        addAndMakeVisible(saveBtn_);

        closeBtn_.setWantsKeyboardFocus(false);
        closeBtn_.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(closeBtn_);
    }

    void SoundBankOverlay::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(28, 32, 40));
        g.setColour(juce::Colour::fromRGB(60, 70, 90));
        g.drawRect(getLocalBounds(), 1);
        g.setColour(juce::Colour::fromRGB(180, 200, 220));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("Sound Bank", getLocalBounds().withHeight(20), juce::Justification::centred);
    }

    void SoundBankOverlay::resized()
    {
        auto r = getLocalBounds().reduced(4);
        r.removeFromTop(20);
        auto topRow = r.removeFromTop(24);
        saveBtn_.setBounds(topRow.removeFromLeft(topRow.getWidth() - 24).reduced(2, 1));
        closeBtn_.setBounds(topRow.reduced(2, 1));
        listBox_.setBounds(r.reduced(0, 2));
    }

    void SoundBankOverlay::refresh()
    {
        listBox_.updateContent();
        listBox_.repaint();
    }

    void SoundBankOverlay::onSaveClicked()
    {
        const int track = getActiveTrack ? getActiveTrack() : 0;
        const juce::String name = "Sound " + juce::String(processor_.soundPoolSize() + 1);
        processor_.saveTrackToSoundPool(track, name.toStdString());
        refresh();
    }

    void SoundBankOverlay::onRecallClicked(int entryIndex)
    {
        const int track = getActiveTrack ? getActiveTrack() : 0;
        processor_.recallSoundFromPool(track, entryIndex);
    }

    void SoundBankOverlay::onDeleteClicked(int entryIndex)
    {
        processor_.removeSoundEntry(entryIndex);
        refresh();
    }

    // -------------------------------------------------------------------------
    // ListBoxModel

    int SoundBankOverlay::Model::getNumRows()
    {
        return owner ? owner->processor_.soundPoolSize() : 0;
    }

    void SoundBankOverlay::Model::paintListBoxItem(int row, juce::Graphics& g,
                                                   int w, int h, bool selected)
    {
        if (!owner) return;
        const auto* e = owner->processor_.soundPoolEntry(row);
        if (!e) return;

        if (selected)
            g.fillAll(juce::Colour::fromRGB(50, 70, 100));
        else if (row % 2 == 0)
            g.fillAll(juce::Colour::fromRGB(32, 36, 44));
        else
            g.fillAll(juce::Colour::fromRGB(36, 40, 50));

        g.setColour(juce::Colour::fromRGB(180, 200, 220));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const auto nameRect = juce::Rectangle<int>(4, 0, w - 70, h);
        g.drawText(juce::String(row + 1) + "  " + juce::String(e->name),
                   nameRect, juce::Justification::centredLeft, true);

        // Recall / Delete buttons drawn as text hints.
        g.setColour(juce::Colour::fromRGB(100, 160, 100));
        g.drawText("[Recall]", juce::Rectangle<int>(w - 68, 0, 40, h),
                   juce::Justification::centred);
        g.setColour(juce::Colour::fromRGB(180, 80, 80));
        g.drawText("[Del]", juce::Rectangle<int>(w - 28, 0, 28, h),
                   juce::Justification::centred);
    }

    void SoundBankOverlay::Model::listBoxItemDoubleClicked(int row,
                                                           const juce::MouseEvent&)
    {
        if (owner) owner->onRecallClicked(row);
    }

    juce::Component* SoundBankOverlay::Model::refreshComponentForRow(int, bool,
                                                                     juce::Component* existing)
    {
        return existing;
    }
}
