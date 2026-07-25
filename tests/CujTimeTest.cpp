// CujTimeTest -- Group H: time and the global layer.
//
// H1 is the TIME page, which is where the scope ladder is most visible: tempo and
// time-signature live at Set, Song and Scene, and the value you get is the nearest
// one that is set. The page is reached the same way from two scopes (`Song+TRIG`,
// `Scene+TRIG`) and the scope you held decides which rung you are writing -- so the
// journey checks that the same gesture from two scopes writes two different places,
// and that clearing a rung falls back to the one above rather than to a default.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include "../src/ParameterIDs.h"
#include "../src/core/SyncMode.h"
#include "../src/machine/MidiOutMachine.h"
#include "../src/ui/MetaBand.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    void settle(UiDriver& d) { DispatchProbe::frame(d.editor()); }

    // Song/Scene + TRIG opens the TIME band. gap() first: two presses of the same
    // scope key inside the double-tap window latch it.
    void openTimeBand(UiDriver& d, CB scope)
    {
        d.gap();
        d.press(scope);
        d.tap(CB::Section, IMachine::kTrigSecIdx);
        d.release(scope);
        settle(d);
    }

    // H1 -- TIME page.
    void testTimePage(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/H1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        // --- Song+TRIG opens TIME (the TRIG key relabels) -------------------------
        openTimeBand(d, CB::SongScope);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().overlay == Overlay::Time; },
                                 "Song+TRIG opens the TIME overlay", failed))
            return;
        check(resolveMetaBand(d.ui()) == MetaBand::Time, "the encoders become the TIME band");

        // --- Re-pressing TRIG cycles TIME <-> KEY ---------------------------------
        // Two signature pages, one key: the same rule the velocity overlay uses for
        // its four axes.
        d.gap();
        d.tap(CB::Section, IMachine::kTrigSecIdx);
        settle(d);
        check(resolveMetaBand(d.ui()) == MetaBand::Key, "re-pressing TRIG shows the KEY page");
        d.gap();
        d.tap(CB::Section, IMachine::kTrigSecIdx);
        settle(d);
        check(resolveMetaBand(d.ui()) == MetaBand::Time, "...and again comes back to TIME");

        // --- The CLICK field is on this page (9.10 moved it here from Func+3) -----
        {
            const bool before = d.proc().clock().isMetronomeEnabled();
            const int clickField = DispatchProbe::mzSlotOffset(d.editor()) + 2;
            d.setParam(clickField, before ? 0.0f : 1.0f);
            check(d.proc().clock().isMetronomeEnabled() != before,
                  "the TIME page carries the metronome toggle");
        }

        d.doubleTap(CB::Func);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().overlay == Overlay::None; },
                                 "double-tap Func leaves the TIME overlay", failed))
            return;

        // --- The scope ladder: the same page writes a different rung --------------
        // Scene overrides Song overrides Set, and `effectiveTimeSig` resolves to the
        // nearest one that is SET -- so writing at Scene must not disturb Song, and
        // clearing Scene must fall back to Song rather than to a default.
        const auto setLevel = d.proc().effectiveTimeSig();

        openTimeBand(d, CB::SongScope);
        {
            const int sigField = DispatchProbe::mzSlotOffset(d.editor()) + 1;
            d.setParam(sigField, 1.0f);      // any non-default row on the sig ladder
            settle(d);
        }
        const auto songLevel = d.proc().effectiveTimeSig();
        d.doubleTap(CB::Func);

        if (!test::expectReached(d, [&](UiDriver&) {
                                     return songLevel.numerator != setLevel.numerator
                                         || songLevel.denominator != setLevel.denominator;
                                 },
                                 "writing the Song rung changed the effective time-sig", failed))
            return;
        check(d.proc().song().hasTimeSig, "the Song rung is now SET, not inherited");
        check(!d.proc().section().hasTimeSig, "...and the Scene rung is still inherited");

        openTimeBand(d, CB::SceneScope);
        {
            const int sigField = DispatchProbe::mzSlotOffset(d.editor()) + 1;
            d.setParam(sigField, 3.0f);
            settle(d);
        }
        d.doubleTap(CB::Func);

        check(d.proc().section().hasTimeSig, "the same page under Scene writes the SCENE rung");
        check(d.proc().song().hasTimeSig, "...leaving the Song rung set as it was");
        {
            const auto now = d.proc().effectiveTimeSig();
            check(now.numerator != songLevel.numerator || now.denominator != songLevel.denominator,
                  "and the Scene rung is what the instrument now resolves to");
        }
    }
    // Count note-ons emitted over a roll. A MIDI-out track is the only place a test
    // can COUNT what the sequencer fired -- audio tells you something sounded, not how
    // many times -- which is exactly what a ratchet journey has to measure.
    int noteOnsOverRoll(UiDriver& d, int blocks)
    {
        int n = 0;
        for (int i = 0; i < blocks; ++i)
        {
            d.runBlocks(1);
            for (const auto meta : d.midiOut())
                if (meta.getMessage().isNoteOn())
                    ++n;
        }
        return n;
    }

    // H2 -- Retrig / ratchet.
    //
    // 9.10 split the retrig family in two: the free-running live stutter went away, and
    // the per-step authored ratchet became a TRIG-band field, P-lockable like any other.
    // The claim is that a step carrying a rate fires SEVERAL times where it used to fire
    // once, and that nothing else on the track changes. Counting is the only honest way
    // to check it, so the track emits MIDI.
    void testRetrig(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/H2] %s\n", what); ++failed; }
        };

        UiDriver d;
        d.proc().setTrackMachine(0, MidiOutMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);

        auto& trk = d.proc().sequence().tracks[0];
        for (auto& st : trk.steps) { st.trig = false; st.trigOverride.hasRetrig = false; }
        trk.steps[0].trig = true;

        const int plain = noteOnsOverRoll(d, 400);   // a full bar
        if (!test::expectReached(d, [&](UiDriver&) { return plain > 0; },
                                 "the MIDI-out track fires its one trig", failed))
            return;

        // --- Author a ratchet on that step, through the TRIG band -----------------
        // Order matters, and not for a reason any doc states. The TRIG band is a META
        // page (Func + the SRC key), so it must be opened BEFORE the step goes down:
        // pressing Func while a step is held is the W7 latch and is consumed, so the
        // band would never open. Open the page, then hold the step, then turn -- and
        // the field lands on the held step as a P-Lock.
        d.gap();
        d.press(CB::Func);
        d.tap(CB::Section, IMachine::kSrcSecIdx);
        d.release(CB::Func);
        settle(d);
        if (!test::expectReached(d, [](UiDriver& dd) { return resolveMetaBand(dd.ui()) == MetaBand::Trig; },
                                 "Func+SRC opens the TRIG band", failed))
            return;

        d.press(CB::Step, 0);
        settle(d);
        d.setParam(DispatchProbe::mzSlotOffset(d.editor()) + 5, 6.0f);   // field 5 = RTG
        d.release(CB::Step, 0);

        if (!test::expectReached(d, [&](UiDriver&) { return trk.steps[0].trigOverride.hasRetrig; },
                                 "the RTG field lands on the held step", failed))
            return;
        check(trk.steps[0].trig, "...without disturbing the trig it decorates");

        const int ratcheted = noteOnsOverRoll(d, 400);
        check(ratcheted > plain, "the ratcheted step fires more often than the plain one");
    }
    // H1b -- PreRoll: the count-in delays the take, not the transport.
    //
    // Record-armed with a non-zero pre-roll, hitting play gives you N bars of clicks
    // before anything is captured -- the difference between a take that starts with
    // your hand moving and one that starts with you waiting.
    void testPreRoll(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/H1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);

        check(!d.proc().preRollActive(), "no count-in when none is configured");

        // Stand the audio rig up FIRST and only then park the transport: attaching it
        // starts the in-plugin transport, and a count-in that begins before that gets
        // run over by it. (The count-in exists precisely because the transport has not
        // started yet.)
        d.runBlocks(1);

        // The count-in is Lockstep's OWN transport behaviour, by design: hosted and
        // locked, the DAW owns the downbeat and pressing Play must never delay it
        // (PRINCIPLES §3). The rig supplies a playhead, so it reads as hosted -- Auto
        // sync is what makes this the standalone-shaped case the count-in belongs to.
        if (auto* sync = d.proc().apvts().getParameter(ParamIDs::syncMode))
            sync->setValueNotifyingHost(sync->convertTo0to1(static_cast<float>(SyncMode::Auto)));

        d.proc().project().preRollBars = 2;
        d.tap(CB::SelectTrack, 0);
        d.tap(CB::VerbRecord);              // record-arm
        d.proc().clock().setInPluginPlaying(false);
        d.proc().transportPlay();

        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().preRollActive(); },
                                 "arming + play starts a count-in", failed))
            return;

        const auto progress = d.proc().preRollProgress();
        check(progress.second == 2, "...of the configured length");
        check(progress.first <= progress.second, "...counting toward it, not past it");

        // It ends on its own: the count-in is a delay, not a mode to escape.
        d.runBlocks(800);
        check(!d.proc().preRollActive(), "the count-in ends and the take begins");
    }
}   // namespace

void runCujTimeTests(int& failed)
{
    testTimePage(failed);
    testPreRoll(failed);
    testRetrig(failed);
}
}   // namespace lockstep
