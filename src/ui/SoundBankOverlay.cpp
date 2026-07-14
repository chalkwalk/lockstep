#include "SoundBankOverlay.h"
#include "../PluginProcessor.h"
#include "../command/StatusText.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // Row component

    SoundBankOverlay::Row::Row(SoundBankOverlay& o) : owner(o)
    {
        nameLabel_.setEditable(false, true);  // double-click edits
        nameLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 200, 220));
        nameLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        nameLabel_.setFont(juce::Font(juce::FontOptions(10.0f)));
        nameLabel_.onTextChange = [this] { owner.doRename(rowIndex, nameLabel_.getText()); };
        addAndMakeVisible(nameLabel_);

        // semantic colour: green = safe (recall), red = destructive (delete). The
        // colour is the warning, and a shared chrome fill would erase the distinction
        // between the two buttons sitting side by side on every row.
        recallBtn_.setColour(juce::TextButton::buttonColourId,
                             juce::Colour::fromRGB(40, 80, 40));
        recallBtn_.setColour(juce::TextButton::textColourOffId,
                             juce::Colour::fromRGB(100, 200, 100));
        recallBtn_.setWantsKeyboardFocus(false);
        recallBtn_.onClick = [this] { owner.doRecall(rowIndex); };
        addAndMakeVisible(recallBtn_);

        // semantic colour: destructive (see above).
        delBtn_.setColour(juce::TextButton::buttonColourId,
                          juce::Colour::fromRGB(80, 30, 30));
        delBtn_.setColour(juce::TextButton::textColourOffId,
                          juce::Colour::fromRGB(200, 80, 80));
        delBtn_.setWantsKeyboardFocus(false);
        delBtn_.onClick = [this] { owner.doDelete(rowIndex); };
        addAndMakeVisible(delBtn_);
    }

    void SoundBankOverlay::Row::resized()
    {
        auto r = getLocalBounds();
        delBtn_.setBounds(r.removeFromRight(32).reduced(1));
        recallBtn_.setBounds(r.removeFromRight(48).reduced(1));
        nameLabel_.setBounds(r.reduced(2, 1));
    }

    void SoundBankOverlay::Row::mouseDown(const juce::MouseEvent& e)
    {
        // Click anywhere on the row outside the buttons = recall.
        if (!recallBtn_.getBounds().contains(e.getPosition()) &&
            !delBtn_.getBounds().contains(e.getPosition()) &&
            !nameLabel_.getBounds().contains(e.getPosition()))
        {
            owner.doRecall(rowIndex);
        }
    }

    void SoundBankOverlay::Row::update(int idx, const juce::String& name)
    {
        rowIndex = idx;
        nameLabel_.setText(juce::String(idx + 1) + "  " + name, juce::dontSendNotification);
    }

    // -------------------------------------------------------------------------
    // SoundBankOverlay

    SoundBankOverlay::SoundBankOverlay(LockstepProcessor& processor) : processor_(processor)
    {
        model_.owner = this;
        listBox_.setModel(&model_);
        listBox_.setRowHeight(24);
        listBox_.setWantsKeyboardFocus(false);
        listBox_.setColour(juce::ListBox::backgroundColourId,
                           juce::Colour::fromRGB(28, 32, 40));
        addAndMakeVisible(listBox_);

        saveBtn_.setWantsKeyboardFocus(false);
        saveBtn_.onClick = [this] { onSaveClicked(); };
        addAndMakeVisible(saveBtn_);

        closeBtn_.setWantsKeyboardFocus(false);
        closeBtn_.onClick = [this] {
            if (onClose) onClose();
        };
        addAndMakeVisible(closeBtn_);

        hintLabel_.setText(status::soundBankHint(), juce::dontSendNotification);
        hintLabel_.setFont(juce::Font(juce::FontOptions(9.5f)));
        hintLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(120, 140, 160));
        hintLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(hintLabel_);
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
        hintLabel_.setBounds(r.removeFromBottom(16));
        listBox_.setBounds(r.reduced(0, 2));
    }

    void SoundBankOverlay::refresh()
    {
        listBox_.updateContent();
        listBox_.deselectAllRows();
        listBox_.repaint();
    }

    void SoundBankOverlay::doRecall(int entryIndex)
    {
        const int track = getActiveTrack ? getActiveTrack() : 0;
        const auto* e = processor_.soundPoolEntry(entryIndex);
        if (!e) return;
        const bool ok = processor_.recallSoundFromPool(track, entryIndex);
        if (onStatus)
        {
            if (ok)
                onStatus(status::soundRecalled(juce::String(e->name)));
            else
                onStatus(status::soundMachineMismatch(juce::String(e->machineId),
                                                      juce::String(processor_.kit(track).machineId)));
        }
    }

    void SoundBankOverlay::doDelete(int entryIndex)
    {
        const auto* e = processor_.soundPoolEntry(entryIndex);
        const juce::String name = e ? juce::String(e->name) : juce::String{};
        processor_.removeSoundEntry(entryIndex);
        if (onStatus && name.isNotEmpty())
            onStatus(status::soundDeleted(name));
        refresh();
    }

    void SoundBankOverlay::doRename(int entryIndex, const juce::String& newName)
    {
        if (newName.isEmpty()) return;
        processor_.renameSoundEntry(entryIndex, newName.toStdString());
        if (onStatus) onStatus(status::soundRenamed(newName));
        refresh();
    }

    void SoundBankOverlay::onSaveClicked()
    {
        const int track = getActiveTrack ? getActiveTrack() : 0;
        // Pass empty name so saveTrackToSoundPool auto-generates "<MachineShort> T<n>".
        const int idx = processor_.saveTrackToSoundPool(track, "");
        if (onStatus && idx >= 0)
        {
            const auto* e = processor_.soundPoolEntry(idx);
            if (e) onStatus(status::soundSaved(juce::String(e->name)));
        }
        refresh();
    }

    // -------------------------------------------------------------------------
    // ListBoxModel

    int SoundBankOverlay::Model::getNumRows()
    {
        return owner ? owner->processor_.soundPoolSize() : 0;
    }

    juce::Component* SoundBankOverlay::Model::refreshComponentForRow(int row, bool,
                                                                     juce::Component* existing)
    {
        if (!owner) return existing;
        const auto* e = owner->processor_.soundPoolEntry(row);
        if (!e) return existing;

        auto* rowComp = dynamic_cast<Row*>(existing);
        if (!rowComp)
        {
            // Row not yet allocated or wrong type — create a new one.
            auto* newRow = new Row(*owner);
            rowComp = newRow;
        }
        rowComp->update(row, juce::String(e->name));
        return rowComp;
    }
}
