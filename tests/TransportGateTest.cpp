// TransportGateTest -- v27 hosted-Locked transport AND-gate + restart re-floor.
//
// Covers (Item 4 of the section-stack/transport cleanup):
//  1. Arm gate: with the host playing, a parked (disarmed) plugin emits no
//     trigs; arming resumes them. Toggling arm never shifts step alignment vs
//     host PPQ (phase is always host-derived, never a phase reset).
//  2. Restart re-floor: a stop -> forward-relocate -> restart fires the correct
//     step in the first block after the run rising edge (Clock::ppqJumped()
//     only catches backward jumps, so the hosted rising edge must re-floor),
//     including a conditional 1:2 trig whose iteration history was cleared.
//
// Uses MidiOutMachine so emitted note-ons are observable in the output MIDI
// buffer (a Drum consumes its note internally). Harness note: EngineHarness
// runs as a hosted plugin (wrapperType is Undefined, not Standalone) and the
// APVTS sync mode defaults to Locked (index 0) -- exactly the regime the arm
// gate governs. setRateAndBufferSizeDetails() is done by the harness ctor.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/machine/MidiOutMachine.h"

namespace lockstep
{
    // Install a machine by cloning DrumMachine's schema size onto the kit.
    // (MidiOutMachine's schema is a subset; we resize to its own numParams.)
    static void installMidiOut(LockstepProcessor& proc, int track)
    {
        MidiOutMachine tmp;
        const int np = tmp.numParams();
        auto& k = proc.kit(track);
        k.machineId = MidiOutMachine::kMachineId;
        k.baseParams.resize(static_cast<std::size_t>(np));
        for (int i = 0; i < np; ++i)
            k.baseParams[static_cast<std::size_t>(i)] = tmp.paramSpec(i).defaultValue;
        proc.reinstallMachinesFromActiveKit();
    }

    // Arm a single step-0 note on track `t`.
    static void armStepZeroNote(LockstepProcessor& proc, int t)
    {
        auto& s0 = proc.sequence().tracks[static_cast<std::size_t>(t)].steps[0];
        s0.trig = true;
        s0.trigOverride.noteCount = 1;
        s0.trigOverride.notes[0] = 60;
        s0.trigOverride.hasGate = true;
        s0.trigOverride.gateValue = MusicalGate::G1_16;
    }

    // Count note-ons over `blocks` rendered blocks; also record the host PPQ of
    // each note-on so we can assert grid alignment. The block just processed by
    // renderBlocks(1) started at ppqPosition()-ppqPerBlock.
    struct OnScan { int count = 0; std::vector<double> ppqs; };
    static OnScan scanOns(EngineHarness& h, int blocks)
    {
        OnScan r;
        const double perBlock = h.playHead().ppqPerBlock();
        const int bs = EngineHarness::kBlockSize;
        for (int b = 0; b < blocks; ++b)
        {
            h.renderBlocks(1);
            const double startPpq = h.playHead().ppqPosition() - perBlock;
            for (const auto meta : h.midiOut())
            {
                if (meta.getMessage().isNoteOn())
                {
                    ++r.count;
                    const double frac = static_cast<double>(meta.samplePosition)
                                        / static_cast<double>(bs);
                    r.ppqs.push_back(startPpq + frac * perBlock);
                }
            }
        }
        return r;
    }

    // ------------------------------------------------------------------------
    // 1. Disarmed = silent under a running host; armed = fires; arm never shifts
    //    step alignment (all note-ons stay on the track's PPQ grid).
    static void testArmGateSilencesAndPreservesPhase()
    {
        // Track period = length(4) * (1/16 = 0.25 ppq) = 1.0 ppq. Step 0 only,
        // so note-ons must land at ppq ~= integer multiples of 1.0.
        constexpr double kPeriod = 1.0;
        constexpr double kTol = 0.02;  // << one block (0.0107 ppq)

        // (a) Parked from the start: host playing, plugin disarmed -> no trigs.
        {
            EngineHarness h;
            installMidiOut(h.processor(), 0);
            h.processor().setTrackLength(0, 4);
            armStepZeroNote(h.processor(), 0);
            h.processor().setPluginArmed(false);
            const auto scan = scanOns(h, 250);  // ~2.6 ppq, would be 2-3 notes if armed
            CHECK(scan.count == 0, "arm gate: disarmed plugin emits trigs under a running host");
        }

        // (b) Armed continuously: fires, and every note-on is grid-aligned.
        std::size_t armedNotes = 0;
        {
            EngineHarness h;
            installMidiOut(h.processor(), 0);
            h.processor().setTrackLength(0, 4);
            armStepZeroNote(h.processor(), 0);
            CHECK(h.processor().isPluginArmed(), "arm gate: fresh instance defaults to armed");
            const auto scan = scanOns(h, 250);
            CHECK(scan.count >= 2, "arm gate: armed plugin does not fire under a running host");
            armedNotes = scan.ppqs.size();
            for (double p : scan.ppqs)
            {
                const double off = std::abs(p - std::round(p / kPeriod) * kPeriod);
                CHECK(off < kTol, "arm gate: armed note-on off the PPQ grid (phase drift)");
            }
        }

        // (c) Toggle arm mid-run: park for a stretch, then re-arm. Note-ons must
        //     still land on the same PPQ grid -- arming is not a phase reset.
        {
            EngineHarness h;
            installMidiOut(h.processor(), 0);
            h.processor().setTrackLength(0, 4);
            armStepZeroNote(h.processor(), 0);
            (void) scanOns(h, 60);                 // armed, host advancing
            h.processor().setPluginArmed(false);
            (void) scanOns(h, 120);                // parked, host keeps moving
            h.processor().setPluginArmed(true);
            const auto after = scanOns(h, 250);
            CHECK(after.count >= 2, "arm gate: re-arm did not resume firing");
            for (double p : after.ppqs)
            {
                const double off = std::abs(p - std::round(p / kPeriod) * kPeriod);
                CHECK(off < kTol, "arm gate: re-arm shifted step alignment vs host PPQ");
            }
            CHECK(armedNotes > 0, "arm gate: precondition (armed run produced notes)");
        }
    }

    // ------------------------------------------------------------------------
    // 2. stop -> forward relocate -> restart re-floors cursors and fires the
    //    correct step in the first block, including a conditional 1:2 trig.
    static void testForwardRestartRefloorsAndFires()
    {
        EngineHarness h;
        installMidiOut(h.processor(), 0);
        // Default length 16, 1/16 grid -> pattern period 4.0 ppq. Step 0 only,
        // with a 1:2 iteration condition (fires on even iterations: 0, 2, 4...).
        armStepZeroNote(h.processor(), 0);
        auto& s0 = h.processor().sequence().tracks[0].steps[0];
        s0.condition.iterNumerator = 1;
        s0.condition.iterDenominator = 2;

        // Run a bit from ppq 0 (step 0 fires at ppq 0, iter 0 -> passes 1:2).
        const auto initial = scanOns(h, 40);
        CHECK(initial.count >= 1, "restart: step 0 did not fire on the initial run");

        // Host stop: park the transport (no trigs while stopped).
        h.playHead().setPlaying(false);
        (void) scanOns(h, 5);

        // Relocate forward to ppq 8.0 == 2 patterns == step 0 at iteration 2
        // (even -> 1:2 fires). This is a FORWARD jump: Clock::ppqJumped() will
        // NOT catch it, so only the hosted run-rising-edge re-floor can save it.
        h.playHead().setPpq(8.0);
        h.playHead().setPlaying(true);

        // First block after the rising edge must fire step 0.
        h.renderBlocks(1);
        int onsFirstBlock = 0;
        for (const auto meta : h.midiOut())
            if (meta.getMessage().isNoteOn()) ++onsFirstBlock;
        CHECK(onsFirstBlock >= 1,
              "restart: forward relocate + restart did not fire step 0 in the first block");
        CHECK(!h.lastBufferHasNaN(), "restart: NaN after re-floor");
    }

    // ------------------------------------------------------------------------
    // 3. Arm flag serialization. The flag is written only when parked (armed =
    //    property absent), so an armed save produces a tree with no arm
    //    property -- byte-for-byte what a pre-v27 (v26) tree looks like. Loading
    //    such a tree must default to armed; a parked save round-trips as parked.
    static void testArmSerializationRoundTrip()
    {
        // (a) Armed save omits the property; loading it defaults to armed even
        //     into an instance we deliberately pre-park (== v26 "missing -> armed").
        juce::MemoryBlock armedState;
        {
            EngineHarness h;  // fresh instance defaults to armed
            CHECK(h.processor().isPluginArmed(), "arm serialize: fresh instance armed");
            h.processor().getStateInformation(armedState);
        }
        {
            EngineHarness h;
            h.processor().setPluginArmed(false);  // pre-park to prove the load re-arms
            h.processor().setStateInformation(armedState.getData(),
                                              static_cast<int>(armedState.getSize()));
            CHECK(h.processor().isPluginArmed(),
                  "arm serialize: armed/v26 state (no arm property) must load armed");
        }

        // (b) Parked save round-trips as parked.
        juce::MemoryBlock parkedState;
        {
            EngineHarness h;
            h.processor().setPluginArmed(false);
            h.processor().getStateInformation(parkedState);
        }
        {
            EngineHarness h;  // starts armed
            h.processor().setStateInformation(parkedState.getData(),
                                              static_cast<int>(parkedState.getSize()));
            CHECK(!h.processor().isPluginArmed(),
                  "arm serialize: parked state must load parked");
        }
    }

    // ------------------------------------------------------------------------
    void runTransportGateTests()
    {
        testArmGateSilencesAndPreservesPhase();
        testForwardRestartRefloorsAndFires();
        testArmSerializationRoundTrip();
    }
}
