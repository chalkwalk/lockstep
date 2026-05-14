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
    }

    // -------------------------------------------------------------------------

    juce::Rectangle<int> SectionBar::cellBounds(int cellIndex) const
    {
        const int w    = getWidth();
        const int h    = getHeight();
        const int cellW = w / kTotalCells;
        return { cellIndex * cellW, 0, cellW, h };
    }

    int SectionBar::cellToSection(int cellIndex)
    {
        if (cellIndex < kFixedCells) return -1;
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

        // Primary label
        const float primaryAlpha = isMasterActive ? 0.2f : 1.0f;
        g.setColour(juce::Colours::white.withAlpha(primaryAlpha));
        g.setFont(juce::Font(juce::FontOptions(isTrackActive ? 11.0f : 9.0f)));
        g.drawText(primaryLabel, topArea, juce::Justification::centredBottom);

        // Secondary label (meta layer name; dimmed when machine layer is active,
        // further dimmed for reserved slots that have no meta function).
        const float secondaryAlpha = isMasterActive ? 1.0f : (reserved ? 0.12f : 0.25f);
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

        paintFixedCell(g, cellBounds(0), "SHF", uiState_.shiftHeld);
        paintFixedCell(g, cellBounds(1), juce::String::charToString(0x25B2), false); // ▲

        for (int s = 0; s < IMachine::kMaxSections; ++s)
            paintSectionCell(g, cellBounds(kFixedCells + s), s, activeTrack);
    }

    // -------------------------------------------------------------------------

    void SectionBar::mouseDown(const juce::MouseEvent& e)
    {
        const int cellW = getWidth() / kTotalCells;
        const int cellIndex = e.x / cellW;
        const int section = cellToSection(cellIndex);
        if (section < 0)
            return;

        if (uiState_.shiftHeld)
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
