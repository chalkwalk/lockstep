// CujRecordTest -- Group F critical user journey: realtime record.
//
// This is the journey that exercises the hardest harness enablers together: the
// live-audio bridge (E1) rolls the transport, MIDI note-in (E2) plays notes into
// the running sequencer, and the record path quantises them to trigs. If record
// writes anything at all, the bridge is genuinely live -- the sequencer only records
// while it is running. See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // F1 -- Realtime record capture (+ overdub arm + chord accumulation).
    void testRealtimeRecord(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/F1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 1);   // record onto track 1 (a synth voice)

        // Clear the rig's seeded trigs on track 1 so a recorded trig is unambiguously
        // new, not a survivor of the fixture.
        {
            auto& t1 = d.proc().sequence().tracks[1];
            for (int s = 0; s < 16; ++s)
                t1.steps[static_cast<std::size_t>(s)].trig = false;
        }

        // Select track 1: the active track moves 0 -> 1, which is what syncs the
        // processor's focus track (setActiveTrack early-returns on no-change, so
        // re-selecting track 0 would NOT sync it). Note-in routes to the focus track.
        d.tap(CB::SelectTrack, 1);
        if (!test::expectReached(
                d, [](UiDriver& dd) { return dd.activeTrack() == 1 && dd.proc().focusTrack() == 1; },
                "focus is on track 1 (editor + processor in sync)", failed))
            return;

        // Arm record (bare U, resolved on release).
        d.tap(CB::VerbRecord);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().clock().isRecordArmed(); },
                                 "bare U arms record", failed))
            return;

        // Roll the transport and play a note in: it quantises to a step and writes a
        // trig carrying the pitch. This only works if the transport is actually
        // running -- so it doubles as proof the audio bridge is live.
        d.playNote(60, 100, 1.0);
        {
            const auto& t1 = d.proc().sequence().tracks[1];
            int rec = -1;
            for (int s = 0; s < 16; ++s)
                if (t1.steps[static_cast<std::size_t>(s)].trig) { rec = s; break; }
            check(rec >= 0, "a realtime note-in writes a quantised trig");
            if (rec >= 0)
            {
                const auto& ov = t1.steps[static_cast<std::size_t>(rec)].trigOverride;
                check(ov.noteCount >= 1 && ov.notes[0] == 60,
                      "the recorded trig carries the played pitch");
            }
        }

        // Double-tap U -> overdub (record stays armed; notes now accumulate rather
        // than overwrite).
        d.doubleTap(CB::VerbRecord);
        check(d.proc().clock().isRecordArmed() && d.proc().clock().isOverdubArmed(),
              "double-tap U arms overdub");

        // A chord played in one instant accumulates onto a single recorded step
        // (up to four notes/step). The three note-ons share a block, so they share a
        // quantised step.
        d.noteOn(60).noteOn(64).noteOn(67);
        d.runBlocks(2);
        d.noteOff(60).noteOff(64).noteOff(67);
        d.runBlocks(1);
        {
            const auto& t1 = d.proc().sequence().tracks[1];
            int chordStep = -1;
            for (int s = 0; s < 16; ++s)
                if (t1.steps[static_cast<std::size_t>(s)].trigOverride.noteCount >= 3) { chordStep = s; break; }
            check(chordStep >= 0, "a three-note chord accumulates onto one recorded step");
        }
    }
}   // namespace

void runCujRecordTests(int& failed)
{
    testRealtimeRecord(failed);
}
}   // namespace lockstep
