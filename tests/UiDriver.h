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

#include "AudioRig.h"
#include "EditorRig.h"

#include "../src/io/ControllerEvent.h"
#include "../src/ui/mode/GestureRecognizer.h"

#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include <initializer_list>
#include <iterator>
#include <memory>
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

        // Press, move, release -- the gesture the MZ's rotaries only respond to.
        //
        // Every drag event goes to the component that was PRESSED, not to whatever is
        // under the moving pointer. That is JUCE's real semantic (a drag belongs to its
        // mouseDown), and it is not a detail: a rotary drag leaves the knob's own bounds
        // almost immediately, so re-resolving the target per step would deliver the
        // interesting half of the gesture to whatever it happened to slide over.
        //
        // The intermediate steps matter too. A down-then-up with no drag between is a
        // click, and a rotary reads it as a value of zero movement -- the state under
        // test would never change and the test would look like a product bug.
        UiDriver& dragPhysical(juce::Point<float> from, juce::Point<float> to, int steps = 8)
        {
            auto* target = targetAt(from);
            deliver(target, MouseKind::Down, from, from, juce::ModifierKeys::leftButtonModifier);
            advanceMs(1.0);

            for (int i = 1; i <= steps; ++i)
            {
                const auto t = static_cast<float>(i) / static_cast<float>(steps);
                deliver(target, MouseKind::Drag, from + (to - from) * t, from,
                        juce::ModifierKeys::leftButtonModifier);
                advanceMs(1.0);
            }

            deliver(target, MouseKind::Up, to, from, juce::ModifierKeys::leftButtonModifier);
            return advanceMs(1.0);
        }

        // Drag one of the MZ's eight rotaries vertically, by a distance in DESIGN
        // pixels. Negative = upward = increase (JUCE's RotaryHorizontalVerticalDrag
        // reads dx + -dy).
        //
        // Why a real drag and not setValue: the P-Lock capture arms in the slider's
        // onDragStart (ManipulationZone.cpp:78), which setValue does not fire. A test
        // that wrote the value directly would report a working param edit and would be
        // blind to the entire held-step routing -- the half most likely to break.
        UiDriver& dragMZSlider(int slot, float dyDesignPx, int steps = 8)
        {
            const auto knob = DispatchProbe::mzSliderBounds(editor(), slot).toFloat();
            jassert(!knob.isEmpty());   // slot not laid out: wrong band, or MZ never sized?

            const auto centre = knob.getCentre() + DispatchProbe::mz(editor()).getTopLeft().toFloat();
            return dragPhysical(toPhysical(centre), toPhysical(centre + juce::Point<float>{ 0.0f, dyDesignPx }),
                                steps);
        }

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

        // -- live audio (the bridge) ---------------------------------------------
        // Attach a real playhead to the EDITOR's processor and pump processBlock, so
        // a gesture and its audible consequence share one timeline. This is what lets
        // a journey assert on engine state that only changes on the audio thread
        // (writeParam enqueues an EngineCmd; drainEngineCmds() applies it at the top
        // of processBlock -- see the MzDriveTest note) AND on the sound itself.
        //
        // Lazy: most interaction tests never touch audio, and standing up AudioRig
        // re-prepares the processor at 48k/256, overriding the editor rig's 44.1k/512.
        // AudioRig is the single owner of that standup (shared with EngineHarness), so
        // the two rigs cannot drift on what "running" means.

        // Run N audio blocks, advancing the UI clock coherently with the playhead.
        UiDriver& runBlocks(int n)
        {
            auto& a = audioRig();
            const double blockMs =
                static_cast<double>(a.blockSize()) / AudioRig::kSampleRate * 1000.0;
            for (int i = 0; i < n; ++i)
            {
                a.renderBlocks(1);
                advanceMs(blockMs);
            }
            return *this;
        }

        // Roll the transport for `beats` quarter-notes' worth of audio. Rounds up to
        // a whole block; at least one block always runs (a zero-length play is a
        // state a human cannot produce and a test never wants).
        UiDriver& play(double beats)
        {
            auto& a = audioRig();
            const int n = std::max(1, static_cast<int>(std::ceil(beats / a.playHead().ppqPerBlock())));
            return runBlocks(n);
        }

        // RMS of the last block: whole buffer (ch < 0) or one channel. Non-zero means
        // the gesture actually produced sound -- the liveness the bridge must prove.
        [[nodiscard]] float lastRms(int channel = -1)
        {
            auto& a = audioRig();
            return channel < 0 ? a.lastBufferRms() : a.lastChannelRms(channel);
        }
        [[nodiscard]] bool hasNaN() { return audioRig().lastBufferHasNaN(); }

        // Direct access to the bridge (playhead, midiOut, input injection) for the
        // journeys that need finer control than the verbs above.
        [[nodiscard]] AudioRig& audioRig()
        {
            if (audio_ == nullptr)
                audio_ = std::make_unique<AudioRig>(proc());
            return *audio_;
        }

        // -- MIDI note input (E2) ------------------------------------------------
        // Inject a note into the MidiBuffer handed to the NEXT processBlock -- the
        // io/MidiInput ingestion seam a controller or the host feeds. In the default
        // Omni channel mode a note routes to the processor's focus track, so set that
        // (setFocusTrack / a track-select gesture) before playing in. This is what
        // realtime record, the step-hold chord-capture window, and MIDI-out journeys
        // consume. Queued, not immediate: it arrives when the transport next rolls.
        UiDriver& noteOn(int pitch, int vel = 100)
        {
            juce::MidiBuffer b;
            b.addEvent(juce::MidiMessage::noteOn(1, pitch, static_cast<juce::uint8>(vel)), 0);
            audioRig().injectInput(b);
            return *this;
        }
        UiDriver& noteOff(int pitch)
        {
            juce::MidiBuffer b;
            b.addEvent(juce::MidiMessage::noteOff(1, pitch), 0);
            audioRig().injectInput(b);
            return *this;
        }
        // Hold `pitch` for `beats` of rolling transport, then release: the note-on
        // lands on the first block, the note-off after the roll. One extra block
        // flushes the off so its gate/off-handling completes.
        UiDriver& playNote(int pitch, int vel, double beats)
        {
            noteOn(pitch, vel);
            play(beats);
            noteOff(pitch);
            return runBlocks(1);
        }

        // -- MIDI output (E3) ----------------------------------------------------
        // What the processor emitted on the last block -- MIDI-out-track note-ons/CCs,
        // for the journeys that assert on emitted MIDI rather than audio.
        [[nodiscard]] const juce::MidiBuffer& midiOut() { return audioRig().midiOut(); }

        // -- observation ---------------------------------------------------------
        // What the live editor would paint right now -- so a test can assert on the
        // surface a gesture produces, not merely on the state behind it.
        [[nodiscard]] SurfaceModel surface() { return DispatchProbe::surface(editor()); }

        // Tier 3: the actual pixels. surface() says what the editor MEANS to draw;
        // this is what it draws. The gap between the two is where a lost transform, a
        // paint/hit-test drift or a colour regression lives -- all invisible to every
        // other verb here.
        [[nodiscard]] juce::Image render() { return DispatchProbe::render(editor()); }

        // Render at a specific window size. setSize drives resized(), which is where
        // the editor re-derives uiScale_ and re-stamps every child's transform -- so
        // this is how a test asks "does it still hold together at 1.2x?".
        [[nodiscard]] juce::Image renderAt(int w, int h)
        {
            editor().setSize(w, h);
            return render();
        }
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

        enum class MouseKind { Down, Drag, Up };

        // Deliver one event to a KNOWN target. Down/Up resolve their own target from
        // the point; a drag must not -- see dragPhysical.
        void deliver(juce::Component* target, MouseKind kind,
                     juce::Point<float> physical, juce::Point<float> physicalDown,
                     juce::ModifierKeys mods)
        {
            const auto local = target->getLocalPoint(&editor(), physical);
            const auto localDown = target->getLocalPoint(&editor(), physicalDown);
            auto srcRef = juce::Desktop::getInstance().getMainMouseSource();   // returns by value
            const auto t = juce::Time::getCurrentTime();
            const juce::MouseEvent ev(srcRef, local, mods,
                                      juce::MouseInputSource::defaultPressure,
                                      juce::MouseInputSource::defaultOrientation,
                                      juce::MouseInputSource::defaultRotation,
                                      0.0f, 0.0f, target, target, t, localDown, t, 1,
                                      kind != MouseKind::Down);
            switch (kind)
            {
                case MouseKind::Down: target->mouseDown(ev); break;
                case MouseKind::Drag: target->mouseDrag(ev); break;
                case MouseKind::Up:   target->mouseUp(ev);   break;
            }
        }

        [[nodiscard]] juce::Component* targetAt(juce::Point<float> physical)
        {
            auto* target = editor().getComponentAt(physical.roundToInt());
            return target != nullptr ? target : &editor();
        }

        void sendMouse(bool down, juce::Point<float> physical, juce::ModifierKeys mods)
        {
            deliver(targetAt(physical), down ? MouseKind::Down : MouseKind::Up,
                    physical, physical, mods);
        }

        Rig rig_;
        // Declared AFTER rig_ so it is destroyed BEFORE it: AudioRig holds a
        // reference to rig_'s processor and releases resources in its dtor.
        std::unique_ptr<AudioRig> audio_;
    };
}   // namespace lockstep::test
