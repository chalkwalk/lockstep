#include "SectionBar.h"
#include "StepGrid.h"

namespace lockstep
{
    const juce::Colour SectionBar::kColourTrackActive  = juce::Colour::fromRGB( 62, 200, 200); // teal
    const juce::Colour SectionBar::kColourMasterActive = juce::Colour::fromRGB(255, 180,  50); // amber
    const juce::Colour SectionBar::kColourInactive     = juce::Colour::fromRGB( 45,  55,  65); // dark
    const juce::Colour SectionBar::kColourShiftActive  = juce::Colour::fromRGB(160, 175, 195); // grey

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
        // Fixed cells at start (0, 1) and tail (last cell) return -1.
        if (cellIndex < kFixedCells || cellIndex >= kTotalCells - kTailCells)
            return -1;
        return cellIndex - kFixedCells;
    }

    // -------------------------------------------------------------------------

    void SectionBar::paintFixedCell(juce::Graphics& g,
                                    const juce::Rectangle<int>& r,
                                    const juce::String& label,
                                    bool highlighted) const
    {
        g.setColour(highlighted ? kColourShiftActive : kColourInactive);
        g.fillRect(r.reduced(2, 2));
        g.setColour(highlighted ? juce::Colours::black : juce::Colour::fromRGB(120, 140, 160));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(label, r, juce::Justification::centred);
    }

    void SectionBar::paintSectionCell(juce::Graphics& g,
                                      const juce::Rectangle<int>& r,
                                      int sectionIndex,
                                      int activeTrack) const
    {
        // Trailing buttons that the machine doesn't use are rendered as empty/disabled.
        if (sectionIndex >= processor_.numSections(activeTrack))
        {
            g.setColour(kColourInactive.withAlpha(0.4f));
            g.fillRect(r.reduced(2, 2));
            return;
        }

        const bool isMasterActive = (uiState_.masterSection == sectionIndex);
        const bool isTrackActive  = (uiState_.masterSection == -1
                                     && uiState_.trackSection[static_cast<std::size_t>(activeTrack)] == sectionIndex);

        // Background
        juce::Colour bg = kColourInactive;
        if (isTrackActive)       bg = kColourTrackActive.withAlpha(0.25f);
        else if (isMasterActive) bg = kColourMasterActive.withAlpha(0.25f);
        g.setColour(bg);
        g.fillRect(r.reduced(2, 2));

        // Border
        if (isTrackActive || isMasterActive)
        {
            g.setColour(isTrackActive ? kColourTrackActive : kColourMasterActive);
            g.drawRect(r.reduced(2, 2), 1);
        }

        // Fetch section info (from the active track's machine)
        const auto info = processor_.section(activeTrack, sectionIndex);
        const juce::String primaryLabel = info.label.isEmpty()
                                              ? juce::String(sectionIndex)
                                              : info.label;
        // Secondary label is the fixed meta-layer name, not the machine's secondaryLabel.
        const bool reserved = isReservedMeta(sectionIndex);
        const juce::String secondaryLabel =
            reserved ? juce::String::charToString(0x2014)  // em dash for reserved
                     : juce::String(kMetaLabels[static_cast<std::size_t>(sectionIndex)]);

        // Text areas: top half = primary, bottom half = secondary (minus dot row)
        const int dotRowH  = 8;
        const int textAreaH = (r.getHeight() - dotRowH) / 2;

        auto topArea    = r.withHeight(textAreaH).reduced(2, 0);
        auto bottomArea = r.withTrimmedTop(textAreaH).withTrimmedBottom(dotRowH).reduced(2, 0);

        // Primary label — dimmed when shift is held (secondary layer is about to activate).
        const float primaryAlpha = isMasterActive ? 0.2f
                                 : uiState_.funcHeld ? 0.3f : 1.0f;
        g.setColour(juce::Colours::white.withAlpha(primaryAlpha));
        g.setFont(juce::Font(juce::FontOptions(isTrackActive ? 11.0f : 9.0f)));
        g.drawText(primaryLabel, topArea, juce::Justification::centredBottom);

        // Secondary label (meta layer name); bright when shift held or master active.
        const float secondaryAlpha = isMasterActive ? 1.0f
                                   : uiState_.funcHeld ? (reserved ? 0.2f : 1.0f)
                                   : (reserved ? 0.12f : 0.25f);
        g.setColour(juce::Colours::white.withAlpha(secondaryAlpha));
        g.setFont(juce::Font(juce::FontOptions(isMasterActive ? 11.0f : 9.0f)));
        g.drawText(secondaryLabel, bottomArea, juce::Justification::centredTop);

        // Divider line between primary and secondary text
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(r.getX() + 4, r.getY() + textAreaH, r.getWidth() - 8, 1);

        // Page dots — only for track sections; master sections have no pages yet.
        const int pageCount = info.pageCount;
        if (pageCount > 1 && !isMasterActive)
        {
            const int activePage = uiState_.trackPage
                [static_cast<std::size_t>(activeTrack)]
                [static_cast<std::size_t>(sectionIndex)];

            const int dotSize   = 4;
            const int dotSpacing = 6;
            const int totalDotW = pageCount * dotSpacing - (dotSpacing - dotSize);
            int dotX = r.getCentreX() - totalDotW / 2;
            const int dotY = r.getBottom() - dotRowH + (dotRowH - dotSize) / 2;

            for (int p = 0; p < pageCount; ++p)
            {
                const bool isActive = (p == activePage) && (isTrackActive || isMasterActive);
                g.setColour((isTrackActive ? kColourTrackActive : kColourMasterActive)
                                .withAlpha(isActive ? 1.0f : 0.3f));
                if (isActive)
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

    void SectionBar::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colour::fromRGB(20, 22, 26));
        g.fillAll();

        const int activeTrack = grid_.getActiveTrack();

        paintFixedCell(g, cellBounds(0), "FNC", uiState_.funcHeld);

        // REC cell (key 2): red when armed; shows SNP func-layer label.
        {
            const bool isArmed  = processor_.clock().isRecordArmed();
            const bool keyDown  = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('2'));
            const auto r = cellBounds(1);
            g.setColour(keyDown  ? juce::Colour::fromRGB(80, 120, 165)
                       : isArmed ? juce::Colour::fromRGB(90, 35, 35)
                                 : juce::Colour::fromRGB(40, 50, 60));
            g.fillRect(r.reduced(2, 2));

            // Use reduced rect so labels stay inside the drawn background.
            const auto cell = r.reduced(2, 2);

            // Primary label — dimmed when Func held.
            const float primAlpha = (keyDown || !uiState_.funcHeld) ? 1.0f : 0.35f;
            g.setColour((isArmed ? juce::Colour::fromRGB(220, 100, 100)
                                 : juce::Colour::fromRGB(100, 120, 140))
                            .withAlpha(primAlpha));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("REC", cell.withTrimmedBottom(10), juce::Justification::centred);

            // Func-layer label (SNP) — bright when Func held, dim otherwise.
            const float secAlpha = uiState_.funcHeld ? 1.0f : 0.25f;
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.setColour(juce::Colour::fromRGB(180, 200, 220).withAlpha(secAlpha));
            g.drawText("SNP",
                       cell.withTrimmedTop(cell.getHeight() - 11).reduced(2, 0),
                       juce::Justification::centredBottom);
        }

        paintFixedCell(g, cellBounds(2), juce::String::charToString(0x25B2),
                       juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('3'))); // NavUp

        for (int s = 0; s < IMachine::kMaxSections; ++s)
            paintSectionCell(g, cellBounds(kFixedCells + s), s, activeTrack);

        // Key-number annotations (1–9) in STG and ORL modes.
        if (displayMode_ != GridDisplayMode::Clean)
        {
            static constexpr const char* kKeyLabels[kTotalCells] = {
                "1", "2", "3", "4", "5", "6", "7", "8", "9"
            };
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.setColour(juce::Colour::fromRGB(75, 92, 108));
            for (int i = 0; i < kTotalCells; ++i)
            {
                g.drawText(kKeyLabels[i],
                           cellBounds(i).withHeight(10).reduced(2, 0),
                           juce::Justification::topLeft);
            }
        }
    }

    // -------------------------------------------------------------------------

    void SectionBar::mouseDown(const juce::MouseEvent& e)
    {
        // Hit-test: find which cell was clicked.
        // In CLN mode the gap shifts cells 1+ rightward, so check all cells.
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

        // Selecting a track section always exits master mode.
        const bool wasInMasterMode = (uiState_.masterSection != -1);
        uiState_.masterSection = -1;

        if (!wasInMasterMode && uiState_.trackSection[ti] == sectionIndex)
        {
            // Same section, not switching from master: advance page, wrap around.
            const auto info = processor_.section(activeTrack, sectionIndex);
            const int pageCount = info.pageCount > 0 ? info.pageCount : 1;
            uiState_.trackPage[ti][si] = (uiState_.trackPage[ti][si] + 1) % pageCount;
        }
        else
        {
            // Different section (or returning from master mode): switch, reset to page 0.
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
        // Re-fire onSectionChanged so the MZ slotOffset stays in sync with the
        // new track's currently active section and page.
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
