// SyntheticMouseTest -- clicks land where the surface is drawn, at any UI scale.
//
// The editor lays out on a FIXED design canvas (990x626) and stamps
// AffineTransform::scale on every child; the window's real size decides the scale.
// That means a physical pixel is no longer a design pixel, and "the click lands on
// the cell you can see" stopped being free. It is also invisible to every existing
// test: the golden speaks ControllerEvents, SurfaceModelTest calls a pure builder,
// and neither has a mouse. The scaled UI shipped visually unverified for exactly
// this reason.
//
// The clicks here route through Component::getComponentAt + getLocalPoint -- both
// transform-aware -- so they resolve against the same transform stack the paint
// path draws through. A child that lost its transform, or an editor that stopped
// dividing by uiScale_, moves the answer and fails a test here.
//
// The cell to aim at comes from KeyboardArea::stepCellAt (the product's own hit
// test), never from layout arithmetic redone in the test. A test that computes its
// own geometry agrees with itself while the screen does something else.

#include "UiDriver.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // The two window sizes: design canvas exactly (scale 1.0), and the shipped
    // default (1.2). resized() derives the scale from the actual width.
    constexpr int kDesignW = 990;
    constexpr int kDesignH = 626;

    void setScale(UiDriver& d, double scale)
    {
        d.editor().setSize(static_cast<int>(kDesignW * scale),
                           static_cast<int>(kDesignH * scale));
    }

    // Spike (the plan's top risk): headless JUCE has no display and no peer, so
    // before anything is built on synthetic mouse events, prove they can exist and
    // that getComponentAt/getLocalPoint really do invert an AffineTransform.
    void testMouseMachineryWorksHeadless(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthMouse/spike] %s\n", what); ++failed; }
        };

        auto src = juce::Desktop::getInstance().getMainMouseSource();
        check(src.isMouse(), "a main mouse source exists without a display");

        struct Probe : juce::Component
        {
            int hits = 0;
            juce::Point<float> last;
            void mouseDown(const juce::MouseEvent& e) override { ++hits; last = e.position; }
        };

        juce::Component parent;
        parent.setSize(400, 400);
        parent.setVisible(true);   // getComponentAt is gated on visibleFlag
        Probe child;
        child.setBounds(0, 0, 100, 100);
        parent.addAndMakeVisible(child);
        child.setTransform(juce::AffineTransform::scale(2.0f));

        // A 2x child covers 0..200 physical; (150,150) is inside it and maps to (75,75).
        auto* hit = parent.getComponentAt(juce::Point<int>{ 150, 150 });
        check(hit == &child, "getComponentAt inverts the child's transform");
        const auto local = child.getLocalPoint(&parent, juce::Point<float>{ 150.0f, 150.0f });
        check(std::abs(local.x - 75.0f) < 0.5f && std::abs(local.y - 75.0f) < 0.5f,
              "getLocalPoint maps physical back to design space");
    }

    // The load-bearing one: the same cell, clicked at two different scales, must do
    // the same thing.
    void testStepCellHitAtEveryScale(int& failed)
    {
        auto check = [&failed](bool ok, const juce::String& what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthMouse/step] %s\n", what.toRawUTF8()); ++failed; }
        };

        for (const double scale : { 1.0, 1.2 })
        {
            const juce::String at = juce::String(" (scale ") + juce::String(scale, 2) + ")";

            UiDriver d;
            setScale(d, scale);
            check(std::abs(DispatchProbe::uiScale(d.editor()) - scale) < 0.01,
                  juce::String("resized() derived the expected scale") + at);

            // Step 0 is seeded ON by the rig; clicking its cell must toggle it OFF.
            const bool before = d.proc().sequence().tracks[0].steps[0].trig;
            d.clickStep(0);
            check(d.proc().sequence().tracks[0].steps[0].trig != before,
                  juce::String("clicking step 0's cell toggles step 0's trig") + at);

            // And a different cell must be a different step -- proof the mapping is
            // not just "everything hits cell 0".
            const bool b5 = d.proc().sequence().tracks[0].steps[5].trig;
            d.clickStep(5);
            check(d.proc().sequence().tracks[0].steps[5].trig != b5,
                  juce::String("clicking step 5's cell toggles step 5") + at);
        }
    }

    // A latched scope must reach the mouse path too (the mirror of the keyboard's
    // LATCH IMPLIES HELD test -- here it is KeyboardArea::mouseDown's own OR).
    void testLatchedMouseSelectsTrack(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthMouse/latch] %s\n", what); ++failed; }
        };

        UiDriver d;
        setScale(d, 1.2);
        d.doubleTap(CB::TrackScope);
        check(d.ui().latch.track, "Track latched");

        const bool trigBefore = d.proc().sequence().tracks[0].steps[5].trig;
        d.clickStep(5);
        check(d.activeTrack() == 5, "latched Track + click on step-5's cell selects track 5");
        check(d.proc().sequence().tracks[0].steps[5].trig == trigBefore,
              "latched Track + click places no trig");
    }

    // The editor's OWN mouse handler (not a child's): masterDrag maps the physical
    // point back through /uiScale_ by hand. That divide is the single place the
    // scaling is done manually, so it is the single place it can be forgotten.
    void testMasterMeterDragAtScale(int& failed)
    {
        auto check = [&failed](bool ok, const juce::String& what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthMouse/meter] %s\n", what.toRawUTF8()); ++failed; }
        };

        for (const double scale : { 1.0, 1.2 })
        {
            const juce::String at = juce::String(" (scale ") + juce::String(scale, 2) + ")";
            UiDriver d;
            setScale(d, scale);

            const auto meter = DispatchProbe::meter(d.editor());   // design space
            check(!meter.isEmpty(), juce::String("the master meter has real estate") + at);

            // Press only: the drag is armed on down and disarmed on up.
            d.pressPhysical(d.toPhysical(meter.getCentre().toFloat()));
            check(DispatchProbe::masterDragActive(d.editor()),
                  juce::String("pressing the meter's centre arms a master drag") + at);

            // And a press well outside it must not -- otherwise "contains" is passing
            // for the wrong reason.
            UiDriver off;
            setScale(off, scale);
            off.pressPhysical(off.toPhysical(juce::Point<float>{ 10.0f, 10.0f }));
            check(!DispatchProbe::masterDragActive(off.editor()),
                  juce::String("pressing far from the meter arms nothing") + at);
        }
    }

    // Resizing re-derives the scale and re-stamps every child's transform. A child
    // added or re-laid-out on a later resized() that misses applyChildScale() would
    // be correct at first paint and wrong forever after -- so the sequence matters,
    // not just the endpoints.
    void testResizeReseatsHitTesting(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthMouse/resize] %s\n", what); ++failed; }
        };

        UiDriver d;
        for (const double scale : { 1.0, 1.2, 1.0, 1.4 })
        {
            setScale(d, scale);
            const int stepIdx = 3;
            const bool before = d.proc().sequence().tracks[0].steps[stepIdx].trig;
            d.clickStep(stepIdx);
            check(d.proc().sequence().tracks[0].steps[stepIdx].trig != before,
                  "after a resize, the cell still hits where it is drawn");
        }
    }

    // The canary: prove this test can fail. At 1.2x, clicking the UNSCALED design
    // point is the exact bug class the harness exists to catch (someone forgets the
    // scale) -- it must land somewhere else, or these tests are decoration.
    void testUnscaledClickMissesTheCell(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthMouse/canary] %s\n", what); ++failed; }
        };

        UiDriver d;
        setScale(d, 1.2);

        auto& kb = DispatchProbe::keyboard(d.editor());
        const auto localCentre = DispatchProbe::centerOfStep(d.editor(), 15);
        check(localCentre.x >= 0.0f, "step 15's cell is on screen");
        const auto designPt = localCentre + kb.getBounds().getTopLeft().toFloat();

        // Scaled: hits the cell.
        const bool before = d.proc().sequence().tracks[0].steps[15].trig;
        d.clickPhysical(d.toPhysical(designPt));
        const bool afterScaled = d.proc().sequence().tracks[0].steps[15].trig;
        check(afterScaled != before, "the SCALED click hits step 15");

        // Unscaled at 1.2x: the same design point is 20% short of where the cell is
        // drawn, so it must not hit step 15.
        UiDriver d2;
        setScale(d2, 1.2);
        const bool before2 = d2.proc().sequence().tracks[0].steps[15].trig;
        d2.clickPhysical(designPt);   // deliberately NOT toPhysical()
        check(d2.proc().sequence().tracks[0].steps[15].trig == before2,
              "the UNSCALED click misses step 15 -- so this test can fail");
    }
}   // namespace

void runSyntheticMouseTests(int& failed)
{
    testMouseMachineryWorksHeadless(failed);
    testStepCellHitAtEveryScale(failed);
    testLatchedMouseSelectsTrack(failed);
    testMasterMeterDragAtScale(failed);
    testResizeReseatsHitTesting(failed);
    testUnscaledClickMissesTheCell(failed);
}
}   // namespace lockstep
