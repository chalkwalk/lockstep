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

#include <initializer_list>
#include <iterator>

namespace lockstep::test
{
    using CB = ControllerButton;
    using CE = ControllerEvent;

    class UiDriver
    {
    public:
        UiDriver()
        {
            freeze(rig_);
        }

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

        // -- observation ---------------------------------------------------------
        [[nodiscard]] const UiState& ui() const { return DispatchProbe::ui(*rig_.editor); }
        [[nodiscard]] int activeTrack() const { return DispatchProbe::activeTrack(*rig_.editor); }
        [[nodiscard]] LockstepProcessor& proc() { return *rig_.proc; }
        [[nodiscard]] LockstepEditor& editor() { return *rig_.editor; }
        [[nodiscard]] const LockstepEditor& editor() const { return *rig_.editor; }
        [[nodiscard]] Rig& rig() { return rig_; }

    private:
        Rig rig_;
    };
}   // namespace lockstep::test
