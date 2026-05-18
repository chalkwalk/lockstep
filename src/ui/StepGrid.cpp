#include "StepGrid.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include <algorithm>
#include <cstddef>

namespace lockstep
{
    static const juce::Colour kColActive   { 0xFF50B478u };  // green trig — certain fire
    static const juce::Colour kColTrigDim  { 0xFF1C3D28u };  // dark green — blocked / low-prob trig
    static const juce::Colour kColInactive { 0xFF2D3741u };  // dark, in-range, no trig
    static const juce::Colour kColOutRange { 0xFF1C2026u };  // near-black, out of track length
    static const juce::Colour kColPlayhead { 0xFFFFCC44u };  // amber highlight
    static const juce::Colour kColHeld     { 0xFFFFFFFFu };  // held-step border
    static const juce::Colour kColPLock    { 0xFF3EC8C8u };  // P-Lock dot

    // Pre-computes a fire-probability in [0,1] for each slot on the visible page.
    // Steps with trig=false → 0. Scans from step 0 so prev-dep chains propagate
    // correctly even when pageBase > 0.
    static std::array<float, StepGrid::kPageSteps>
    computePagePreview(const Track& track,
                       int          trackLen,
                       std::int64_t loopBase,
                       int          pageBase) noexcept
    {
        std::array<float, StepGrid::kPageSteps> out{};
        const int limit = std::min(pageBase + StepGrid::kPageSteps, trackLen);

        float prevProb = 0.5f;  // unknown prior from previous loop's last step

        for (int i = 0; i < limit; ++i)
        {
            const auto& step = track.steps[static_cast<std::size_t>(i)];
            float prob = 0.0f;

            if (step.trig)
            {
                const auto& cond = step.condition.isTrivial()
                                       ? track.baseCond : step.condition;

                // m:n gate — deterministic for the current loop iteration.
                bool iterPass = true;
                if (cond.iterDenominator > 1)
                {
                    const auto len   = static_cast<std::int64_t>(std::max(trackLen, 1));
                    const auto denom = static_cast<std::int64_t>(cond.iterDenominator);
                    const auto iter  = (loopBase + static_cast<std::int64_t>(i)) / len;
                    iterPass = (iter % denom
                                == static_cast<std::int64_t>(cond.iterNumerator) - 1);
                }

                if (iterPass)
                {
                    prob = std::clamp(
                        static_cast<float>(cond.probabilityPercent) / 100.0f,
                        0.0f, 1.0f);

                    // Propagate prev-dep uncertainty.
                    if      (cond.prevDependency == 1) prob *= prevProb;
                    else if (cond.prevDependency == 2) prob *= (1.0f - prevProb);
                }
            }

            prevProb = prob;

            if (i >= pageBase)
                out[static_cast<std::size_t>(i - pageBase)] = prob;
        }

        return out;
    }

    StepGrid::StepGrid(LockstepProcessor& processor, UiState& uiState)
        : processor_(processor), uiState_(uiState)
    {
        processor_.setFocusTrack(activeTrack_);  // sync initial focus (track 0)

        prevBtn_.onClick = [this] { prevPage(); repaint(); };
        prevBtn_.setWantsKeyboardFocus(false);
        nextBtn_.onClick = [this] { nextPage(); repaint(); };
        nextBtn_.setWantsKeyboardFocus(false);
        addAndMakeVisible(prevBtn_);
        addAndMakeVisible(nextBtn_);

        lengthSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        lengthSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
        lengthSlider_.setWantsKeyboardFocus(false);
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

        activeTrack_ = clamped;
        processor_.setFocusTrack(clamped);
        stepPage_    = 0;
        rebuildLengthAttachment();
        repaint();
        if (onActiveTrackChanged)
            onActiveTrackChanged(activeTrack_);
    }

    void StepGrid::setDisplayMode(GridDisplayMode mode)
    {
        displayMode_ = mode;
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


        // ---- Step cell area ----
        const auto navArea  = bounds.removeFromBottom(kNavRowH);
        const auto cellArea = bounds;

        // Compute playhead from PPQ — thread-safe via cumulativePpq().
        const int trackLen = trackLength();
        const auto& clk = processor_.clock();
        auto* divP = processor_.apvts().getRawParameterValue(
            ParamIDs::trackDivider(activeTrack_));
        const int div = divP ? std::max(1, static_cast<int>(divP->load())) : 1;
        // 16th note = 0.25 PPQ; divider scales coarser.
        const double divisionPpq = 0.25 * static_cast<double>(div);

        int playheadAbs = -1;
        std::int64_t loopBase = 0;
        if (divisionPpq > 0.0 && trackLen > 0)
        {
            const auto stepNum = static_cast<std::int64_t>(
                clk.cumulativePpq() / divisionPpq);
            playheadAbs = static_cast<int>(stepNum % trackLen);
            loopBase    = (stepNum / static_cast<std::int64_t>(trackLen))
                          * static_cast<std::int64_t>(trackLen);
        }

        const int baseStep = stepPage_ * kPageSteps;
        const auto& track =
            processor_.sequence().tracks[static_cast<std::size_t>(activeTrack_)];

        const auto preview = computePagePreview(track, trackLen, loopBase, baseStep);

        // Corrected key letters: A and Z are modifier keys, not step keys.
        static constexpr const char* kKeyLetters[kPageSteps] = {
            "S","D","F","G","H","J","K","L",
            "X","C","V","B","N","M",",","."
        };

        // 9-column grid: col 0 = modifier (A/Z), cols 1..8 = steps.
        static constexpr int kTotalGridCols = kCols + 1;

        const bool showKeyLetters  = (displayMode_ != GridDisplayMode::Clean);
        const bool useClnGap       = (displayMode_ == GridDisplayMode::Clean);

        // Geometry — shared modifier and step cell width in all modes.
        int cellW, staggerA, staggerZ;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(cellArea.getWidth());
            cellW    = staggerCellW(hu);
            staggerA = staggerOffsetA(hu);
            staggerZ = staggerOffsetZ(hu);
        }
        else if (useClnGap)
        {
            cellW    = (cellArea.getWidth() - kClnColGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        else  // Ortholinear
        {
            cellW    = cellArea.getWidth() / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        const int cellH = cellArea.getHeight() / kRows;

        // Returns the x-position for a column (0 = modifier, 1..8 = steps) in a given row.
        auto colX = [&](int row, int col) -> int
        {
            const int stagger = (row == 0) ? staggerA : staggerZ;
            if (useClnGap && col >= 1)
                return cellArea.getX() + stagger + cellW + kClnColGap + (col - 1) * cellW;
            return cellArea.getX() + stagger + col * cellW;
        };

        // ---- Modifier column (A = MuteScope, Z = FillScope) ----
        for (int row = 0; row < kRows; ++row)
        {
            const int x = colX(row, 0);
            const int y = cellArea.getY() + row * cellH;
            const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

            const bool isModHeld = (row == 0) ? uiState_.muteHeld : uiState_.fillHeld;
            const bool modKeyDown = juce::KeyPress::isKeyCurrentlyDown(
                (row == 0) ? static_cast<int>('A') : static_cast<int>('Z'));

            g.setColour(modKeyDown ? juce::Colour::fromRGB(80, 120, 165)
                        : isModHeld ? (row == 0 ? juce::Colour(0xFF702020u)
                                                : juce::Colour(0xFF502870u))
                                    : juce::Colour::fromRGB(25, 32, 42));
            g.fillRoundedRectangle(cell.toFloat(), 3.0f);
            g.setColour(juce::Colour::fromRGB(48, 58, 70));
            g.drawRoundedRectangle(cell.toFloat(), 3.0f, 1.0f);

            if (showKeyLetters)
            {
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                g.setColour(juce::Colour::fromRGB(65, 80, 95));
                g.drawText((row == 0) ? "A" : "Z",
                           cell.withHeight(10).reduced(2, 0),
                           juce::Justification::topLeft);
            }
            const juce::Colour labelColour = isModHeld ? juce::Colours::white
                                                       : juce::Colour::fromRGB(80, 100, 120);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.setColour(labelColour);
            g.drawText((row == 0) ? "MUT" : "FIL", cell, juce::Justification::centred);
        }

        // ---- Step cells (S..L in row 0, X..period in row 1) ----
        for (int row = 0; row < kRows; ++row)
        {
            for (int col = 0; col < kCols; ++col)
            {
                const int localIdx = row * kCols + col;
                const int absIdx   = baseStep + localIdx;
                const bool inRange = absIdx < trackLen;
                const auto& ctx    = processor_.editContext();
                const auto& held   = ctx.heldSteps();
                const bool isHeld  = inRange
                    && ctx.heldTrackIndex() == activeTrack_
                    && std::find(held.begin(), held.end(), absIdx) != held.end();
                const bool hasTrig = inRange
                    && track.steps[static_cast<std::size_t>(absIdx)].trig;
                const int  activeSlot    = ctx.activeSlot();
                const bool hasLock = inRange
                    && !track.steps[static_cast<std::size_t>(absIdx)].overrides.empty();
                const bool hasActiveLock = hasLock && activeSlot >= 0
                    && track.steps[static_cast<std::size_t>(absIdx)].overrides.has(activeSlot);
                const bool isHead  = (absIdx == playheadAbs);

                // col+1 because col 0 is the modifier.
                const int x = colX(row, col + 1);
                const int y = cellArea.getY() + row * cellH;
                const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                // Layer 1: base state (step on/off)
                if (!inRange)
                {
                    g.setColour(kColOutRange);
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                }
                else if (hasTrig)
                {
                    g.setColour(kColActive);
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                }
                else
                {
                    g.setColour(kColInactive);
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                    g.setColour(juce::Colour::fromRGB(70, 85, 100));
                    g.drawRoundedRectangle(cell.toFloat(), 3.0f, 1.0f);
                }

                // Layer 2: conditional trig overlay.
                if (inRange && hasTrig)
                {
                    const float prob = preview[static_cast<std::size_t>(localIdx)];
                    if (prob < 1.0f)
                    {
                        g.setColour(kColTrigDim.withAlpha(1.0f - prob));
                        g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                    }
                }

                // Layer 3: playhead tint.
                if (isHead)
                {
                    g.setColour(kColPlayhead.withAlpha(0.45f));
                    g.fillRoundedRectangle(cell.toFloat(), 3.0f);
                }

                // P-Lock dot.
                if (hasLock)
                {
                    g.setColour(kColPLock);
                    g.fillRect(juce::Rectangle<int>(cell.getRight() - 5,
                                                    cell.getY() + 2, 4, 4));
                }

                if (hasActiveLock)
                {
                    g.setColour(kColPLock);
                    g.drawRoundedRectangle(cell.toFloat(), 3.0f, 2.0f);
                }

                if (isHeld)
                {
                    g.setColour(kColHeld);
                    g.drawRoundedRectangle(cell.toFloat(), 3.0f, 2.0f);
                }

                // Key letter.
                if (showKeyLetters && inRange)
                {
                    g.setFont(juce::Font(juce::FontOptions(8.0f)));
                    g.setColour(juce::Colour::fromRGB(88, 108, 128));
                    g.drawText(kKeyLetters[static_cast<std::size_t>(localIdx)],
                               cell.withHeight(10).reduced(2, 0),
                               juce::Justification::topLeft);
                }

                // Row 0: step number + track-select label (Track-scope Func layer).
                const bool hasTrackSelect = inRange && (row == 0);
                const bool shiftHeld = uiState_.funcHeld;

                static constexpr int kTrackLabelH = 11;
                const auto stepNumArea = hasTrackSelect ? cell.withTrimmedBottom(kTrackLabelH) : cell;

                const float stepNumAlpha = (hasTrackSelect && shiftHeld) ? 0.35f : 1.0f;
                g.setColour((inRange ? juce::Colour::fromRGB(110, 130, 150)
                                     : juce::Colour::fromRGB(40, 46, 54))
                                .withMultipliedAlpha(stepNumAlpha));
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText(juce::String(absIdx + 1), stepNumArea, juce::Justification::centred);

                if (hasTrackSelect)
                {
                    static constexpr const char* kTrackLabels[kCols] = {
                        "T1","T2","T3","T4","T5","T6","T7","T8"
                    };
                    const float trackAlpha = shiftHeld ? 1.0f : 0.3f;
                    g.setFont(juce::Font(juce::FontOptions(8.0f)));
                    g.setColour(juce::Colour::fromRGB(160, 185, 210).withAlpha(trackAlpha));
                    g.drawText(kTrackLabels[col],
                               cell.withTrimmedTop(cell.getHeight() - kTrackLabelH).reduced(2, 0),
                               juce::Justification::centredBottom);
                }
            }
        }

        // ---- Nav row info text (page / length) drawn to the left of buttons ----
        const int pages = numPages();
        g.setColour(juce::Colour::fromRGB(100, 120, 140));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const auto infoRect = navArea.withTrimmedLeft(96).withTrimmedRight(120);
        g.drawText(
            "Page " + juce::String(stepPage_ + 1) + " / " + juce::String(pages)
                + "     Length:",
            infoRect, juce::Justification::centredLeft);

        // ---- Trig grid mode indicator (top-left badge, non-Default only) ----
        const auto mode = uiState_.trigGridMode;
        if (mode != TrigGridMode::Default)
        {
            const char* label = (mode == TrigGridMode::Keyboard)  ? "KEY"
                              : (mode == TrigGridMode::Retrig)     ? "RTG"
                                                                   : "POL";
            const auto badgeRect = getLocalBounds().removeFromTop(18).removeFromLeft(40).reduced(3);
            g.setColour(juce::Colour(0xFFD07030u));
            g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(label, badgeRect, juce::Justification::centred);
        }
    }

    void StepGrid::resized()
    {
        auto bounds = getLocalBounds();

        // Nav + length row at the bottom
        auto navRow = bounds.removeFromBottom(kNavRowH).reduced(0, 2);
        prevBtn_.setBounds(navRow.removeFromLeft(28).reduced(1));
        nextBtn_.setBounds(navRow.removeFromLeft(28).reduced(1));
        navRow.removeFromLeft(64);   // space for page info text drawn in paint
        lengthSlider_.setBounds(navRow.reduced(2, 0));
    }

    // -------------------------------------------------------------------------

    int StepGrid::stepCellAt(juce::Point<int> pos) const
    {
        auto bounds = getLocalBounds();
        bounds.removeFromBottom(kNavRowH);
        const auto cellArea = bounds;

        if (!cellArea.contains(pos))
            return -1;

        static constexpr int kTotalGridCols = kCols + 1;
        const bool useClnGap = (displayMode_ == GridDisplayMode::Clean);

        int cellW, staggerA, staggerZ;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(cellArea.getWidth());
            cellW    = staggerCellW(hu);
            staggerA = staggerOffsetA(hu);
            staggerZ = staggerOffsetZ(hu);
        }
        else if (useClnGap)
        {
            cellW    = (cellArea.getWidth() - kClnColGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        else
        {
            cellW    = cellArea.getWidth() / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        const int cellH = cellArea.getHeight() / kRows;
        if (cellW <= 0 || cellH <= 0)
            return -1;

        const int row = (pos.getY() - cellArea.getY()) / cellH;
        if (row < 0 || row >= kRows)
            return -1;

        const int rowStagger = (row == 0) ? staggerA : staggerZ;
        const int relX = pos.getX() - cellArea.getX() - rowStagger;

        int col;
        if (useClnGap)
        {
            if (relX < 0)              return -1;
            if (relX < cellW)          col = 0;  // modifier column
            else if (relX < cellW + kClnColGap) return -1;  // in the gap
            else                       col = 1 + (relX - cellW - kClnColGap) / cellW;
        }
        else
        {
            col = relX / cellW;
        }

        // col 0 = modifier (A/Z); not a step cell.
        if (col <= 0 || col >= kTotalGridCols)
            return -1;

        const int stepCol = col - 1;
        const int absIdx  = stepPage_ * kPageSteps + row * kCols + stepCol;
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
        processor_.editContext().release(mouseHeldStep_);

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
