#include "KeyboardArea.h"
#include "KeyButton.h"
#include "UITheme.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include "../core/TrigCondition.h"
#include <algorithm>
#include <cstddef>

namespace lockstep
{
    using namespace theme;

    // -------------------------------------------------------------------------
    // Static colour constants (from SectionBar)

    const juce::Colour KeyboardArea::kColourTrackActive  = juce::Colour::fromRGB( 62, 200, 200);
    const juce::Colour KeyboardArea::kColourMasterActive = juce::Colour::fromRGB(255, 180,  50);
    const juce::Colour KeyboardArea::kColourInactive     = juce::Colour::fromRGB( 45,  55,  65);
    const juce::Colour KeyboardArea::kColourShiftActive  = juce::Colour::fromRGB(160, 175, 195);

    // -------------------------------------------------------------------------
    // Step-grid preview (unchanged from StepGrid.cpp)

    static std::array<float, KeyboardArea::kPageSteps>
    computePagePreview(const Track& track,
                       int          trackLen,
                       std::int64_t loopBase,
                       int          pageBase,
                       bool         fillActive) noexcept
    {
        std::array<float, KeyboardArea::kPageSteps> out{};
        const int limit = std::min(pageBase + KeyboardArea::kPageSteps, trackLen);

        float prevProb = 0.5f;

        for (int i = 0; i < limit; ++i)
        {
            const auto& step = track.steps[static_cast<std::size_t>(i)];
            float prob = 0.0f;

            if (step.trig)
            {
                const auto& cond = step.condition.isTrivial()
                                       ? track.baseCond : step.condition;

                bool fillPass = true;
                if (cond.fillRule == FillRule::OnlyFill  && !fillActive) fillPass = false;
                if (cond.fillRule == FillRule::NeverFill &&  fillActive) fillPass = false;

                bool iterPass = true;
                if (cond.iterDenominator > 1)
                {
                    const auto len   = static_cast<std::int64_t>(std::max(trackLen, 1));
                    const auto denom = static_cast<std::int64_t>(cond.iterDenominator);
                    const auto iter  = (loopBase + static_cast<std::int64_t>(i)) / len;
                    iterPass = (iter % denom
                                == static_cast<std::int64_t>(cond.iterNumerator) - 1);
                }

                if (fillPass && iterPass)
                {
                    prob = std::clamp(
                        static_cast<float>(cond.probabilityPercent) / 100.0f,
                        0.0f, 1.0f);
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

    // -------------------------------------------------------------------------
    // Constructor / destructor

    KeyboardArea::KeyboardArea(LockstepProcessor& processor, UiState& uiState)
        : processor_(processor), uiState_(uiState)
    {
        processor_.setFocusTrack(activeTrack_);

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

    KeyboardArea::~KeyboardArea() = default;

    // -------------------------------------------------------------------------
    // Layout helper

    KeyboardArea::RowAreas KeyboardArea::computeRowAreas() const
    {
        static constexpr int kRowGap = 4;
        auto area = getLocalBounds();
        const int availH = area.getHeight();
        const int cellH  = juce::jmax(1, (availH - 50) / 4);
        const int secH   = cellH + 4;
        const int funcH  = cellH + 4;
        const int rowGap2 = (displayMode_ == GridDisplayMode::Clean) ? kClnRowGap : kRowGap;

        RowAreas r;
        r.section  = area.removeFromTop(secH).reduced(8, 2);
        area.removeFromTop(kRowGap);
        r.function = area.removeFromTop(funcH).reduced(8, 2);
        area.removeFromTop(rowGap2);
        r.step     = area.reduced(8, 4);
        return r;
    }

    int KeyboardArea::stepRowsLocalY() const
    {
        static constexpr int kRowGap = 4;
        const int availH  = getHeight();
        const int cellH   = juce::jmax(1, (availH - 50) / 4);
        const int secH    = cellH + 4;
        const int funcH   = cellH + 4;
        const int rowGap2 = (displayMode_ == GridDisplayMode::Clean) ? kClnRowGap : kRowGap;
        // +4 = the top reduction from reduced(8,4) on the step area
        return secH + kRowGap + funcH + rowGap2 + 4;
    }

    // -------------------------------------------------------------------------
    // Track / page

    void KeyboardArea::setActiveTrack(int t)
    {
        const int clamped = juce::jlimit(0, static_cast<int>(kNumTracks) - 1, t);
        if (clamped == activeTrack_)
            return;
        activeTrack_ = clamped;
        processor_.setFocusTrack(clamped);
        stepPage_ = 0;
        rebuildLengthAttachment();
        repaint();
        if (onActiveTrackChanged)
            onActiveTrackChanged(activeTrack_);
    }

    void KeyboardArea::nextPage() { ++stepPage_; clampPage(); }
    void KeyboardArea::prevPage() { --stepPage_; clampPage(); }

    void KeyboardArea::setDisplayMode(GridDisplayMode mode)
    {
        displayMode_ = mode;
        resized();  // nav row button positions may shift
        repaint();
    }

    void KeyboardArea::timerCallback() { repaint(); }

    // -------------------------------------------------------------------------
    // Step helpers

    int KeyboardArea::trackLength() const
    {
        auto* p = processor_.apvts().getRawParameterValue(
            ParamIDs::trackLength(activeTrack_));
        return p ? static_cast<int>(p->load()) : kPageSteps;
    }

    int KeyboardArea::numPages() const
    {
        return (trackLength() + kPageSteps - 1) / kPageSteps;
    }

    void KeyboardArea::clampPage()
    {
        stepPage_ = juce::jlimit(0, juce::jmax(0, numPages() - 1), stepPage_);
        repaint();
    }

    void KeyboardArea::rebuildLengthAttachment()
    {
        lengthAttachment_.reset();
        lengthAttachment_ = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor_.apvts(),
                ParamIDs::trackLength(activeTrack_),
                lengthSlider_);
    }

    int KeyboardArea::stepCellAt(juce::Point<int> pos) const
    {
        const auto areas = computeRowAreas();
        auto stepArea = areas.step;
        stepArea.removeFromBottom(kNavRowH);
        const auto cellArea = stepArea;

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
            if (relX < 0)                              return -1;
            if (relX < cellW)                          col = 0;
            else if (relX < cellW + kClnColGap)        return -1;
            else col = 1 + (relX - cellW - kClnColGap) / cellW;
        }
        else
        {
            col = relX / cellW;
        }

        if (col <= 0 || col >= kTotalGridCols)
            return -1;

        const int absIdx = stepPage_ * kPageSteps + row * kCols + (col - 1);
        return absIdx < trackLength() ? absIdx : -1;
    }

    // -------------------------------------------------------------------------
    // Section helpers

    juce::Rectangle<int> KeyboardArea::sectionCellBounds(int cellIndex,
                                                          juce::Rectangle<int> area) const
    {
        const int w = area.getWidth();
        const int h = area.getHeight();
        if (displayMode_ == GridDisplayMode::Clean)
        {
            const int cellW = (w - kClnColGap) / kTotalSectionCells;
            const int x = (cellIndex == 0)
                ? area.getX()
                : area.getX() + cellW + kClnColGap + (cellIndex - 1) * cellW;
            return { x, area.getY(), cellW, h };
        }
        const int cellW = (displayMode_ == GridDisplayMode::Staggered)
                              ? staggerCellW(staggerHalfUnit(w))
                              : w / kTotalSectionCells;
        return { area.getX() + cellIndex * cellW, area.getY(), cellW, h };
    }

    int KeyboardArea::cellToSection(int cellIndex)
    {
        if (cellIndex < kFixedSectionCells || cellIndex >= kTotalSectionCells)
            return -1;
        return cellIndex - kFixedSectionCells;
    }

    bool KeyboardArea::isReservedMeta(int sectionIndex)
    {
        if (sectionIndex < 0 || sectionIndex >= IMachine::kMaxSections)
            return true;
        return kMetaLabels[static_cast<std::size_t>(sectionIndex)][0] == '\0';
    }

    void KeyboardArea::notifySectionChanged(int sectionIndex, int track)
    {
        if (!onSectionChanged)
            return;
        const auto ti   = static_cast<std::size_t>(track);
        const auto si   = static_cast<std::size_t>(sectionIndex);
        const auto info = processor_.section(track, sectionIndex);
        const int page  = uiState_.trackPage[ti][si];
        onSectionChanged(sectionIndex, page, info.firstSlot + 4 * page);
    }

    bool KeyboardArea::selectSection(int sectionIndex)
    {
        if (activeTrack_ < 0 || activeTrack_ >= static_cast<int>(kNumTracks))
            return false;
        if (sectionIndex < 0 || sectionIndex >= IMachine::kMaxSections)
            return false;
        if (sectionIndex >= processor_.numSections(activeTrack_))
            return false;

        const auto ti = static_cast<std::size_t>(activeTrack_);
        const auto si = static_cast<std::size_t>(sectionIndex);

        const bool wasInMasterMode = (uiState_.masterSection != -1);
        uiState_.masterSection = -1;

        if (!wasInMasterMode && uiState_.trackSection[ti] == sectionIndex)
        {
            const auto info = processor_.section(activeTrack_, sectionIndex);
            const int pageCount = info.pageCount > 0 ? info.pageCount : 1;
            uiState_.trackPage[ti][si] = (uiState_.trackPage[ti][si] + 1) % pageCount;
        }
        else
        {
            uiState_.trackSection[ti] = sectionIndex;
            uiState_.trackPage[ti][si] = 0;
        }

        repaint();
        notifySectionChanged(sectionIndex, activeTrack_);
        return true;
    }

    void KeyboardArea::selectMetaSection(int sectionIndex)
    {
        if (isReservedMeta(sectionIndex))
            return;
        uiState_.masterSection = (uiState_.masterSection == sectionIndex) ? -1 : sectionIndex;
        repaint();
        if (onMetaSectionChanged)
            onMetaSectionChanged(uiState_.masterSection);
    }

    void KeyboardArea::syncToActiveTrack()
    {
        repaint();
        if (activeTrack_ < 0 || activeTrack_ >= static_cast<int>(kNumTracks))
            return;
        if (uiState_.masterSection < 0)
            notifySectionChanged(uiState_.trackSection[static_cast<std::size_t>(activeTrack_)],
                                 activeTrack_);
    }

    // -------------------------------------------------------------------------
    // Mouse

    void KeyboardArea::mouseDown(const juce::MouseEvent& e)
    {
        const auto pos = e.getPosition();

        // Check section row click (keys 4-9 only; fixed cells 0-2 have no mouse action)
        {
            const auto areas = computeRowAreas();
            for (int i = 0; i < kTotalSectionCells; ++i)
            {
                if (sectionCellBounds(i, areas.section).contains(pos))
                {
                    const int section = cellToSection(i);
                    if (section >= 0)
                    {
                        if (uiState_.funcHeld)
                            selectMetaSection(section);
                        else
                            selectSection(section);
                    }
                    return;
                }
            }
        }

        // Check step cells
        const int absIdx = stepCellAt(pos);
        if (absIdx >= 0)
        {
            mouseHeldStep_ = absIdx;
            processor_.editContext().hold(activeTrack_, absIdx);
        }
    }

    void KeyboardArea::mouseUp(const juce::MouseEvent& e)
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

    // -------------------------------------------------------------------------
    // resized

    void KeyboardArea::resized()
    {
        const auto areas = computeRowAreas();
        auto navRow = areas.step;
        navRow.removeFromBottom(kNavRowH);  // consume step cells area
        // navRow is now just the nav strip at bottom of step area
        auto nav = areas.step.withTop(navRow.getBottom()).reduced(0, 2);
        prevBtn_.setBounds(nav.removeFromLeft(28).reduced(1));
        nextBtn_.setBounds(nav.removeFromLeft(28).reduced(1));
        nav.removeFromLeft(64);
        lengthSlider_.setBounds(nav.reduced(2, 0));
    }

    // -------------------------------------------------------------------------
    // paint

    void KeyboardArea::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(20, 22, 26));
        const auto areas = computeRowAreas();
        paintSectionRow (g, areas.section);
        paintFunctionRow(g, areas.function);
        paintStepRows   (g, areas.step);
    }

    // -------------------------------------------------------------------------
    // paintSectionRow — reproduces SectionBar::paint() exactly

    void KeyboardArea::paintSectionRow(juce::Graphics& g, juce::Rectangle<int> area)
    {
        const bool showKeyHint  = (displayMode_ != GridDisplayMode::Clean);
        const int  activeTrack  = activeTrack_;

        static constexpr const char* kKeyHints[kTotalSectionCells] = {
            "1", "2", "3", "4", "5", "6", "7", "8", "9"
        };

        // Cell 0: FNC
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('1'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)           st = KeyButtonState::Pressed;
            else if (uiState_.funcHeld) st = KeyButtonState::ModeActive;
            const KeyGroup grp { kFuncInactive, kFuncActive, kFuncAccent };
            paintKeyButton(g, sectionCellBounds(0, area), kKeyHints[0], "FNC", "", grp, st,
                           showKeyHint);
        }

        // Cell 1: REC
        {
            const bool isArmed = processor_.clock().isRecordArmed();
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('2'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)           st = KeyButtonState::Pressed;
            else if (isArmed)           st = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld) st = KeyButtonState::FuncHeld;
            const KeyGroup grp { kRecInactive, kRecActive, kRecAccent };
            paintKeyButton(g, sectionCellBounds(1, area), kKeyHints[1], "REC", "SNP", grp, st,
                           showKeyHint);
        }

        // Cell 2: Nav-Up (^)
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('3'));
            KeyButtonState st = pressed ? KeyButtonState::Pressed : KeyButtonState::Normal;
            if (!pressed && uiState_.funcHeld) st = KeyButtonState::FuncHeld;
            const KeyGroup grp { kNavInactive, kNavActive, kNavAccent };
            paintKeyButton(g, sectionCellBounds(2, area), kKeyHints[2], "^", "", grp, st,
                           showKeyHint);
        }

        // Section cells 3-8
        for (int s = 0; s < IMachine::kMaxSections; ++s)
        {
            const int cellIdx   = kFixedSectionCells + s;
            const bool available = (s < processor_.numSections(activeTrack));

            if (!available)
            {
                const KeyGroup grp { kSecInactive, kSecActive, kSecAccent };
                paintKeyButton(g, sectionCellBounds(cellIdx, area),
                               kKeyHints[cellIdx], "", "",
                               grp, KeyButtonState::Disabled, showKeyHint);
                continue;
            }

            const bool isMasterActive = (uiState_.masterSection == s);
            const bool isTrackActive  = (uiState_.masterSection == -1
                && uiState_.trackSection[static_cast<std::size_t>(activeTrack)] == s);

            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(
                static_cast<int>('4' + s));

            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)                           st = KeyButtonState::Pressed;
            else if (isTrackActive || isMasterActive)   st = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld)                 st = KeyButtonState::FuncHeld;

            const KeyGroup secGrp = isMasterActive
                ? KeyGroup{ kSecInactive, 0xFF404010u, 0xFFFFB432u }
                : KeyGroup{ kSecInactive, kSecActive,  kSecAccent  };

            const auto info     = processor_.section(activeTrack, s);
            const juce::String primLabel = info.label.isEmpty()
                                           ? juce::String(s) : info.label;
            const bool reserved  = isReservedMeta(s);
            const juce::String secLabel =
                reserved ? juce::String::charToString(0x2014)
                         : juce::String(kMetaLabels[static_cast<std::size_t>(s)]);

            const auto cell = sectionCellBounds(cellIdx, area);
            paintKeyButton(g, cell, kKeyHints[cellIdx],
                           primLabel, secLabel, secGrp, st, showKeyHint);

            // Page dots
            const int pageCount = info.pageCount;
            if (pageCount > 1 && !isMasterActive)
            {
                const auto ti       = static_cast<std::size_t>(activeTrack);
                const auto si       = static_cast<std::size_t>(s);
                const int activePage = uiState_.trackPage[ti][si];

                const juce::Colour dotCol = isTrackActive
                    ? kColourTrackActive : kColourTrackActive.withAlpha(0.4f);

                const int dotSize    = 4;
                const int dotSpacing = 6;
                const int totalDotW  = pageCount * dotSpacing - (dotSpacing - dotSize);
                int dotX = cell.getCentreX() - totalDotW / 2;
                const int dotY = cell.getBottom() - 6;

                for (int p = 0; p < pageCount; ++p)
                {
                    const bool isActiveDot = (p == activePage) && isTrackActive;
                    g.setColour(dotCol.withAlpha(isActiveDot ? 1.0f : 0.3f));
                    if (isActiveDot)
                        g.fillEllipse(static_cast<float>(dotX),
                                      static_cast<float>(dotY),
                                      static_cast<float>(dotSize),
                                      static_cast<float>(dotSize));
                    else
                        g.drawEllipse(static_cast<float>(dotX) + 0.5f,
                                      static_cast<float>(dotY) + 0.5f,
                                      static_cast<float>(dotSize) - 1.0f,
                                      static_cast<float>(dotSize) - 1.0f, 1.0f);
                    dotX += dotSpacing;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // paintFunctionRow — reproduces FunctionBar::paint() exactly

    void KeyboardArea::paintFunctionRow(juce::Graphics& g, juce::Rectangle<int> area)
    {
        struct QKeyDef
        {
            int         keyCode;
            const char* keyHint;
            const char* primary;
            const char* secondary;
            KeyGroup    group;
        };

        static const std::array<QKeyDef, 9> kDefs = {{
            { 'Q', "Q", "TRK", "",    { kModInactive, kModActive, kModAccent } },
            { 'W', "W", "<",   "",    { kNavInactive, kNavActive, kNavAccent } },
            { 'E', "E", "v",   "",    { kNavInactive, kNavActive, kNavAccent } },
            { 'R', "R", ">",   "",    { kNavInactive, kNavActive, kNavAccent } },
            { 'T', "T", "CPY", "KEY", { kActInactive, kActActive, kActAccent } },
            { 'Y', "Y", "PST", "RTG", { kActInactive, kActActive, kActAccent } },
            { 'U', "U", "CLR", "POL", { kActInactive, kActActive, kActAccent } },
            { 'I', "I", "TAP", "MET", { kTapInactive, kTapActive, kTapAccent } },
            { 'P', "P", "PLY", "RST", { kTrnInactive, kTrnActive, kTrnAccent } },
        }};

        const bool showKeyHint = (displayMode_ != GridDisplayMode::Clean);
        const auto gridMode    = uiState_.trigGridMode;

        int leftPad, cellW;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(area.getWidth());
            cellW   = staggerCellW(hu);
            leftPad = staggerOffsetQ(hu);
        }
        else if (displayMode_ == GridDisplayMode::Clean)
        {
            cellW   = (area.getWidth() - kClnColGap) / static_cast<int>(kDefs.size());
            leftPad = 0;
        }
        else
        {
            cellW   = area.getWidth() / static_cast<int>(kDefs.size());
            leftPad = 0;
        }

        for (int i = 0; i < static_cast<int>(kDefs.size()); ++i)
        {
            const auto& def = kDefs[static_cast<std::size_t>(i)];

            int x;
            if (displayMode_ == GridDisplayMode::Clean && i >= 1)
                x = area.getX() + leftPad + cellW + kClnColGap + (i - 1) * cellW;
            else
                x = area.getX() + leftPad + i * cellW;

            const auto cell = juce::Rectangle<int>(x, area.getY(), cellW, area.getHeight());

            const bool isPressed    = juce::KeyPress::isKeyCurrentlyDown(def.keyCode);
            const bool isPlaying    = (def.keyCode == 'P') && processor_.clock().inPluginPlaying();
            const bool isMetActive  = (def.keyCode == 'I') && processor_.clock().isMetronomeEnabled();
            const bool isTrkHeld    = (def.keyCode == 'Q') && uiState_.trackHeld;
            const bool isModeActive = (def.keyCode == 'T' && gridMode == TrigGridMode::Keyboard)
                                   || (def.keyCode == 'Y' && gridMode == TrigGridMode::Retrig)
                                   || (def.keyCode == 'U' && gridMode == TrigGridMode::SoundPool)
                                   || isPlaying || isMetActive || isTrkHeld;

            KeyButtonState state = KeyButtonState::Normal;
            if      (isPressed)          state = KeyButtonState::Pressed;
            else if (isModeActive)       state = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld)  state = KeyButtonState::FuncHeld;

            paintKeyButton(g, cell,
                           def.keyHint, def.primary, def.secondary,
                           def.group, state, showKeyHint);
        }
    }

    // -------------------------------------------------------------------------
    // paintStepRows — reproduces StepGrid::paint() exactly

    void KeyboardArea::paintStepRows(juce::Graphics& g, juce::Rectangle<int> area)
    {
        static const juce::Colour kColActive   { kStepActive   };
        static const juce::Colour kColFillOnly { kStepFillOnly };
        static const juce::Colour kColInactive { kStepInactive };
        static const juce::Colour kColOutRange { kStepOutRange };
        static const juce::Colour kColPlayhead { kStepPlayhead };
        static const juce::Colour kColHeld     { kStepHeld     };
        static const juce::Colour kColPLock    { kStepPLock    };

        const auto navArea  = area.removeFromBottom(kNavRowH);
        const auto cellArea = area;

        const int trackLen = trackLength();
        const auto& clk = processor_.clock();
        auto* divP = processor_.apvts().getRawParameterValue(
            ParamIDs::trackDivider(activeTrack_));
        const int div = divP ? std::max(1, static_cast<int>(divP->load())) : 1;
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
        const auto& track = processor_.sequence().tracks[static_cast<std::size_t>(activeTrack_)];
        const bool fillActive = processor_.fillActive();
        const auto preview = computePagePreview(track, trackLen, loopBase, baseStep, fillActive);

        static constexpr const char* kKeyLetters[kPageSteps] = {
            "S","D","F","G","H","J","K","L",
            "X","C","V","B","N","M",",","."
        };

        static constexpr int kTotalGridCols = kCols + 1;
        const bool showKeyLetters = (displayMode_ != GridDisplayMode::Clean);
        const bool useClnGap      = (displayMode_ == GridDisplayMode::Clean);

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

        auto colX = [&](int row, int col) -> int
        {
            const int stagger = (row == 0) ? staggerA : staggerZ;
            if (useClnGap && col >= 1)
                return cellArea.getX() + stagger + cellW + kClnColGap + (col - 1) * cellW;
            return cellArea.getX() + stagger + col * cellW;
        };

        // Modifier column (A / Z)
        {
            static constexpr const char* kModKeys[kRows]   = { "A", "Z" };
            static constexpr const char* kModLabels[kRows] = { "MUT", "FIL" };
            const KeyGroup modGrp { kModInactive, kModActive, kModAccent };

            for (int row = 0; row < kRows; ++row)
            {
                const int x = colX(row, 0);
                const int y = cellArea.getY() + row * cellH;
                const auto cell = juce::Rectangle<int>(x, y, cellW, cellH);

                const bool isHeld  = (row == 0) ? uiState_.muteHeld : uiState_.fillHeld;
                const bool keyDown = juce::KeyPress::isKeyCurrentlyDown(
                    (row == 0) ? static_cast<int>('A') : static_cast<int>('Z'));

                KeyButtonState st = KeyButtonState::Normal;
                if      (keyDown) st = KeyButtonState::Pressed;
                else if (isHeld)  st = KeyButtonState::ModeActive;

                paintKeyButton(g, cell,
                               showKeyLetters ? kModKeys[row] : "",
                               kModLabels[row], "",
                               modGrp, st, showKeyLetters);
            }
        }

        // Step cells
        const auto& ctx = processor_.editContext();
        const auto& held = ctx.heldSteps();

        for (int row = 0; row < kRows; ++row)
        {
            for (int col = 0; col < kCols; ++col)
            {
                const int localIdx = row * kCols + col;
                const int absIdx   = baseStep + localIdx;
                const bool inRange = absIdx < trackLen;
                const bool isHeld  = inRange
                    && ctx.heldTrackIndex() == activeTrack_
                    && std::find(held.begin(), held.end(), absIdx) != held.end();
                const bool hasTrig = inRange
                    && track.steps[static_cast<std::size_t>(absIdx)].trig;
                const int activeSlot = ctx.activeSlot();
                const bool hasLock   = inRange
                    && !track.steps[static_cast<std::size_t>(absIdx)].overrides.empty();
                const bool hasActiveLock = hasLock && activeSlot >= 0
                    && track.steps[static_cast<std::size_t>(absIdx)].overrides.has(activeSlot);
                const bool isHead = (absIdx == playheadAbs);

                const int x = colX(row, col + 1);
                const int y = cellArea.getY() + row * cellH;
                const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                FillRule stepFillRule = FillRule::Always;
                if (inRange && hasTrig)
                {
                    const auto& rawCond = track.steps[static_cast<std::size_t>(absIdx)].condition;
                    const auto& cond = rawCond.isTrivial() ? track.baseCond : rawCond;
                    stepFillRule = cond.fillRule;
                }

                const float prob = inRange ? preview[static_cast<std::size_t>(localIdx)] : 0.0f;

                if (!inRange)
                {
                    g.setColour(kColOutRange);
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                }
                else if (hasTrig)
                {
                    const bool isOnlyFillInactive =
                        (stepFillRule == FillRule::OnlyFill && !fillActive);
                    const bool isNeverFillActive =
                        (stepFillRule == FillRule::NeverFill && fillActive);

                    juce::Colour baseCol;
                    if (isOnlyFillInactive)
                        baseCol = kColFillOnly;
                    else if (isNeverFillActive)
                        baseCol = kColActive.withAlpha(0.20f);
                    else
                    {
                        const float bright = juce::jlimit(0.15f, 1.0f, 0.15f + prob * 0.85f);
                        baseCol = kColActive.withAlpha(bright);
                    }

                    if (isHead)
                        g.setColour(baseCol.withAlpha(baseCol.getFloatAlpha() * 0.18f));
                    else
                        g.setColour(baseCol);
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                }
                else
                {
                    if (isHead)
                        g.setColour(kColInactive.withAlpha(0.4f));
                    else
                        g.setColour(kColInactive);
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                    if (!isHead)
                    {
                        g.setColour(juce::Colour::fromRGB(70, 85, 100));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }
                }

                if (isHead)
                {
                    g.setColour(kColPlayhead);
                    g.drawRoundedRectangle(cell.toFloat(), 4.0f, 2.0f);
                }

                if (hasLock)
                {
                    g.setColour(kColPLock);
                    g.fillRect(juce::Rectangle<int>(cell.getRight() - 5,
                                                    cell.getY() + 2, 4, 4));
                }
                if (hasActiveLock)
                {
                    g.setColour(kColPLock);
                    g.drawRoundedRectangle(cell.toFloat(), 4.0f, 2.0f);
                }
                if (isHeld)
                {
                    g.setColour(kColHeld);
                    g.drawRoundedRectangle(cell.toFloat(), 4.0f, 2.0f);
                }

                if (showKeyLetters && inRange)
                {
                    g.setFont(juce::Font(juce::FontOptions(8.0f)));
                    g.setColour(juce::Colour::fromRGB(88, 108, 128));
                    g.drawText(kKeyLetters[static_cast<std::size_t>(localIdx)],
                               cell.withHeight(10).reduced(2, 0),
                               juce::Justification::topLeft);
                }

                const bool hasTrackSelect = inRange && (row == 0);
                const bool shiftHeld = uiState_.funcHeld;
                static constexpr int kTrackLabelH = 11;
                const auto stepNumArea = hasTrackSelect
                    ? cell.withTrimmedBottom(kTrackLabelH) : cell;
                const float stepNumAlpha = (hasTrackSelect && shiftHeld) ? 0.35f : 1.0f;
                g.setColour((inRange ? juce::Colour::fromRGB(110, 130, 150)
                                     : juce::Colour::fromRGB(40, 46, 54))
                            .withMultipliedAlpha(stepNumAlpha));
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText(juce::String(absIdx + 1), stepNumArea,
                           juce::Justification::centred);

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

        // Nav row: page info text
        const int pages = numPages();
        g.setColour(juce::Colour::fromRGB(100, 120, 140));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const auto infoRect = navArea.withTrimmedLeft(96).withTrimmedRight(120);
        g.drawText(
            "Page " + juce::String(stepPage_ + 1) + " / " + juce::String(pages)
                + "     Length:",
            infoRect, juce::Justification::centredLeft);

        // Trig grid mode indicator badge
        const auto mode = uiState_.trigGridMode;
        if (mode != TrigGridMode::Default)
        {
            const char* label = (mode == TrigGridMode::Keyboard)  ? "KEY"
                              : (mode == TrigGridMode::Retrig)     ? "RTG"
                                                                   : "POL";
            // Badge positioned in the step cell area's top-left corner
            const auto badgeRect = cellArea.withHeight(18).withWidth(40).reduced(3);
            g.setColour(juce::Colour(0xFFD07030u));
            g.fillRoundedRectangle(badgeRect.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(label, badgeRect, juce::Justification::centred);
        }
    }

}  // namespace lockstep
