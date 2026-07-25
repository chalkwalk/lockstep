// CujSoundTest -- Group C: the sound. Which machine a track runs, and where a
// parameter edit lands.
//
// C1 walks the ONE resolution rule the whole instrument rests on: Override-ELSE-Base.
// With a step held, a knob writes that step; with none, it writes the track. Nothing
// else in the product decides a value. MzDriveTest proves the fork at the pixel level
// (a real drag, in either scope); this journey is the user-shaped version -- pick a
// section, hold a step, turn a knob, see the lock appear on the surface, clear it
// again -- and it is the only test that covers the CLEAR half of the gesture.
//
// C3 loads a different machine through the picker and then asks the question the
// picker's own tests never did: is the new machine's SRC page REACHABLE? That is the
// numSections() trap (CLAUDE.md) -- numSections() is highestSectionIndex + 1, not a
// count, so a machine whose only params sit at kSrcSecIdx while returning 1 makes
// `1 < 1` false and its entire source panel unreachable. It shipped that way once.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // Slot 4 of the FM schema = "fm_fine_1": continuous and centred, so a write has
    // room to move and is not quantised onto a coarse enum (MzDriveTest's reasoning).
    constexpr int kFineSlot = 4;

    float baseParamOf(UiDriver& d, int track, int slot)
    {
        return d.proc().sequence().tracks[static_cast<std::size_t>(track)]
                   .baseParams[static_cast<std::size_t>(slot)];
    }

    // Bring the MZ up to date with the machine just installed: slider ranges come from
    // the ParamSpec via the surface frame.
    void settle(UiDriver& d)
    {
        DispatchProbe::frame(d.editor());
    }

    // A base param write is applied on the AUDIO thread (drainEngineCmds at the top of
    // processBlock), so engine state must be asked for AFTER a block has run.
    void runBlock(UiDriver& d)
    {
        const int chans = juce::jmax(2, d.proc().getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(chans, 512);
        buf.clear();
        juce::MidiBuffer midi;
        d.proc().processBlock(buf, midi);
    }

    // C1 -- P-Lock one step.
    void testPLockOneStep(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/C1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        settle(d);

        // Open the SRC page, which is where a sound edit starts for a person.
        d.tap(CB::Section, IMachine::kSrcSecIdx);
        settle(d);

        const int slot = DispatchProbe::mzSlotOffset(d.editor()) + kFineSlot;
        const auto& step0 = d.proc().sequence().tracks[0].steps[0];
        if (!test::expectReached(d, [&](UiDriver&) { return !step0.overrides.has(slot); },
                                 "step 0 starts with no override on the slot", failed))
            return;

        const float baseBefore = baseParamOf(d, 0, slot);

        // Hold the step, turn the knob. Same verb as a base edit -- the held step is
        // the entire difference.
        d.press(CB::Step, 0);
        d.setParam(slot, baseBefore + 20.0f);
        runBlock(d);

        check(step0.overrides.has(slot), "a param write under a held step lands as a P-Lock");
        check(std::abs(baseParamOf(d, 0, slot) - baseBefore) < 1.0e-6f,
              "...and the track's base value is untouched (Override-ELSE-Base)");

        // The lock is visible where the value is: the MZ slot reports the override, so
        // a controller's ring can colour it without asking the sequencer anything.
        {
            const auto surf = d.surface();
            const int zone = slot - DispatchProbe::mzSlotOffset(d.editor());
            check(zone >= 0 && zone < 8 && surf.slots[static_cast<std::size_t>(zone)].hasOverride,
                  "the MZ slot shows the step's override");
        }

        // Clear it: held step + CLEAR wipes that step whole -- trig, condition and every
        // P-Lock on it. (The catalogue said Func+O here; Func+O under a held step is the
        // slot-picker P-Lock clear mode, and pressing Func mid-hold also LATCHES the
        // step, so that gesture belongs to C6 with the rest of the clear family.)
        d.tap(CB::VerbClear);
        runBlock(d);
        check(!step0.overrides.has(slot), "held step + CLEAR wipes the step's P-Locks");
        d.release(CB::Step, 0);

        // With the step released the same write is a base edit again -- the scope must
        // not leak past the hold. (settle() first: the slider is still showing the
        // override's value, and setValue to a value it already holds notifies nobody --
        // the write would silently never happen and the leak would look real.)
        settle(d);
        const float baseAfter = baseParamOf(d, 0, slot);
        d.setParam(slot, baseAfter + 35.0f);
        runBlock(d);
        check(baseParamOf(d, 0, slot) > baseAfter,
              "releasing the step returns the knob to editing the track base");
    }

    // C3 -- Machine picker.
    void testMachinePicker(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/C3] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        settle(d);

        const std::string before = d.proc().getMachineIdRaw(0);

        // Track + hold(SRC) opens the picker (a bare hold(SRC) is the machine console --
        // the scope gate is what keeps the two apart).
        d.press(CB::TrackScope);
        d.longPress(CB::Section, IMachine::kSrcSecIdx);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().machinePickerOpen; },
                                 "Track + hold(SRC) opens the machine picker", failed))
        {
            d.release(CB::TrackScope);
            return;
        }
        {
            const auto surf = d.surface();
            check(surf.activeLayer == SurfaceLayer::MachinePicker,
                  "the step grid re-skins to the picker layer");
        }

        // Pick a machine that is NOT the one already loaded. Cells are indexed into the
        // catalogue, and under a held Track the step keys route as SelectTrack.
        int pick = -1;
        for (int i = 0; i < d.proc().numAvailableMachines(); ++i)
            if (std::string(d.proc().availableMachineInfo(i).id) != before)
            {
                pick = i;
                break;
            }
        if (!test::expectReached(d, [&](UiDriver&) { return pick >= 0; },
                                 "the catalogue offers a second machine to pick", failed))
        {
            d.release(CB::TrackScope);
            return;
        }
        const std::string wanted = d.proc().availableMachineInfo(pick).id;

        d.tap(CB::SelectTrack, pick);
        d.release(CB::TrackScope);
        settle(d);

        check(std::string(d.proc().getMachineIdRaw(0)) == wanted,
              "picking a cell loads that machine on the focused track");
        check(!d.ui().machinePickerOpen, "the pick closes the picker -- choosing IS the verb");

        // The trap: the picked machine's SRC page must be REACHABLE. numSections() is
        // highestSectionIndex + 1, and a machine that gets that wrong has an SRC panel
        // the section key cannot open -- silently, because the key still presses.
        d.tap(CB::Section, IMachine::kSrcSecIdx);
        settle(d);
        {
            const auto sec = d.proc().section(0, IMachine::kSrcSecIdx);
            check(sec.firstSlot >= 0 && sec.pageCount > 0,
                  "the picked machine's SRC section reports slots (the numSections trap)");

            const auto surf = d.surface();
            check(!surf.section[static_cast<std::size_t>(IMachine::kSrcSecIdx)].disabled,
                  "the SRC key is live for the picked machine");
            check(surf.slots[0].inRange, "the SRC page fills the MZ with real params");
        }
    }
}   // namespace

void runCujSoundTests(int& failed)
{
    testPLockOneStep(failed);
    testMachinePicker(failed);
}
}   // namespace lockstep
