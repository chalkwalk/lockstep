#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/Sequence.h"
#include "../io/ControllerEvent.h"
#include "../state/UiState.h"
#include "GridDisplayMode.h"

namespace lockstep
{
    class LockstepProcessor;

    // Merged keyboard-area component: renders all four QWERTY button rows
    // (section bar, function bar, and the two step-grid rows) plus the nav row,
    // previously spread across SectionBar, FunctionBar, and StepGrid.
    class KeyboardArea : public juce::Component, public juce::Timer
    {
    public:
        KeyboardArea(LockstepProcessor& processor, UiState& uiState);
        ~KeyboardArea() override;

        // Track / page (was in StepGrid)
        void setActiveTrack(int t);
        int  getActiveTrack() const { return activeTrack_; }
        int  currentPage()    const { return stepPage_; }
        void nextPage();
        void prevPage();

        // Display mode
        void setDisplayMode(GridDisplayMode mode);
        GridDisplayMode displayMode() const { return displayMode_; }

        // Section API (was in SectionBar)
        bool selectSection(int sectionIndex);
        void selectMetaSection(int sectionIndex);
        void syncToActiveTrack();

        // Returns the Y position (in this component's local space) of where the step-cell
        // rows begin, i.e. the equivalent of the old stepGrid_.getBounds().getY() - getY().
        // Used by the editor to position the pool overlay.
        int stepRowsLocalY() const;

        // Callbacks
        std::function<void(int)>             onActiveTrackChanged;
        std::function<void(GridDisplayMode)> onDisplayModeChanged;
        std::function<void(int, int, int)>   onSectionChanged;    // (section, page, firstSlot)
        std::function<void(int)>             onMetaSectionChanged;

        // Mouse button events for non-step, non-section buttons (nav, verb, modifiers).
        // PluginEditor wires these to route through the same handler as QWERTY events.
        std::function<void(ControllerEvent)> onButtonDown;
        std::function<void(ControllerEvent)> onButtonUp;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

        static constexpr int kPageSteps = 16;
        static constexpr int kCols      = 8;
        static constexpr int kRows      = 2;

    private:
        // Layout helpers — reproduce the same area math used in PluginEditor::resized()
        // but applied to this component's own bounds.
        struct RowAreas {
            juce::Rectangle<int> section;
            juce::Rectangle<int> function;
            juce::Rectangle<int> step;   // includes nav row at bottom
            int intraStepGap = 0;        // vertical gap between the two step rows (ORL only)
        };
        RowAreas computeRowAreas() const;

        // Step cell helpers (from StepGrid)
        int  trackLength()  const;
        int  numPages()     const;
        void clampPage();
        void rebuildLengthAttachment();
        int  stepCellAt(juce::Point<int> pos) const;

        // Section helpers (from SectionBar)
        juce::Rectangle<int> sectionCellBounds(int cellIndex,
                                                juce::Rectangle<int> area) const;
        static int  cellToSection(int cellIndex);
        static bool isReservedMeta(int sectionIndex);
        void notifySectionChanged(int sectionIndex, int track);

        // One entry per section in the cycling order for a canonical key:
        // canonical section first, then any extension sections in index order.
        struct SecGroup { int sectionIdx = 0; int pageCount = 0; };
        // Returns the ordered SecGroup list (canonical + extensions) for the given
        // canonical key on the given track. Empty if the canonical section has no slots
        // AND there are no extension sections.
        std::vector<SecGroup> sectionsForKey(int track, int canonicalIdx) const;

        // Hit-testing for non-step, non-section-5-0 buttons.
        // Returns a ButtonDown event for the hit button, or {ButtonDown, None} if no hit.
        ControllerEvent hitTestFunctionRow (juce::Point<int> pos, juce::Rectangle<int> area) const;
        ControllerEvent hitTestModifierCell(juce::Point<int> pos, juce::Rectangle<int> stepArea) const;

        // Paint helpers
        void paintSectionRow(juce::Graphics& g, juce::Rectangle<int> area);
        void paintFunctionRow(juce::Graphics& g, juce::Rectangle<int> area);
        void paintStepRows  (juce::Graphics& g, juce::Rectangle<int> area);
        // Decorative edge/anchor keys rendered just outside each main row (ORL and STG
        // only; rowIndex 0-3 for number/Q/A/Z rows; JUCE clips the outer halves).
        void paintEdgeRow   (juce::Graphics& g, int rowIndex, juce::Rectangle<int> rowArea) const;

        LockstepProcessor& processor_;
        UiState&           uiState_;

        int             activeTrack_    = 0;
        int             stepPage_       = 0;
        GridDisplayMode displayMode_    = GridDisplayMode::Ortholinear;
        int             mouseHeldStep_  = -1;
        ControllerEvent mouseHeldButton_{};  // non-step button held via mouse
        double          lastPpq_        = -1.0;

        juce::TextButton prevBtn_{ juce::String(u8"←") };
        juce::TextButton nextBtn_{ juce::String(u8"→") };
        juce::Slider     lengthSlider_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment_;

        static constexpr int kNavRowH    = 26;
        static constexpr int kVertMargin = 4;  // top/bottom margin of the key area

        // Section row constants (from SectionBar).
        // Number row: Func(1) Fill(2) TAP(3) NavUp(4) | TRIG..FX(5-0) = 10 cells.
        static constexpr int kFixedSectionCells = 4;   // Func + Fill + TAP + NavUp (left)
        static constexpr int kTailSectionCells  = 0;   // no tail cells; sections run to key 0
        static constexpr int kTotalSectionCells =
            kFixedSectionCells + IMachine::kMaxSections + kTailSectionCells; // 10

        static constexpr std::array<const char*, IMachine::kMaxSections> kMetaLabels = {
            "COND", "NOTE", "TRACK", "", "", "GLOBAL"
        };

        // Colours (from SectionBar)
        static const juce::Colour kColourTrackActive;
        static const juce::Colour kColourMasterActive;
        static const juce::Colour kColourInactive;
        static const juce::Colour kColourShiftActive;
    };
}
