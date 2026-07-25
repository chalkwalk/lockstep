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

#include "../src/ui/MetaBand.h"
#include "../src/ui/SectionResolve.h"
#include "../src/ui/UITheme.h"

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
    // C4 -- Section paging and scope colour.
    //
    // Six section keys have to address more parameters than six pages can hold, on a
    // surface where the same key means different things depending on which scope is
    // held. Two rules carry that: a section key re-pressed CYCLES its pages, and the
    // page you land on is COLOURED by the scope layer that won it -- neutral for the
    // machine's own params, the scope's colour when a held scope re-skins the row.
    void testSectionPagingAndScopeColour(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/C4] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        settle(d);

        // --- A machine page is the machine's own, and reads neutral ---------------
        d.tap(CB::Section, IMachine::kSrcSecIdx);
        settle(d);
        check(DispatchProbe::mzPageOrigin(d.editor()) == SecOrigin::Machine,
              "a bare section key opens the machine's own page");
        const int firstPage = DispatchProbe::mzSlotOffset(d.editor());

        // --- Re-pressing the same key cycles its pages ----------------------------
        // FM's SRC section is deep enough to have more than one page; if it were not,
        // the offset would legitimately stay put, so the precondition says so first.
        const auto sec = d.proc().section(0, IMachine::kSrcSecIdx);
        if (!test::expectReached(d, [&](UiDriver&) { return sec.pageCount > 1; },
                                 "the section has more than one page to cycle", failed))
            return;
        d.gap();
        d.tap(CB::Section, IMachine::kSrcSecIdx);
        settle(d);
        check(DispatchProbe::mzSlotOffset(d.editor()) != firstPage,
              "re-pressing the section key pages it");

        // --- Under a held Track the row re-skins, and the colour says so ----------
        d.gap();
        d.press(CB::TrackScope);
        d.tap(CB::Section, IMachine::kTrigSecIdx);   // Track+TRIG = the track's own page
        settle(d);
        // The same key, under a scope, is not a deeper page of the machine -- it is a
        // different LAYER: Track+TRIG is the track's own divider/length band, which is
        // why the MZ answers with a meta band rather than a machine page.
        check(resolveMetaBand(d.ui()) == MetaBand::Divider,
              "a held Track re-points TRIG at the track's own layer");
        {
            const auto surf = d.surface();
            const auto& cell = surf.section[static_cast<std::size_t>(IMachine::kTrigSecIdx)];
            check(cell.scopeTint == theme::kScopeTrack,
                  "...and the key wears the Track scope's colour while it is held");
        }
        d.release(CB::TrackScope);
    }

    // C5 -- Control-All.
    //
    // Hold Track without picking one and the instrument stops addressing a track at
    // all: the next knob move goes to EVERY track that has the same control. It is how
    // a filter sweep happens across a whole kit with one hand. The matching rule is by
    // slot ID, not position, so a track running a different machine is left alone
    // unless it genuinely has that parameter -- and selecting a track ends the mode.
    void testControlAll(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/C5] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        installRealMachine(d.rig(), 1);   // same machine: the ids match
        settle(d);

        // --- Arm Control-All, then turn the knob ----------------------------------
        // Track must stay DOWN (releasing it ends the mode), and the section key in
        // the middle is not optional: a bare Track hold puts the SWING band under the
        // knobs, and any non-scope press dismisses it back to the machine params. So
        // the real gesture is hold Track -> press a section -> sweep.
        d.gap();
        d.press(CB::TrackScope);
        check(d.proc().controlAllActive(), "holding Track with no pick arms Control-All");
        d.tap(CB::Section, IMachine::kSrcSecIdx);
        settle(d);

        // Read the target slot AFTER the page settles: the section press pages the MZ,
        // and a slot that is no longer on the visible page is a write that silently
        // does not happen.
        const int slot = DispatchProbe::mzSlotOffset(d.editor()) + kFineSlot;
        const float before0 = baseParamOf(d, 0, slot);
        const float before1 = baseParamOf(d, 1, slot);
        if (!test::expectReached(d, [&](UiDriver&) { return before0 == before1; },
                                 "both tracks start this control from the same value", failed))
        {
            d.release(CB::TrackScope);
            return;
        }

        d.setParam(slot, before0 + 20.0f);
        runBlock(d);
        d.release(CB::TrackScope);

        check(baseParamOf(d, 0, slot) > before0, "the focused track moves");
        check(baseParamOf(d, 1, slot) > before1, "...and so does every track sharing that control");

        // --- Picking a track ends it ----------------------------------------------
        d.gap();
        d.press(CB::TrackScope);
        d.tap(CB::SelectTrack, 0);
        d.release(CB::TrackScope);
        settle(d);
        check(!d.proc().controlAllActive(), "choosing a track ends Control-All");

        const float mid1 = baseParamOf(d, 1, slot);
        d.setParam(slot, baseParamOf(d, 0, slot) + 15.0f);
        runBlock(d);
        check(std::abs(baseParamOf(d, 1, slot) - mid1) < 1.0e-6f,
              "...and the next write lands on the chosen track alone");
    }
}   // namespace

void runCujSoundTests(int& failed)
{
    testPLockOneStep(failed);
    testMachinePicker(failed);
    testSectionPagingAndScopeColour(failed);
    testControlAll(failed);
}
}   // namespace lockstep
