#include "KeyboardArea.h"
#include "KeyButton.h"
#include "../core/Subdivision.h"
#include "KeyLabel.h"
#include "PageNav.h"
#include "ScopedSectionMatrix.h"
#include "ScopeSectionSelect.h"
#include "SectionResolve.h"
#include "UITheme.h"
#include "../command/ButtonLayers.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include "../core/TrigCondition.h"
#include "../machine/EffectPickerModel.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lockstep
{
    using namespace theme;

    // -------------------------------------------------------------------------
    // Static colour constants (from SectionBar)

    const juce::Colour KeyboardArea::kColourTrackActive = juce::Colour::fromRGB(62, 200, 200);
    const juce::Colour KeyboardArea::kColourMasterActive = juce::Colour::fromRGB(255, 180, 50);
    const juce::Colour KeyboardArea::kColourInactive = juce::Colour::fromRGB(45, 55, 65);
    const juce::Colour KeyboardArea::kColourShiftActive = juce::Colour::fromRGB(160, 175, 195);

    // -------------------------------------------------------------------------
    // -------------------------------------------------------------------------
    // Constructor / destructor

    KeyboardArea::KeyboardArea(LockstepProcessor& processor, UiState& uiState)
        : processor_(processor), uiState_(uiState)
    {
        processor_.setFocusTrack(uiState_.activeTrack);
        // No timer: the grid repaints through the surface-invalidation channel
        // (9.15). Playback frames come from the editor tick's playhead detection;
        // length/divider changes route through the editor's APVTS listener; param
        // changes settle via the audio→UI dirty flag. (PRINCIPLES §22.)
    }

    KeyboardArea::~KeyboardArea() = default;

    // -------------------------------------------------------------------------
    // Layout helper

    KeyboardArea::RowAreas KeyboardArea::computeRowAreas() const
    {
        const int totalH = getHeight();
        const int usableH = totalH - kNavRowH - 2 * kVertMargin;

        int cellH, row0Y, row1Y, row2Y, intraStepGap, leftX, usableW;

        if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            // Size cells so that half a cell is visible on each side as an edge anchor.
            // 10 cell-widths + 10 gaps fit the full component width:
            //   cellW = (W - 10*g) / 10
            //   leftX = g + cellW/2  (main block inset; edge key centered at x=0)
            const int g = kOrlGap;
            const int cw = juce::jmax(1, (getWidth() - 10 * g) / 10);
            leftX = g + cw / 2;
            usableW = getWidth() - 2 * leftX;
            cellH = juce::jmax(1, (usableH - 3 * g) / 4);
            intraStepGap = g;
            row0Y = kVertMargin;
            row1Y = row0Y + cellH + g;
            row2Y = row1Y + cellH + g;
        }
        else if (displayMode_ == GridDisplayMode::Clean)
        {
            leftX = kSideMargin;
            usableW = getWidth() - 2 * kSideMargin;
            cellH = juce::jmax(1, (usableH - kClnRowGap) / 4);
            intraStepGap = 0;
            row0Y = kVertMargin;
            row1Y = row0Y + cellH;
            row2Y = row1Y + cellH + kClnRowGap;
        }
        else  // Staggered
        {
            leftX = kSideMargin;
            usableW = getWidth() - 2 * kSideMargin;
            cellH = juce::jmax(1, usableH / 4);
            intraStepGap = 0;
            row0Y = kVertMargin;
            row1Y = row0Y + cellH;
            row2Y = row1Y + cellH;
        }

        RowAreas r;
        r.section = { leftX, row0Y, usableW, cellH };
        r.function = { leftX, row1Y, usableW, cellH };
        r.step = { leftX, row2Y, usableW, (totalH - kVertMargin) - row2Y };
        r.intraStepGap = intraStepGap;
        return r;
    }

    int KeyboardArea::stepRowsLocalY() const
    {
        return computeRowAreas().step.getY();
    }

    juce::Rectangle<int> KeyboardArea::navAreaBounds() const
    {
        auto stepArea = computeRowAreas().step;
        return stepArea.removeFromBottom(kNavRowH);
    }

    bool KeyboardArea::navStripOverlayActive() const noexcept
    {
        return uiState_.masterFxPickerOpen
            || uiState_.funcFxHeld
            || uiState_.funcTrackHeld
            || (uiState_.noteEditMode && !uiState_.noteEditSteps.empty())
            || (uiState_.trigGridMode != TrigGridMode::Default);
    }

    // -------------------------------------------------------------------------
    // Track / page

    void KeyboardArea::setActiveTrack(int t)
    {
        const int clamped = juce::jlimit(0, static_cast<int>(kNumTracks) - 1, t);
        if (clamped == uiState_.activeTrack)
            return;
        uiState_.activeTrack = clamped;
        processor_.setFocusTrack(clamped);
        stepPage_ = 0;
        markSurfaceDirty();
        if (onActiveTrackChanged)
            onActiveTrackChanged(uiState_.activeTrack);
    }

    void KeyboardArea::nextPage()
    {
        ++stepPage_;
        clampPage();
    }
    void KeyboardArea::prevPage()
    {
        --stepPage_;
        clampPage();
    }
    void KeyboardArea::setPage(int page)
    {
        stepPage_ = page;
        clampPage();   // invalidates
    }

    void KeyboardArea::setDisplayMode(GridDisplayMode mode)
    {
        displayMode_ = mode;
        resized();  // nav row button positions may shift
        markSurfaceDirty();
    }

    // -------------------------------------------------------------------------
    // Step helpers

    int KeyboardArea::trackLength() const
    {
        auto* p = processor_.apvts().getRawParameterValue(
            ParamIDs::trackLength(uiState_.activeTrack));
        return p ? static_cast<int>(p->load()) : kPageSteps;
    }

    int KeyboardArea::numPages() const
    {
        return (trackLength() + kPageSteps - 1) / kPageSteps;
    }

    void KeyboardArea::unlockScrollPastEnd()
    {
        // Set the flag without clamping: the caller advances onto the empty page
        // immediately afterwards (via nextPage()). Clamping here would relock at
        // once, since stepPage_ is still the last in-length page.
        scrollPastEndUnlocked_ = true;
    }

    void KeyboardArea::clampPage()
    {
        const auto r = clampStepPage(stepPage_, numPages(), scrollPastEndUnlocked_);
        stepPage_ = r.page;
        scrollPastEndUnlocked_ = r.unlocked;
        markSurfaceDirty();
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
        const bool useClnGap = (displayMode_ == GridDisplayMode::Clean);
        const int intraStepGap = areas.intraStepGap;

        int cellW, staggerA, staggerZ;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(cellArea.getWidth());
            cellW = staggerCellW(hu);
            staggerA = staggerOffsetA(hu);
            staggerZ = staggerOffsetZ(hu);
        }
        else if (useClnGap)
        {
            cellW = (cellArea.getWidth() - kClnColGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        else  // Ortholinear
        {
            cellW = (cellArea.getWidth() - (kTotalGridCols - 1) * kOrlGap) / kTotalGridCols;
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
            if (relX < 0) return -1;
            if (relX < 2 * cellW) col = relX / cellW;
            else if (relX < 2 * cellW + kClnColGap) return -1;
            else col = 2 + (relX - 2 * cellW - kClnColGap) / cellW;
        }
        else if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            const int pitch = cellW + kOrlGap;
            if (relX < 0 || pitch <= 0) return -1;
            col = relX / pitch;
            if (relX % pitch >= cellW) return -1;  // click in gap
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
                              : area.getX() + kFixedSectionCells * cellW + kClnColGap + (cellIndex - kFixedSectionCells) * cellW;
            return { x, area.getY(), cellW, h };
        }
        if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            const int g = kOrlGap;
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
        if (cellIndex < kFixedSectionCells || cellIndex >= kFixedSectionCells + IMachine::kMaxSections)
            return -1;
        return cellIndex - kFixedSectionCells;
    }

    bool KeyboardArea::isReservedMeta(int sectionIndex)
    {
        if (sectionIndex < 0 || sectionIndex >= IMachine::kMaxSections)
            return true;
        return kMetaLabels[static_cast<std::size_t>(sectionIndex)][0] == '\0';
    }

    bool KeyboardArea::metaContentExists(int contentIndex)
    {
        // Meta CONTENT groups wired in ManipulationZone (MetaBand enum):
        //   0=COND  1=TRIG  2=TRANSPORT  3=DIV  4=PHRASELEN  5=GLOBAL(master FX).
        // Reached by: Func+TRIG/SRC, Track+TRIG, Phrase+LEN, Func+7, Song+FX.
        // Distinct from the Func-row label set in kMetaLabels.
        return contentIndex == 0 || contentIndex == 1 || contentIndex == 2 || contentIndex == 3 || contentIndex == 4 || contentIndex == 5;
    }

    void KeyboardArea::notifySectionChanged(int sectionIndex, int track, bool trackScope)
    {
        if (!onSectionChanged)
            return;

        const auto groups = sectionsForKey(track, sectionIndex, trackScope);
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
        const auto info = processor_.section(track, first.sectionIdx);
        if (info.firstSlot >= 0)
            onSectionChanged(sectionIndex, 0, info.firstSlot);
    }

    std::vector<KeyboardArea::SecGroup>
    KeyboardArea::sectionsForKey(int track, int canonicalIdx, bool trackScope) const
    {
        // Classified machine/track candidates (Item 7: shared with the resolver
        // via buildParamCandidates so both paths classify identically). A group is
        // machine-owned iff its first slot lives inside the machine's param range;
        // track blocks (FLTR/AMP/FX inserts) and virtual extensions live past
        // numParams(). The pure scope filter then picks the visible list.
        std::vector<SecCandidate> candidates = buildParamCandidates(processor_, track, canonicalIdx);

        // heldFloor: unqualified fills top-down (Machine); Track hold peels Machine.
        const SecOrigin heldFloor = trackScope ? SecOrigin::Track : SecOrigin::Machine;
        std::vector<SecGroup> groups;
        for (const auto& c : selectScopeSections(candidates, heldFloor))
            groups.push_back({ c.sectionIdx, c.pageCount, c.origin });
        return groups;
    }

    bool KeyboardArea::selectSection(int sectionIndex, bool trackScope)
    {
        if (uiState_.activeTrack < 0 || uiState_.activeTrack >= static_cast<int>(kNumTracks))
            return false;
        if (sectionIndex < 0 || sectionIndex >= IMachine::kMaxSections)
            return false;

        const auto groups = sectionsForKey(uiState_.activeTrack, sectionIndex, trackScope);
        if (groups.empty())  // no slots in this canonical section (this scope) or extensions
            return false;

        int totalPages = 0;
        for (const auto& g : groups) totalPages += g.pageCount;

        const auto ti = static_cast<std::size_t>(uiState_.activeTrack);
        const auto si = static_cast<std::size_t>(sectionIndex);

        const bool wasInMasterMode = (uiState_.masterSection != -1);
        uiState_.masterSection = -1;

        // A re-press cycles pages only when both the section AND the resolving scope
        // are unchanged — the two scopes have different page lists, so a scope flip
        // starts fresh at page 0.
        const bool sameSectionSameScope =
            !wasInMasterMode && uiState_.trackSection[ti] == sectionIndex
            && uiState_.trackPageTrackScope[ti][si] == trackScope;

        if (sameSectionSameScope)
        {
            uiState_.trackPage[ti][si] = (uiState_.trackPage[ti][si] + 1) % totalPages;
        }
        else
        {
            uiState_.trackSection[ti] = sectionIndex;
            uiState_.trackPage[ti][si] = 0;
        }
        uiState_.trackPageTrackScope[ti][si] = trackScope;

        markSurfaceDirty();
        notifySectionChanged(sectionIndex, uiState_.activeTrack, trackScope);
        return true;
    }

    void KeyboardArea::selectMetaSection(int sectionIndex, bool toggle)
    {
        // sectionIndex is a meta CONTENT index (0/1/2/5), not a Func-row label
        // slot — scope gestures (Track+TRIG, Phrase+LEN, Song+FX) reach content
        // the Func row no longer advertises, so validate against the content set.
        if (!metaContentExists(sectionIndex))
            return;
        if (toggle)
            uiState_.masterSection = (uiState_.masterSection == sectionIndex) ? -1 : sectionIndex;
        else
            uiState_.masterSection = sectionIndex;
        markSurfaceDirty();
        if (onMetaSectionChanged)
            onMetaSectionChanged(uiState_.masterSection);
    }

    void KeyboardArea::syncToActiveTrack()
    {
        markSurfaceDirty();
        if (uiState_.activeTrack < 0 || uiState_.activeTrack >= static_cast<int>(kNumTracks))
            return;
        if (uiState_.masterSection >= 0)
            return;

        const auto ti = static_cast<std::size_t>(uiState_.activeTrack);
        int sec = uiState_.trackSection[ti];

        // If the current section is empty for this machine, snap to the first available one.
        if (processor_.section(uiState_.activeTrack, sec).firstSlot < 0)
        {
            for (int s = 0; s < IMachine::kMaxSections; ++s)
            {
                if (processor_.section(uiState_.activeTrack, s).firstSlot >= 0)
                {
                    uiState_.trackSection[ti] = s;
                    sec = s;
                    break;
                }
            }
        }

        notifySectionChanged(sec, uiState_.activeTrack,
                             uiState_.trackPageTrackScope[ti][static_cast<std::size_t>(sec)]);
    }

    // -------------------------------------------------------------------------
    // Mouse hit-testing

    ControllerEvent KeyboardArea::hitTestFunctionRow(juce::Point<int> pos,
                                                     juce::Rectangle<int> area) const
    {
        if (!area.contains(pos))
            return {};

        // Q-row logical button mapping (matches kDefs order in paintFunctionRow).
        using CB = ControllerButton;
        static constexpr std::array<CB, 10> kButtons = { {
            CB::PhraseScope,
            CB::SceneScope,
            CB::NavLeft,
            CB::NavDown,
            CB::NavRight,
            CB::VerbSnapshot,
            CB::VerbRecord,
            CB::VerbPlay,
            CB::VerbStopLegacy,
            CB::VerbConfirm,
        } };

        const int n = static_cast<int>(kButtons.size());
        int cellW, leftPad;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(area.getWidth());
            cellW = staggerCellW(hu);
            leftPad = staggerOffsetQ(hu);
        }
        else if (displayMode_ == GridDisplayMode::Clean)
        {
            cellW = (area.getWidth() - kClnColGap) / n;
            leftPad = 0;
        }
        else  // Ortholinear
        {
            cellW = (area.getWidth() - (n - 1) * kOrlGap) / n;
            leftPad = 0;
        }

        for (int i = 0; i < n; ++i)
        {
            int x;
            if (displayMode_ == GridDisplayMode::Clean)
                x = (i >= 2) ? area.getX() + 2 * cellW + kClnColGap + (i - 2) * cellW
                             : area.getX() + i * cellW;
            else if (displayMode_ == GridDisplayMode::Ortholinear)
                x = area.getX() + i * (cellW + kOrlGap);
            else
                x = area.getX() + leftPad + i * cellW;

            if (juce::Rectangle<int>(x, area.getY(), cellW, area.getHeight()).contains(pos))
                return { ControllerEvent::Type::ButtonDown, kButtons[static_cast<std::size_t>(i)], -1, 0 };
        }
        return {};
    }

    ControllerEvent KeyboardArea::hitTestModifierCell(juce::Point<int> pos,
                                                      juce::Rectangle<int> stepArea) const
    {
        auto area = stepArea;
        area.removeFromBottom(kNavRowH);
        if (!area.contains(pos))
            return {};

        static constexpr int kTotalGridCols = kCols + 2;
        const bool useClnGap = (displayMode_ == GridDisplayMode::Clean);
        const int intraStepGap = (displayMode_ == GridDisplayMode::Ortholinear) ? kOrlGap : 0;

        int cellW, staggerA, staggerZ;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(area.getWidth());
            cellW = staggerCellW(hu);
            staggerA = staggerOffsetA(hu);
            staggerZ = staggerOffsetZ(hu);
        }
        else if (useClnGap)
        {
            cellW = (area.getWidth() - kClnColGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        else
        {
            cellW = (area.getWidth() - (kTotalGridCols - 1) * kOrlGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        const int cellH = (area.getHeight() - intraStepGap) / kRows;
        if (cellW <= 0 || cellH <= 0)
            return {};

        const int relY = pos.getY() - area.getY();
        int row = -1;
        if (relY >= 0 && relY < cellH)
            row = 0;
        else if (relY >= cellH + intraStepGap && relY < 2 * cellH + intraStepGap)
            row = 1;
        if (row < 0)
            return {};

        const int rowStagger = (row == 0) ? staggerA : staggerZ;
        const int cellY = area.getY() + row * cellH + (row > 0 ? intraStepGap : 0);

        // Modifier buttons: logical layout
        //   row 0, col 0 = A (Scene)   row 0, col 1 = S (Master)
        //   row 1, col 0 = Z (Mute)    row 1, col 1 = X (Fill)
        using CB = ControllerButton;
        static constexpr CB kModButtons[2][2] = {
            { CB::MorphScope, CB::SongScope },
            { CB::MuteScope, CB::FillScope },
        };

        for (int mc = 0; mc < 2; ++mc)
        {
            int x;
            if (useClnGap)
                x = area.getX() + rowStagger + mc * cellW;
            else if (displayMode_ == GridDisplayMode::Ortholinear)
                x = area.getX() + mc * (cellW + kOrlGap);
            else
                x = area.getX() + rowStagger + mc * cellW;

            if (juce::Rectangle<int>(x, cellY, cellW, cellH).contains(pos))
                return { ControllerEvent::Type::ButtonDown,
                         kModButtons[row][mc], -1, 0 };
        }
        return {};
    }

    // -------------------------------------------------------------------------
    // Mouse

    void KeyboardArea::mouseDown(const juce::MouseEvent& e)
    {
        const auto pos = e.getPosition();
        const auto areas = computeRowAreas();

        // Section row — all 10 cells produce ControllerEvents.
        for (int i = 0; i < kTotalSectionCells; ++i)
        {
            if (!sectionCellBounds(i, areas.section).contains(pos)) continue;

            using CB = ControllerButton;
            ControllerEvent ev{ ControllerEvent::Type::ButtonDown, CB::None, -1, 0 };

            if (i < kFixedSectionCells)
            {
                static constexpr CB kFixed[kFixedSectionCells] = {
                    CB::Func, CB::TrackScope, CB::TapTempo, CB::NavUp
                };
                ev.button = kFixed[i];
            }
            else
            {
                // Section cells 4-9: emit Section (always), then apply layer remaps.
                // resolveLayer handles Section → MetaSection when Func is held,
                // matching QwertyOverlay and the controller path.
                const int section = cellToSection(i);
                if (section < 0) return;
                ev.button = CB::Section;
                ev.index = section;
                const LayerContext lctx{
                    uiState_.funcHeld,
                    uiState_.trackHeld || uiState_.latch.track,
                    uiState_.muteHeld || uiState_.latch.mute
                };
                ev = resolveLayer(ev, lctx);
            }

            mouseHeldButton_ = ev;
            if (onButtonDown) onButtonDown(ev);
            return;
        }

        // Function row (Q-P): modifiers + nav + verb buttons.
        {
            const auto ev = hitTestFunctionRow(pos, areas.function);
            if (ev.button != ControllerButton::None)
            {
                mouseHeldButton_ = ev;
                if (onButtonDown) onButtonDown(ev);
                return;
            }
        }

        // Step row modifier columns (A/S, Z/X).
        {
            const auto ev = hitTestModifierCell(pos, areas.step);
            if (ev.button != ControllerButton::None)
            {
                mouseHeldButton_ = ev;
                if (onButtonDown) onButtonDown(ev);
                return;
            }
        }

        // Nav strip (64-step overview): left=toggle trig, middle=scroll page, right=set length.
        {
            auto stepCheck = areas.step;
            const auto navArea = stepCheck.removeFromBottom(kNavRowH);
            if (navArea.contains(pos))
            {
                const int kNavSteps = 64;
                const int absStep = juce::jlimit(0, kNavSteps - 1,
                    (pos.x - navArea.getX()) * kNavSteps / juce::jmax(1, navArea.getWidth()));
                if (e.mods.isLeftButtonDown() && onMiniSeqToggle)
                    onMiniSeqToggle(absStep);
                else if (e.mods.isMiddleButtonDown() && onMiniSeqScrollToStep)
                    onMiniSeqScrollToStep(absStep);
                else if (e.mods.isRightButtonDown() && onMiniSeqSetLength)
                    onMiniSeqSetLength(absStep);
                return;
            }
        }

        // Step cells: emit Step event, then apply layer remaps so Track+click selects
        // the track and Mute+click toggles mute, mirroring QWERTY and controller paths.
        const int absIdx = stepCellAt(pos);
        if (absIdx >= 0)
        {
            // Page-relative index for the event (dispatchDown recomputes absStep itself,
            // but we store absIdx in mouseHeldStep_ for the matching mouseUp).
            const int pageRelIdx = absIdx % kPageSteps;
            mouseHeldStep_ = absIdx;
            const LayerContext lctx{
                uiState_.funcHeld,
                uiState_.trackHeld || uiState_.latch.track,
                uiState_.muteHeld || uiState_.latch.mute
            };
            ControllerEvent ev{
                ControllerEvent::Type::ButtonDown, ControllerButton::Step, pageRelIdx, 0
            };
            ev = resolveLayer(ev, lctx);
            mouseHeldButton_ = ev;
            if (onButtonDown) onButtonDown(ev);
        }
    }

    void KeyboardArea::mouseUp(const juce::MouseEvent&)
    {
        if (mouseHeldButton_.button == ControllerButton::None)
            return;

        const ControllerEvent up{
            ControllerEvent::Type::ButtonUp, mouseHeldButton_.button,
            mouseHeldButton_.index, 0
        };
        mouseHeldButton_ = {};
        mouseHeldStep_ = -1;
        if (onButtonUp) onButtonUp(up);
        markSurfaceDirty();   // the release clears held state — visible on controllers too
    }

    // -------------------------------------------------------------------------
    // resized

    void KeyboardArea::resized() {}

    // -------------------------------------------------------------------------
    // paint

    void KeyboardArea::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(20, 22, 26));
        const auto areas = computeRowAreas();
        const SurfaceModel model = buildSurfaceModel(
            uiState_, processor_.editContext(), pressTracker_,
            processor_, uiState_.activeTrack, stepPage_, displayMode_,
            slotOffset_, crossfaderValue_, morphView_);
        paintSectionRow(g, areas.section, model);
        paintFunctionRow(g, areas.function, model);
        paintStepRows(g, areas.step, model);
    }

    // -------------------------------------------------------------------------
    // paintSectionRow — number row (10 cells, MHY identities):
    //   Func(1)  Track(2)  TAP(3)  ^(4)  TRIG(5) SRC(6) FLTR(7) AMP(8) MOD(9) FX(0)
    //   cell 0   cell 1    cell 2  cell3  cell 4   ...                          cell 9

    void KeyboardArea::paintSectionRow(juce::Graphics& g, juce::Rectangle<int> area,
                                       const SurfaceModel& model)
    {
        paintEdgeRow(g, 0, area);
        const bool showKeyHint = (displayMode_ != GridDisplayMode::Clean);

        // Fixed cells: Func, Track, TAP, NavUp — rendered from model
        paintCell(g, sectionCellBounds(0, area), model.modifiers[0], showKeyHint);
        paintCell(g, sectionCellBounds(1, area), model.modifiers[1], showKeyHint);
        paintCell(g, sectionCellBounds(2, area), model.tap, showKeyHint);
        paintCell(g, sectionCellBounds(3, area), model.navUp, showKeyHint);

        // Section keys 5-0 (cells 4-9): render from model + page dots (screen-only)
        using PS = EditMode::PrimaryScope;
        const PS sectionScope = firstHeldSectionSuiteScope(uiState_);
        const bool isScopedMode = (sectionScope != PS::None);

        for (int s = 0; s < IMachine::kMaxSections; ++s)
        {
            const int cellIdx = kFixedSectionCells + s;
            const auto cell = sectionCellBounds(cellIdx, area);
            paintCell(g, cell, model.section[static_cast<std::size_t>(s)], showKeyHint);

            // Page dots — from model.pageDots (§35.8.1 residual now closed).
            const bool isMasterActive = !isScopedMode && (uiState_.masterSection == s);
            const bool isTrackActive = !isScopedMode && (uiState_.masterSection == -1 && uiState_.trackSection[static_cast<std::size_t>(uiState_.activeTrack)] == s);

            // §26.4.3 / Part 4 — Section-key fill by scope origin. Every key is
            // coloured by the scope its resolved page comes from: machine-owned →
            // neutral, track-owned → cyan (kScopeTrack). The active key gets a
            // stronger highlight; resting track-origin keys get a faint cyan wash
            // so origin reads at a glance. Holding Track peels the machine layer:
            // sections with no track params render dim (strict track-only view).
            const int at = uiState_.activeTrack;
            const bool haveTrack = at >= 0 && at < static_cast<int>(kNumTracks);
            const juce::Colour kCyan{ 0xFF30A0C0u };  // kScopeTrack
            if (haveTrack && !isScopedMode)
            {
                const auto ti = static_cast<std::size_t>(at);
                const bool keyScope =
                    uiState_.trackPageTrackScope[ti][static_cast<std::size_t>(s)];
                const auto groups = sectionsForKey(at, s, keyScope);
                const bool trackOrigin =
                    !groups.empty() && groups.front().origin == SecOrigin::Track;

                if (isMasterActive || isTrackActive)
                {
                    // Resolve the origin of the page actually shown (multi-page).
                    bool activePageIsTrack = trackOrigin;
                    if (isTrackActive)
                    {
                        int remaining = uiState_.trackPage[ti][static_cast<std::size_t>(s)];
                        for (const auto& gg : groups)
                        {
                            if (remaining < gg.pageCount)
                            {
                                activePageIsTrack = (gg.origin == SecOrigin::Track);
                                break;
                            }
                            remaining -= gg.pageCount;
                        }
                    }
                    const juce::Colour hi = activePageIsTrack ? kCyan : kColourTrackActive;
                    g.setColour(hi.withAlpha(0.18f));
                    g.fillRect(cell);
                }
                else if (trackOrigin)
                {
                    g.setColour(kCyan.withAlpha(0.07f));
                    g.fillRect(cell);
                }
            }
            else if (haveTrack && isScopedMode && sectionScope != PS::Morph)
            {
                // Item 7: a section key with no content at the held floor washes
                // dim — for every scope, not just Track (Phrase/Song dim their
                // non-content keys too). Morph keeps its bespoke section handling.
                const SecOrigin washFloor = sectionFloorForScope(sectionScope);
                if (!resolveSectionKey(processor_, at, s, washFloor, /*funcLayer*/ false,
                                       uiState_.stepHeld)
                         .hasContent)
                {
                    g.setColour(juce::Colours::black.withAlpha(0.38f));
                    g.fillRect(cell);
                }
            }
            const auto& dots = model.pageDots[static_cast<std::size_t>(s)];

            if (dots.count > 1 && !isMasterActive)
            {
                const int totalPageCount = dots.count;
                const int activePage = dots.active;

                const juce::Colour dotCol = isTrackActive
                                                ? kColourTrackActive
                                                : kColourTrackActive.withAlpha(0.4f);

                const int dotSize = 4;
                const int dotSpacing = 6;
                const int totalDotW = totalPageCount * dotSpacing - (dotSpacing - dotSize);
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

    void KeyboardArea::paintFunctionRow(juce::Graphics& g, juce::Rectangle<int> area,
                                        const SurfaceModel& model)
    {
        paintEdgeRow(g, 1, area);
        const bool showKeyHint = (displayMode_ != GridDisplayMode::Clean);

        // Layout geometry (unchanged — depends on display mode)
        static constexpr int kNumCells = 10;
        int leftPad, cellW;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(area.getWidth());
            cellW = staggerCellW(hu);
            leftPad = staggerOffsetQ(hu);
        }
        else if (displayMode_ == GridDisplayMode::Clean)
        {
            cellW = (area.getWidth() - kClnColGap) / kNumCells;
            leftPad = 0;
        }
        else  // Ortholinear
        {
            cellW = (area.getWidth() - (kNumCells - 1) * kOrlGap) / kNumCells;
            leftPad = 0;
        }

        for (int i = 0; i < kNumCells; ++i)
        {
            int x;
            if (displayMode_ == GridDisplayMode::Clean)
            {
                // Gap between the 2 modifier cells (Q, W) and the nav/verb keys.
                x = (i >= 2) ? area.getX() + 2 * cellW + kClnColGap + (i - 2) * cellW
                             : area.getX() + i * cellW;
            }
            else if (displayMode_ == GridDisplayMode::Ortholinear)
                x = area.getX() + i * (cellW + kOrlGap);
            else  // Staggered
                x = area.getX() + leftPad + i * cellW;

            const auto cellRect = juce::Rectangle<int>(x, area.getY(), cellW, area.getHeight());
            paintCell(g, cellRect, model.functionRow[static_cast<std::size_t>(i)], showKeyHint);
        }
    }

    // -------------------------------------------------------------------------
    // paintStepRows — main step area renderer.
    // Normal step cells consume model.step[] (Slice 2+).
    // Overlay re-skins (mute/scope/machine/etc.) early-return before consuming step cells.

    void KeyboardArea::paintStepRows(juce::Graphics& g, juce::Rectangle<int> area,
                                     const SurfaceModel& model)
    {
        static const juce::Colour kColOutRange{ kStepOutRange };
        static const juce::Colour kColPlayhead{ kStepPlayhead };
        static const juce::Colour kColHeld{ kStepHeld };
        static const juce::Colour kColPLock{ kStepPLock };

        const auto navArea = area.removeFromBottom(kNavRowH);
        const auto cellArea = area;

        const int trackLen = trackLength();
        const int baseStep = stepPage_ * kPageSteps;
        const auto& track = processor_.sequence().tracks[static_cast<std::size_t>(uiState_.activeTrack)];
        const bool fillActive = processor_.fillActive();

        // MHX step keys: D-; (steps 0-7, A row), C-/ (steps 8-15, Z row).
        static constexpr const char* kKeyLetters[kPageSteps] = {
            "D", "F", "G", "H", "J", "K", "L", ";",
            "C", "V", "B", "N", "M", ",", ".", "/"
        };
        // MHX: 2 modifier columns (A/S | Z/X) + 8 step columns = 10 total.
        static constexpr int kTotalGridCols = kCols + 2;
        const bool showKeyLetters = (displayMode_ != GridDisplayMode::Clean);
        const bool useClnGap = (displayMode_ == GridDisplayMode::Clean);
        const int intraStepGap = (displayMode_ == GridDisplayMode::Ortholinear) ? kOrlGap : 0;

        int cellW, staggerA, staggerZ;
        if (displayMode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(cellArea.getWidth());
            cellW = staggerCellW(hu);
            staggerA = staggerOffsetA(hu);
            staggerZ = staggerOffsetZ(hu);
        }
        else if (useClnGap)
        {
            cellW = (cellArea.getWidth() - kClnColGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        else  // Ortholinear
        {
            cellW = (cellArea.getWidth() - (kTotalGridCols - 1) * kOrlGap) / kTotalGridCols;
            staggerA = 0;
            staggerZ = 0;
        }
        const int cellH = (cellArea.getHeight() - intraStepGap) / kRows;

        auto rowY = [&](int row) -> int {
            return cellArea.getY() + row * cellH + (row > 0 ? intraStepGap : 0);
        };

        // col 0/1 = modifiers, col 2+ = steps.
        auto colX = [&](int row, int col) -> int {
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

        // Step-row modifier cells (A/S/Z/X) — rendered from model.modifiers[4..7].
        // Model carries funcHint (P-MUTE for Mute, etc.), pressed, pip, and strip.
        // modifiers index: 4=Scene(A), 5=Master(S), 6=Mute(Z), 7=Fill(X).
        {
            for (int row = 0; row < kRows; ++row)
            {
                for (int mc = 0; mc < 2; ++mc)
                {
                    const int modIdx = 4 + row * 2 + mc;
                    const int x = colX(row, mc);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH);
                    paintCell(g, cell, model.modifiers[static_cast<std::size_t>(modIdx)],
                              showKeyLetters);
                }
            }
        }

        // KEY panel (DESIGN §4.10): modifier checkboxes (top row) + symmetric
        // scales (bottom-right). Names + the "on" check border are screen
        // residuals over the model.step[] cells (KeyMod*/KeySym* states).
        if (uiState_.overlay == Overlay::Time && uiState_.sigPage == UiState::SigPage::Key)
        {
            for (int row = 0; row < kRows; ++row)
            {
                for (int col = 0; col < kCols; ++col)
                {
                    const int idx = row * kCols + col;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const int x = colX(row, col + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    const bool live = sc.base != CellState::StepOutOfRange;
                    paintGridCellFill(g, cell, sc);

                    const bool marked = (sc.base == CellState::KeyModActive
                                      || sc.base == CellState::KeyModDormant
                                      || sc.base == CellState::KeySymOn);
                    if (marked)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.75f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }

                    if (live) paintGridCellText(g, cell, sc);
                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)],
                                         live ? 1.0f : 0.4f);
                }
            }
            if (model.gridBanner)
            {
                g.setColour(juce::Colour::fromRGB(80, 95, 115));
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(model.gridBanner, navArea, juce::Justification::centred);
            }
            return;
        }

        // MHZ.3.5: Func+Part machine picker — step cells show available machine names.
        // Fill and press come from model; machine name text is a screen residual.
        if (uiState_.funcTrackHeld)
        {
            const juce::Colour machineTint = scopeColour(EditMode::PrimaryScope::Scene, true);

            for (int row = 0; row < kRows; ++row)
            {
                for (int col = 0; col < kCols; ++col)
                {
                    const int idx = row * kCols + col;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const bool avail = sc.base != CellState::MachineUnavailable;
                    const bool isCurrent = sc.base == CellState::MachineCurrent;

                    const int x = colX(row, col + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    paintGridCellFill(g, cell, sc);

                    if (avail && isCurrent)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.60f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }
                    else if (avail)
                    {
                        g.setColour(machineTint.withAlpha(0.35f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }

                    if (avail) paintGridCellText(g, cell, sc);
                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)],
                                         avail ? 1.0f : 0.45f);
                }
            }
            if (model.gridBanner)
            {
                g.setColour(juce::Colour::fromRGB(80, 95, 115));
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(model.gridBanner, navArea, juce::Justification::centred);
            }
            return;
        }

        // 6.5: FX insert picker — step cells show available effect names.
        if (uiState_.funcFxHeld)
        {
            const juce::Colour fxTint = col(compatColour(CellState::EffectAvailable));
            const juce::Colour otherTint = col(compatColour(CellState::EffectLoadedOther));

            for (int row = 0; row < kRows; ++row)
            {
                for (int col2 = 0; col2 < kCols; ++col2)
                {
                    const int idx = row * kCols + col2;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const bool avail = sc.base != CellState::MachineUnavailable;
                    const bool isCurrent = sc.base == CellState::EffectLoaded;
                    const bool isOther = sc.base == CellState::EffectLoadedOther;

                    const int x = colX(row, col2 + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    paintGridCellFill(g, cell, sc);

                    if (avail && isCurrent)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.60f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }
                    else if (avail && isOther)
                    {
                        g.setColour(otherTint.withAlpha(0.45f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }
                    else if (avail)
                    {
                        g.setColour(fxTint.withAlpha(0.35f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }

                    if (avail)
                    {
                        // 9.24 S12: masterOnly effects are compacted out of the track
                        // picker entirely, so every available cell here is track-legal.
                        const float ta = isCurrent ? 0.90f : (isOther ? 0.55f : 0.65f);
                        paintGridCellText(g, cell, sc, ta);
                    }
                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)],
                                         avail ? 1.0f : 0.45f);
                }
            }

            juce::String slotLabel = "INSERT " + juce::String(uiState_.funcFxInsertSlot + 1)
                + "  (tap FX = slot · reselect = bypass · hold = remove)";
            // 9.24 S12: page indicator when the track catalogue spans >1 page.
            if (const int tp = fxPickerPageCount(FxPickerCtx::TrackInsert); tp > 1)
                slotLabel += "   P" + juce::String(uiState_.fxPickerPage + 1)
                           + "/" + juce::String(tp) + " (Nav L/R)";
            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(slotLabel, navArea, juce::Justification::centred);
            return;
        }

        // 6.5: Master FX picker overlay.
        if (uiState_.masterFxPickerOpen)
        {
            const juce::Colour fxTint = col(compatColour(CellState::EffectAvailable));
            const juce::Colour otherTint = col(compatColour(CellState::EffectLoadedOther));

            for (int row = 0; row < kRows; ++row)
            {
                for (int col2 = 0; col2 < kCols; ++col2)
                {
                    const int idx = row * kCols + col2;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const bool avail = sc.base != CellState::MachineUnavailable;
                    const bool isCurrent = sc.base == CellState::EffectLoaded;
                    const bool isOther = sc.base == CellState::EffectLoadedOther;

                    const int x = colX(row, col2 + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    paintGridCellFill(g, cell, sc);

                    if (avail && isCurrent)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.60f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }
                    else if (avail && isOther)
                    {
                        g.setColour(otherTint.withAlpha(0.45f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }
                    else if (avail)
                    {
                        g.setColour(fxTint.withAlpha(0.35f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }

                    if (avail)
                        paintGridCellText(g, cell, sc,
                                          isCurrent ? 0.90f : (isOther ? 0.55f : 0.65f));
                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)],
                                         avail ? 1.0f : 0.45f);
                }
            }

            // 8.26: friendly label for the 4 master units.
            static const char* kUnitNames[4] = { "INSERT 1", "INSERT 2", "SEND A", "SEND B" };
            juce::String slotLabel = juce::String("MASTER ")
                + juce::String(kUnitNames[uiState_.masterFxInsertSlot])
                + "  (tap FX = unit · reselect = bypass · hold = remove)";
            // 9.24 S12: page indicator when this unit's catalogue spans >1 page.
            const FxPickerCtx mCtx = (uiState_.masterFxInsertSlot >= 2)
                                         ? FxPickerCtx::MasterSend : FxPickerCtx::MasterInsert;
            if (const int mp = fxPickerPageCount(mCtx); mp > 1)
                slotLabel += "   P" + juce::String(uiState_.fxPickerPage + 1)
                           + "/" + juce::String(mp) + " (Nav L/R)";
            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(slotLabel, navArea, juce::Justification::centred);
            return;
        }

        // Generator hub (9.10 / Bug B fix): cells driven fully from model.
        // model.step[0-2] = EUCLID/DENSITY/VEL with c.primary set; rest are dark.
        if (uiState_.generatorHubHeld)
        {
            for (int row = 0; row < kRows; ++row)
            {
                for (int col2 = 0; col2 < kCols; ++col2)
                {
                    const int idx = row * kCols + col2;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const int x = colX(row, col2 + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    paintGridCellFill(g, cell, sc);
                    paintGridCellText(g, cell, sc, 0.90f);
                    if (showKeyLetters && idx < 3)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)], 1.0f);
                }
            }
            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("GENERATOR HUB  --  select generator type",
                       navArea, juce::Justification::centred);
            return;
        }

        // S3: always-on looper console. Cells (fill/state token/press) + labels are
        // fully model-driven (SurfaceModel LooperConsole branch); this branch just
        // renders each cell's primary label (REC/PLAY/STOP/... , 1/16.. , TSTOP..).
        // Gated on the resolved layer (SSOT) so higher overlays still take priority.
        if (model.activeLayer == SurfaceLayer::LooperConsole)
        {
            for (int row = 0; row < kRows; ++row)
            {
                for (int col2 = 0; col2 < kCols; ++col2)
                {
                    const int idx = row * kCols + col2;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const int x = colX(row, col2 + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    paintGridCellFill(g, cell, sc);
                    paintGridCellText(g, cell, sc, 0.90f);
                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)], 0.55f);
                }
            }
            // Mini-seq (S2) occupies the nav row — the loop position display.
            paintTimeline(g, navArea);
            return;
        }

        // S1: Tape console text. The MachineConsole layer is shared by the Tape
        // console (per-track tape transport) and the Route matrix; only the Tape
        // variant is rendered here — the labels (REC/PLAY/STOP/... , |< CUE >| ,
        // << >>) are model-driven (SurfaceModel MachineConsole/tape branch,
        // c.primary), mirroring the LooperConsole path above. The Route variant
        // (non-tape track) is intentionally left to fall through unchanged.
        if (model.activeLayer == SurfaceLayer::MachineConsole
            && processor_.isTapeTrack(uiState_.activeTrack))
        {
            for (int row = 0; row < kRows; ++row)
            {
                for (int col2 = 0; col2 < kCols; ++col2)
                {
                    const int idx = row * kCols + col2;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const int x = colX(row, col2 + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    paintGridCellFill(g, cell, sc);
                    paintGridCellText(g, cell, sc, 0.90f);
                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)], 0.55f);
                }
            }
            // Tape has the always-on timeline strip; no nav-row timeline here.
            return;
        }

        // NoteEdit mode: 1-octave chromatic keyboard overlay.
        // Cells 0-11 = C through B; cells 12-15 = unused.
        // Fill + press feedback from model; outlines, note names, cross-octave badges inline.
        if (uiState_.noteEditMode && !uiState_.noteEditSteps.empty())
        {
            static constexpr const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

            const juce::Colour noteTint = col(kScopeNoteEdit);
            const juce::Colour stageTint = juce::Colour::fromRGB(220, 100, 60);

            const int octave = uiState_.noteEditOctave;
            const int trackIdx = uiState_.activeTrack;

            for (int row = 0; row < kRows; ++row)
            {
                for (int col2 = 0; col2 < kCols; ++col2)
                {
                    const int cellIdx = row * kCols + col2;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(cellIdx)];
                    const int x = colX(row, col2 + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    // Fill from model
                    g.setColour(juce::Colour(sc.baseColour));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);

                    if (cellIdx >= 12)
                        continue;

                    const int semitone = cellIdx;
                    const bool isStaged = sc.base == CellState::NoteEditStaged;
                    const bool isActive = sc.base == CellState::NoteEditActive
                                       || sc.base == CellState::NoteEditActiveOff || isStaged;
                    const bool isCrossOct = sc.base == CellState::NoteEditOther;

                    // Outline (screen residual)
                    if (isStaged)
                    {
                        g.setColour(stageTint.withAlpha(0.60f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }
                    else if (isActive)
                    {
                        g.setColour(noteTint.withAlpha(0.90f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }
                    else if (isCrossOct)
                    {
                        g.setColour(noteTint.withAlpha(0.40f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }

                    // Press feedback (new)
                    if (sc.pressed)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.65f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }

                    // Note name (screen residual)
                    g.setColour(isActive
                                    ? (isStaged ? stageTint : juce::Colours::white)
                                    : noteTint.withAlpha(0.60f));
                    g.setFont(juce::Font(juce::FontOptions(9.0f)).boldened());
                    g.drawText(juce::String(kNoteNames[semitone]), cell, juce::Justification::centred);

                    // Cross-octave badges (screen residual): re-derive other-octave list for glyph text.
                    // < / << / <<< = 1/2/3+ octaves below; > / >> / >>> = above; numeric fallback.
                    if (isCrossOct)
                    {
                        std::vector<int> otherOctaves;
                        for (const int stepIdx : uiState_.noteEditSteps)
                        {
                            if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) continue;
                            const auto& s = processor_.sequence()
                                                .tracks[static_cast<std::size_t>(trackIdx)]
                                                .steps[static_cast<std::size_t>(stepIdx)];
                            for (int n = 0; n < s.trigOverride.noteCount; ++n)
                            {
                                const int noteVal = s.trigOverride.notes[static_cast<std::size_t>(n)];
                                if (noteVal % 12 != semitone) continue;
                                const int noteOctave = noteVal / 12 - 1;
                                if (noteOctave == octave) continue;
                                bool alreadyListed = false;
                                for (int o : otherOctaves)
                                    if (o == noteOctave)
                                    {
                                        alreadyListed = true;
                                        break;
                                    }
                                if (!alreadyListed) otherOctaves.push_back(noteOctave);
                            }
                        }
                        if (!otherOctaves.empty())
                        {
                            std::sort(otherOctaves.begin(), otherOctaves.end());
                            juce::Font badgeFont(juce::FontOptions(11.0f));
                            g.setFont(badgeFont);
                            const int badgeH = 14, gap = 2;
                            const int by = cell.getY() + 2;

                        // Build glyph strings from relative octave offsets.
                            std::vector<juce::String> glyphs;
                            glyphs.reserve(static_cast<std::size_t>(otherOctaves.size()));
                            for (const int otherOct : otherOctaves)
                            {
                                const int diff = otherOct - octave;
                                juce::String gl;
                                if (diff == -1) gl = "<";
                                else if (diff == -2) gl = "<<";
                                else if (diff <= -3) gl = "<<<";
                                else if (diff == 1) gl = ">";
                                else if (diff == 2) gl = ">>";
                                else if (diff >= 3) gl = ">>>";
                                else gl = juce::String(otherOct);
                                glyphs.push_back(gl);
                            }

                        // Measure badge widths, right-align in the top-right corner.
                            std::vector<int> widths;
                            widths.reserve(glyphs.size());
                            int totalW = -gap;
                            for (const auto& gl : glyphs)
                            {
                                const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(badgeFont, gl)) + 6;
                                widths.push_back(w);
                                totalW += w + gap;
                            }

                            int bx = cell.getRight() - 2 - totalW;
                            for (int gi = 0; gi < static_cast<int>(glyphs.size()); ++gi)
                            {
                                const auto sz = static_cast<std::size_t>(gi);
                                const auto badge = juce::Rectangle<int>(bx, by, widths[sz], badgeH);
                                g.setColour(noteTint.withAlpha(0.55f));
                                g.fillRoundedRectangle(badge.toFloat(), 2.0f);
                                g.setColour(juce::Colours::white.withAlpha(0.85f));
                                g.drawText(glyphs[sz], badge, juce::Justification::centred);
                                bx += widths[sz] + gap;
                            }
                        }
                    } // if (isCrossOct)

                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(cellIdx)], 0.55f);
                }
            }

            const juce::String navMsg = "NOTE EDIT  oct " + juce::String(octave) + "  (NavLeft/Right to shift octave, release FUNC to commit)";
            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(navMsg, navArea, juce::Justification::centred);
            return;
        }

        // MHZ.3.4: P-Lock clear mode — packed display of only the set P-locks.
        // Fill and press from model; label text (slot name) is a screen residual that
        // still needs lockedSlots for the label text and nav message count.
        // Gated on !stepMoveActive: while moving a held step the layer flips to Base
        // (sequencer view), so model.step holds Base cells, not the packed P-lock
        // cells this branch indexes — rendering it here would read lockedSlots past
        // its end. resolveActiveLayer is the SSOT; this mirrors its StepInspector/
        // PLockClear gate (which also yields to stepMoveActive).
        if (uiState_.pLockClearMode && !uiState_.stepMoveActive
            && uiState_.pLockClearTrack == uiState_.activeTrack && uiState_.pLockClearStep >= 0)
        {
            const juce::Colour clearTint = col(kScopePLock);
            const int targetStep = uiState_.pLockClearStep;
            const auto& stepData = processor_.sequence()
                                       .tracks[static_cast<std::size_t>(uiState_.activeTrack)]
                                       .steps[static_cast<std::size_t>(targetStep)];
            const int numSlots = processor_.numParams(uiState_.activeTrack);

            // lockedSlots needed for label text and nav count (screen residual).
            std::vector<int> lockedSlots;
            const auto& tov = stepData.trigOverride;
            if (tov.hasVelocity) lockedSlots.push_back(-2);
            if (tov.hasGate) lockedSlots.push_back(-3);
            for (int s = 0; s < numSlots; ++s)
                if (stepData.overrides.has(s))
                    lockedSlots.push_back(s);

            for (int row = 0; row < kRows; ++row)
            {
                for (int col = 0; col < kCols; ++col)
                {
                    const int cellIdx = row * kCols + col;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(cellIdx)];
                    const bool hasPacked = sc.base != CellState::SelectorOutRange;
                    const bool isStaged = sc.base == CellState::SelectorEmpty;
                    const int slotIdx = hasPacked
                                            ? lockedSlots[static_cast<std::size_t>(cellIdx)]
                                            : -1;

                    const int x = colX(row, col + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    // Fill from model
                    g.setColour(juce::Colour(sc.baseColour));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);

                    if (hasPacked)
                    {
                        if (isStaged)
                        {
                            g.setColour(clearTint.withAlpha(0.35f));
                            g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                        }
                        else
                        {
                            g.setColour(clearTint.withAlpha(0.80f));
                            g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                        }

                        // Press feedback (new)
                        if (sc.pressed)
                        {
                            g.setColour(juce::Colours::white.withAlpha(0.65f));
                            g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                        }

                        // Screen residual: slot label
                        juce::String label;
                        if (slotIdx == -2) label = "Vel";
                        else if (slotIdx == -3) label = "Gate";
                        else label = processor_.paramSpec(uiState_.activeTrack, slotIdx).label;
                        g.setColour(juce::Colours::white.withAlpha(isStaged ? 0.35f : 0.90f));
                        g.setFont(juce::Font(juce::FontOptions(8.0f)));
                        g.drawText(label, cell.reduced(2), juce::Justification::centred, true);
                    }

                    if (showKeyLetters && hasPacked)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(cellIdx)], 1.0f);
                }
            }
            const int stagedCount = static_cast<int>(uiState_.pLockClearStaged.size());
            const juce::String navMsg = stagedCount > 0
                                            ? "CLEAR P-LOCK  " + juce::String(stagedCount) + " staged  (release FUNC to commit)"
                                            : "CLEAR P-LOCK  " + juce::String(static_cast<int>(lockedSlots.size())) + " lock(s)  (release FUNC to exit)";
            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(navMsg, navArea, juce::Justification::centred);
            return;
        }

        // MHZ.7.3: CHROMATIC mode — step cells become a piano keyboard.
        // Fill and press from model; note names, key hints, nav text inline.
        {
            const auto mode = (uiState_.activeTrack >= 0 && uiState_.activeTrack < static_cast<int>(kNumTracks))
                                  ? uiState_.trackInputMode[static_cast<std::size_t>(uiState_.activeTrack)]
                                  : TrackInputMode::Play;
            if (mode == TrackInputMode::Chromatic)
            {
                const juce::Colour border = col(kScopeTrack).withAlpha(0.70f);
                for (int row = 0; row < kRows; ++row)
                {
                    for (int col2 = 0; col2 < kCols; ++col2)
                    {
                        const int cellIdx = row * kCols + col2;
                        const SurfaceCell& sc = model.step[static_cast<std::size_t>(cellIdx)];
                        const int x = colX(row, col2 + 2);
                        const int y = rowY(row);
                        const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                        const int semitone = kPianoNoteOffset[static_cast<std::size_t>(cellIdx)];
                        const char* name = kPianoNoteNames[static_cast<std::size_t>(cellIdx)];

                        // Fill from model (includes dead-cell and pressed states)
                        g.setColour(juce::Colour(sc.baseColour));
                        g.fillRoundedRectangle(cell.toFloat(), 4.0f);

                        if (semitone < 0)
                            continue;

                        g.setColour(sc.pressed ? juce::Colours::white : border);
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, sc.pressed ? 1.5f : 1.0f);

                        // Screen residuals: note name, key hint
                        g.setColour(sc.pressed ? juce::Colours::black.withAlpha(0.90f)
                                               : juce::Colours::white.withAlpha(0.85f));
                        g.setFont(juce::Font(juce::FontOptions(9.0f)).boldened());
                        g.drawText(juce::String(name), cell, juce::Justification::centred);

                        if (showKeyLetters)
                            paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(cellIdx)], 0.65f);
                    }
                }

                // Mode badge is in the top context band; mini-seq occupies nav row.
                paintTimeline(g, navArea);
                return;
            }
        }

        // MHZ.7.4: LEVELS mode — step cells are 16 velocity buckets (1/16..16/16 of 127).
        // Fill and press from model; velocity text, outline, key hint inline.
        {
            const auto mode = (uiState_.activeTrack >= 0 && uiState_.activeTrack < static_cast<int>(kNumTracks))
                                  ? uiState_.trackInputMode[static_cast<std::size_t>(uiState_.activeTrack)]
                                  : TrackInputMode::Play;
            if (mode == TrackInputMode::Levels)
            {
                static const juce::Colour lowCol{ 0xFF204060u };
                static const juce::Colour highCol{ 0xFFE07030u };

                for (int row = 0; row < kRows; ++row)
                {
                    for (int col2 = 0; col2 < kCols; ++col2)
                    {
                        const int cellIdx = row * kCols + col2;
                        const SurfaceCell& sc = model.step[static_cast<std::size_t>(cellIdx)];
                        const int x = colX(row, col2 + 2);
                        const int y = rowY(row);
                        const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                        // Fill from model (gradient colour, pressed → white)
                        g.setColour(juce::Colour(sc.baseColour));
                        g.fillRoundedRectangle(cell.toFloat(), 4.0f);

                        // Outline: derive gradient colour from sc.level for brightness
                        const float t = sc.level;
                        const juce::Colour cellCol = lowCol.interpolatedWith(highCol, t);
                        g.setColour(sc.pressed ? juce::Colours::white
                                               : cellCol.brighter(0.3f).withAlpha(0.80f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, sc.pressed ? 1.5f : 1.0f);

                        // Screen residuals: velocity text, key hint
                        const int vel = juce::roundToInt(t * 127.0f);
                        g.setColour(sc.pressed ? juce::Colours::black.withAlpha(0.90f)
                                               : juce::Colours::white.withAlpha(0.80f));
                        g.setFont(juce::Font(juce::FontOptions(8.5f)));
                        g.drawText(juce::String(vel), cell.reduced(2),
                                   juce::Justification::centred);

                        if (showKeyLetters)
                            paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(cellIdx)], 0.65f);
                    }
                }

                // Mode badge is in the top context band; mini-seq occupies nav row.
                paintTimeline(g, navArea);
                return;
            }
        }

        // Mute re-skin — Slice 3: consume model.step[] built by builder.
        // Fills derive from model; border and text stay inline (screen residual).
        if (uiState_.muteHeld)
        {
            const bool isPatternMute = uiState_.funcHeld;
            const juce::Colour mutedCol = col(isPatternMute ? kScopePMute : kScopeMute);

            for (int row = 0; row < kRows; ++row)
            {
                for (int col2 = 0; col2 < kCols; ++col2)
                {
                    const int idx = row * kCols + col2;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const int x = colX(row, col2 + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    // Fill from model (fixes: builder now computes correct colour)
                    g.setColour(juce::Colour(sc.baseColour));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);

                    if (sc.base != CellState::SelectorOutRange)
                    {
                        const bool muted = sc.base == CellState::MuteMuted;
                        g.setColour(muted ? mutedCol.brighter(0.2f).withAlpha(0.90f)
                                          : juce::Colour(kScopeStep).withAlpha(0.40f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);

                        // Press feedback (Slice 3 fix: was missing)
                        if (sc.pressed)
                        {
                            g.setColour(juce::Colours::white.withAlpha(0.65f));
                            g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                        }

                        g.setColour(muted ? juce::Colours::white.withAlpha(0.90f)
                                          : juce::Colours::white.withAlpha(0.45f));
                        g.setFont(juce::Font(juce::FontOptions(9.0f)));
                        g.drawText(juce::String(idx + 1), cell.reduced(2),
                                   juce::Justification::centred);
                    }

                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)],
                                         sc.base != CellState::SelectorOutRange ? 1.0f : 0.45f);
                }
            }

            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(isPatternMute ? "SCENE MUTE" : "GLOBAL MUTE",
                       navArea, juce::Justification::centred);
            return;
        }

        // MHZ.2.1: scope re-skin — Slice 4: consume model.step[] built by builder.
        // Fill and pressed derive from model; border, text, badge stay inline.
        const bool scopeReskin = uiState_.trackHeld || uiState_.phraseScopeHeld || uiState_.sceneHeld;
        if (scopeReskin)
        {
            const juce::Colour scopeTint = scopeColourFromState(uiState_);

            for (int row = 0; row < kRows; ++row)
            {
                for (int col = 0; col < kCols; ++col)
                {
                    const int idx = row * kCols + col;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(idx)];
                    const int x = colX(row, col + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    const bool avail = sc.base != CellState::SelectorOutRange;
                    const bool isEmpty = sc.base == CellState::SelectorEmpty;
                    const bool isCurrent = sc.base == CellState::SelectorCurrent;
                    const bool isNext = sc.base == CellState::SelectorNext;
                    const bool isChain = sc.base == CellState::SelectorChain;
                    const int cpos = static_cast<int>(sc.level);

                    // Fill from model
                    g.setColour(juce::Colour(sc.baseColour));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);

                    // Border: occupied slots get a rim
                    if (!isEmpty && avail && isCurrent && !isNext)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.70f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }
                    else if (!isEmpty && avail && !isNext)
                    {
                        g.setColour(scopeTint.withAlpha(isChain ? 0.65f : 0.40f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }

                    // Home/global marker (DESIGN §4.7): amber outline on the scene's
                    // home phrase, set by the builder as sc.border (SelectorHome).
                    // Drawn on top so deviation reads as fill (current) ≠ border (home).
                    if (sc.border.present && avail)
                    {
                        g.setColour(juce::Colour(sc.border.colour));
                        g.drawRoundedRectangle(cell.toFloat().reduced(0.5f), 4.0f, 2.0f);
                    }

                    // Press feedback (Slice 4 fix: was missing)
                    if (sc.pressed && avail)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.65f));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }

                    // Label
                    const juce::String label = isEmpty
                                                   ? (uiState_.funcHeld ? "+" : "~")
                                                   : juce::String(idx + 1);
                    const juce::Colour textCol = !avail
                                                     ? juce::Colour::fromRGB(50, 55, 60)
                                                 : isEmpty
                                                     ? juce::Colour::fromRGB(80, 85, 90)
                                                     : (isNext ? juce::Colours::black
                                                               : juce::Colours::white.withAlpha(isCurrent ? 0.90f : 0.65f));
                    g.setColour(textCol);
                    g.setFont(juce::Font(juce::FontOptions(9.0f)));
                    g.drawText(label, cell.reduced(2), juce::Justification::centred);

                    // Chain-position badge (cpos encoded in sc.level by builder)
                    if (cpos > 0 && !isEmpty)
                    {
                        const auto badge = cell.withWidth(11).withHeight(11).withRightX(cell.getRight()).withY(cell.getY());
                        g.setColour(isNext ? juce::Colours::black.withAlpha(0.70f)
                                           : scopeTint.brighter(0.3f).withAlpha(0.85f));
                        g.fillRoundedRectangle(badge.toFloat(), 2.0f);
                        g.setColour(isNext ? juce::Colours::white : juce::Colours::black);
                        g.setFont(juce::Font(juce::FontOptions(7.0f)).boldened());
                        g.drawText(juce::String(cpos), badge, juce::Justification::centred);
                    }

                    if (showKeyLetters)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(idx)],
                                         !avail ? 0.45f : isNext ? 0.4f
                                                                 : 1.0f);
                }
            }
            if (model.gridBanner)
            {
                g.setColour(juce::Colour::fromRGB(80, 95, 115));
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(model.gridBanner, navArea, juce::Justification::centred);
            }
            return;
        }

        // ── 5.2 Morph step view ──────────────────────────────────────────────────
        // Morph held (no Func): step grid shows A/B pole states per MZ slot.
        // Row 0 (D-;) = A poles, Row 1 (C-/) = B poles.
        // Model cell base is MorphPoleActive/Dormant/Dark; primary holds param label.
        if (uiState_.morphHeld && !uiState_.funcHeld)
        {
            for (int row = 0; row < kRows; ++row)
            {
                for (int col = 0; col < kCols; ++col)
                {
                    const int localIdx = row * kCols + col;
                    const SurfaceCell& sc = model.step[static_cast<std::size_t>(localIdx)];
                    const int x = colX(row, col + 2);
                    const int y = rowY(row);
                    const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                    paintGridCellFill(g, cell, sc);
                    if (sc.base != CellState::MorphPoleDark)
                    {
                        const float ta = (sc.base == CellState::MorphPoleActive) ? 0.90f : 0.50f;
                        paintGridCellText(g, cell, sc, ta);
                    }
                    if (showKeyLetters && sc.base != CellState::MorphPoleDark)
                        paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(localIdx)], 0.6f);
                }
            }

            // Nav area: row labels
            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("A (top row) | B (bottom row)  --  tap to toggle  *  dark = capture  *  dim = dormant",
                       navArea, juce::Justification::centred);
            return;
        }
        // ── End morph step view ──────────────────────────────────────────────────

        // Step cells — Slice 2: consume model.step[] for body fill, press feedback,
        // and major decorations. Screen-only residuals (note-count ticks, key hints,
        // step numbers, track labels, keyboard note names) stay inline per §35.8.1.
        static const juce::Colour kColFillAdd{ kStepFillAdd };
        static const juce::Colour kColFillSuppress{ kStepFillSuppress };
        static const juce::Colour kColFillPLock{ kStepFillPLock };

        const auto& ctx = processor_.editContext();

        for (int row = 0; row < kRows; ++row)
        {
            for (int col = 0; col < kCols; ++col)
            {
                const int localIdx = row * kCols + col;
                const int absIdx = baseStep + localIdx;
                const bool inRange = absIdx < trackLen;
                const SurfaceCell& sc = model.step[static_cast<std::size_t>(localIdx)];

                const int x = colX(row, col + 2);
                const int y = rowY(row);
                const auto cell = juce::Rectangle<int>(x, y, cellW, cellH).reduced(2);

                // Body fill — from model (fixes always-green: baseColour uses scopeColourFromState)
                if (sc.base == CellState::StepOutOfRange)
                {
                    g.setColour(kColOutRange);
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                }
                else
                {
                    g.setColour(juce::Colour(sc.baseColour));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);

                    // Subtle outline on empty in-range steps (not playhead)
                    if (sc.level < 0.001f && !sc.border.present)
                    {
                        g.setColour(juce::Colour::fromRGB(70, 85, 100));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    }

                    // Fill-mode border (strip channel: orange=FillOn, blue=FillOff)
                    if (sc.strip.present && fillActive)
                    {
                        g.setColour(juce::Colour(sc.strip.colour));
                        g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                    }
                }

                // Playhead overlay (border channel)
                if (sc.border.present)
                {
                    g.setColour(kColPlayhead.withAlpha(0.35f));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                    g.setColour(kColPlayhead);
                    g.drawRoundedRectangle(cell.toFloat(), 4.0f, 2.0f);
                }

                // P-Lock dot (dot channel) + active-slot outline (inline: screen only)
                if (sc.dot.present)
                {
                    g.setColour(kColPLock);
                    g.fillRect(juce::Rectangle<int>(cell.getRight() - 5,
                                                    cell.getY() + 2, 4, 4));
                    const int activeSlot = ctx.activeSlot();
                    if (inRange && activeSlot >= 0)
                    {
                        const auto& stepRef = track.steps[static_cast<std::size_t>(absIdx)];
                        if (stepRef.overrides.has(activeSlot))
                        {
                            g.setColour(kColPLock);
                            g.drawRoundedRectangle(cell.toFloat(), 4.0f, 2.0f);
                        }
                    }
                }

                // Fill P-Lock badge (strip channel, in non-fill mode only)
                if (sc.strip.present && !fillActive)
                {
                    g.setColour(kColFillPLock);
                    g.fillRect(juce::Rectangle<int>(cell.getRight() - 5,
                                                    cell.getY() + 7, 4, 4));
                }

                // Held step outline
                if (sc.base == CellState::StepHeld)
                {
                    g.setColour(kColHeld);
                    g.drawRoundedRectangle(cell.toFloat(), 4.0f, 2.0f);
                }

                // Pressed step outline (Slice 2: new press feedback)
                if (sc.pressed && sc.base != CellState::StepOutOfRange)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.65f));
                    g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
                }

                // Latch pip (pip channel)
                if (sc.pip.present)
                {
                    constexpr int kPipSz = 5;
                    g.setColour(juce::Colour(sc.pip.colour));
                    g.fillEllipse(juce::Rectangle<int>(cell.getX() + 2,
                                                       cell.getBottom() - kPipSz - 2,
                                                       kPipSz, kPipSz)
                                      .toFloat());
                }

                // --- Screen-only residuals (controllers ignore) ---

                // Hold-affordance hint: a faint hollow ring on every in-range step
                // signals "press & hold to edit / P-lock" (ring = hold in the gesture
                // glyph vocab). Subtle and on the free right-middle edge so it clears
                // the key letter, P-lock dot, note ticks, micro-offset tick and pip.
                // The exact gesture set is spelled out in the inspector KEY region.
                if (inRange && sc.base != CellState::StepOutOfRange)
                {
                    constexpr float kHintR = 5.0f;
                    const float hx = static_cast<float>(cell.getRight()) - kHintR - 2.0f;
                    const float hy = static_cast<float>(cell.getCentreY()) - kHintR * 0.5f;
                    g.setColour(juce::Colours::white.withAlpha(0.16f));
                    g.drawEllipse(hx, hy, kHintR, kHintR, 1.0f);
                }

                // Note-count badge: stacked tick marks on the left edge, one per note.
                if (inRange)
                {
                    const auto& stepRef = track.steps[static_cast<std::size_t>(absIdx)];
                    const int nc = stepRef.trigOverride.noteCount;
                    if (nc > 0)
                    {
                        const juce::Colour noteCol = stepRef.trig
                                                         ? juce::Colours::white.withAlpha(0.75f)
                                                         : juce::Colour::fromRGB(120, 180, 220).withAlpha(0.70f);
                        g.setColour(noteCol);
                        const int dotH = 3;
                        const int dotW = 3;
                        const int gap = 1;
                        const int blockH = nc * dotH + (nc - 1) * gap;
                        int dotY = cell.getCentreY() - blockH / 2;
                        for (int n = 0; n < nc; ++n)
                        {
                            g.fillRect(cell.getX() + 2, dotY, dotW, dotH);
                            dotY += dotH + gap;
                        }
                    }
                }

                // MicroOffset tick (DESIGN §19.1 / show-microtiming-ticks).
                // A short bar at the bottom of the cell displaced left (early) or
                // right (late) to show the sub-step nudge direction.
                if (inRange)
                {
                    const float mo = track.steps[static_cast<std::size_t>(absIdx)].microOffset;
                    if (mo > 0.005f || mo < -0.005f)
                    {
                        constexpr int kTickH = 3;
                        constexpr int kTickW = 5;
                        const int tickY = cell.getBottom() - kTickH - 1;
                        const int cx = cell.getCentreX();
                        // Positive (late) → right of centre; negative (early) → left.
                        const int tickX = (mo > 0.0f)
                                              ? (cx + 2)
                                              : (cx - kTickW - 2);
                        const juce::Colour tickCol = (mo > 0.0f)
                                                         ? juce::Colour::fromRGB(255, 200, 80).withAlpha(0.85f)  // late: amber
                                                         : juce::Colour::fromRGB(80, 200, 255).withAlpha(0.85f); // early: cyan
                        g.setColour(tickCol);
                        g.fillRect(tickX, tickY, kTickW, kTickH);
                    }
                }

                if (showKeyLetters)
                    paintCellKeyHint(g, cell, kKeyLetters[static_cast<std::size_t>(localIdx)],
                                     inRange ? 1.0f : 0.45f);

                g.setColour(inRange ? juce::Colour::fromRGB(110, 130, 150)
                                    : juce::Colour::fromRGB(40, 46, 54));
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText(juce::String(absIdx + 1), cell, juce::Justification::centred);
            }
        }

        paintTimeline(g, navArea);
    }

    // -------------------------------------------------------------------------
    // paintTimeline — read-only 64-step overview strip in the nav row.
    // Fixed scale: 64 cells always fill the full width.
    // Active steps (0..trackLen-1) are drawn normally; the tail is dimmed.
    // Trig-active cells are filled with the track scope colour.
    // The current page window is highlighted with a bright border band.
    // The playhead cell is marked with the amber playhead colour.
    // Beat/bar dividers come from the time signature + track divider.

    void KeyboardArea::paintTimeline(juce::Graphics& g, juce::Rectangle<int> navArea)
    {
        static constexpr int kTimelineSteps = 64;

        // Fetch track state
        const int trackLen = juce::jlimit(1, kTimelineSteps, trackLength());

        auto* divP = processor_.apvts().getRawParameterValue(
            ParamIDs::trackDivider(uiState_.activeTrack));
        const int subdivIdx = divP ? std::clamp(static_cast<int>(divP->load()),
                                                 kSubdivMin, kSubdivMax) : kSubdivDefault;
        const double divPpq = subdivisionPpqFromIndex(subdivIdx);

        // Playhead position (-1 when stopped / divPpq==0)
        int playheadAbs = -1;
        if (divPpq > 0.0)
        {
            const auto stepNum = static_cast<std::int64_t>(
                processor_.clock().cumulativePpq() / divPpq);
            playheadAbs = static_cast<int>(stepNum % trackLen);
        }

        // Beat / bar dividers from time signature
        const auto& ts = processor_.section().coreTime;
        const double beatPpq = 4.0 / static_cast<double>(ts.denominator);
        const int stepsPerBeat = juce::jmax(1, static_cast<int>(std::round(beatPpq / divPpq)));
        const int stepsPerBar = juce::jmax(stepsPerBeat,
                                           static_cast<int>(std::round(ts.barPpq() / divPpq)));

        const auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(uiState_.activeTrack)];

        // Layout: vertical centering within navArea, leaving a small margin
        const int margin = 3;
        const int cellH = navArea.getHeight() - 2 * margin;
        const float cellW = static_cast<float>(navArea.getWidth()) / static_cast<float>(kTimelineSteps);

        const int pageFirst = stepPage_ * kPageSteps;
        const int pageLast = pageFirst + kPageSteps - 1;

        // Page window background band drawn first (underneath cells)
        {
            const float bandX = navArea.getX() + static_cast<float>(pageFirst) * cellW;
            const float bandW = static_cast<float>(kPageSteps) * cellW;
            g.setColour(juce::Colour(0xFF1E2A38u));
            g.fillRect(juce::Rectangle<float>(bandX,
                                              static_cast<float>(navArea.getY()),
                                              bandW,
                                              static_cast<float>(navArea.getHeight())));
        }

        // Draw each cell
        for (int i = 0; i < kTimelineSteps; ++i)
        {
            const float cx = navArea.getX() + static_cast<float>(i) * cellW;
            const auto cellF = juce::Rectangle<float>(cx + 1.0f,
                                                      static_cast<float>(navArea.getY() + margin),
                                                      cellW - 2.0f,
                                                      static_cast<float>(cellH));

            const bool inRange = i < trackLen;
            const bool hasTrig = inRange && trk.steps[static_cast<std::size_t>(i)].trig;
            const bool isHead = (i == playheadAbs);
            const bool onPage = (i >= pageFirst && i <= pageLast);

            // Cell background
            if (hasTrig)
                g.setColour(juce::Colour(inRange ? kScopeTrack : kScopeTrackDim));
            else if (inRange)
                g.setColour(juce::Colour(onPage ? 0xFF2A3848u : 0xFF1C2530u));
            else
                g.setColour(juce::Colour(kStepOutRange));

            g.fillRect(cellF);

            // Playhead marker — amber border drawn over the cell
            if (isHead)
            {
                g.setColour(juce::Colour(kStepPlayhead));
                g.drawRect(cellF, 1.5f);
            }
        }

        // Beat / bar divider lines (drawn on top of cells)
        for (int i = 1; i < kTimelineSteps; ++i)
        {
            if (i % stepsPerBar == 0)
            {
                // Bar line: bright, full height
                const float lx = navArea.getX() + static_cast<float>(i) * cellW;
                g.setColour(juce::Colour(0xFF485868u));
                g.fillRect(juce::Rectangle<float>(lx - 0.5f,
                                                  static_cast<float>(navArea.getY()),
                                                  1.5f,
                                                  static_cast<float>(navArea.getHeight())));
            }
            else if (i % stepsPerBeat == 0)
            {
                // Beat line: dimmer, partial height
                const float lx = navArea.getX() + static_cast<float>(i) * cellW;
                g.setColour(juce::Colour(0xFF2E3A48u));
                g.fillRect(juce::Rectangle<float>(lx - 0.5f,
                                                  static_cast<float>(navArea.getY() + margin),
                                                  1.0f,
                                                  static_cast<float>(cellH)));
            }
        }

        // Page window border (drawn on top of everything)
        {
            const float bandX = navArea.getX() + static_cast<float>(pageFirst) * cellW;
            const float bandW = static_cast<float>(kPageSteps) * cellW;
            g.setColour(juce::Colour(0xFF506880u));
            g.drawRect(juce::Rectangle<float>(bandX,
                                              static_cast<float>(navArea.getY()),
                                              bandW,
                                              static_cast<float>(navArea.getHeight())),
                       1.0f);
        }

        // S2: loop-position overlay. A focused looper turns the mini-seq into its
        // loop display: a *continuous* playhead (sub-cell, smooth) swept by the
        // machine's own phase 0..1 across the loop (= the track grid in Sync), plus
        // a landing pip at the loop-start anchor while a quantized edge is pending
        // (Armed / scheduled stop / re-play). The cell playhead above stays as a
        // coarse marker; this line is the precise position.
        if (processor_.isLooperTrack(uiState_.activeTrack))
        {
            const float phase = processor_.looperPhase(uiState_.activeTrack);  // 0..1 or -1
            if (phase >= 0.0f)
            {
                const float hx = navArea.getX()
                    + juce::jlimit(0.0f, 1.0f, phase) * static_cast<float>(trackLen) * cellW;
                g.setColour(juce::Colour(0xFF40D0D0u));  // bright cyan — loop playhead
                g.fillRect(juce::Rectangle<float>(hx - 0.75f,
                                                  static_cast<float>(navArea.getY()),
                                                  1.5f,
                                                  static_cast<float>(navArea.getHeight())));
            }
            if (processor_.looperPending(uiState_.activeTrack))
            {
                // Landing pip: an amber triangle at the loop-start (cell 0), where the
                // armed/pending edge will fire on the next grid boundary.
                const float px = navArea.getX();
                const float py = static_cast<float>(navArea.getY());
                juce::Path pip;
                pip.addTriangle(px, py, px + 7.0f, py, px, py + 7.0f);
                g.setColour(juce::Colour(kStepPlayhead));  // amber
                g.fillPath(pip);
            }
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
        const KeyGroup dimGrp{ 0xFF0E1218u, 0xFF1C2430u, 0xFF3040A0u };

        if (displayMode_ == GridDisplayMode::Ortholinear)
        {
            const int gap = kOrlGap;
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
            static const int kLeftCode[4] = { 96, 9, 0, 0 };  // ` Tab CAP SHF
            static const char* const kLeftLabel[4] = { "`", "TAB", "CAP", "SHF" };
            static const int kRightCode[4] = { 45, 91, 39, 0 };  // - [ ' (none)
            static const char* const kRightLabel[4] = { "-", "[", "'", "" };

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
                const int kc = kRightCode[rowIndex];
                const bool pressed = (kc > 0) && juce::KeyPress::isKeyCurrentlyDown(kc);
                paintKeyButton(g, rightRect, kRightLabel[rowIndex], "", "",
                               dimGrp,
                               pressed ? KeyButtonState::Pressed : KeyButtonState::Normal,
                               showHint);
            }
        }
        else  // Staggered
        {
            const int hu = staggerHalfUnit(rowArea.getWidth());
            const int cw = staggerCellW(hu);   // 2*hu
            const int rY = rowArea.getY();
            const int rH = rowArea.getHeight();

            // Row stagger offsets (half-units): number=0, Q=1, A=2, Z=3
            static constexpr int kStaggerHu[4] = { 0, 1, 2, 3 };
            const int mainLeft = rowArea.getX() + kStaggerHu[rowIndex] * hu;
            const int mainRight = mainLeft + 10 * cw;  // MHX: 10-wide

            // Left edge key: one key per row, ANSI width (backtick=2hu, Tab=3hu,
            // CapsLock=4hu, LShift=5hu).  All start at the same x (kSideMargin-2hu).
            {
                static const int kLWHu[4] = { 2, 3, 4, 5 };
                static const int kLCode[4] = { 96, 9, 0, 0 };  // ` Tab - -
                static const char* const kLLabel[4] = { "`", "TAB", "CAP", "SHF" };
                const int keyW = kLWHu[rowIndex] * hu;
                const int kx = mainLeft - keyW;
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
                static const int kRCode[4][3] = {
                    { 45, 61, 0 },  // number: -  =  (0 is now in main block)
                    { 91, 93, 0 },  // Q:      [  ]  (P is now in main block)
                    { 39, 0, 0 },  // A:      '     (; is now in main block)
                    { 0, 0, 0 },  // Z:      none  (/ is now in main block)
                };
                static const char* const kRLabel[4][3] = {
                    { "-", "=", "" },
                    { "[", "]", "" },
                    { "'", "", "" },
                    { "", "", "" },
                };
                const int compW = getWidth();
                int rx = mainRight;
                for (int j = 0; j < 3; ++j)
                {
                    if (kRLabel[rowIndex][j][0] == '\0') break;
                    if (rx >= compW) break;
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
