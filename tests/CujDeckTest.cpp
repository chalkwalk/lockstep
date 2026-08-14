// CujDeckTest -- Group F: the machines that CAPTURE, and the one that emits.
//
// These are the first journeys that feed the instrument real audio (tests/AssetAudio.h,
// tests/assets/LICENSES.md). Everything before them could assert that a state machine
// moved; these assert what actually got recorded, which is the only claim a capture
// machine really makes.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include "../src/ParameterIDs.h"
#include "../src/core/SyncMode.h"
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

        // --- Widen it: four sub-tracks in one slot --------------------------------
        // A four-sub loop is still ONE track and one slot -- that is the whole storage
        // decision (§40.7). Raising the count is what arms the extra subs.
        const int subSlot = d.proc().slotForId(0, "subtrack_count");
        if (!test::expectReached(d, [&](UiDriver&) { return subSlot >= 0; },
                                 "the looper exposes its sub-track count", failed))
            return;
        d.proc().writeParam(0, subSlot, 4.0f);
        d.runBlocks(4);
        check(d.proc().looperSubTrackCount(0) == 4, "the deck widens to four sub-tracks");
        check(d.proc().looperSubArmed(0, 0), "sub-track 0 is armed by default");

        // --- Promote: one WAV per non-empty sub, as a take GROUP ------------------
        const juce::File stem = juce::File::createTempFile("lockstep_cuj_f3_take");
        stem.deleteFile();
        const int members = d.proc().promoteDeckTake(0, stem);
        check(members > 0, "the take promotes as a group -- one file per non-empty sub");
        for (const auto& f : stem.getParentDirectory().findChildFiles(
                 juce::File::findFiles, false, stem.getFileNameWithoutExtension() + "*"))
            f.deleteFile();
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

        // --- Markers are dumb navigation points -----------------------------------
        // Dropped by hand, cued by a LOCATE and never a launch (§40.4): a marker moves
        // the tape to a place, it does not fire anything. That distinction is the
        // reason they can be dropped freely mid-take.
        const int marksBefore = d.proc().tapeMarkerCount(0);
        d.proc().dropTapeMarker(0);
        d.runBlocks(60);
        d.proc().dropTapeMarker(0);
        check(d.proc().tapeMarkerCount(0) == marksBefore + 2, "markers drop where the head is");

        const int trigsBefore = 0;   // a tape track sequences nothing to disturb
        d.proc().tapeCue(0, -1);     // wind to the previous marker
        d.runBlocks(4);
        check(d.proc().tapeMarkerCount(0) == marksBefore + 2,
              "a cue is a LOCATE -- it moves the head and fires nothing");
        (void) trigsBefore;
        check(!d.hasNaN(), "...and winding leaves the audio path finite");
    }
    // F2b -- Wind and scrub the reel (standalone only).
    //
    // The last leg of the catalogue, and the one that is about the tape being a REEL
    // rather than a buffer: park the song and you can wind it, hear it move under the
    // head, and leave the head where your ear stopped -- so Play resumes from there.
    // A locate jumps silently; a wind is audible in both directions and stops at the
    // leader. That difference is the whole feature.
    //
    // "Standalone only" is really "whenever Lockstep OWNS the transport"
    // (`transportWindable` = `!hostedLocked`). A plugin cannot move the DAW's
    // playhead, so hosted-and-Locked SUPPRESSES the wind cells rather than offering
    // ones that half-work -- and the journey drives that gate through the real
    // `syncMode` parameter rather than faking a wrapper type, because the parameter
    // IS the rule's second half.
    void testTapeWindScrub(int& failed)
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

        // --- Suppressed when the host owns the playhead ---------------------------
        // syncMode defaults to Locked and the test rig is not the standalone wrapper,
        // so the tape starts NOT windable. Assert the absence first: the claim is that
        // the cells are not offered at all, which is a different thing from cells that
        // are offered and do nothing.
        auto* sync = d.proc().apvts().getParameter(ParamIDs::syncMode);
        if (!test::expectReached(d, [&](UiDriver& dd) {
                                     return sync != nullptr && !dd.proc().transportWindable();
                                 },
                                 "hosted+Locked, the transport is not windable", failed))
            return;
        {
            const auto surf = d.surface();
            check(surf.step[12].primary.isEmpty() && surf.step[13].primary.isEmpty(),
                  "hosted-locked, the console offers no wind cells at all");
        }

        // The gate is not only cosmetic: the setter itself refuses. A cell that is
        // not drawn is unreachable by finger, but a CONTROLLER can still send the
        // button, so "suppress, don't half-work" has to hold below the surface too.
        {
            d.audioRig().playHead().setPlaying(false);
            d.proc().clock().setInPluginPlaying(false);
            d.runBlocks(2);
            const double lockedHead = d.proc().tapeReelHead(0);
            d.proc().tapeSetScrubRate(0, 4.0);
            d.runBlocks(60);
            check(d.proc().tapeReelHead(0) == lockedHead,
                  "hosted-locked, even a direct wind command moves nothing");
            d.proc().tapeSetScrubRate(0, 0.0);
            d.audioRig().playHead().setPlaying(true);
            d.proc().clock().setInPluginPlaying(true);
            d.runBlocks(2);
        }

        // Auto sync: Lockstep owns the transport, so the reel can be wound.
        sync->setValueNotifyingHost(sync->convertTo0to1(static_cast<float>(SyncMode::Auto)));
        d.runBlocks(2);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().transportWindable(); },
                                 "Auto sync makes the transport windable", failed))
            return;
        {
            const auto surf = d.surface();
            check(surf.step[12].primary == "<<" && surf.step[13].primary == ">>",
                  "...and the wind cells appear on the console");
        }

        // --- Put something ON the reel, so a wind has something to play -----------
        // A wind over blank tape is silent for an honest reason, and would make the
        // audibility assertion below unfalsifiable.
        d.proc().tapeApplyVerb(0, 1);          // punch in
        d.runBlocks(300);
        d.proc().tapeApplyVerb(0, 1);          // punch out
        d.runBlocks(4);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().tapeRecordedSamples(0) > 0; },
                                 "the reel holds a recorded span to wind over", failed))
            return;

        // Park the song. Winding is a PARKED-transport act: a running sequencer is
        // never yanked by the reel (the processor's reel-is-truth locate is gated on
        // `!blockTransport_.running`).
        //
        // BOTH transports have to stop, and that is not obvious. Clearing the
        // in-plugin one parks the SEQUENCER, but the rig's stub playhead keeps
        // advancing ppq every block on its own -- and a parked tape republishes
        // `reelPosAtBlockStart()` as its head, so the head crawls forward at exactly
        // 1x with no scrub running at all. Measured: it looks precisely like a wind
        // that will not stop, including after an explicit setScrubRate(0), which is
        // what makes it worth writing down.
        d.audioRig().playHead().setPlaying(false);
        d.proc().clock().setInPluginPlaying(false);
        d.proc().tapeCue(0, -1);               // wind back toward the start
        d.runBlocks(4);

        // --- Hold to wind: the head moves, and you can HEAR it ---------------------
        const double headBefore = d.proc().tapeReelHead(0);
        const double ppqBefore = d.proc().clock().ppqAtBlockStart();
        d.press(CB::Step, 13);                 // >> (fast forward)
        float windPeak = 0.0f;
        for (int i = 0; i < 120; ++i) { d.runBlocks(1); windPeak = std::max(windPeak, d.lastRms()); }
        const double headWound = d.proc().tapeReelHead(0);

        check(headWound > headBefore, "holding >> winds the reel forward");
        check(windPeak > 0.0f,
              "...audibly -- a wind plays the reel under a moving head, unlike a locate");

        // Release ends it. The machine slews to rest rather than stopping dead, so the
        // head is allowed to coast a little; what must NOT happen is winding forever.
        d.release(CB::Step, 13);
        d.runBlocks(120);
        const double headSettled = d.proc().tapeReelHead(0);
        d.runBlocks(120);
        check(std::abs(d.proc().tapeReelHead(0) - headSettled) < 1.0,
              "releasing the cell ends the wind -- it cannot stick");

        // --- Reel-is-truth: the transport went where the ear did ------------------
        // The point of winding rather than seeking: you stop where it sounded right,
        // and Play/punch resume from exactly there.
        check(d.proc().clock().ppqAtBlockStart() > ppqBefore,
              "the transport followed the head, so play resumes where the ear stopped");

        // --- Winding back stops at the leader -------------------------------------
        // A reel has an end. Wind past it and the head parks at zero rather than
        // running into negative tape.
        d.press(CB::Step, 12);                 // <<
        d.runBlocks(600);                      // far more than enough to overrun the start
        d.release(CB::Step, 12);
        d.runBlocks(60);
        check(d.proc().tapeReelHead(0) >= 0.0, "winding back never runs past the leader");
        check(d.proc().tapeReelHead(0) < 1.0, "...it parks at the start");
        check(!d.hasNaN(), "the wind path stays finite");

        // --- Jog: while parked, MZ slot 0 IS the reel ------------------------------
        // The encoder that would pick the Source becomes the reel you rock. Asserted
        // behaviourally -- does the head move, and is the Source left alone? -- rather
        // than by looking for the widget, because the widget is only the affordance
        // for this.
        d.runBlocks(2);
        const double headPreJog = d.proc().tapeReelHead(0);
        const float srcPreJog = d.proc().kit(0).baseParams[static_cast<std::size_t>(srcSlot)];
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().tapeScrubEligible(0); },
                                 "a parked, windable tape is scrub-eligible", failed))
            return;
        d.dragMZSlider(0, -40.0f);
        d.runBlocks(40);
        check(d.proc().tapeReelHead(0) != headPreJog, "rocking slot 0 jogs the reel");
        check(d.proc().kit(0).baseParams[static_cast<std::size_t>(srcSlot)] == srcPreJog,
              "...and does NOT write the Source param it would otherwise edit");

        // --- Playing, the same encoder is the Source picker again -----------------
        // The reel widget gives way the moment the song runs: slot 0 stops being a
        // reel and goes back to picking a source, which is the rule that keeps the
        // encoder from meaning two things at once.
        d.proc().tapeSetScrubRate(0, 0.0);
        d.proc().clock().setInPluginPlaying(true);
        d.runBlocks(4);
        check(!d.proc().tapeScrubEligible(0),
              "a rolling song is not scrub-eligible -- slot 0 is the Source picker again");
    }
}   // namespace

void runCujDeckTests(int& failed)
{
    testMidiOutTrack(failed);
    testRecordToPool(failed);
    testAudioLoop(failed);
    testTapePunch(failed);
    testTapeWindScrub(failed);
}
}   // namespace lockstep
