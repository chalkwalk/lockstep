#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/Sequence.h"
#include "ScopeSectionSelect.h"
#include "../io/ControllerEvent.h"
#include "../io/PressTracker.h"
#include "../state/UiState.h"
#include "GridDisplayMode.h"
#include "SurfaceModel.h"

namespace lockstep
{
    class LockstepProcessor;

    // Merged keyboard-area component: renders all four QWERTY button rows
    // (section bar, function bar, and the two step-grid rows) plus the nav row,
    // previously spread across SectionBar, FunctionBar, and StepGrid.
    class KeyboardArea : public juce::Component
    {
    public:
        KeyboardArea(LockstepProcessor& processor, UiState& uiState);
        ~KeyboardArea() override;

        // Track / page (was in StepGrid)
        void setActiveTrack(int t);
        int getActiveTrack() const { return uiState_.activeTrack; }
        int currentPage() const { return stepPage_; }
        void nextPage();
        void prevPage();
        // Scroll-past-end (DESIGN §34.4): a double-tap NavRight unlocks one empty
        // page beyond the track length so a longer length can be set out there.
        // Auto-relocks once the visible page is back within the length.
        void unlockScrollPastEnd();
        bool isScrollPastEndUnlocked() const { return scrollPastEndUnlocked_; }
        int numPages() const;

        // Display mode
        void setDisplayMode(GridDisplayMode mode);
        GridDisplayMode displayMode() const { return displayMode_; }

        // Section API (was in SectionBar)
        // trackScope=true routes the page list to track-level params only (P6);
        // false (default) is the unqualified machine-preferring view.
        bool selectSection(int sectionIndex, bool trackScope = false);
        void selectMetaSection(int sectionIndex, bool toggle = true);
        // True if the section key carries a Func-row secondary (COND/NOTE only).
        static bool isReservedMeta(int sectionIndex);
        // True if metaSection_=contentIndex is a real MZ group (0/1/2/5).
        static bool metaContentExists(int contentIndex);
        void syncToActiveTrack();

        // Returns the Y position (in this component's local space) of where the step-cell
        // rows begin, i.e. the equivalent of the old stepGrid_.getBounds().getY() - getY().
        // Used by the editor to position the pool overlay.
        int stepRowsLocalY() const;

        // Returns the nav-strip (64-step overview) bounds in this component's local space.
        juce::Rectangle<int> navAreaBounds() const;

        // True when an overlay (FX picker, note edit, machine picker, etc.) is drawing
        // its own banner over the nav strip. Used by the editor to suppress the floating
        // context pill that would otherwise overlap the overlay's banner.
        bool navStripOverlayActive() const noexcept;


        // Callbacks
        std::function<void(int)> onActiveTrackChanged;
        std::function<void(GridDisplayMode)> onDisplayModeChanged;
        std::function<void(int, int, int)> onSectionChanged;    // (section, page, firstSlot)
        std::function<void(int)> onMetaSectionChanged;

        // Mouse button events for non-step, non-section buttons (nav, verb, modifiers).
        // PluginEditor wires these to route through the same handler as QWERTY events.
        std::function<void(ControllerEvent)> onButtonDown;
        std::function<void(ControllerEvent)> onButtonUp;

        // Mini-sequencer strip (nav row) mouse aids — new-user training wheels.
        // absStep is the 0-based absolute step index (0-63).
        std::function<void(int)> onMiniSeqToggle;        // left-click: toggle trig
        std::function<void(int)> onMiniSeqScrollToStep;  // middle-click: page to step
        std::function<void(int)> onMiniSeqSetLength;     // right-click: set track length

        // Jump the visible 16-step page to the one that contains absStep.
        void setPage(int page);

        // Wired from PluginEditor so paint can query held state for both
        // keyboard and mouse without polling juce::KeyPress::isKeyCurrentlyDown.
        void setPressTracker(const PressTracker* pt) { pressTracker_ = pt; }
        void setMorphViewState(const MorphViewState& mv, int slotOffset, float crossfaderValue)
        {
            morphView_ = mv;
            slotOffset_ = slotOffset;
            crossfaderValue_ = crossfaderValue;
        }

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

        static constexpr int kPageSteps = 16;
        static constexpr int kCols = 8;
        static constexpr int kRows = 2;

    private:
        // Layout helpers — reproduce the same area math used in PluginEditor::resized()
        // but applied to this component's own bounds.
        struct RowAreas
        {
            juce::Rectangle<int> section;
            juce::Rectangle<int> function;
            juce::Rectangle<int> step;   // includes nav row at bottom
            int intraStepGap = 0;        // vertical gap between the two step rows (ORL only)
        };
        RowAreas computeRowAreas() const;

        // Step cell helpers (from StepGrid)
        int trackLength() const;
        void clampPage();
        int stepCellAt(juce::Point<int> pos) const;

        // Section helpers (from SectionBar)
        juce::Rectangle<int> sectionCellBounds(int cellIndex,
                                               juce::Rectangle<int> area) const;
        static int cellToSection(int cellIndex);
        void notifySectionChanged(int sectionIndex, int track, bool trackScope = false);

        // One entry per section in the cycling order for a canonical key:
        // canonical section first, then any extension sections in index order.
        struct SecGroup
        {
            int sectionIdx = 0;
            int pageCount = 0;
            SecOrigin origin = SecOrigin::Machine;  // scope layer this page comes from
        };
        // Returns the ordered SecGroup list (canonical + extensions) for the given
        // canonical key on the given track. Empty if the canonical section has no slots
        // AND there are no extension sections.
        std::vector<SecGroup> sectionsForKey(int track, int canonicalIdx,
                                             bool trackScope = false) const;

        // Hit-testing for non-step, non-section-5-0 buttons.
        // Returns a ButtonDown event for the hit button, or {ButtonDown, None} if no hit.
        ControllerEvent hitTestFunctionRow(juce::Point<int> pos, juce::Rectangle<int> area) const;
        ControllerEvent hitTestModifierCell(juce::Point<int> pos, juce::Rectangle<int> stepArea) const;

        // Paint helpers
        void paintSectionRow(juce::Graphics& g, juce::Rectangle<int> area,
                             const SurfaceModel& model);
        void paintFunctionRow(juce::Graphics& g, juce::Rectangle<int> area,
                              const SurfaceModel& model);
        void paintStepRows(juce::Graphics& g, juce::Rectangle<int> area,
                           const SurfaceModel& model);
        void paintTimeline(juce::Graphics& g, juce::Rectangle<int> navArea);
        // Decorative edge/anchor keys rendered just outside each main row (ORL and STG
        // only; rowIndex 0-3 for number/Q/A/Z rows; JUCE clips the outer halves).
        void paintEdgeRow(juce::Graphics& g, int rowIndex, juce::Rectangle<int> rowArea) const;

        // Helper: true if the given raw key code is currently held (keyboard) OR
        // if the mouse is holding the given logical (button, index) cell.
        // Replaces scattered juce::KeyPress::isKeyCurrentlyDown calls in paint.
        [[nodiscard]] bool isKeyPressed(int rawCode) const
        {
            if (pressTracker_)
                return pressTracker_->isKeyHeld(rawCode);
            return juce::KeyPress::isKeyCurrentlyDown(rawCode);
        }
        [[nodiscard]] bool isMouseCellPressed(ControllerButton btn, int idx = -1) const
        {
            return pressTracker_ && pressTracker_->isMouseHeld(btn, idx);
        }

        LockstepProcessor& processor_;
        UiState& uiState_;
        const PressTracker* pressTracker_ = nullptr;
        MorphViewState morphView_;           // set by editor before repaint; drives morph step view
        int slotOffset_ = 0;
        float crossfaderValue_ = 0.5f;

        int stepPage_ = 0;
        bool scrollPastEndUnlocked_ = false;
        GridDisplayMode displayMode_ = GridDisplayMode::Ortholinear;
        int mouseHeldStep_ = -1;
        ControllerEvent mouseHeldButton_{};  // non-step button held via mouse

        static constexpr int kNavRowH = 34;
        static constexpr int kVertMargin = 4;  // top/bottom margin of the key area

        // Section row constants (from SectionBar).
        // Number row: Func(1) Fill(2) TAP(3) NavUp(4) | TRIG..FX(5-0) = 10 cells.
        static constexpr int kFixedSectionCells = 4;   // Func + Fill + TAP + NavUp (left)
        static constexpr int kTailSectionCells = 0;   // no tail cells; sections run to key 0
        static constexpr int kTotalSectionCells =
            kFixedSectionCells + IMachine::kMaxSections + kTailSectionCells; // 10

        // Func-row secondary labels (Func-held section row); empty = dims under
        // Func. TRACK→Track+TRIG, GLOBAL→Song+FX relocated. FILTER (idx 2) =
        // transport globals + per-track Scale (Func+7, README §5.8). The meta
        // CONTENT groups wired in the MZ are a superset — see metaContentExists().
        static constexpr std::array<const char*, IMachine::kMaxSections> kMetaLabels = {
            "COND", "NOTE", "TRSP", "", "", ""
        };

        // Colours (from SectionBar)
        static const juce::Colour kColourTrackActive;
        static const juce::Colour kColourMasterActive;
        static const juce::Colour kColourInactive;
        static const juce::Colour kColourShiftActive;
    };
}
