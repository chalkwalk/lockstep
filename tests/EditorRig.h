#pragma once

// EditorRig -- the shared headless-editor test rig.
//
// A real (headless) LockstepEditor plus the one seam into its private guts
// (DispatchProbe, friended in PluginEditor.h). Extracted from DispatchGoldenTest
// so the golden net and the UI-interaction harness (UiDriver.h) drive the SAME
// editor the product ships, through the same doors: two rigs would eventually
// disagree, and the one nobody looked at would be the one telling the truth.
//
// Headless is fine -- 9.13's note that an editor harness "proved unviable
// headless (component-teardown segfault)" was a misdiagnosis: LockstepProcessor
// embeds Arrangement (~47 MB), so a stack-local processor overflows the stack in
// the enclosing function's prologue and dies before its first statement.
// Heap-allocate it and dispatch runs fine. Never stack-construct a Rig's
// processor; never stack-construct a Song either (they are MBs each).
//
// The probe is a struct of statics on purpose: PluginEditor.h grants exactly ONE
// named friend, so every test seam funnels through here rather than growing a
// public test-only API that production code could start calling by accident.

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"
#include "../src/ui/mode/GestureRecognizer.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace lockstep
{
    // The one seam into dispatch (friended in PluginEditor.h): the entry points stay
    // private to production code.
    struct DispatchProbe
    {
        // FIDELITY (7b). Every real input path -- QWERTY, mouse, controller -- runs
        // the raw event through resolveLayer() BEFORE dispatch (ButtonLayers.h says
        // so in its first line). The first version of this probe called dispatchDown
        // directly and skipped it, so `Func+Y` never became CB::Restore and the
        // golden recorded RESTORE as "pushes a second checkpoint" -- a path the
        // product never takes. A net that models a different input path than the
        // instrument is worse than no net: it is green about fiction.
        static ControllerEvent layered(const LockstepEditor& ed, ControllerEvent raw)
        {
            const UiState& u = ed.uiState_;
            const LayerContext lctx{ u.funcHeld,
                                     u.trackHeld || u.latch.track,
                                     u.muteHeld || u.latch.mute };
            return resolveLayer(raw, lctx);
        }
        static bool down(LockstepEditor& ed, ControllerEvent ev)
        {
            return ed.dispatchDown(layered(ed, ev), 0);
        }
        static void up(LockstepEditor& ed, ControllerEvent ev)
        {
            ed.dispatchUp(layered(ed, ev), 0);
        }
        static const UiState& ui(const LockstepEditor& ed) { return ed.uiState_; }
        static int activeTrack(const LockstepEditor& ed) { return ed.keyboardArea_.getActiveTrack(); }
        static const Clipboard& clip(const LockstepEditor& ed) { return ed.clipboard_; }
        // 7c: the step page lives in KeyboardArea, not UiState, so a UiState-only
        // digest recorded every page-nav gesture as "(no observable state change)".
        static int page(const LockstepEditor& ed) { return ed.keyboardArea_.currentPage(); }

        // The editor's press state -- and, through it, the substitutable key-state
        // oracle the synthetic-keyboard harness installs (see PressTracker.h).
        static PressTracker& press(LockstepEditor& ed) noexcept { return ed.pressTracker_; }

        // -- virtual time (UI harness Tier 1) -----------------------------------
        // The editor reads its gesture clock through nowMs() (see the WI-1 funnel
        // and tests/ClockFunnelGuardTest.cpp). Setting testNowMs_ freezes it, so a
        // double-tap or long-press is expressed by ADVANCING time rather than by
        // sleeping through it: deterministic, and roughly a thousand times faster.
        static void setNow(LockstepEditor& ed, double ms) noexcept { ed.testNowMs_ = ms; }
        static void advance(LockstepEditor& ed, double ms) noexcept { ed.testNowMs_ += ms; }
        static double now(const LockstepEditor& ed) noexcept { return ed.testNowMs_; }

        // 9.30 st.6 — layout verification. Real pixels, traced through resized(), never
        // arithmetic done in a comment: the bands were reordered and re-sized, and the
        // only honest way to know the grid still fits is to lay it out and MEASURE it.
        static juce::Rectangle<int> kbBounds(const LockstepEditor& ed)
        {
            return ed.keyboardArea_.getBounds();
        }
        // (KeyboardArea::computeRowAreas is private; the step grid's real estate is
        //  measured through the public nav-area + component bounds instead, which is
        //  the same geometry the paint path uses.)
        static juce::Rectangle<int> navArea(const LockstepEditor& ed)
        {
            return ed.keyboardArea_.navAreaBounds();
        }
        static void setGridMode(LockstepEditor& ed, GridDisplayMode m)
        {
            ed.applyDisplayMode(m);
            ed.resized();
        }
        static juce::Rectangle<int> inspector(const LockstepEditor& ed) { return ed.inspectorRow_; }
        static juce::Rectangle<int> popover(const LockstepEditor& ed) { return ed.confirmPopoverRegion_; }
        static juce::Rectangle<int> mz(const LockstepEditor& ed)
        {
            return ed.manipulationZone_.getBounds();
        }
        static juce::Rectangle<int> meter(const LockstepEditor& ed) { return ed.masterChromeRegion_; }

        // -- UI scale + mouse (UI harness Tier 2) --------------------------------
        // The editor lays out on a FIXED design canvas and stamps
        // AffineTransform::scale on every child (Item E). So a physical pixel is not
        // a design pixel, and "the click lands where the cell is drawn" became a
        // property that can break silently -- no test could see it, because no test
        // had a mouse.
        static double uiScale(const LockstepEditor& ed) noexcept { return ed.uiScale_; }
        static bool masterDragActive(const LockstepEditor& ed) noexcept { return ed.masterDrag_.active; }

        static KeyboardArea& keyboard(LockstepEditor& ed) noexcept { return ed.keyboardArea_; }

        // The surface model the LIVE editor would paint right now.
        //
        // Mirrors KeyboardArea::paint's buildSurfaceModel call exactly (same args,
        // same order) -- that is the whole point: an interaction test asserts on what
        // the screen WOULD show after the gesture, not on a model assembled from
        // stubs. If paint's argument list ever changes, this must change with it or
        // the harness starts describing a surface nobody sees.
        static SurfaceModel surface(LockstepEditor& ed)
        {
            auto& kb = ed.keyboardArea_;
            return buildSurfaceModel(kb.uiState_, ed.processor_.editContext(), kb.pressTracker_,
                                     ed.processor_, kb.uiState_.activeTrack, kb.stepPage_,
                                     kb.displayMode_, kb.slotOffset_, kb.crossfaderValue_,
                                     kb.morphView_);
        }

        // -- Tier 3: pixels ------------------------------------------------------
        // The editor, rendered offscreen exactly as the host would paint it.
        //
        // paintEntireComponent walks the child tree applying each child's
        // AffineTransform, which is the whole reason this is worth doing: it is the
        // same walk the screen gets, so a child that lost its transform renders wrong
        // HERE too. A test that composited the children itself would be checking its
        // own arithmetic and would happily stay green through exactly the bug this is
        // meant to catch (Item E).
        //
        // Reproducible only because the paint clocks are pushed, not read (see
        // setAnimClockMs + ClockFunnelGuardTest): render the same frozen state twice
        // and the bytes match.
        static juce::Image render(LockstepEditor& ed)
        {
            juce::Image img(juce::Image::ARGB, ed.getWidth(), ed.getHeight(), true);
            juce::Graphics g(img);
            ed.paintEntireComponent(g, true);
            return img;
        }

        // The bounding box of a step cell, in KeyboardArea-LOCAL (design) coordinates.
        //
        // Same principle as centerOfStep below -- SCAN the product's own hit test, do
        // not recompute the layout -- but keeping the extent, not just the centroid, so
        // a region of the cell can be examined rather than a single pixel. Empty if the
        // cell is not on screen.
        //
        // The 2 px scan step costs up to 2 px of extent per side. Every caller insets
        // the result substantially, so it does not matter; if one ever does not, it
        // should scan at 1 px rather than trust this to be tight.
        static juce::Rectangle<int> stepCellBounds(LockstepEditor& ed, int absIdx)
        {
            auto& kb = ed.keyboardArea_;
            int minX = kb.getWidth(), minY = kb.getHeight(), maxX = -1, maxY = -1;
            for (int y = 0; y < kb.getHeight(); y += 2)
            {
                for (int x = 0; x < kb.getWidth(); x += 2)
                {
                    if (kb.stepCellAt({ x, y }) != absIdx)
                        continue;
                    minX = juce::jmin(minX, x);
                    minY = juce::jmin(minY, y);
                    maxX = juce::jmax(maxX, x);
                    maxY = juce::jmax(maxY, y);
                }
            }
            if (maxX < 0)
                return {};
            return juce::Rectangle<int>::leftTopRightBottom(minX, minY, maxX + 1, maxY + 1);
        }

        // A section-row cell's bounds, from the accessor the PAINT path uses
        // (sectionCellBounds + computeRowAreas), in KeyboardArea-local coordinates.
        // The section row already has a shared geometry owner, so there is nothing to
        // scan for -- this is the shape the step row is being moved towards (WI-4).
        static juce::Rectangle<int> sectionCellBounds(LockstepEditor& ed, int cellIdx)
        {
            auto& kb = ed.keyboardArea_;
            return kb.sectionCellBounds(cellIdx, kb.computeRowAreas().section);
        }

        // The centre of a step cell, in KeyboardArea-LOCAL (design) coordinates.
        //
        // Found by SCANNING the real hit test (KeyboardArea::stepCellAt) rather than
        // recomputing the layout: the harness must aim at whatever the product thinks
        // the cell is, or it proves only that the test's arithmetic matches itself.
        // Returns the centroid of the matching pixels; {-1,-1} if the cell is not on
        // screen (wrong page, or a display mode that hides it).
        static juce::Point<float> centerOfStep(LockstepEditor& ed, int absIdx)
        {
            auto& kb = ed.keyboardArea_;
            juce::Point<int> sum{ 0, 0 };
            int n = 0;
            for (int y = 0; y < kb.getHeight(); y += 2)
            {
                for (int x = 0; x < kb.getWidth(); x += 2)
                {
                    if (kb.stepCellAt({ x, y }) != absIdx)
                        continue;
                    sum += juce::Point<int>{ x, y };
                    ++n;
                }
            }
            if (n == 0)
                return { -1.0f, -1.0f };
            return { static_cast<float>(sum.x) / static_cast<float>(n),
                     static_cast<float>(sum.y) / static_cast<float>(n) };
        }
    };

    // ---------------------------------------------------------------------------
    // A fresh editor per case: gestures mutate state, so cases must not contaminate
    // each other. Heap, always (see the Arrangement note above).
    // ---------------------------------------------------------------------------
    struct Rig
    {
        std::unique_ptr<LockstepProcessor> proc;
        std::unique_ptr<LockstepEditor> editor;

        Rig()
        {
            proc = std::make_unique<LockstepProcessor>();
            proc->setRateAndBufferSizeDetails(44100.0, 512);

            // PRIOR STATE (7b). A blank project makes destructive verbs invisible:
            // Clear on an empty phrase changes nothing, so the golden would record
            // "no observable state change" and stay green even if Clear stopped
            // working. Seed trigs so Clear/Paste/Init have something to destroy.
            auto& seq = proc->sequence();
            for (std::size_t t = 0; t < 4; ++t)
                for (int s = 0; s < 16; s += 4)
                    seq.tracks[t].steps[static_cast<std::size_t>(s)].trig = true;

            editor = std::make_unique<LockstepEditor>(*proc);
            editor->setSize(1400, 900);
        }
        ~Rig() { editor.reset(); }   // editor before processor: it holds a reference
    };

    // Freeze the rig's clock at a fixed epoch.
    //
    // The epoch must be LARGE. DoubleTapDetector::lastTimeMs_ and
    // GestureRecognizer::playLastMs_ both init to 0.0, so a virtual clock starting
    // at 0 makes the very first Play press read as a double-tap against a phantom
    // press "0 ms ago". Starting at 1.0e6 (~17 minutes) puts every uninitialised
    // last-press timestamp comfortably in the distant past, which is exactly what
    // it means in production: nothing was pressed recently.
    inline constexpr double kTestEpochMs = 1.0e6;

    inline void freeze(Rig& rig, double epochMs = kTestEpochMs) noexcept
    {
        DispatchProbe::setNow(*rig.editor, epochMs);
    }
}   // namespace lockstep
