#include "StepGrid.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"

namespace lockstep
{
    StepGrid::StepGrid(LockstepProcessor& processor)
        : processor_(processor)
    {
        prevBtn_.onClick = [this] { prevPage(); repaint(); };
        nextBtn_.onClick = [this] { nextPage(); repaint(); };
        addAndMakeVisible(prevBtn_);
        addAndMakeVisible(nextBtn_);
    }

    void StepGrid::setActiveTrack(int t)
    {
        activeTrack_ = juce::jlimit(0, static_cast<int>(kNumTracks) - 1, t);
        stepPage_ = 0;
        repaint();
    }

    void StepGrid::nextPage()
    {
        ++stepPage_;
        clampPage();
    }

    void StepGrid::prevPage()
    {
        --stepPage_;
        clampPage();
    }

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

    void StepGrid::paint(juce::Graphics& g)
    {
        const auto bounds = getLocalBounds();

        // Header row: track + page info.
        const auto header = bounds.withHeight(18);
        g.setColour(juce::Colour::fromRGB(100, 120, 140));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        const int pages = numPages();
        g.drawText("Track " + juce::String(activeTrack_ + 1)
                       + "   Page " + juce::String(stepPage_ + 1)
                       + " / "      + juce::String(pages),
                   header.reduced(4, 0), juce::Justification::centredLeft);

        // Step cells.
        const auto gridArea = bounds.withTrimmedTop(18).withTrimmedBottom(24);
        const int cellW = gridArea.getWidth()  / kCols;
        const int cellH = gridArea.getHeight() / kRows;
        const int baseStep = stepPage_ * kPageSteps;
        const int len = trackLength();

        const auto& track =
            processor_.sequence().tracks[static_cast<std::size_t>(activeTrack_)];

        for (int row = 0; row < kRows; ++row)
        {
            for (int col = 0; col < kCols; ++col)
            {
                const int localIdx  = row * kCols + col;
                const int absIdx    = baseStep + localIdx;
                const bool inRange  = absIdx < len;
                const bool hasTrig  = inRange
                    && track.steps[static_cast<std::size_t>(absIdx)].trig;

                const int x = gridArea.getX() + col * cellW;
                const int y = gridArea.getY() + row * cellH;
                const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                if (hasTrig)
                {
                    g.setColour(juce::Colour::fromRGB(80, 180, 120));
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                }
                else if (inRange)
                {
                    g.setColour(juce::Colour::fromRGB(45, 55, 65));
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                    g.setColour(juce::Colour::fromRGB(70, 85, 100));
                    g.drawRoundedRectangle(cell.toFloat(), 3.0f, 1.0f);
                }
                else
                {
                    g.setColour(juce::Colour::fromRGB(28, 32, 38));
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                }

                // Step number hint (1-indexed).
                g.setColour(inRange ? juce::Colour::fromRGB(100, 120, 140)
                                    : juce::Colour::fromRGB(45, 50, 58));
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText(juce::String(absIdx + 1), cell, juce::Justification::centred);
            }
        }
    }

    void StepGrid::resized()
    {
        const auto bounds  = getLocalBounds();
        const auto btnRow  = bounds.withTrimmedTop(bounds.getHeight() - 22).reduced(2, 2);
        const int  btnW    = 32;
        prevBtn_.setBounds(btnRow.withWidth(btnW));
        nextBtn_.setBounds(btnRow.withRightX(btnRow.getRight()).withWidth(btnW));
    }
}
