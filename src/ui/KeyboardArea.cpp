#include "KeyboardArea.h"
#include "KeyButton.h"
#include "KeyLabel.h"
#include "ScopedSectionMatrix.h"
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
        const int totalH  = getHeight();
        const int usableH = totalH - kNavRowH - 2 * kVertMargin;

        int cellH, row0Y, row1Y, row2Y, intraStepGap, leftX, usableW;

        if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            // Size cells so that half a cell is visible on each side as an edge anchor.
            // 10 cell-widths + 10 gaps fit the full component width:
            //   cellW = (W - 10*g) / 10
            //   leftX = g + cellW/2  (main block inset; edge key centered at x=0)
            const int g  = kOrlGap;
            const int cw = juce::jmax(1, (getWidth() - 10 * g) / 10);
            leftX        = g + cw / 2;
            usableW      = getWidth() - 2 * leftX;
            cellH        = juce::jmax(1, (usableH - 3 * g) / 4);
            intraStepGap = g;
            row0Y = kVertMargin;
            row1Y = row0Y + cellH + g;
            row2Y = row1Y + cellH + g;
        }
        else if (displayMode_ == GridDisplayMode::Clean)
        {
            leftX        = kSideMargin;
            usableW      = getWidth() - 2 * kSideMargin;
            cellH        = juce::jmax(1, (usableH - kClnRowGap) / 4);
            intraStepGap = 0;
            row0Y = kVertMargin;
            row1Y = row0Y + cellH;
            row2Y = row1Y + cellH + kClnRowGap;
        }
        else  // Staggered
        {
            leftX        = kSideMargin;
            usableW      = getWidth() - 2 * kSideMargin;
            cellH        = juce::jmax(1, usableH / 4);
            intraStepGap = 0;
            row0Y = kVertMargin;
            row1Y = row0Y + cellH;
            row2Y = row1Y + cellH;
        }

        RowAreas r;
        r.section      = { leftX, row0Y, usableW, cellH };
        r.function     = { leftX, row1Y, usableW, cellH };
        r.step         = { leftX, row2Y, usableW, (totalH - kVertMargin) - row2Y };
        r.intraStepGap = intraStepGap;
        return r;
    }

    int KeyboardArea::stepRowsLocalY() const
    {
        return computeRowAreas().step.getY();
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

    void KeyboardArea::timerCallback()
    {
        const double ppq = processor_.clock().cumulativePpq();
        if (ppq != lastPpq_)
        {
            lastPpq_ = ppq;
            repaint();
        }
    }

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

        static constexpr int kTotalGridCols = kCols + 2;  // 2 modifier cols + 8 step cols
        const bool useClnGap      = (displayMode_ == GridDisplayMode::Clean);
        const int  intraStepGap   = areas.intraStepGap;

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
            cellW    = (cellArea.getWidth() - (kTotalGridCols - 1) * kOrlGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        const int cellH = (cellArea.getHeight() - intraStepGap) / kRows;
        if (cellW <= 0 || cellH <= 0)
            return -1;

        // Row detection — handle optional intra-step gap for ORL.
        const int relY = pos.getY() - cellArea.getY();
        int row = -1;
        if (relY >= 0 && relY < cellH)
            row = 0;
        else if (relY >= cellH + intraStepGap && relY < 2 * cellH + intraStepGap)
            row = 1;
        if (row < 0)
            return -1;

        const int rowStagger = (row == 0) ? staggerA : staggerZ;
        const int relX = pos.getX() - cellArea.getX() - rowStagger;

        int col;
        if (useClnGap)
        {
            // Cols 0/1 = modifiers, gap, cols 2+ = steps.
            if (relX < 0)                                    return -1;
            if (relX < 2 * cellW)                            col = relX / cellW;
            else if (relX < 2 * cellW + kClnColGap)         return -1;
            else col = 2 + (relX - 2 * cellW - kClnColGap) / cellW;
        }
        else if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            const int pitch = cellW + kOrlGap;
            if (relX < 0 || pitch <= 0)                      return -1;
            col = relX / pitch;
            if (relX % pitch >= cellW)                       return -1;  // click in gap
        }
        else
        {
            col = relX / cellW;
        }

        // Cols 0 and 1 are modifier cells — not step cells.
        if (col <= 1 || col >= kTotalGridCols)
            return -1;

        const int absIdx = stepPage_ * kPageSteps + row * kCols + (col - 2);
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
            // Gap between the 2 left modifier cells and the section + tail cells.
            const int cellW = (w - kClnColGap) / kTotalSectionCells;
            const int x = (cellIndex < kFixedSectionCells)
                ? area.getX() + cellIndex * cellW
                : area.getX() + kFixedSectionCells * cellW + kClnColGap
                      + (cellIndex - kFixedSectionCells) * cellW;
            return { x, area.getY(), cellW, h };
        }
        if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            const int g     = kOrlGap;
            const int cellW = (w - (kTotalSectionCells - 1) * g) / kTotalSectionCells;
            return { area.getX() + cellIndex * (cellW + g), area.getY(), cellW, h };
        }
        // Staggered
        const int cellW = staggerCellW(staggerHalfUnit(w));
        return { area.getX() + cellIndex * cellW, area.getY(), cellW, h };
    }

    int KeyboardArea::cellToSection(int cellIndex)
    {
        // Section cells are kFixedSectionCells .. kFixedSectionCells+kMaxSections-1.
        // Cells 0-1 (modifiers) and tail cells (ARM/PLY) map to -1.
        if (cellIndex < kFixedSectionCells
                || cellIndex >= kFixedSectionCells + IMachine::kMaxSections)
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

        const auto groups = sectionsForKey(track, sectionIndex);
        if (groups.empty())
            return;

        const auto ti = static_cast<std::size_t>(track);
        const auto si = static_cast<std::size_t>(sectionIndex);
        const int combinedPage = uiState_.trackPage[ti][si];

        // Walk the groups to find which section owns the combined page and what
        // page-within-section that corresponds to.
        int remaining = combinedPage;
        for (const auto& g : groups)
        {
            if (remaining < g.pageCount)
            {
                const auto info = processor_.section(track, g.sectionIdx);
                if (info.firstSlot >= 0)
                    onSectionChanged(sectionIndex, combinedPage,
                                     info.firstSlot + kParamsPerPage * remaining);
                return;
            }
            remaining -= g.pageCount;
        }

        // combinedPage out of range (stale state after machine change) — fall back.
        const auto& first = groups.front();
        const auto info   = processor_.section(track, first.sectionIdx);
        if (info.firstSlot >= 0)
            onSectionChanged(sectionIndex, 0, info.firstSlot);
    }

    std::vector<KeyboardArea::SecGroup>
    KeyboardArea::sectionsForKey(int track, int canonicalIdx) const
    {
        std::vector<SecGroup> groups;

        // Canonical section first.
        {
            const auto info = processor_.section(track, canonicalIdx);
            if (info.firstSlot >= 0)
                groups.push_back({ canonicalIdx, std::max(1, info.pageCount) });
        }

        // Extension sections: indices >= kMaxSections whose parentCanonical matches.
        const int total = processor_.numSections(track);
        for (int s = IMachine::kMaxSections; s < total; ++s)
        {
            const auto info = processor_.section(track, s);
            if (info.parentCanonical == canonicalIdx && info.firstSlot >= 0)
                groups.push_back({ s, std::max(1, info.pageCount) });
        }

        return groups;
    }

    bool KeyboardArea::selectSection(int sectionIndex)
    {
        if (activeTrack_ < 0 || activeTrack_ >= static_cast<int>(kNumTracks))
            return false;
        if (sectionIndex < 0 || sectionIndex >= IMachine::kMaxSections)
            return false;

        const auto groups = sectionsForKey(activeTrack_, sectionIndex);
        if (groups.empty())  // no machine slots in this canonical section or its extensions
            return false;

        int totalPages = 0;
        for (const auto& g : groups) totalPages += g.pageCount;

        const auto ti = static_cast<std::size_t>(activeTrack_);
        const auto si = static_cast<std::size_t>(sectionIndex);

        const bool wasInMasterMode = (uiState_.masterSection != -1);
        uiState_.masterSection = -1;

        if (!wasInMasterMode && uiState_.trackSection[ti] == sectionIndex)
        {
            uiState_.trackPage[ti][si] = (uiState_.trackPage[ti][si] + 1) % totalPages;
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
        if (uiState_.masterSection >= 0)
            return;

        const auto ti = static_cast<std::size_t>(activeTrack_);
        int sec = uiState_.trackSection[ti];

        // If the current section is empty for this machine, snap to the first available one.
        if (processor_.section(activeTrack_, sec).firstSlot < 0)
        {
            for (int s = 0; s < IMachine::kMaxSections; ++s)
            {
                if (processor_.section(activeTrack_, s).firstSlot >= 0)
                {
                    uiState_.trackSection[ti] = s;
                    sec = s;
                    break;
                }
            }
        }

        notifySectionChanged(sec, activeTrack_);
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
    // paintSectionRow — number row (10 cells, MHY identities):
    //   Func(1)  Track(2)  TAP(3)  ^(4)  TRIG(5) SRC(6) FLTR(7) AMP(8) MOD(9) FX(0)
    //   cell 0   cell 1    cell 2  cell3  cell 4   ...                          cell 9

    void KeyboardArea::paintSectionRow(juce::Graphics& g, juce::Rectangle<int> area)
    {
        paintEdgeRow(g, 0, area);
        const bool showKeyHint = (displayMode_ != GridDisplayMode::Clean);
        const int  activeTrack = activeTrack_;

        static constexpr const char* kKeyHints[kTotalSectionCells] = {
            "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"
        };

        // Compound-chord overlay: show on modifier cells when two cross-column modifiers held.
        // MHY columns: col-1 non-Func = {pattern, scene, mute}; col-2 = {track, part, master, fill}.
        const bool col1any = uiState_.patternScopeHeld || uiState_.sceneHeld || uiState_.muteHeld;
        const bool col2any = uiState_.trackHeld || uiState_.partHeld
                                                || uiState_.masterHeld || uiState_.fillHeld;
        const bool hasCompound = (uiState_.funcHeld && (col1any || col2any)) || (col1any && col2any);

        // Cell 0: Func (key 1) — amber, universal qualifier (col-1 row 0).
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('1'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)           st = KeyButtonState::Pressed;
            else if (uiState_.funcHeld) st = KeyButtonState::ModeActive;
            const KeyGroup grp { kFuncInactive, kFuncActive, kFuncAccent };
            const bool overlay = hasCompound && uiState_.funcHeld;
            paintKeyButton(g, sectionCellBounds(0, area), kKeyHints[0], "FUNC", "",
                           grp, st, showKeyHint, overlay);
        }

        // Cell 1: Track (key 2) — section-scope (col-2 row 0), violet.
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('2'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)            st = KeyButtonState::Pressed;
            else if (uiState_.trackHeld) st = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld)  st = KeyButtonState::FuncHeld;
            const KeyGroup grp { kPerfInactive, kPerfActive, kPerfAccent };
            const bool overlay = hasCompound && uiState_.trackHeld;
            paintKeyButton(g, sectionCellBounds(1, area), kKeyHints[1], "TRACK", "",
                           grp, st, showKeyHint, overlay);
        }

        // Cell 2: TAP (key 3) — tap tempo; Func+3 = MetronomeToggle.
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('3'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)           st = KeyButtonState::Pressed;
            else if (uiState_.funcHeld) st = KeyButtonState::FuncHeld;
            const KeyGroup grp { kTapInactive, kTapActive, kTapAccent };
            paintKeyButton(g, sectionCellBounds(2, area), kKeyHints[2], "TAP", "MET",
                           grp, st, showKeyHint);
        }

        // Cell 3: NavUp (key 4) — nav up; Func-layer: TrigModeSoundPool
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('4'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)           st = KeyButtonState::Pressed;
            else if (uiState_.funcHeld) st = KeyButtonState::FuncHeld;
            const KeyGroup grp { kNavInactive, kNavActive, kNavAccent };
            paintKeyButton(g, sectionCellBounds(3, area), kKeyHints[3], u8"↑", "POOL",
                           grp, st, showKeyHint);
        }

        // Section key codes for keys 5,6,7,8,9,0 (can't use arithmetic: '0' != '5'+5).
        static constexpr int kSectionKeyCodes[IMachine::kMaxSections] = {
            '5', '6', '7', '8', '9', '0'
        };

        // Determine the active section-suite scope (MHY.5). Performance-specialist
        // scopes (Mute, Fill) have no section suite so they don't override labels.
        using PS = EditMode::PrimaryScope;
        PS sectionScope = PS::None;
        if      (uiState_.trackHeld)        sectionScope = PS::Track;
        else if (uiState_.patternScopeHeld) sectionScope = PS::Pattern;
        else if (uiState_.partHeld)         sectionScope = PS::Part;
        else if (uiState_.sceneHeld)        sectionScope = PS::Scene;
        else if (uiState_.masterHeld)       sectionScope = PS::Master;
        const bool isScopedMode = (sectionScope != PS::None);

        // Cells 4-9: section keys 5-0 — canonical TRIG/SRC/FILTER/AMP/MOD/FX.
        // resolveKeyLabel() drives label + availability; page-dots paint separately.
        for (int s = 0; s < IMachine::kMaxSections; ++s)
        {
            const int cellIdx = kFixedSectionCells + s;

            const auto groups         = sectionsForKey(activeTrack, s);
            const bool machineHasSection = !groups.empty();

            // Build a KeyDef for the resolver.  funcLayer carries the meta-section
            // secondary (em-dash for reserved cells, label otherwise); the resolver
            // returns it in KeyLabel.hint for non-scoped mode.
            const char* metaLabel = isReservedMeta(s)
                ? nullptr  // em-dash handled below; can't store non-ASCII in const char*
                : kMetaLabels[static_cast<std::size_t>(s)];
            const KeyDef kd {
                KeyRole::SectionKey,
                IMachine::kCanonicalSectionNames[static_cast<std::size_t>(s)],
                (metaLabel != nullptr) ? metaLabel : "",
                s,
                machineHasSection
            };
            const KeyLabel kl = resolveKeyLabel(kd, uiState_, processor_.editContext());

            const bool available = !kl.disabled;

            if (!available)
            {
                const bool pressed = juce::KeyPress::isKeyCurrentlyDown(kSectionKeyCodes[s]);
                const KeyGroup grp { kSecInactive, kSecActive, kSecAccent };
                paintKeyButton(g, sectionCellBounds(cellIdx, area),
                               kKeyHints[cellIdx], kl.primary, "", grp,
                               pressed ? KeyButtonState::Pressed : KeyButtonState::Disabled,
                               showKeyHint);
                continue;
            }

            const bool isMasterActive = !isScopedMode && (uiState_.masterSection == s);
            const bool isTrackActive  = !isScopedMode && (uiState_.masterSection == -1
                && uiState_.trackSection[static_cast<std::size_t>(activeTrack)] == s);

            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(kSectionKeyCodes[s]);

            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)                           st = KeyButtonState::Pressed;
            else if (isTrackActive || isMasterActive)   st = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld)                 st = KeyButtonState::FuncHeld;

            const KeyGroup secGrp = isMasterActive
                ? KeyGroup{ kSecInactive, 0xFF404010u, 0xFFFFB432u }
                : KeyGroup{ kSecInactive, kSecActive,  kSecAccent  };

            // Secondary (meta) label: resolver supplies it for normal mode; em-dash for
            // reserved-meta sections (non-ASCII, handled outside resolver).
            const juce::String secLabel = isScopedMode
                ? juce::String{}
                : (isReservedMeta(s)
                    ? juce::String::charToString(0x2014)
                    : kl.hint);

            const auto cell = sectionCellBounds(cellIdx, area);
            paintKeyButton(g, cell, kKeyHints[cellIdx],
                           kl.primary, secLabel, secGrp, st, showKeyHint);

            // Page dots — reflect total combined pages (canonical + extension).
            int totalPageCount = 0;
            for (const auto& grp : groups) totalPageCount += grp.pageCount;
            if (totalPageCount > 1 && !isMasterActive)
            {
                const auto ti        = static_cast<std::size_t>(activeTrack);
                const auto si        = static_cast<std::size_t>(s);
                const int activePage = uiState_.trackPage[ti][si];

                const juce::Colour dotCol = isTrackActive
                    ? kColourTrackActive : kColourTrackActive.withAlpha(0.4f);

                const int dotSize    = 4;
                const int dotSpacing = 6;
                const int totalDotW  = totalPageCount * dotSpacing - (dotSpacing - dotSize);
                int dotX = cell.getCentreX() - totalDotW / 2;
                const int dotY = cell.getBottom() - 6;

                for (int p = 0; p < totalPageCount; ++p)
                {
                    const bool isActiveDot = (p == activePage) && isTrackActive;
                    g.setColour(dotCol.withAlpha(isActiveDot ? 1.0f : 0.3f));
                    if (isActiveDot)
                        g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                                      static_cast<float>(dotSize), static_cast<float>(dotSize));
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
    // paintFunctionRow — Q-row (10 keys, MHY identities):
    //   Q/PAT  W/PART  E/<  R/v  T/>  Y/YES  U/REC  I/PLAY  O/STOP  P/NO
    //   MHZ.1: labels expanded to 6-char cap; resolveKeyLabel() drives U/I/O.

    void KeyboardArea::paintFunctionRow(juce::Graphics& g, juce::Rectangle<int> area)
    {
        paintEdgeRow(g, 1, area);
        // Note: string fields use const char8_t* so nav-key arrow glyphs (E/R/T)
        // can be stored as U+2190/2193/2192 UTF-8 sequences. juce::String has a
        // dedicated const char8_t* overload. Plain-ASCII labels use u8"..." for
        // type-consistency across the array.
        struct QKeyDef
        {
            int             keyCode;
            const char8_t*  keyHint;
            const char8_t*  primary;    // natural label (default state)
            const char8_t*  secondary;  // Func-layer secondary; "" = none
            KeyGroup        group;
            KeyRole         role = KeyRole::Utility;
        };

        // MHZ.1.2: labels expanded where they benefit (≤ 6 chars hard cap).
        // U/I/O carry KeyRole so resolveKeyLabel() drives their primary + hint.
        static const std::array<QKeyDef, 10> kDefs = {{
            { 'Q', u8"Q", u8"PAT",   u8"",      { kModInactive,  kModActive,  kModAccent  }, KeyRole::Modifier  },
            { 'W', u8"W", u8"PART",  u8"",      { kPerfInactive, kPerfActive, kPerfAccent }, KeyRole::Modifier  },
            { 'E', u8"E", u8"←",     u8"RST",   { kNavInactive,  kNavActive,  kNavAccent  }, KeyRole::Nav       },
            { 'R', u8"R", u8"↓",     u8"KEY",   { kNavInactive,  kNavActive,  kNavAccent  }, KeyRole::Nav       },
            { 'T', u8"T", u8"→",     u8"RETRIG",{ kNavInactive,  kNavActive,  kNavAccent  }, KeyRole::Nav       },
            { 'Y', u8"Y", u8"YES",   u8"SNAP",  { kActInactive,  kActActive,  kActAccent  }, KeyRole::VerbYes   },
            { 'U', u8"U", u8"REC",   u8"",      { kRecInactive,  kRecActive,  kRecAccent  }, KeyRole::VerbCopy  },
            { 'I', u8"I", u8"PLAY",  u8"",      { kTrnInactive,  kTrnActive,  kTrnAccent  }, KeyRole::VerbPaste },
            { 'O', u8"O", u8"STOP",  u8"RST",   { kTrnInactive,  kTrnActive,  kTrnAccent  }, KeyRole::VerbClear },
            { 'P', u8"P", u8"NO",    u8"POP",   { kActInactive,  kActActive,  kActAccent  }, KeyRole::VerbNo    },
        }};

        const bool showKeyHint = (displayMode_ != GridDisplayMode::Clean);

        // Compound overlay on Q (Pattern) and W (Part) — MHY identities.
        const bool col1any = uiState_.patternScopeHeld || uiState_.sceneHeld || uiState_.muteHeld;
        const bool col2any = uiState_.trackHeld || uiState_.partHeld
                                                || uiState_.masterHeld || uiState_.fillHeld;
        const bool hasCompound = (uiState_.funcHeld && (col1any || col2any)) || (col1any && col2any);

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
        else  // Ortholinear
        {
            const int n = static_cast<int>(kDefs.size());
            cellW   = (area.getWidth() - (n - 1) * kOrlGap) / n;
            leftPad = 0;
        }

        for (int i = 0; i < static_cast<int>(kDefs.size()); ++i)
        {
            const auto& def = kDefs[static_cast<std::size_t>(i)];

            int x;
            if (displayMode_ == GridDisplayMode::Clean)
            {
                // Gap between the 2 modifier cells (Q, W) and the nav/verb keys.
                if (i >= 2)
                    x = area.getX() + 2 * cellW + kClnColGap + (i - 2) * cellW;
                else
                    x = area.getX() + i * cellW;
            }
            else if (displayMode_ == GridDisplayMode::Ortholinear)
                x = area.getX() + i * (cellW + kOrlGap);
            else  // Staggered
                x = area.getX() + leftPad + i * cellW;

            const auto cell = juce::Rectangle<int>(x, area.getY(), cellW, area.getHeight());

            const bool isPressed    = juce::KeyPress::isKeyCurrentlyDown(def.keyCode);
            const bool isArmed      = (def.keyCode == 'U') && processor_.clock().isRecordArmed();
            const bool isPlaying    = (def.keyCode == 'I') && processor_.clock().inPluginPlaying();
            const bool isPatHeld    = (def.keyCode == 'Q') && uiState_.patternScopeHeld;
            const bool isPrtHeld    = (def.keyCode == 'W') && uiState_.partHeld;
            const bool isModeActive = isArmed || isPlaying || isPatHeld || isPrtHeld;

            const bool sectionScopeHeld = uiState_.trackHeld || uiState_.patternScopeHeld
                || uiState_.partHeld || uiState_.sceneHeld || uiState_.masterHeld;
            const bool isVerbKey = (def.keyCode == 'Y' || def.keyCode == 'U'
                                 || def.keyCode == 'I' || def.keyCode == 'O'
                                 || def.keyCode == 'P');

            KeyButtonState state = KeyButtonState::Normal;
            if      (isPressed)                         state = KeyButtonState::Pressed;
            else if (isModeActive)                      state = KeyButtonState::ModeActive;
            else if (sectionScopeHeld && isVerbKey)     state = KeyButtonState::FuncHeld;
            else if (uiState_.funcHeld)                 state = KeyButtonState::FuncHeld;

            const bool overlay = hasCompound
                && ((def.keyCode == 'Q' && uiState_.patternScopeHeld)
                 || (def.keyCode == 'W' && uiState_.partHeld));

            // MHZ.1.3: resolveKeyLabel() drives primary + hint for verb keys.
            // Nav/Modifier keys use def.primary / def.secondary directly (arrows
            // are stored as char8_t* and cannot pass through the const char* resolver).
            juce::String displayPrimary { def.primary };
            juce::String displayHint    { def.secondary };

            if (def.role == KeyRole::VerbCopy
             || def.role == KeyRole::VerbPaste
             || def.role == KeyRole::VerbClear)
            {
                // secondary is ASCII (RST / empty) — safe to reinterpret.
                const KeyDef kd {
                    def.role,
                    reinterpret_cast<const char*>(def.primary),
                    reinterpret_cast<const char*>(def.secondary),
                    -1, true
                };
                const KeyLabel kl = resolveKeyLabel(kd, uiState_, processor_.editContext());
                displayPrimary = kl.primary;
                displayHint    = kl.hint;
            }

            paintKeyButton(g, cell,
                           def.keyHint, displayPrimary, displayHint,
                           def.group, state, showKeyHint, overlay);
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

        // MHX step keys: D-; (steps 0-7, A row), C-/ (steps 8-15, Z row).
        static constexpr const char* kKeyLetters[kPageSteps] = {
            "D","F","G","H","J","K","L",";",
            "C","V","B","N","M",",",".","/"
        };

        // MHX: 2 modifier columns (A/S | Z/X) + 8 step columns = 10 total.
        static constexpr int kTotalGridCols = kCols + 2;
        const bool showKeyLetters = (displayMode_ != GridDisplayMode::Clean);
        const bool useClnGap      = (displayMode_ == GridDisplayMode::Clean);
        const int  intraStepGap   = (displayMode_ == GridDisplayMode::Ortholinear) ? kOrlGap : 0;

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
            cellW    = (cellArea.getWidth() - (kTotalGridCols - 1) * kOrlGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        const int cellH = (cellArea.getHeight() - intraStepGap) / kRows;

        auto rowY = [&](int row) -> int
        {
            return cellArea.getY() + row * cellH + (row > 0 ? intraStepGap : 0);
        };

        // col 0/1 = modifiers, col 2+ = steps.
        auto colX = [&](int row, int col) -> int
        {
            const int stagger = (row == 0) ? staggerA : staggerZ;
            if (useClnGap && col >= 2)
                return cellArea.getX() + stagger + 2 * cellW + kClnColGap + (col - 2) * cellW;
            if (useClnGap)
                return cellArea.getX() + stagger + col * cellW;
            if (displayMode_ == GridDisplayMode::Ortholinear)
                return cellArea.getX() + col * (cellW + kOrlGap);
            return cellArea.getX() + stagger + col * cellW;
        };

        // Edge anchor rows
        for (int row = 0; row < kRows; ++row)
        {
            const auto rowRect = juce::Rectangle<int>(cellArea.getX(), rowY(row),
                                                       cellArea.getWidth(), cellH);
            paintEdgeRow(g, 2 + row, rowRect);
        }

        // Compound overlay state — MHY columns.
        const bool col1any = uiState_.patternScopeHeld || uiState_.sceneHeld || uiState_.muteHeld;
        const bool col2any = uiState_.trackHeld || uiState_.partHeld
                                                || uiState_.masterHeld || uiState_.fillHeld;
        const bool hasCompound = (uiState_.funcHeld && (col1any || col2any)) || (col1any && col2any);

        // Two modifier columns per row (MHY identities):
        //   col-1: row 0 = A/SCN, row 1 = Z/MUT
        //   col-2: row 0 = S/MST, row 1 = X/FIL
        {
            struct ModDef {
                int  keyCode;
                const char* keyHint;
                const char* label;
                bool        isHeld;
                KeyGroup    grp;
                bool        overlay;
            };

            const std::array<std::array<ModDef, 2>, kRows> mods = {{
                // Row 0 (A row): A=Scene (col-1), S=Master (col-2)
                std::array<ModDef, 2>{{
                    { 'A', "A", "SCENE",  uiState_.sceneHeld,
                      { kModInactive, kModActive, kModAccent },
                      hasCompound && uiState_.sceneHeld },
                    { 'S', "S", "MASTER", uiState_.masterHeld,
                      { kPerfInactive, kPerfActive, kPerfAccent },
                      hasCompound && uiState_.masterHeld },
                }},
                // Row 1 (Z row): Z=Mute (col-1), X=Fill (col-2)
                std::array<ModDef, 2>{{
                    { 'Z', "Z", "MUTE", uiState_.muteHeld,
                      { kModInactive, kModActive, kModAccent },
                      hasCompound && uiState_.muteHeld },
                    { 'X', "X", "FILL", uiState_.fillHeld,
                      { kPerfInactive, kPerfActive, kPerfAccent },
                      hasCompound && uiState_.fillHeld },
                }},
            }};

            for (int row = 0; row < kRows; ++row)
            {
                for (int mc = 0; mc < 2; ++mc)
                {
                    const auto& md  = mods[static_cast<std::size_t>(row)]
                                         [static_cast<std::size_t>(mc)];
                    const int x     = colX(row, mc);
                    const int y     = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH);

                    const bool keyDown = juce::KeyPress::isKeyCurrentlyDown(md.keyCode);
                    KeyButtonState st  = KeyButtonState::Normal;
                    if      (keyDown)    st = KeyButtonState::Pressed;
                    else if (md.isHeld)  st = KeyButtonState::ModeActive;

                    paintKeyButton(g, cell,
                                   showKeyLetters ? md.keyHint : "",
                                   md.label, "",
                                   md.grp, st, showKeyLetters, md.overlay);
                }
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

                const int x = colX(row, col + 2);  // +2: skip the two modifier columns
                const int y = rowY(row);
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

                // MG.1: In Keyboard mode show the note name centred on the cell.
                if (uiState_.trigGridMode == TrigGridMode::Keyboard && inRange)
                {
                    static constexpr const char* kNoteNames[] = {
                        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
                    };
                    const int midiNote  = juce::jlimit(0, 127,
                                             uiState_.keyboardRoot + localIdx);
                    const int noteClass = midiNote % 12;
                    const bool isSharp  = (noteClass == 1 || noteClass == 3 || noteClass == 6
                                        || noteClass == 8 || noteClass == 10);
                    g.setFont(juce::Font(juce::FontOptions(9.0f)));
                    g.setColour(isSharp ? juce::Colour::fromRGB(200, 160, 100)
                                       : juce::Colour::fromRGB(220, 220, 220));
                    g.drawText(juce::String(kNoteNames[noteClass]),
                               cell.reduced(2),
                               juce::Justification::centred, false);
                }

                // MG.5: In Sound Pool mode show the pool entry index on the cell.
                if (uiState_.trigGridMode == TrigGridMode::SoundPool && inRange)
                {
                    const int poolSize = processor_.soundPoolSize();
                    const bool hasEntry = localIdx < poolSize;
                    g.setFont(juce::Font(juce::FontOptions(9.0f)));
                    if (hasEntry)
                    {
                        const auto* e = processor_.soundPoolEntry(localIdx);
                        g.setColour(juce::Colour::fromRGB(180, 220, 180));
                        const juce::String label = (e != nullptr)
                            ? juce::String(localIdx + 1) + " " + juce::String(e->name).substring(0, 6)
                            : juce::String(localIdx + 1);
                        g.drawText(label, cell.reduced(2),
                                   juce::Justification::centred, true);
                    }
                    else
                    {
                        g.setColour(juce::Colour::fromRGB(60, 70, 80));
                        g.drawText("--", cell.reduced(2),
                                   juce::Justification::centred, false);
                    }
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
            static constexpr const char* kRateLabels[] = {
                "RTG 1/16", "RTG 1/32", "RTG 1/48", "RTG 1/96"
            };
            const char* label;
            int badgeW;
            if (mode == TrigGridMode::Keyboard)  { label = "KEY"; badgeW = 36; }
            else if (mode == TrigGridMode::Retrig)
            {
                label  = kRateLabels[juce::jlimit(0, 3, uiState_.retrigRateIndex)];
                badgeW = 68;
            }
            else { label = "SPL"; badgeW = 36; }

            const auto badgeRect = cellArea.withHeight(18).withWidth(badgeW).reduced(3);
            g.setColour(juce::Colour(0xFFD07030u));
            g.fillRoundedRectangle(badgeRect.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(label, badgeRect, juce::Justification::centred);
        }
    }

    // -------------------------------------------------------------------------
    // paintEdgeRow — decorative anchor keys just outside the 9-column block
    //
    // ORL: one equal-width half-cell on each side, centred at the component edge.
    // STG: ANSI-accurate widths, walking outward until off-screen (JUCE clips).
    // CLN: no edge keys (returns immediately).

    void KeyboardArea::paintEdgeRow(juce::Graphics& g,
                                     int rowIndex,
                                     juce::Rectangle<int> rowArea) const
    {
        if (displayMode_ == GridDisplayMode::Clean)
            return;

        const bool showHint = true;
        // Dimmed group: very dark, clearly non-interactive
        const KeyGroup dimGrp { 0xFF0E1218u, 0xFF1C2430u, 0xFF3040A0u };

        if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            const int gap   = kOrlGap;
            // 10 cells, 9 internal gaps
            const int cellW = (rowArea.getWidth() - 9 * gap) / 10;
            const int cellH = rowArea.getHeight();

            // Left edge key: right edge abuts main block left, center at x=0 (half clipped)
            const auto leftRect = juce::Rectangle<int>(
                rowArea.getX() - gap - cellW, rowArea.getY(), cellW, cellH);
            // Right edge key: left edge abuts main block right, center at x=W (half clipped)
            const auto rightRect = juce::Rectangle<int>(
                rowArea.getRight() + gap, rowArea.getY(), cellW, cellH);

            // Per-row labels/keycodes. In MHX the 10th key of each row is in the main block
            // (0/P/;// respectively), so the right-edge is the key after that.
            static const int        kLeftCode[4]   = { 96, 9,  0,  0  };  // ` Tab CAP SHF
            static const char*const kLeftLabel[4]  = { "`", "TAB", "CAP", "SHF" };
            static const int        kRightCode[4]  = { 45, 91, 39,  0  };  // - [ ' (none)
            static const char*const kRightLabel[4] = { "-", "[", "'", "" };

            {
                const int kc = kLeftCode[rowIndex];
                const bool pressed = (kc > 0)
                    ? juce::KeyPress::isKeyCurrentlyDown(kc)
                    : (rowIndex == 3 && juce::ModifierKeys::currentModifiers.isShiftDown());
                paintKeyButton(g, leftRect, kLeftLabel[rowIndex], "", "",
                               dimGrp,
                               pressed ? KeyButtonState::Pressed : KeyButtonState::Normal,
                               showHint);
            }
            {
                const int kc      = kRightCode[rowIndex];
                const bool pressed = (kc > 0) && juce::KeyPress::isKeyCurrentlyDown(kc);
                paintKeyButton(g, rightRect, kRightLabel[rowIndex], "", "",
                               dimGrp,
                               pressed ? KeyButtonState::Pressed : KeyButtonState::Normal,
                               showHint);
            }
        }
        else  // Staggered
        {
            const int hu  = staggerHalfUnit(rowArea.getWidth());
            const int cw  = staggerCellW(hu);   // 2*hu
            const int rY  = rowArea.getY();
            const int rH  = rowArea.getHeight();

            // Row stagger offsets (half-units): number=0, Q=1, A=2, Z=3
            static constexpr int kStaggerHu[4] = { 0, 1, 2, 3 };
            const int mainLeft  = rowArea.getX() + kStaggerHu[rowIndex] * hu;
            const int mainRight = mainLeft + 10 * cw;  // MHX: 10-wide

            // Left edge key: one key per row, ANSI width (backtick=2hu, Tab=3hu,
            // CapsLock=4hu, LShift=5hu).  All start at the same x (kSideMargin-2hu).
            {
                static const int kLWHu[4] = { 2, 3, 4, 5 };
                static const int kLCode[4] = { 96, 9, 0, 0 };  // ` Tab - -
                static const char*const kLLabel[4] = { "`", "TAB", "CAP", "SHF" };
                const int keyW = kLWHu[rowIndex] * hu;
                const int kx   = mainLeft - keyW;
                const int kc = kLCode[rowIndex];
                const bool pressed = (kc > 0)
                    ? juce::KeyPress::isKeyCurrentlyDown(kc)
                    : (rowIndex == 3 && juce::ModifierKeys::currentModifiers.isShiftDown());
                paintKeyButton(g, juce::Rectangle<int>(kx, rY, keyW, rH),
                               kLLabel[rowIndex], "", "", dimGrp,
                               pressed ? KeyButtonState::Pressed : KeyButtonState::Normal,
                               showHint);
            }

            // Right edge keys: regular 2hu keys walking right until off-screen.
            // In MHX all 10 keys per row are in the main block (0/P/;// included),
            // so right-edge keys are the physical keys after the 10th column.
            {
                static const int    kRCode[4][3] = {
                    { 45, 61,  0 },  // number: -  =  (0 is now in main block)
                    { 91, 93,  0 },  // Q:      [  ]  (P is now in main block)
                    { 39,  0,  0 },  // A:      '     (; is now in main block)
                    {  0,  0,  0 },  // Z:      none  (/ is now in main block)
                };
                static const char*const kRLabel[4][3] = {
                    { "-", "=", "" },
                    { "[", "]", "" },
                    { "'", "",  "" },
                    { "",  "",  "" },
                };
                const int compW = getWidth();
                int rx = mainRight;
                for (int j = 0; j < 3; ++j)
                {
                    if (kRLabel[rowIndex][j][0] == '\0') break;
                    if (rx >= compW)                     break;
                    const int kc = kRCode[rowIndex][j];
                    const bool pressed = (kc > 0) && juce::KeyPress::isKeyCurrentlyDown(kc);
                    paintKeyButton(g, juce::Rectangle<int>(rx, rY, cw, rH),
                                   kRLabel[rowIndex][j], "", "", dimGrp,
                                   pressed ? KeyButtonState::Pressed : KeyButtonState::Normal,
                                   showHint);
                    rx += cw;
                }
            }
        }
    }

}  // namespace lockstep
