#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../state/UiState.h"

namespace lockstep
{
    class StepGrid;

    // Renders the 8-cell control row: [SHIFT] [NAV] [Section 0..5].
    // Reads display state from UiState; fires onSectionChanged when the active
    // (section, page, firstSlot) changes so the editor can update the encoder strip.
    class SectionBar : public juce::Component
    {
    public:
        SectionBar(LockstepProcessor& processor, StepGrid& grid, UiState& uiState);

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;

        // Called by the editor's key handler when a section key (0–5) is pressed.
        // Returns true if the state changed.
        bool selectSection(int sectionIndex);

        // Callback: fired whenever the active first slot changes.
        // Arguments: (sectionIndex, pageIndex, firstSlot)
        std::function<void(int, int, int)> onSectionChanged;

    private:
        static constexpr int kFixedCells  = 2;   // SHIFT + NAV
        static constexpr int kTotalCells  = kFixedCells + IMachine::kNumSections;

        // Returns the cell rectangle for a given cell index (0 = SHIFT, 1 = NAV, 2..7 = sections).
        [[nodiscard]] juce::Rectangle<int> cellBounds(int cellIndex) const;

        // Returns the section index (0–5) for a cell index ≥ 2, or -1 for fixed cells.
        [[nodiscard]] static int cellToSection(int cellIndex);

        void paintFixedCell(juce::Graphics& g, const juce::Rectangle<int>& r,
                            const juce::String& label, bool highlighted) const;

        void paintSectionCell(juce::Graphics& g, const juce::Rectangle<int>& r,
                              int sectionIndex, int activeTrack) const;

        void notifyChanged(int sectionIndex, int activeTrack);

        LockstepProcessor& processor_;
        StepGrid&          grid_;
        UiState&           uiState_;

        // Colour palette — matches the rest of the UI.
        static const juce::Colour kColourTrackActive;  // teal
        static const juce::Colour kColourMasterActive; // amber
        static const juce::Colour kColourInactive;     // dark blue-grey
        static const juce::Colour kColourShiftActive;  // light grey
    };
}
