// ToneConsoleTest — the two-press program picker (4.10, DESIGN §29.3).
//
// 128 General MIDI instruments, reachable in TWO presses with no paging:
// press one picks the family, press two picks the program. It works because
// GM's shape and the grid's shape agree exactly — 16 families fills the
// 16-cell page, and a family holds 8.
//
// It also needs NO new gesture, and that is the part worth guarding. Bare
// `hold(SRC)` already toggles a machine console (`ConsoleMode::OnDemand` plus
// the default `consoleSectionIndex()`), the same rail Route's routing matrix
// rides. `Track + hold(SRC)` still picks the MACHINE — the scope gate is what
// keeps the two apart, so this file drives both and checks they stay apart.

#include "UiDriver.h"

#include "../src/machine/ToneMachine.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    void check(int& failed, bool ok, const char* what)
    {
        if (!ok) { std::fprintf(stderr, "FAIL [ToneConsole] %s\n", what); ++failed; }
    }
}   // namespace

void runToneConsoleTests(int& failed)
{
    UiDriver d;
    d.proc().setTrackMachine(0, ToneMachine::kMachineId);
    d.tap(CB::SelectTrack, 0);

    if (!test::expectReached(d, [](UiDriver& dd) {
                                 return dd.proc().kit(0).machineId == ToneMachine::kMachineId;
                             },
                             "track 0 holds a Tone", failed))
        return;

    // --- Bare hold(SRC) opens the console; Track+hold(SRC) does NOT ----------
    // The scope gate is the whole reason this needed no new gesture, so assert
    // the gate rather than trusting it.
    d.gap();
    d.press(CB::TrackScope);
    d.longPress(CB::Section, IMachine::kSrcSecIdx);
    d.release(CB::TrackScope);
    check(failed, !d.ui().machineConsoleOpen,
          "Track + hold(SRC) still picks the MACHINE, not the program");
    d.doubleTap(CB::Func);   // drop whatever picker that opened

    d.gap();
    d.longPress(CB::Section, IMachine::kSrcSecIdx);
    if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().machineConsoleOpen; },
                             "bare hold(SRC) opens the Tone console", failed))
        return;
    check(failed, d.ui().toneConsoleFamily < 0,
          "...on the FAMILY page — two presses is the promise, so it never resumes mid-drill");

    // --- Press one: the family page shows all 16 families --------------------
    {
        const auto surf = d.surface();
        check(failed, surf.activeLayer == SurfaceLayer::MachineConsole,
              "the grid re-skins to the console");
        // Families are ROTATED BY 8: GM's conventional first eight sit on the
        // BOTTOM row, where a held step is strictly less likely to be, and the
        // specialised eight take the top. A static relabelling -- nothing moves
        // at runtime -- so there is still one arrangement to learn.
        check(failed, surf.step[8].primary == "Piano" && surf.step[12].primary == "Bass",
              "GM's first eight families sit on the BOTTOM row");
        check(failed, surf.step[0].primary == "Reed" && surf.step[7].primary == "SFX",
              "...and the specialised eight on the top row");
        // The family holding the current program is lit, so opening the console
        // shows you where you already are rather than a blank menu.
        check(failed, surf.step[8].base == CellState::SelectorCurrent,
              "the family holding the current program reads as current");
    }

    // --- Press two: a family's 8 programs ------------------------------------
    d.clickStep(13);         // cell 13 -> family 5 (Strings), rotated
    check(failed, d.ui().toneConsoleFamily == 5, "pressing a family drills into it");
    {
        const auto surf = d.surface();
        check(failed, surf.step[8].primary == "Violin",
              "the family's programs are on the BOTTOM row");
        check(failed, surf.step[15].primary == "Timpani", "...all eight of them");
        check(failed, surf.step[0].primary == "BACK",
              "top-left is BACK -- drilling in is no longer one-way");
        check(failed, surf.step[3].base == CellState::StepOutOfRange,
              "the rest of the spare row is dim rather than meaning something else");
    }

    // BACK returns to the families without closing and re-opening.
    d.clickStep(0);
    check(failed, d.ui().toneConsoleFamily < 0, "BACK returns to the family page");
    check(failed, d.ui().machineConsoleOpen, "...without closing the console");
    d.clickStep(13);         // back into Strings

    // --- Pressing a program selects it and closes ----------------------------
    d.clickStep(10);         // bottom-row slot 2 -> Strings + 2 = program 42 (Cello)
    d.runBlocks(4);
    check(failed, !d.ui().machineConsoleOpen, "picking a program closes the console");

    const auto prog = d.proc().kit(0).baseParams[
        static_cast<std::size_t>(ToneMachine::kProgram)];
    check(failed, std::abs(prog - 42.0f) < 0.5f,
          "...and the instrument is the one the two presses named (family 5, slot 2 = 42)");

    // --- Re-opening starts at the family page again --------------------------
    d.gap();
    d.longPress(CB::Section, IMachine::kSrcSecIdx);
    check(failed, d.ui().machineConsoleOpen && d.ui().toneConsoleFamily < 0,
          "re-opening starts at the families again -- always two presses, never one");
    {
        // Now the CURRENT family is 5, so the lit cell moved with the selection.
        const auto surf = d.surface();
        check(failed, surf.step[13].base == CellState::SelectorCurrent,
              "the lit family follows the chosen instrument");
    }

    // Re-holding SRC closes it -- the existing console rail, not a new rule.
    d.gap();
    d.longPress(CB::Section, IMachine::kSrcSecIdx);
    check(failed, !d.ui().machineConsoleOpen, "re-holding SRC closes the console");

    // --- P-Locking an instrument onto a step, the house way -----------------
    // The standard method for a modal picker, set by the SoundPool overlay:
    // HOLD THE STEP FIRST, then open the picker, and the picker writes onto the
    // held step(s). It only works if the picker's layer outranks StepInspector
    // -- otherwise the grid silently reverts to the step's inspector and the
    // picker is unreachable the moment a step is down. SoundPool has always sat
    // above the inspector for exactly this reason; the Tone console now does too
    // (4.10), and this is the assertion that keeps it there.
    d.gap();
    const float baseBefore = d.proc().kit(0).baseParams[
        static_cast<std::size_t>(ToneMachine::kProgram)];

    // Real QWERTY keys, not the controller path: a controller source shares one
    // id across every button, so releasing a picker cell matches the HELD step's
    // entry and tears the edit context down. On the keyboard each step has its
    // own code, which is the surface this gesture is designed for.
    d.keyDown('H');                                         // step 3, held first
    d.longPress(CB::Section, IMachine::kSrcSecIdx);         // then the picker
    check(failed, d.surface().activeLayer == SurfaceLayer::MachineConsole,
          "with a step held, the picker still owns the grid -- the step is its "
          "OPERAND, not a competitor for the cells");

    d.keyTap('B');           // cell 10 -> family 2 (Organ)
    // NB: the picker cell must not be the held step's OWN key -- pressing it is
    // that step's release. Inherent to one grid being two things, and the same
    // constraint the SoundPool overlay has.
    d.keyTap(',');           // cell 13 -> slot 5 -> program 21
    d.runBlocks(4);
    d.keyUp('H');

    const auto& step3 = d.proc().sequence().tracks[0].steps[3];
    check(failed, step3.overrides.has(ToneMachine::kProgram),
          "picking with a step held P-LOCKS the instrument onto that step");
    check(failed, std::abs(d.proc().kit(0).baseParams[
              static_cast<std::size_t>(ToneMachine::kProgram)] - baseBefore) < 0.5f,
          "...and the track's base instrument is untouched (Override-ELSE-Base)");
    check(failed, !step3.trig,
          "...and the release does not toggle the trig -- the step was the operand (9.38)");

    // --- Why the layout is bottom-row-first ---------------------------------
    // The whole point of rotating families and putting programs on cells 8..15:
    // a held step is strictly less likely to sit on the bottom row (for any
    // track length that is not a multiple of 16 the last page's bottom row has
    // fewer live steps, and on a track of 8 or less it has none). So with a step
    // held on the TOP row -- the common case, and the near-certain one on a short
    // track -- EVERY program cell is reachable. Guarded so nobody tidies the
    // layout back to the natural-looking one.
    d.gap();
    d.keyDown('D');          // step 0: the most likely step to be P-Locked
    d.longPress(CB::Section, IMachine::kSrcSecIdx);
    check(failed, d.surface().activeLayer == SurfaceLayer::MachineConsole,
          "picker opens over a held step 0");
    d.keyTap('C');           // cell 8 -> family 0 (Piano)
    check(failed, d.ui().toneConsoleFamily == 0,
          "with step 0 held, its own cell blocks a SPECIALISED family, not a common one");
    {
        const auto surf = d.surface();
        bool allReachable = true;
        for (int i = ToneMachine::kProgramsPerFamily; i < 16; ++i)
            if (surf.step[static_cast<std::size_t>(i)].primary.isEmpty())
                allReachable = false;
        check(failed, allReachable,
              "...and every one of the family's 8 programs is on a cell the held "
              "step is not occupying");
    }
    d.keyTap('V');           // cell 9 -> slot 1
    d.runBlocks(4);
    d.keyUp('D');
    check(failed, d.proc().sequence().tracks[0].steps[0].overrides.has(ToneMachine::kProgram),
          "so the whole pick lands as a P-Lock with no latch needed");

    // --- The held step's OWN cell: unreachable while the key is down --------
    // Holding step 3 means key 'H' is down, so cell 3 cannot be pressed on
    // either page -- you cannot press a key that is already pressed. The way
    // out is the LATCH (W7): hold the step, tap Func, let go. The step stays in
    // the edit context as a VIRTUAL hold, the finger is free, and all sixteen
    // cells are reachable again.
    d.gap();
    d.keyDown('H');               // step 3
    d.tap(CB::Func);              // latch it
    d.keyUp('H');                 // finger off -- the step is still the operand
    check(failed, d.proc().editContext().hasAnyLatchedStep()
                      && d.proc().editContext().isActiveForEditing(),
          "hold + Func latches the step and keeps it as the edit operand");

    const float baseBeforeLatch = d.proc().kit(0).baseParams[
        static_cast<std::size_t>(ToneMachine::kProgram)];

    d.gap();
    d.longPress(CB::Section, IMachine::kSrcSecIdx);
    check(failed, d.surface().activeLayer == SurfaceLayer::MachineConsole,
          "the picker opens over a LATCHED step too");

    d.keyTap('H');                // cell 3 -- the very cell that was blocked
    check(failed, d.ui().toneConsoleFamily == 11,
          "...and the step's OWN cell is now pressable, because no key is held");
    d.keyTap('N');                // cell 11 -> slot 3 -> program 11*8+3 = 91
    d.runBlocks(4);

    const auto& latchedStep = d.proc().sequence().tracks[0].steps[3];
    check(failed, latchedStep.overrides.has(ToneMachine::kProgram),
          "picking over a latched step P-LOCKS onto it");
    check(failed, std::abs(d.proc().kit(0).baseParams[
              static_cast<std::size_t>(ToneMachine::kProgram)] - baseBeforeLatch) < 0.5f,
          "...leaving the track's base instrument alone");
    d.doubleTap(CB::Func);        // universal escape drops the latch
    check(failed, !d.proc().editContext().hasAnyLatchedStep(),
          "double-tap Func releases the latched step");
}
}   // namespace lockstep
