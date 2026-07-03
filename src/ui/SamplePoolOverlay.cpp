#include "SamplePoolOverlay.h"
#include "../PluginProcessor.h"
#include <algorithm>

namespace lockstep
{
    SamplePoolOverlay::SamplePoolOverlay(LockstepProcessor& processor)
        : processor_(processor)
    {
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colour::fromRGB(22, 26, 32));
        list_.setColour(juce::ListBox::outlineColourId, juce::Colour::fromRGB(55, 65, 80));
        list_.setOutlineThickness(1);
        list_.setRowHeight(22);
        list_.setWantsKeyboardFocus(false);
        addAndMakeVisible(list_);

        loadBtn_.setWantsKeyboardFocus(false);
        loadBtn_.onClick = [this] {
            // W3b: when the focused track is a Stream (disk-stream) machine, the
            // Load button assigns its streamed source file instead of decoding into
            // the pool — the discoverable keyboard path to pick a Stream file (the
            // only prior way was drag-and-drop onto the track).
            const int track = getActiveTrack ? getActiveTrack() : -1;
            const bool streamTrack = track >= 0 && processor_.isStreamTrack(track);
            fileChooser_ = std::make_unique<juce::FileChooser>(
                streamTrack ? "Choose Stream Source" : "Load Sample(s)",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif;*.flac;*.ogg");
            const int flags = juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles
                | (streamTrack ? 0 : juce::FileBrowserComponent::canSelectMultipleItems);
            fileChooser_->launchAsync(flags, [this, track, streamTrack](const juce::FileChooser& fc) {
                if (streamTrack)
                {
                    const auto results = fc.getResults();
                    if (!results.isEmpty())
                        processor_.setStreamFile(track, results[0].getFullPathName());
                }
                else
                {
                    for (const auto& f : fc.getResults())
                        processor_.samplePool().load(f.getFullPathName());
                }
                list_.updateContent();
                updateButtonStates();
            });
        };
        addAndMakeVisible(loadBtn_);

        relinkBtn_.setWantsKeyboardFocus(false);
        relinkBtn_.setEnabled(false);
        relinkBtn_.onClick = [this] {
            const int row = selectedPoolIndex();
            if (row < 0 || !processor_.samplePool().isMissing(row))
                return;
            const auto* s = processor_.samplePool().get(row);
            const juce::File startDir = s
                                            ? juce::File(juce::String(s->ref.path)).getParentDirectory()
                                            : juce::File::getSpecialLocation(juce::File::userMusicDirectory);

            fileChooser_ = std::make_unique<juce::FileChooser>(
                "Relink Sample",
                startDir,
                "*.wav;*.aiff;*.aif;*.flac;*.ogg");
            fileChooser_->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, row](const juce::FileChooser& fc) {
                    const auto results = fc.getResults();
                    if (results.isEmpty()) return;
                    processor_.relinkSample(row, results[0].getFullPathName());
                    list_.updateContent();
                    updateButtonStates();
                });
        };
        addAndMakeVisible(relinkBtn_);

        removeBtn_.setWantsKeyboardFocus(false);
        removeBtn_.onClick = [this] {
            const int row = selectedPoolIndex();
            if (row < 0 || row >= processor_.samplePool().size())
                return;
            processor_.removeSample(row);
            list_.updateContent();
        };
        addAndMakeVisible(removeBtn_);

        // Up/Down reorder file samples only (volatile REC slots have fixed ordinals).
        upBtn_.setWantsKeyboardFocus(false);
        upBtn_.onClick = [this] {
            const int row = selectedPoolIndex();
            if (row <= 0 || processor_.samplePool().isVolatileIndex(row)
                || processor_.samplePool().isVolatileIndex(row - 1))
                return;
            processor_.swapSamples(row, row - 1);
            list_.updateContent();
        };
        addAndMakeVisible(upBtn_);

        downBtn_.setWantsKeyboardFocus(false);
        downBtn_.onClick = [this] {
            const int row = selectedPoolIndex();
            if (row < 0 || row >= processor_.samplePool().size() - 1
                || processor_.samplePool().isVolatileIndex(row)
                || processor_.samplePool().isVolatileIndex(row + 1))
                return;
            processor_.swapSamples(row, row + 1);
            list_.updateContent();
        };
        addAndMakeVisible(downBtn_);

        closeBtn_.setWantsKeyboardFocus(false);
        closeBtn_.onClick = [this] {
            if (onClose) onClose();
        };
        addAndMakeVisible(closeBtn_);
        // No always-on timer (9.15): the pool poll runs only while the overlay is
        // visible — see visibilityChanged().
    }

    SamplePoolOverlay::~SamplePoolOverlay()
    {
        stopTimer();
    }

    void SamplePoolOverlay::visibilityChanged()
    {
        if (isVisible())
        {
            // Sync immediately on open, then poll at 10 Hz to catch external pool
            // changes (e.g. drag-drop additions) while the manager is up.
            list_.updateContent();
            updateButtonStates();
            startTimerHz(10);
        }
        else
        {
            stopTimer();
        }
    }

    void SamplePoolOverlay::timerCallback()
    {
        rebuildRows();
        list_.updateContent();
        updateButtonStates();
    }

    void SamplePoolOverlay::rebuildRows()
    {
        rows_.clear();
        auto& pool = processor_.samplePool();
        const int n = pool.size();

        // SAMPLES — every disk-backed (non-volatile) entry, in pool order.
        bool anyFile = false;
        for (int i = 0; i < n; ++i)
            if (!pool.isVolatileIndex(i)) { anyFile = true; break; }
        if (anyFile)
        {
            rows_.push_back({ true, "SAMPLES", -1 });
            for (int i = 0; i < n; ++i)
                if (!pool.isVolatileIndex(i))
                    rows_.push_back({ false, {}, i });
        }

        // RECORD / LOOP — captured volatile slots only (empties hidden).
        auto addGroup = [&](SampleOrigin o, const char* label) {
            bool any = false;
            for (int i = 0; i < n; ++i)
                if (pool.isVolatileIndex(i) && pool.origin(i) == o) { any = true; break; }
            if (!any) return;
            rows_.push_back({ true, label, -1 });
            for (int i = 0; i < n; ++i)
                if (pool.isVolatileIndex(i) && pool.origin(i) == o)
                    rows_.push_back({ false, {}, i });
        };
        addGroup(SampleOrigin::Record, "RECORD");
        addGroup(SampleOrigin::Loop, "LOOP");
    }

    int SamplePoolOverlay::selectedPoolIndex() const
    {
        const int row = list_.getSelectedRow();
        if (row < 0 || row >= static_cast<int>(rows_.size()))
            return -1;
        return rows_[static_cast<std::size_t>(row)].isHeader
                   ? -1 : rows_[static_cast<std::size_t>(row)].poolIndex;
    }

    void SamplePoolOverlay::updateButtonStates()
    {
        // W3b: on a Stream track the Load button picks the disk-stream source.
        const int activeTrack = getActiveTrack ? getActiveTrack() : -1;
        const bool streamTrack = activeTrack >= 0 && processor_.isStreamTrack(activeTrack);
        loadBtn_.setButtonText(streamTrack ? "Stream..." : "Load...");

        auto& pool = processor_.samplePool();
        const int row = selectedPoolIndex();  // absolute pool index, or -1 for a header
        const bool hasSel = (row >= 0 && row < pool.size());
        const bool isFile = hasSel && !pool.isVolatileIndex(row);
        const bool missing = hasSel && pool.isMissing(row);
        relinkBtn_.setEnabled(missing);
        removeBtn_.setEnabled(isFile);   // volatile REC slots aren't removable
        upBtn_.setEnabled(isFile && row > 0 && !pool.isVolatileIndex(row - 1));
        downBtn_.setEnabled(isFile && row < pool.size() - 1
                            && !pool.isVolatileIndex(row + 1));
    }

    int SamplePoolOverlay::getNumRows()
    {
        // getNumRows is called during updateContent(); keep the row model in sync.
        rebuildRows();
        return static_cast<int>(rows_.size());
    }

    void SamplePoolOverlay::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                             int width, int height, bool rowIsSelected)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(rows_.size()))
            return;
        const auto& row = rows_[static_cast<std::size_t>(rowNumber)];
        const int textY = (height - 13) / 2;

        if (row.isHeader)
        {
            g.setColour(juce::Colour::fromRGB(30, 36, 44));
            g.fillAll();
            g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
            g.setColour(juce::Colour::fromRGB(130, 150, 175));
            g.drawText(row.label,
                       juce::Rectangle<int>(6, textY, width - 12, 14),
                       juce::Justification::centredLeft);
            return;
        }

        if (rowIsSelected)
        {
            g.setColour(juce::Colour::fromRGB(50, 80, 120));
            g.fillAll();
        }

        auto& pool = processor_.samplePool();
        const auto* sample = pool.get(row.poolIndex);
        if (sample == nullptr)
            return;

        const bool isMissing = sample->missing;

        // Name + hint come from the shared pool display model so the browser and
        // the in-machine pickers can never drift (W3a).
        const juce::String name = pool.displayName(row.poolIndex);
        const juce::String hint = pool.displayHint(row.poolIndex);

        g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        g.setColour(isMissing ? juce::Colour::fromRGB(255, 160, 50)
                              : juce::Colours::white);
        g.drawText("  " + name,
                   juce::Rectangle<int>(6, textY, width - 12, 14),
                   juce::Justification::centredLeft);

        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(isMissing ? juce::Colour::fromRGB(200, 100, 30)
                              : juce::Colour::fromRGB(120, 140, 160));
        g.drawText(hint,
                   juce::Rectangle<int>(6, textY, width - 12, 14),
                   juce::Justification::centredRight);
    }

    void SamplePoolOverlay::listBoxItemClicked(int /*rowNumber*/, const juce::MouseEvent& /*e*/)
    {
        updateButtonStates();
        const int poolIdx = selectedPoolIndex();
        if (poolIdx < 0)
            return;  // header row — nothing to preview
        if (processor_.samplePool().isMissing(poolIdx))
            return;  // no preview for missing samples
        const int track = getActiveTrack ? getActiveTrack() : 0;
        processor_.triggerPreview(poolIdx, std::max(0, track));
    }

    void SamplePoolOverlay::listBoxItemDoubleClicked(int /*rowNumber*/, const juce::MouseEvent& /*e*/)
    {
        const int poolIdx = selectedPoolIndex();
        if (poolIdx < 0)
            return;
        const int track = getActiveTrack ? getActiveTrack() : 0;
        if (track < 0) return;
        const int sampleSlot = processor_.slotForId(track, "sample_id");
        if (sampleSlot < 0) return;
        processor_.writeParam(track, sampleSlot, static_cast<float>(poolIdx));
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
        relinkBtn_.setBounds(btnRow.removeFromRight(70).reduced(1));
        btnRow.removeFromRight(4);
        loadBtn_.setBounds(btnRow.removeFromLeft(70).reduced(1));

        bounds.removeFromBottom(4);
        list_.setBounds(bounds);
    }
}
