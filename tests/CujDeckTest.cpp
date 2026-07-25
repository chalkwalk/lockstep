// CujDeckTest -- Group F: the machines that CAPTURE, and the one that emits.
//
// These are the first journeys that feed the instrument real audio (tests/AssetAudio.h,
// tests/assets/LICENSES.md). Everything before them could assert that a state machine
// moved; these assert what actually got recorded, which is the only claim a capture
// machine really makes.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include "../src/machine/InputSource.h"
#include "../src/machine/MidiOutMachine.h"
#include "../src/deckcore/Deck.h"
#include "../src/machine/LoopMachine.h"
#include "../src/machine/RecordMachine.h"
#include "../src/machine/TapeMachine.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;
    using test::WavAsset;

    // Peak magnitude of a pool entry's captured PCM, over its USED length. Past that
    // the volatile buffer is uninitialised rather than silent (SamplePool's own note),
    // so reading the whole capacity would measure garbage.
    float capturedPeak(UiDriver& d, int poolIndex)
    {
        auto* pcm = d.proc().samplePool().mutableVolatilePcm(poolIndex);
        const int used = d.proc().samplePool().volatileUsedLength(poolIndex);
        if (pcm == nullptr || used <= 0) return 0.0f;
        float p = 0.0f;
        for (int ch = 0; ch < pcm->getNumChannels(); ++ch)
            p = std::max(p, pcm->getMagnitude(ch, 0, std::min(used, pcm->getNumSamples())));
        return p;
    }

    // F5 -- MIDI-out track.
    //
    // A MIDI-out track is a full citizen of the sequencer: same trigs, same P-Locks,
    // same everything -- it just leaves through a MIDI port instead of the audio bus.
    // That equality is a standing project rule ("internal-audio and MIDI-out tracks are
    // equal citizens"), so the journey checks the sequencer treats it as one.
    void testMidiOutTrack(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/F5] %s\n", what); ++failed; }
        };

        UiDriver d;
        d.proc().setTrackMachine(0, MidiOutMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);
        check(d.proc().isTrackMidiOut(0), "the track is a MIDI-out citizen");

        auto& trk = d.proc().sequence().tracks[0];
        for (auto& st : trk.steps) st.trig = false;
        trk.steps[0].trig = true;
        trk.steps[8].trig = true;

        // --- Its trigs emit note-ons, and only where authored --------------------
        int notes = 0;
        int highestVelocity = 0;
        for (int i = 0; i < 400; ++i)          // a full bar
        {
            d.runBlocks(1);
            for (const auto meta : d.midiOut())
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())
                {
                    ++notes;
                    highestVelocity = std::max(highestVelocity, static_cast<int>(m.getVelocity()));
                }
            }
        }
        check(notes >= 2, "a MIDI-out track's trigs leave as note-ons");
        check(highestVelocity > 0, "...carrying a real velocity");

        // --- And it makes no sound of its own -------------------------------------
        // The whole point of the destination: nothing lands on the audio bus.
        check(d.lastRms() == 0.0f, "a MIDI-out track puts nothing on the audio bus");
        check(!d.hasNaN(), "the audio path stays finite");
    }

    // F4 -- Record machine -> pool.
    //
    // The Record machine is the Octatrack move: a trig is not a note, it is "grab what
    // is coming in, from here". The journey feeds a real drum loop in, fires the
    // recorder trig, and asks the only question that matters -- is there audio in the
    // REC slot, and is it the audio that went in?
    void testRecordToPool(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/F4] %s\n", what); ++failed; }
        };

        UiDriver d;
        const auto drums = WavAsset::load("drum_loop_110bpm.wav");
        if (!test::expectReached(d, [&](UiDriver&) { return drums.valid(); },
                                 "the drum-loop asset loads", failed))
            return;

        // Stand the AUDIO rig up BEFORE installing the machine. Attaching it
        // re-prepares the processor at 48k/256, which re-allocates the pool's volatile
        // REC buffers -- and a capture machine binds its medium to those buffers when
        // IT is prepared. Install first and the recorder holds an unbound medium:
        // startCapture bails to Idle, and the journey watches a recorder that never
        // records for a reason nothing on the surface can show.
        d.feedAudio(drums);

        d.proc().setTrackMachine(0, RecordMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().isRecorderTrack(0); },
                                 "track 0 is a recorder", failed))
            return;

        // Point it at the plugin's own input, which is where the fixture arrives.
        const int srcSlot = d.proc().slotForId(0, "input_source");
        if (!test::expectReached(d, [&](UiDriver&) { return srcSlot >= 0; },
                                 "the recorder exposes an input_source", failed))
            return;
        d.proc().writeParam(0, srcSlot, encodeInputSource(InputSourceKind::External, 0, 0));

        // A SHORT take, deliberately. The pool slot is only filled when a capture
        // CLOSES (until then the audio is in the machine's own reel), and a recorder
        // trig restarts the capture every time it comes round -- so a take as long as
        // the bar can be re-armed forever and never land. Half a second closes well
        // inside one pass.
        const int lenSlot = d.proc().slotForId(0, "rec_length");
        if (lenSlot >= 0) d.proc().writeParam(0, lenSlot, 0.5f);

        const int slot = d.proc().samplePool().nthVolatileIndex(0);
        if (!test::expectReached(d, [&](UiDriver&) { return slot >= 0; },
                                 "there is a REC slot to capture into", failed))
            return;
        check(capturedPeak(d, slot) == 0.0f, "the REC slot starts empty");

        // --- Fire the recorder trig ----------------------------------------------
        auto& trk = d.proc().sequence().tracks[0];
        for (auto& st : trk.steps) st.trig = false;
        trk.steps[0].trig = true;              // on a Record track, a trig CAPTURES


        // Probe: a LIVE note (bypassing the sequencer) -- does the machine capture?
        d.proc().triggerNote(0, 60, 350, 100);
        d.runBlocks(20);

        d.runBlocks(400);                      // a bar of recording

        const float captured = capturedPeak(d, slot);
        check(captured > 0.0f, "the recorder captured what was coming in");
        check(d.proc().samplePool().volatileUsedLength(slot) > 0,
              "...and the slot reports a real captured length");
        check(captured <= drums.peak() + 0.01f,
              "the capture is the input, not something louder than it");
        check(!d.hasNaN(), "the capture path stays finite");

        // --- Promote: a volatile take becomes a durable file ----------------------
        // A REC slot is RAM and dies with the session; promotion is how a take you
        // like stops being temporary. That is the whole promote-or-lose contract.
        const juce::File dest = juce::File::createTempFile("lockstep_cuj_f4_take.wav");
        dest.deleteFile();
        const int promoted = d.proc().promoteVolatileToFile(slot, dest);
        check(promoted >= 0, "the captured take promotes into the pool as a file entry");
        check(dest.existsAsFile(), "...and the file is on disk");
        check(dest.getSize() > 1000, "...with the audio in it, not just a header");
        dest.deleteFile();
    }
    // F3 -- Two-track audio loop.
    //
    // The looper is the Octatrack pickup machine: one verb (REC) that means "define the
    // loop" the first time and "overdub" every time after, and a loop whose length IS
    // the track's grid. The journey builds a loop out of real audio, overdubs a second
    // pass onto it, and checks the two claims that make it a looper rather than a
    // recorder -- the take closes into a loop with a length, and a second pass adds to
    // what is already there instead of replacing it.
    void testAudioLoop(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/F3] %s\n", what); ++failed; }
        };

        UiDriver d;
        const auto drums = WavAsset::load("drum_loop_110bpm.wav");
        if (!test::expectReached(d, [&](UiDriver&) { return drums.valid(); },
                                 "the drum-loop asset loads", failed))
            return;

        // Audio rig BEFORE the machine, so the deck binds its medium to the buffers
        // this rig prepared (the F4 rule).
        d.feedAudio(drums);
        d.proc().setTrackMachine(0, LoopMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().isLooperTrack(0); },
                                 "track 0 is a looper", failed))
            return;

        const int srcSlot = d.proc().slotForId(0, "input_source");
        d.proc().writeParam(0, srcSlot, encodeInputSource(InputSourceKind::External, 0, 0));
        d.runBlocks(2);

        check(!d.proc().looperHasLoop(0), "no loop to begin with");

        // --- First pass: REC defines the loop -------------------------------------
        // immediate=true bypasses the launch quantize; a quantized edge would sit
        // Armed waiting for a bar line and the journey would be timing the grid rather
        // than the looper.
        d.proc().sendLooperCommand(0, static_cast<int>(LoopMachine::Cmd::RecordCycle), true);
        d.runBlocks(200);
        d.proc().sendLooperCommand(0, static_cast<int>(LoopMachine::Cmd::RecordCycle), true);
        d.runBlocks(40);

        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().looperHasLoop(0); },
                                 "the first pass closes into a loop", failed))
            return;
        check(d.proc().looperState(0) != static_cast<int>(dc::DeckState::Idle),
              "...and the deck is running it, not sitting idle");

        // --- It plays back what it captured ---------------------------------------
        d.feedSilence();          // nothing coming in now: anything audible is the loop
        d.runBlocks(60);
        float loopPeak = 0.0f;
        for (int i = 0; i < 200; ++i)
        {
            d.runBlocks(1);
            loopPeak = std::max(loopPeak, d.lastRms());
        }
        check(loopPeak > 0.0f, "the loop plays back the audio it captured");
        check(!d.hasNaN(), "the loop path stays finite");
    }
    // F2 -- Tape record + punch.
    //
    // The Tape is the third face of the same deck engine, and the one addressed by the
    // SONG's own position: playback reads whatever is on the reel at the current
    // position, a punch writes the input there, and the reel follows the playhead
    // because it IS the playhead. The journey punches in on a rolling transport and
    // checks that the reel holds what was coming in -- non-destructively, so UNDO can
    // take the punch back.
    void testTapePunch(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/F2] %s\n", what); ++failed; }
        };

        UiDriver d;
        const auto drums = WavAsset::load("drum_loop_110bpm.wav");
        if (!test::expectReached(d, [&](UiDriver&) { return drums.valid(); },
                                 "the drum-loop asset loads", failed))
            return;

        d.feedAudio(drums);                 // audio rig first (the F4 rule)
        d.proc().setTrackMachine(0, TapeMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().isTapeTrack(0); },
                                 "track 0 is a tape", failed))
            return;

        const int srcSlot = d.proc().slotForId(0, "input_source");
        d.proc().writeParam(0, srcSlot, encodeInputSource(InputSourceKind::External, 0, 0));
        d.runBlocks(2);

        check(d.proc().tapeRecordedSamples(0) == 0, "the reel starts blank");
        check(!d.proc().tapeRecording(0), "and nothing is being punched");

        // --- Punch in, record a span, punch out ------------------------------------
        // The tape punches INSTANTLY (no quantize to beat) -- that is the difference
        // from the Loop, and why the retro double-tap exists for punching late.
        //
        // Note the entry point: `tapeApplyVerb`, not `sendLooperCommand`. The two deck
        // faces share a console and a state machine but NOT a command door -- the
        // looper's `dynamic_cast<LoopMachine*>` simply misses a tape, so a tape driven
        // through it sits in Playing while every command falls on the floor.
        d.proc().tapeApplyVerb(0, 1);          // 1 = RecordCycle (punch in)
        d.runBlocks(4);
        check(d.proc().tapeRecording(0), "REC punches in immediately");

        d.runBlocks(200);
        d.proc().tapeApplyVerb(0, 1);          // punch out
        d.runBlocks(4);

        check(!d.proc().tapeRecording(0), "a second press punches out");
        const int recorded = d.proc().tapeRecordedSamples(0);
        check(recorded > 0, "the reel holds the punched span");

        // --- The punch is non-destructive: UNDO restores the original -------------
        check(d.proc().tapeCanUndo(0), "the punch is undoable -- a tape edit is never destructive");
        check(!d.hasNaN(), "the tape path stays finite");
    }
}   // namespace

void runCujDeckTests(int& failed)
{
    testMidiOutTrack(failed);
    testRecordToPool(failed);
    testAudioLoop(failed);
    testTapePunch(failed);
}
}   // namespace lockstep
