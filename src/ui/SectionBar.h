#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../state/UiState.h"
#include "GridDisplayMode.h"

namespace lockstep
{
    class StepGrid;

    // Renders the 8-cell control row: [SHIFT] [NAV] [Section 0..5].
    // Reads display state from UiState; fires onSectionChanged when the active
    // (section, page, firstSlot) changes so the editor can update the encoder strip.
    class SectionBar : public juce::Component, public juce::Timer
    {
    public:
        SectionBar(LockstepProcessor& processor, StepGrid& grid, UiState& uiState);

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void timerCallback() override { repaint(); }

        // Called by the editor's key handler when a section key (0–5) is pressed.
        // Returns true if the state changed.
        bool selectSection(int sectionIndex);

        // Called when Shift+section is pressed. Ignores reserved meta slots (indices 2–4).
        void selectMetaSection(int sectionIndex);

        // Called when the active track changes so the bar redraws and re-fires
        // onSectionChanged with the new track's current section/page.
        void syncToActiveTrack();
        void setDisplayMode(GridDisplayMode mode);

        // Callback: fired whenever the active machine-section first slot changes.
        // Arguments: (sectionIndex, pageIndex, firstSlot)
        std::function<void(int, int, int)> onSectionChanged;

        // Callback: fired when the active meta section changes.
        // Argument: masterSection index (0–5), or -1 when deselected.
        std::function<void(int)> onMetaSectionChanged;

    private:
        // Fixed cells: 0=FNC(1), 1=REC(2), 2=NavUp(3); no trailing tail.
        static constexpr int kFixedCells  = 3;
        static constexpr int kTailCells   = 0;
        static constexpr int kTotalCells  = kFixedCells + IMachine::kMaxSections + kTailCells;

        // Returns the cell rectangle for a given cell index.
        // In Clean mode the modifier column (index 0) is separated from the rest by kClnColGap.
        [[nodiscard]] juce::Rectangle<int> cellBounds(int cellIndex) const;

        // Returns the section index (0–5) for section cells, or -1 for fixed/tail cells.
        [[nodiscard]] static int cellToSection(int cellIndex);

        void notifyChanged(int sectionIndex, int activeTrack);

        LockstepProcessor& processor_;
        StepGrid&          grid_;
        UiState&           uiState_;
        GridDisplayMode    displayMode_ = GridDisplayMode::Ortholinear;

        // Fixed meta-section labels for the shift layer, indexed by section (0–5).
        // Empty string = reserved (Shift press is a no-op for that index).
        // Layout: COND(0) TRIG(1) TRACK(2) —(3) —(4) GLOBAL(5)
        static constexpr std::array<const char*, IMachine::kMaxSections> kMetaLabels = {
            "COND", "TRIG", "TRACK", "", "", "GLOBAL"
        };

        [[nodiscard]] static bool isReservedMeta(int sectionIndex)
        {
            if (sectionIndex < 0 || sectionIndex >= IMachine::kMaxSections) return true;
            return kMetaLabels[static_cast<std::size_t>(sectionIndex)][0] == '\0';
        }

        // Colour palette — matches the rest of the UI.
        static const juce::Colour kColourTrackActive;  // teal
        static const juce::Colour kColourMasterActive; // amber
        static const juce::Colour kColourInactive;     // dark blue-grey
        static const juce::Colour kColourShiftActive;  // light grey
    };
}
