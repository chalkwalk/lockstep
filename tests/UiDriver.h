#pragma once

// UiDriver -- write interaction tests the way a person describes using the thing.
//
//     UiDriver d;
//     d.doubleTap(CB::TrackScope);      // latch the Track scope
//     d.step(3);                        // press a step pad
//     CHECK(d.ui().activeTrack == 3, "latched Track + step selects the track");
//
// WHY. The dispatch golden proves each binding-table row does what it did
// yesterday, but it speaks in ControllerEvents and state digests. The questions
// that break in practice are human-shaped -- "does a double-tap latch?", "does
// the click land on the cell it drew?" -- and answering them used to require
// hand-rolling event pairs and sleeping through gesture windows. So most of them
// never got asked.
//
// TIME IS VIRTUAL. The driver freezes the editor's clock (WI-1's nowMs funnel)
// at a large epoch and advances it explicitly. A double-tap is "advance a third
// of the window"; a long-press is "advance past the threshold". Nothing sleeps,
// nothing races, and a gesture-timing boundary can be asserted from BOTH sides
// (kDoubleTapMs-10 latches, +10 does not) -- which is the interesting test and is
// simply not writable against a wall clock.
//
// TWO TIERS, ON PURPOSE.
//   Tier 1 (press/tap/doubleTap/longPress/chord/step) enters at the
//     ControllerEvent layer via DispatchProbe, exactly where a MIDI controller
//     enters. Fast, and independent of which key or pixel happens to be bound.
//   Tier 2 (keyDown/keyUp/keyTap, clickDesign/clickPhysical/clickStep) drives the
//     REAL scancode and mouse paths -- the layers Tier 1 skips, where the QWERTY
//     map and the UI-scale transform live. Slower and fussier, so it is reserved
//     for what only it can see.
// If a Tier 1 and a Tier 2 gesture disagree, that is a finding, not a harness
// bug: it means the keyboard and the mouse do different things (see WI-6).
//
// LIFETIME / THREADING. One UiDriver at a time, on one thread. The synthetic-key
// oracle it installs into PressTracker is process-global (the OS key-state query
// it replaces is), so two live drivers would answer each other's key questions.
// The ctor installs it and the dtor restores the real one.

#include "EditorRig.h"

#include "../src/io/ControllerEvent.h"
#include "../src/ui/mode/GestureRecognizer.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <initializer_list>
#include <iterator>
#include <set>

namespace lockstep::test
{
    using CB = ControllerButton;
    using CE = ControllerEvent;

    class UiDriver
    {
    public:
        UiDriver()
        {
            sSyntheticKeys.clear();
            DispatchProbe::press(editor()).setKeyDownFn(&syntheticKeyOracle);
            freeze(rig_);

            // Component::getComponentAt is gated on visibleFlag, and a bare
            // heap-constructed editor has never been shown -- in the product the host
            // window does this. Without it every click hit-tests to nothing and
            // silently falls through to the editor, which looks exactly like a
            // routing bug. (The spike in SyntheticMouseTest caught this; it is why
            // the spike exists.)
            editor().setVisible(true);
        }

        ~UiDriver()
        {
            // Restore the OS oracle: the seam is process-global, and a stale
            // synthetic answer outliving its driver would be a very confusing bug
            // to chase from the other side.
            DispatchProbe::press(editor()).setKeyDownFn(nullptr);
            sSyntheticKeys.clear();
        }

        UiDriver(const UiDriver&) = delete;
        UiDriver& operator=(const UiDriver&) = delete;

        // -- time ---------------------------------------------------------------
        UiDriver& advanceMs(double ms)
        {
            DispatchProbe::advance(editor(), ms);
            return *this;
        }
        // Past the double-tap window: the "these are two deliberate presses, not a
        // double-tap" spacer. Same interval the golden's scripts use.
        UiDriver& gap() { return advanceMs(GestureRecognizer::kDoubleTapMs + 60.0); }

        [[nodiscard]] double now() const { return DispatchProbe::now(*rig_.editor); }

        // -- Tier 1: semantic gestures (ControllerEvent layer) --------------------
        // Each event nudges the clock 1 ms: real presses are never simultaneous, and
        // a zero-duration press is a state a human cannot produce.
        UiDriver& press(CB b, int idx = -1)
        {
            (void) DispatchProbe::down(editor(), CE{ CE::Type::ButtonDown, b, idx, 0 });
            return advanceMs(1.0);
        }
        UiDriver& release(CB b, int idx = -1)
        {
            DispatchProbe::up(editor(), CE{ CE::Type::ButtonUp, b, idx, 0 });
            return advanceMs(1.0);
        }
        UiDriver& tap(CB b, int idx = -1) { return press(b, idx).release(b, idx); }

        // Two taps inside the window. A third of the window apart: comfortably
        // inside it, and far enough from the boundary that the boundary tests
        // (which set their own spacing) stay the only place the edge is asserted.
        UiDriver& doubleTap(CB b, int idx = -1)
        {
            tap(b, idx);
            advanceMs(GestureRecognizer::kDoubleTapMs / 3.0);
            return tap(b, idx);
        }

        // Held past the long-press threshold, then released. The editor decides a
        // long-press has elapsed on its timer, so tick it while time is advanced --
        // otherwise the hold is invisible to everything but the release.
        UiDriver& longPress(CB b, int idx = -1)
        {
            press(b, idx);
            advanceMs(GestureRecognizer::kLongPressMs + 60.0);
            editor().timerCallback();
            return release(b, idx);
        }

        // mods held for the duration of the key press, released in reverse order --
        // the order a hand actually lets go of a chord.
        UiDriver& chord(std::initializer_list<CB> mods, CB key, int idx = -1)
        {
            for (CB m : mods)
                press(m);
            tap(key, idx);
            for (auto it = std::rbegin(mods); it != std::rend(mods); ++it)
                release(*it);
            return *this;
        }

        UiDriver& step(int n) { return tap(CB::Step, n); }

        // -- Tier 2: the real QWERTY path ----------------------------------------
        // keyPressed() is where the scancode becomes a ControllerEvent (via
        // QwertyOverlay::resolve) -- the layer Tier 1 enters below. Everything the
        // key map, the Func/Track/Mute held-flag plumbing and the repeat suppression
        // do lives HERE and nowhere else, so it is only ever exercised this way.
        //
        // JUCE has no key-up callback: releases are found by keyStateChanged asking
        // the OS which tracked keys are still down. sSyntheticKeys is that answer
        // for keys the OS never saw.
        UiDriver& keyDown(int keyCode)
        {
            sSyntheticKeys.insert(keyCode);
            editor().keyPressed(juce::KeyPress(keyCode, juce::ModifierKeys(), 0), nullptr);
            return advanceMs(1.0);
        }
        UiDriver& keyUp(int keyCode)
        {
            sSyntheticKeys.erase(keyCode);
            editor().keyStateChanged(false, nullptr);   // the editor diffs and synthesizes the up
            return advanceMs(1.0);
        }
        UiDriver& keyTap(int keyCode) { return keyDown(keyCode).keyUp(keyCode); }

        // -- Tier 2: the real mouse path -----------------------------------------
        // Routed through Component::getComponentAt + getLocalPoint, which is what
        // makes this worth doing: BOTH are transform-aware, so the click is resolved
        // against the same AffineTransform stack the paint path draws through. Aiming
        // a hand-computed local coordinate at a child's mouseDown would test nothing
        // -- it would bypass precisely the mapping that can be wrong.
        //
        // Limits, deliberately: no event bubbling to parents, no MouseListener
        // fan-out, no drag synthesis. This delivers to the component JUCE says is
        // under the point, which is the question being asked.
        // Press and release are SEPARATE, because plenty of state lives only between
        // them: a master-VU drag is armed on down and disarmed on up, so a
        // press-and-release helper can never observe it. (Learned the hard way -- the
        // meter test failed against a perfectly working editor.)
        UiDriver& pressPhysical(juce::Point<float> physical,
                                juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
        {
            sendMouse(true, physical, mods);
            return advanceMs(1.0);
        }
        UiDriver& releasePhysical(juce::Point<float> physical,
                                  juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
        {
            sendMouse(false, physical, mods);
            return advanceMs(1.0);
        }
        UiDriver& clickPhysical(juce::Point<float> physical,
                                juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
        {
            return pressPhysical(physical, mods).releasePhysical(physical, mods);
        }

        // A point in DESIGN space (what the layout code and the eye use), converted
        // to the physical pixel it currently occupies.
        [[nodiscard]] juce::Point<float> toPhysical(juce::Point<float> design) const
        {
            return design * static_cast<float>(DispatchProbe::uiScale(*rig_.editor));
        }
        UiDriver& clickDesign(juce::Point<float> design) { return clickPhysical(toPhysical(design)); }

        // Click step cell absIdx where it is actually drawn, at whatever scale.
        UiDriver& clickStep(int absIdx)
        {
            const auto localCentre = DispatchProbe::centerOfStep(editor(), absIdx);
            jassert(localCentre.x >= 0.0f);   // cell not on screen: wrong page/mode?
            auto& kb = DispatchProbe::keyboard(editor());
            // KeyboardArea-local design point -> editor design space -> physical.
            const auto designPt = localCentre + kb.getBounds().getTopLeft().toFloat();
            return clickPhysical(toPhysical(designPt));
        }

        // -- observation ---------------------------------------------------------
        [[nodiscard]] const UiState& ui() const { return DispatchProbe::ui(*rig_.editor); }
        [[nodiscard]] int activeTrack() const { return DispatchProbe::activeTrack(*rig_.editor); }
        [[nodiscard]] LockstepProcessor& proc() { return *rig_.proc; }
        [[nodiscard]] LockstepEditor& editor() { return *rig_.editor; }
        [[nodiscard]] const LockstepEditor& editor() const { return *rig_.editor; }
        [[nodiscard]] Rig& rig() { return rig_; }

    private:
        // The OS key-state answer for synthetic keys. Static because the oracle is a
        // plain function pointer (PressTracker stays allocation-free) -- which is why
        // only one driver may be live at a time; see the header note.
        inline static std::set<int> sSyntheticKeys;

        static bool syntheticKeyOracle(int rawCode)
        {
            return sSyntheticKeys.count(rawCode) > 0;
        }

        void sendMouse(bool down, juce::Point<float> physical, juce::ModifierKeys mods)
        {
            auto* target = editor().getComponentAt(physical.roundToInt());
            if (target == nullptr)
                target = &editor();

            const auto local = target->getLocalPoint(&editor(), physical);
            auto srcRef = juce::Desktop::getInstance().getMainMouseSource();   // returns by value
            const auto t = juce::Time::getCurrentTime();
            const juce::MouseEvent ev(srcRef, local, mods,
                                      juce::MouseInputSource::defaultPressure,
                                      juce::MouseInputSource::defaultOrientation,
                                      juce::MouseInputSource::defaultRotation,
                                      0.0f, 0.0f, target, target, t, local, t, 1, false);
            if (down)
                target->mouseDown(ev);
            else
                target->mouseUp(ev);
        }

        Rig rig_;
    };
}   // namespace lockstep::test
