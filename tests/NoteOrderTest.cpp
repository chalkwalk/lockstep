// NoteOrderTest -- a note-off for a pitch may never follow a note-on for that
// same pitch inside one block.
//
// Reported as: playing a four-note chord pattern through the Tone machine, a
// note would occasionally vanish from a chord, and about once a minute the whole
// chord would vanish. It looked like a Tone bug for a long time -- the same
// project with an FM machine on that track sounded perfect.
//
// It was the SEQUENCER, and the reason FM sounded fine is worth writing down.
// The pending note-off drain ran BEFORE trig scheduling, so it cleared
// `pendingNoteOffs_` and emitTrig's guard -- which exists precisely to emit a
// still-pending note-off ahead of the new note-ons -- found nothing to do. When
// the new trig then fired earlier in the block than the drained note-off,
// MidiBuffer ordered the events by SAMPLE POSITION rather than insertion, and
// the machine received:
//
//     ON 60, ON 63, ON 68, ON 72, OFF 60, OFF 63, OFF 65, OFF 67
//
// FM survives that by accident: with kMaxVoices == 4 and four-note chords every
// retrigger steals a voice, so the new note is DEFERRED behind a choke fade and
// `findVoiceByNote` still matches the outgoing voice -- the stale note-off
// retires the note that was leaving anyway. FluidLite has no such indirection:
// noteOn starts a voice immediately and noteOff resolves to "the voice playing
// this pitch", which is now the new one. It is released before a single sample
// is rendered, so it produces no sound at all. Hence one machine losing whole
// chords while another was flawless on identical MIDI.
//
// The invariant is therefore stated about the MIDI, not about any machine, and
// checked through MidiOutMachine so no synthesis is involved.
//
// TEMPO IS LOAD-BEARING. The fault needs the gate's end to land in the same
// block as, but LATER than, the next trig -- a sub-block phase relationship that
// only some tempos reach. Measured over 200 s at 44100/512:
//
//     88.23974935925473 (the reporter's tapped tempo)  ->  5 inverted blocks
//     88.24                                            -> 29 inverted blocks
//     88.235294                                        ->  0
//
// The last one is what a reasonable person picks when the UI says "88.24 BPM",
// and it is why this went unreproduced for so long. Both firing tempos are kept.

#include "TestHarness.h"
#include "AudioRig.h"

#include "../src/machine/MidiOutMachine.h"
#include "../src/ParameterIDs.h"

#include <set>

namespace lockstep
{
namespace
{
    struct Result
    {
        int invertedBlocks = 0;
        int worstNotesKilled = 0;
        int chordsSeen = 0;
    };

    // The reporter's chord track: quarter-note steps, a chord every fourth step,
    // and a gate of exactly one whole note -- so each chord's note-off falls on
    // the same musical instant as the next chord's note-on. The voicings matter
    // too: adjacent chords share two notes, and chord 4 shares all four with
    // chord 1, which is why the whole chord could disappear at the loop wrap.
    Result runChordPattern(double bpm, double seconds)
    {
        constexpr double kSR = 44100.0;
        constexpr int kBlk = 512;

        auto proc = std::make_unique<LockstepProcessor>();   // ~47 MB: never on the stack
        AudioRig rig(*proc, bpm, kSR, kBlk);

        {
            MidiOutMachine tmp;
            auto& k = proc->kit(1);
            k.machineId = MidiOutMachine::kMachineId;
            k.baseParams.resize(static_cast<std::size_t>(tmp.numParams()));
            for (int i = 0; i < tmp.numParams(); ++i)
                k.baseParams[static_cast<std::size_t>(i)] = tmp.paramSpec(i).defaultValue;
            proc->reinstallMachinesFromActiveKit();
        }

        // The default project arms trigs on every track; leave only the chord
        // track emitting or the scan cannot tell whose notes it is looking at.
        for (std::size_t tr = 0; tr < kNumTracks; ++tr)
            for (auto& st : proc->sequence().tracks[tr].steps) st.trig = false;

        auto& t = proc->sequence().tracks[1];
        t.length = 16;
        const int chords[4][4] = { { 60, 63, 65, 67 }, { 60, 63, 68, 72 },
                                   { 58, 60, 65, 68 }, { 60, 63, 65, 67 } };
        for (int c = 0; c < 4; ++c)
        {
            auto& s = t.steps[static_cast<std::size_t>(c * 4)];
            s.trig = true;
            s.trigOverride.noteCount = 4;
            for (int n = 0; n < 4; ++n)
                s.trigOverride.notes[static_cast<std::size_t>(n)] = chords[c][n];
            s.trigOverride.hasGate = true;
            s.trigOverride.gateValue = MusicalGate::G1;
        }

        // The divider's owner is the APVTS param, and it must be written AFTER the
        // last reinstallMachinesFromActiveKit -- that call re-syncs track params
        // from the kit and silently reverts an earlier write, leaving the track on
        // sixteenths and the fixture testing nothing.
        if (auto* dv = proc->apvts().getParameter(ParamIDs::trackDivider(1)))
        {
            dv->beginChangeGesture();
            dv->setValueNotifyingHost(dv->convertTo0to1(12.0f));   // quarter notes
            dv->endChangeGesture();
        }

        proc->clock().setInPluginPlaying(true);

        Result r;
        const int blocks = static_cast<int>(seconds * kSR / kBlk);
        for (int b = 0; b < blocks; ++b)
        {
            rig.renderBlocks(1);

            std::set<int> onThisBlock;
            int killed = 0;
            for (const auto meta : rig.midiOut())
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())
                {
                    onThisBlock.insert(m.getNoteNumber());
                    ++r.chordsSeen;
                }
                else if (m.isNoteOff() && onThisBlock.count(m.getNoteNumber()) > 0)
                {
                    ++killed;   // this note-off cancels a note-on from the same block
                }
            }
            if (killed > 0)
            {
                ++r.invertedBlocks;
                r.worstNotesKilled = std::max(r.worstNotesKilled, killed);
            }
        }
        return r;
    }
}   // namespace

void runNoteOrderTests()
{
    for (const double bpm : { 88.23974935925473, 88.24 })
    {
        const auto r = runChordPattern(bpm, 60.0);

        // Self-check first: a scan that stopped finding notes would report a clean
        // result forever. 60 s at ~2.7 s per chord is ~22 chords, 4 notes each.
        CHECK(r.chordsSeen > 60,
              juce::String("the chord track actually played (") + juce::String(r.chordsSeen)
                  + " note-ons at " + juce::String(bpm, 6) + " BPM)");

        CHECK(r.invertedBlocks == 0,
              juce::String("no note-off follows a note-on of the same pitch in one block @ ")
                  + juce::String(bpm, 8) + " BPM ("
                  + juce::String(r.invertedBlocks) + " inverted blocks, worst "
                  + juce::String(r.worstNotesKilled)
                  + " notes cancelled -- a machine that starts notes immediately "
                    "loses exactly those)");
    }
}
}   // namespace lockstep
