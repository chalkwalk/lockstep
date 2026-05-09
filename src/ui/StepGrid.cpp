#include "StepGrid.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include <algorithm>
#include <cstddef>

namespace lockstep
{
    static const juce::Colour kColActive   { 0xFF50B478u };  // green trig
    static const juce::Colour kColInactive { 0xFF2D3741u };  // dark, in-range
    static const juce::Colour kColOutRange { 0xFF1C2026u };  // near-black
    static const juce::Colour kColPlayhead { 0xFFFFCC44u };  // amber highlight

    StepGrid::StepGrid(LockstepProcessor& processor)
        : processor_(processor)
    {
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            const auto ti = static_cast<std::size_t>(i);

            trackBtns_[ti].setButtonText(juce::String(i + 1));
            trackBtns_[ti].setClickingTogglesState(false);
            trackBtns_[ti].onClick = [this, i] { setActiveTrack(i); };
            addAndMakeVisible(trackBtns_[ti]);

            muteBtns_[ti].setButtonText("M");
            muteBtns_[ti].setClickingTogglesState(true);
            addAndMakeVisible(muteBtns_[ti]);
            muteAttachments_[ti] =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    processor_.apvts(), ParamIDs::trackMute(i), muteBtns_[ti]);
        }
        trackBtns_[0].setToggleState(true, juce::dontSendNotification);

        prevBtn_.onClick = [this] { prevPage(); repaint(); };
        nextBtn_.onClick = [this] { nextPage(); repaint(); };
        addAndMakeVisible(prevBtn_);
        addAndMakeVisible(nextBtn_);

        lengthSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        lengthSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
        addAndMakeVisible(lengthSlider_);

        rebuildLengthAttachment();
        startTimerHz(30);
    }

    StepGrid::~StepGrid() = default;

    void StepGrid::setActiveTrack(int t)
    {
        const int clamped = juce::jlimit(0, static_cast<int>(kNumTracks) - 1, t);
        if (clamped == activeTrack_)
            return;

        for (auto& b : trackBtns_)
            b.setToggleState(false, juce::dontSendNotification);
        trackBtns_[static_cast<std::size_t>(clamped)].setToggleState(
            true, juce::dontSendNotification);

        activeTrack_ = clamped;
        stepPage_    = 0;
        rebuildLengthAttachment();
        repaint();
    }

    void StepGrid::nextPage() { ++stepPage_; clampPage(); }
    void StepGrid::prevPage() { --stepPage_; clampPage(); }

    void StepGrid::timerCallback() { repaint(); }

    int StepGrid::trackLength() const
    {
        auto* p = processor_.apvts().getRawParameterValue(
            ParamIDs::trackLength(activeTrack_));
        return p ? static_cast<int>(p->load()) : kPageSteps;
    }

    int StepGrid::numPages() const
    {
        return (trackLength() + kPageSteps - 1) / kPageSteps;
    }

    void StepGrid::clampPage()
    {
        stepPage_ = juce::jlimit(0, juce::jmax(0, numPages() - 1), stepPage_);
    }

    void StepGrid::rebuildLengthAttachment()
    {
        lengthAttachment_.reset();
        lengthAttachment_ = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor_.apvts(),
                ParamIDs::trackLength(activeTrack_),
                lengthSlider_);
    }

    // -------------------------------------------------------------------------

    void StepGrid::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds();

        // ---- Track selector + mute rows are laid out via resized(). ----
        bounds.removeFromTop(kTrackRowH + kMuteRowH);

        // ---- Step cell area ----
        const auto navArea  = bounds.removeFromBottom(kNavRowH);
        const auto cellArea = bounds;

        // Compute playhead.
        const int trackLen = trackLength();
        const auto& clk = processor_.clock();
        const double sps = clk.samplesPerStep();
        auto* divP = processor_.apvts().getRawParameterValue(
            ParamIDs::trackDivider(activeTrack_));
        const int div = divP ? std::max(1, static_cast<int>(divP->load())) : 1;
        const double effectiveSPS = sps * static_cast<double>(div);

        int playheadAbs = -1;
        if (effectiveSPS > 0.0 && trackLen > 0)
        {
            const auto stepNum = static_cast<std::int64_t>(
                static_cast<double>(clk.samplePosition()) / effectiveSPS);
            playheadAbs = static_cast<int>(stepNum % trackLen);
        }

        const int baseStep = stepPage_ * kPageSteps;
        const auto& track =
            processor_.sequence().tracks[static_cast<std::size_t>(activeTrack_)];

        const int cellW = cellArea.getWidth()  / kCols;
        const int cellH = cellArea.getHeight() / kRows;

        for (int row = 0; row < kRows; ++row)
        {
            for (int col = 0; col < kCols; ++col)
            {
                const int localIdx = row * kCols + col;
                const int absIdx   = baseStep + localIdx;
                const bool inRange = absIdx < trackLen;
                const bool hasTrig = inRange
                    && track.steps[static_cast<std::size_t>(absIdx)].trig;
                const bool isHead  = (absIdx == playheadAbs);

                const int x = cellArea.getX() + col * cellW;
                const int y = cellArea.getY() + row * cellH;
                const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                if (!inRange)
                {
                    g.setColour(kColOutRange);
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                }
                else if (hasTrig)
                {
                    g.setColour(isHead ? kColPlayhead : kColActive);
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                }
                else
                {
                    g.setColour(isHead ? kColPlayhead.withAlpha(0.55f) : kColInactive);
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                    g.setColour(juce::Colour::fromRGB(70, 85, 100));
                    g.drawRoundedRectangle(cell.toFloat(), 3.0f, 1.0f);
                }

                // Step number
                g.setColour(inRange ? juce::Colour::fromRGB(110, 130, 150)
                                    : juce::Colour::fromRGB(40, 46, 54));
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText(juce::String(absIdx + 1), cell, juce::Justification::centred);
            }
        }

        // ---- Nav row info text (page / length) drawn to the left of buttons ----
        const int pages = numPages();
        g.setColour(juce::Colour::fromRGB(100, 120, 140));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const auto infoRect = navArea.withTrimmedLeft(64).withTrimmedRight(120);
        g.drawText(
            "Page " + juce::String(stepPage_ + 1) + " / " + juce::String(pages)
                + "     Length:",
            infoRect, juce::Justification::centredLeft);
    }

    void StepGrid::resized()
    {
        auto bounds = getLocalBounds();

        // Track selector row
        auto trackRow = bounds.removeFromTop(kTrackRowH);
        const int btnW = trackRow.getWidth() / static_cast<int>(kNumTracks);
        for (std::size_t i = 0; i < kNumTracks; ++i)
            trackBtns_[i].setBounds(trackRow.removeFromLeft(btnW).reduced(1, 2));

        // Mute row (one small toggle per track)
        auto muteRow = bounds.removeFromTop(kMuteRowH);
        const int muteW = muteRow.getWidth() / static_cast<int>(kNumTracks);
        for (std::size_t i = 0; i < kNumTracks; ++i)
            muteBtns_[i].setBounds(muteRow.removeFromLeft(muteW).reduced(1, 1));

        // Nav + length row at the bottom
        auto navRow = bounds.removeFromBottom(kNavRowH).reduced(0, 2);
        prevBtn_.setBounds(navRow.removeFromLeft(28).reduced(1));
        nextBtn_.setBounds(navRow.removeFromLeft(28).reduced(1));
        navRow.removeFromLeft(100);  // space for page info text drawn in paint
        lengthSlider_.setBounds(navRow.reduced(2, 0));
    }

    // -------------------------------------------------------------------------

    int StepGrid::stepCellAt(juce::Point<int> pos) const
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(kTrackRowH + kMuteRowH);
        bounds.removeFromBottom(kNavRowH);
        const auto cellArea = bounds;

        if (!cellArea.contains(pos))
            return -1;

        const int cellW = cellArea.getWidth()  / kCols;
        const int cellH = cellArea.getHeight() / kRows;
        if (cellW <= 0 || cellH <= 0)
            return -1;

        const int col = (pos.getX() - cellArea.getX()) / cellW;
        const int row = (pos.getY() - cellArea.getY()) / cellH;

        if (col < 0 || col >= kCols || row < 0 || row >= kRows)
            return -1;

        const int absIdx = stepPage_ * kPageSteps + row * kCols + col;
        return absIdx < trackLength() ? absIdx : -1;
    }

    void StepGrid::mouseDown(const juce::MouseEvent& e)
    {
        const int absIdx = stepCellAt(e.getPosition());
        if (absIdx >= 0)
        {
            mouseHeldStep_ = absIdx;
            processor_.editContext().hold(activeTrack_, absIdx);
        }
    }

    void StepGrid::mouseUp(const juce::MouseEvent& e)
    {
        if (mouseHeldStep_ < 0)
            return;

        const bool shouldToggle = !processor_.editContext().wasParamWritten()
                                  && stepCellAt(e.getPosition()) == mouseHeldStep_;
        processor_.editContext().release();

        if (shouldToggle)
        {
            auto& step = processor_.sequence()
                .tracks[static_cast<std::size_t>(activeTrack_)]
                .steps[static_cast<std::size_t>(mouseHeldStep_)];
            step.trig = !step.trig;
        }

        mouseHeldStep_ = -1;
    }
}
