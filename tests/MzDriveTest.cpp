// MzDriveTest -- the eight rotaries, driven the way a hand drives them.
//
// The Manipulation Zone is where nearly every value in the instrument is actually
// edited, and until now no test had ever touched one. The reason is instructive: you
// cannot test a rotary by calling setValue(). JUCE routes a real gesture through
// mouseDown -> onDragStart -> mouseDrag -> onValueChange, and the MZ arms its P-Lock
// capture in onDragStart (ManipulationZone.cpp:78) -- the step the direct write skips.
// So a setValue()-based test would report a perfectly working param edit while being
// structurally blind to the half most likely to break: WHERE the value lands.
//
// That "where" is the whole point of the scope grammar. The same drag writes the
// track's base value, or a P-Lock on the held step, depending on nothing but whether a
// step key is down. The two tests below are that fork, and per the project rule that
// every new input modality ships with resolution + scope-routing coverage, they are
// what makes the drag verb legitimate rather than merely present.
//
// The rig gets a real machine first (installRealMachine): the default StubMachine has
// no params at all, so every assertion here would be vacuous on it -- passing, and
// worthless.

#include "UiDriver.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::UiDriver;

    // Slot 4 of the FM schema = "fm_fine_1" (Op1 Fine): -100..100, default 0. Chosen
    // deliberately -- it is continuous and centred, so a drag has room to move in either
    // direction and the result is not quantised onto a coarse enum the way slots 0-3
    // (the stepped ratio params) are.
    constexpr int kFineSlot = 4;

    float baseParamOf(UiDriver& d, int track, int slot)
    {
        return d.proc().sequence().tracks[static_cast<std::size_t>(track)]
                   .baseParams[static_cast<std::size_t>(slot)];
    }

    // Bring the MZ up to date with the machine just installed: the sliders' ranges come
    // from the ParamSpec via the surface frame, and dragging a slider whose range is
    // still the default would write meaningless numbers.
    void settle(UiDriver& d)
    {
        DispatchProbe::frame(d.editor());
    }

    // Run one audio block, because a base param write does not take effect until one
    // does. writeParam enqueues an EngineCmd and the AUDIO thread applies it at the top
    // of processBlock (drainEngineCmds) -- so with no engine running, the drag is real,
    // the slider moves, and baseParams never changes.
    //
    // That cost an hour and is worth writing down: the first version of this test
    // failed, and every visible symptom pointed at the new drag verb not working. It
    // was working the whole time. If a UI test asserts on engine-side state, it has to
    // let the engine run.
    void runBlock(UiDriver& d)
    {
        const int chans = juce::jmax(2, d.proc().getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(chans, 512);
        buf.clear();
        juce::MidiBuffer midi;
        d.proc().processBlock(buf, midi);
    }

    void testDragWritesTheBaseParam(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [MzDrive/base] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig());
        settle(d);

        const int slot = DispatchProbe::mzSlotOffset(d.editor()) + kFineSlot;
        const float before = baseParamOf(d, 0, slot);

        // Up = increase (RotaryHorizontalVerticalDrag reads dx + -dy). Asserting the
        // DIRECTION and not a value on purpose: the exact number depends on JUCE's
        // pixels-for-full-drag, which is a look-and-feel decision, not a promise the
        // instrument makes.
        d.dragMZSlider(kFineSlot, -60.0f);
        runBlock(d);
        const float afterUp = baseParamOf(d, 0, slot);
        check(afterUp > before, "dragging a rotary up raises the track's base param");

        d.dragMZSlider(kFineSlot, 60.0f);
        runBlock(d);
        check(baseParamOf(d, 0, slot) < afterUp, "and dragging it back down lowers it");
    }

    void testDragUnderAHeldStepWritesAPLock(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [MzDrive/plock] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig());
        settle(d);

        const int slot = DispatchProbe::mzSlotOffset(d.editor()) + kFineSlot;
        const auto& step0 = d.proc().sequence().tracks[0].steps[0];

        check(!step0.overrides.has(slot), "baseline: step 0 carries no override for the slot");
        const float baseBefore = baseParamOf(d, 0, slot);

        // 'D' is step 0's key. Holding it makes the EditContext active, which is the
        // entire difference between the two tests -- same drag, same slot, other scope.
        d.keyDown('D');
        d.dragMZSlider(kFineSlot, -60.0f);
        runBlock(d);

        check(step0.overrides.has(slot), "a drag under a held step P-Locks that step");
        check(std::abs(baseParamOf(d, 0, slot) - baseBefore) < 1.0e-6f,
              "...and leaves the track's base value alone (an override is not a base write)");

        d.keyUp('D');

        // Releasing the step must end the capture: the next drag is a base edit again.
        // Without this the P-Lock scope would leak into every later gesture, which is
        // the kind of bug that only shows up three actions later and gets blamed on
        // something else.
        const float baseAfterRelease = baseParamOf(d, 0, slot);
        d.dragMZSlider(kFineSlot, -60.0f);
        runBlock(d);
        check(baseParamOf(d, 0, slot) > baseAfterRelease,
              "releasing the step returns the rotary to editing the base value");
    }

    void testRightClickDoesNotEditTheValue(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [MzDrive/button] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig());
        settle(d);

        const int slot = DispatchProbe::mzSlotOffset(d.editor()) + kFineSlot;
        const float before = baseParamOf(d, 0, slot);

        // Right-click is the CC-learn menu (ManipulationZone.cpp:412), not a value
        // gesture. Asserted so the drag verb's own contract is clear: it is the LEFT
        // button that edits. (The menu itself is modal and is not opened here -- this
        // is about the value, which must not move.)
        const auto knob = DispatchProbe::mzSliderBounds(d.editor(), kFineSlot).toFloat();
        const auto centre = knob.getCentre()
                          + DispatchProbe::mz(d.editor()).getTopLeft().toFloat();
        d.pressPhysical(d.toPhysical(centre), juce::ModifierKeys::rightButtonModifier);
        d.releasePhysical(d.toPhysical(centre), juce::ModifierKeys::rightButtonModifier);
        runBlock(d);

        check(std::abs(baseParamOf(d, 0, slot) - before) < 1.0e-6f,
              "a right-button press does not move the value");
    }
}   // namespace

void runMzDriveTests(int& failed)
{
    testDragWritesTheBaseParam(failed);
    testDragUnderAHeldStepWritesAPLock(failed);
    testRightClickDoesNotEditTheValue(failed);
}
}   // namespace lockstep
