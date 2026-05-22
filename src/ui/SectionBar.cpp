#include "SectionBar.h"
#include "KeyButton.h"
#include "UITheme.h"
#include "StepGrid.h"

namespace lockstep
{
    using namespace theme;

    const juce::Colour SectionBar::kColourTrackActive  = juce::Colour::fromRGB( 62, 200, 200);
    const juce::Colour SectionBar::kColourMasterActive = juce::Colour::fromRGB(255, 180,  50);
    const juce::Colour SectionBar::kColourInactive     = juce::Colour::fromRGB( 45,  55,  65);
    const juce::Colour SectionBar::kColourShiftActive  = juce::Colour::fromRGB(160, 175, 195);

    SectionBar::SectionBar(LockstepProcessor& processor, StepGrid& grid, UiState& uiState)
        : processor_(processor), grid_(grid), uiState_(uiState)
    {
        startTimerHz(20);
    }

    // -------------------------------------------------------------------------

    juce::Rectangle<int> SectionBar::cellBounds(int cellIndex) const
    {
        const int w = getWidth();
        const int h = getHeight();
        if (displayMode_ == GridDisplayMode::Clean)
        {
            const int cellW = (w - kClnColGap) / kTotalCells;
            const int x = (cellIndex == 0) ? 0
                          : cellW + kClnColGap + (cellIndex - 1) * cellW;
            return { x, 0, cellW, h };
        }
        const int cellW = (displayMode_ == GridDisplayMode::Staggered)
                              ? staggerCellW(staggerHalfUnit(w))
                              : w / kTotalCells;
        return { cellIndex * cellW, 0, cellW, h };
    }

    int SectionBar::cellToSection(int cellIndex)
    {
        if (cellIndex < kFixedCells || cellIndex >= kTotalCells - kTailCells)
            return -1;
        return cellIndex - kFixedCells;
    }

    // -------------------------------------------------------------------------

    void SectionBar::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colour::fromRGB(20, 22, 26));
        g.fillAll();

        const bool showKeyHint = (displayMode_ != GridDisplayMode::Clean);
        const int  activeTrack = grid_.getActiveTrack();

        static constexpr const char* kKeyHints[kTotalCells] = {
            "1", "2", "3", "4", "5", "6", "7", "8", "9"
        };

        // ---- Cell 0: FNC ----
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('1'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)           st = KeyButtonState::Pressed;
            else if (uiState_.funcHeld) st = KeyButtonState::ModeActive;
            const KeyGroup grp { kFuncInactive, kFuncActive, kFuncAccent };
            paintKeyButton(g, cellBounds(0), kKeyHints[0], "FNC", "", grp, st, showKeyHint);
        }

        // ---- Cell 1: REC ----
        {
            const bool isArmed = processor_.clock().isRecordArmed();
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('2'));
            KeyButtonState st = KeyButtonState::Normal;
            if      (pressed)           st = KeyButtonState::Pressed;
            else if (isArmed)           st = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld) st = KeyButtonState::FuncHeld;
            const KeyGroup grp { kRecInactive, kRecActive, kRecAccent };
            paintKeyButton(g, cellBounds(1), kKeyHints[1], "REC", "SNP", grp, st, showKeyHint);
        }

        // ---- Cell 2: Nav-Up (▲) — rendered as nav key ----
        {
            const bool pressed = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('3'));
            KeyButtonState st = pressed ? KeyButtonState::Pressed : KeyButtonState::Normal;
            if (!pressed && uiState_.funcHeld) st = KeyButtonState::FuncHeld;
            const KeyGroup grp { kNavInactive, kNavActive, kNavAccent };
            paintKeyButton(g, cellBounds(2), kKeyHints[2], "^", "", grp, st, showKeyHint);
        }

        // ---- Section cells (3-8, keys 4-9) ----
        for (int s = 0; s < IMachine::kMaxSections; ++s)
        {
            const int cellIdx = kFixedCells + s;
            const bool available = (s < processor_.numSections(activeTrack));

            if (!available)
            {
                const KeyGroup grp { kSecInactive, kSecActive, kSecAccent };
                paintKeyButton(g, cellBounds(cellIdx),
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
            if      (pressed)                       st = KeyButtonState::Pressed;
            else if (isTrackActive || isMasterActive) st = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld)             st = KeyButtonState::FuncHeld;

            // Section key: use the teal group; master-active uses amber accent
            const KeyGroup secGrp = isMasterActive
                ? KeyGroup{ kSecInactive, 0xFF404010u, 0xFFFFB432u }
                : KeyGroup{ kSecInactive, kSecActive,  kSecAccent  };

            const auto info = processor_.section(activeTrack, s);
            const juce::String primLabel = info.label.isEmpty()
                                           ? juce::String(s) : info.label;
            const bool reserved = isReservedMeta(s);
            const juce::String secLabel =
                reserved ? juce::String::charToString(0x2014)
                         : juce::String(kMetaLabels[static_cast<std::size_t>(s)]);

            const auto cell = cellBounds(cellIdx);
            paintKeyButton(g, cell, kKeyHints[cellIdx],
                           primLabel, secLabel, secGrp, st, showKeyHint);

            // Page dots — drawn below the inner button area if multiple pages exist
            const int pageCount = info.pageCount;
            if (pageCount > 1 && !isMasterActive)
            {
                const int ti = static_cast<int>(activeTrack);
                const int activePage = uiState_.trackPage
                    [static_cast<std::size_t>(ti)]
                    [static_cast<std::size_t>(s)];

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

    void SectionBar::mouseDown(const juce::MouseEvent& e)
    {
        int cellIndex = -1;
        for (int i = 0; i < kTotalCells; ++i)
        {
            if (cellBounds(i).contains(e.getPosition()))
            {
                cellIndex = i;
                break;
            }
        }
        const int section = (cellIndex >= 0) ? cellToSection(cellIndex) : -1;
        if (section < 0)
            return;

        if (uiState_.funcHeld)
            selectMetaSection(section);
        else
            selectSection(section);
    }

    bool SectionBar::selectSection(int sectionIndex)
    {
        const int activeTrack = grid_.getActiveTrack();
        if (activeTrack < 0 || activeTrack >= static_cast<int>(kNumTracks))
            return false;
        if (sectionIndex < 0 || sectionIndex >= IMachine::kMaxSections)
            return false;
        if (sectionIndex >= processor_.numSections(activeTrack))
            return false;

        const auto ti = static_cast<std::size_t>(activeTrack);
        const auto si = static_cast<std::size_t>(sectionIndex);

        const bool wasInMasterMode = (uiState_.masterSection != -1);
        uiState_.masterSection = -1;

        if (!wasInMasterMode && uiState_.trackSection[ti] == sectionIndex)
        {
            const auto info = processor_.section(activeTrack, sectionIndex);
            const int pageCount = info.pageCount > 0 ? info.pageCount : 1;
            uiState_.trackPage[ti][si] = (uiState_.trackPage[ti][si] + 1) % pageCount;
        }
        else
        {
            uiState_.trackSection[ti] = sectionIndex;
            uiState_.trackPage[ti][si] = 0;
        }

        repaint();
        notifyChanged(sectionIndex, activeTrack);
        return true;
    }

    void SectionBar::selectMetaSection(int sectionIndex)
    {
        if (isReservedMeta(sectionIndex))
            return;

        uiState_.masterSection = (uiState_.masterSection == sectionIndex) ? -1 : sectionIndex;
        repaint();

        if (onMetaSectionChanged)
            onMetaSectionChanged(uiState_.masterSection);
    }

    void SectionBar::setDisplayMode(GridDisplayMode mode)
    {
        displayMode_ = mode;
        repaint();
    }

    void SectionBar::syncToActiveTrack()
    {
        repaint();
        const int track = grid_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;
        if (uiState_.masterSection < 0)
            notifyChanged(uiState_.trackSection[static_cast<std::size_t>(track)], track);
    }

    void SectionBar::notifyChanged(int sectionIndex, int activeTrack)
    {
        if (!onSectionChanged)
            return;
        const auto ti = static_cast<std::size_t>(activeTrack);
        const auto si = static_cast<std::size_t>(sectionIndex);
        const auto info = processor_.section(activeTrack, sectionIndex);
        const int page = uiState_.trackPage[ti][si];
        const int firstSlot = info.firstSlot + 4 * page;
        onSectionChanged(sectionIndex, page, firstSlot);
    }
}
