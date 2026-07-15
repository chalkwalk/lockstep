// CueBalanceTest -- 6.4 per-track cue balance overlay (DESIGN §31).
//
// Task 1 covers the state layer: the cueBalance APVTS param + accessors and
// its free serialization round-trip. Audio-path cases (crossfade, send fade,
// tap semantics) are added by Tasks 2/3.
//
// NB: LockstepProcessor embeds the ~47 MB Arrangement — always heap-allocate
// it (a stack instance overflows the stack; see project memory).

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/PluginProcessor.h"
#include "../src/machine/DrumMachine.h"
#include "../src/machine/RouteMachine.h"
#include "../src/machine/InputSource.h"
#include <memory>

namespace lockstep
{
    // ---- audio-path helpers ------------------------------------------------

    // Bus channel offsets in the flat processBlock buffer: main = [0,1],
    // Cue = [2,3] (each bus is stereo; Cue is host output bus 1).
    static constexpr int kMainCh0 = 0;
    static constexpr int kCueCh0  = 2;

    static double chEnergy(const juce::AudioBuffer<float>& buf, int ch0, int nCh)
    {
        double e = 0.0;
        for (int c = ch0; c < ch0 + nCh && c < buf.getNumChannels(); ++c)
        {
            const auto* d = buf.getReadPointer(c);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                e += static_cast<double>(d[i]) * static_cast<double>(d[i]);
        }
        return e;
    }

    static void installDrum(LockstepProcessor& proc, int track)
    {
        DrumMachine tmp;
        const int np = tmp.numParams();
        auto& k = proc.kit(track);
        k.machineId = DrumMachine::kMachineId;
        k.baseParams.resize(static_cast<std::size_t>(np));
        for (int i = 0; i < np; ++i)
            k.baseParams[static_cast<std::size_t>(i)] = tmp.paramSpec(i).defaultValue;
        proc.reinstallMachinesFromActiveKit();
    }

    static void armStep0(LockstepProcessor& proc, int track)
    {
        auto& s0 = proc.sequence().tracks[static_cast<std::size_t>(track)].steps[0];
        s0.trig = true;
        s0.trigOverride.hasGate = true;
        s0.trigOverride.gateValue = MusicalGate::G1_8;
    }

    // Install a RouteMachine on `track` whose input_source is a direct tap of
    // `srcTrack`'s post-chain output (DESIGN §27). RouteMachine's process() is a
    // passthrough; the processor fills the buffer from the tap upstream.
    static void installTapOfTrack(LockstepProcessor& proc, int track, int srcTrack)
    {
        RouteMachine tmp;
        const int np = tmp.numParams();
        auto& k = proc.kit(track);
        k.machineId = RouteMachine::kMachineId;
        k.baseParams.resize(static_cast<std::size_t>(np));
        for (int i = 0; i < np; ++i)
            k.baseParams[static_cast<std::size_t>(i)] = tmp.paramSpec(i).defaultValue;
        const int slot = tmp.slotForId(kInputSourceSlotId);
        k.baseParams[static_cast<std::size_t>(slot)] =
            encodeInputSource(InputSourceKind::Track, srcTrack);
        proc.reinstallMachinesFromActiveKit();
    }

    // -----------------------------------------------------------------------
    static void testCueBalanceDefaultAndSet()
    {
        auto proc = std::make_unique<LockstepProcessor>();
        CHECK(feq(proc->getCueBalance(0), 0.0f), "cue balance default should be 0");

        proc->setCueBalance(0, 0.75f);
        CHECK(feq(proc->getCueBalance(0), 0.75f, 1e-4f), "cue balance should read back 0.75");

        // Clamp out of range.
        proc->setCueBalance(0, 2.0f);
        CHECK(feq(proc->getCueBalance(0), 1.0f, 1e-4f), "cue balance should clamp to 1");
        proc->setCueBalance(0, -1.0f);
        CHECK(feq(proc->getCueBalance(0), 0.0f, 1e-4f), "cue balance should clamp to 0");

        // Out-of-range track index is a safe no-op / zero.
        CHECK(feq(proc->getCueBalance(-1), 0.0f), "cue balance for bad track is 0");
        CHECK(feq(proc->getCueBalance(9999), 0.0f), "cue balance for bad track is 0");
        proc->setCueBalance(-1, 0.5f);  // must not crash
    }

    // -----------------------------------------------------------------------
    static void testCueBalanceToggle()
    {
        auto proc = std::make_unique<LockstepProcessor>();
        CHECK(feq(proc->getCueBalance(1), 0.0f), "start uncued");
        proc->toggleCueBalance(1);
        CHECK(feq(proc->getCueBalance(1), 1.0f, 1e-4f), "toggle 0 -> 1");
        proc->toggleCueBalance(1);
        CHECK(feq(proc->getCueBalance(1), 0.0f, 1e-4f), "toggle 1 -> 0");

        // A partial value toggles toward off (>0 counts as cued).
        proc->setCueBalance(2, 0.3f);
        proc->toggleCueBalance(2);
        CHECK(feq(proc->getCueBalance(2), 0.0f, 1e-4f), "partial cue toggles off");
    }

    // -----------------------------------------------------------------------
    static void testCueBalanceRoundTrip()
    {
        auto proc = std::make_unique<LockstepProcessor>();
        proc->setCueBalance(0, 1.0f);
        proc->setCueBalance(3, 0.5f);

        juce::MemoryBlock state;
        proc->getStateInformation(state);

        auto fresh = std::make_unique<LockstepProcessor>();
        fresh->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        CHECK(feq(fresh->getCueBalance(0), 1.0f, 1e-4f), "cue balance 0 survives round-trip");
        CHECK(feq(fresh->getCueBalance(3), 0.5f, 1e-4f), "cue balance 3 survives round-trip");
        CHECK(feq(fresh->getCueBalance(1), 0.0f, 1e-4f), "untouched track stays 0");
    }

    // -----------------------------------------------------------------------
    // Crossfade: a Master-routed drum track producing a known trig. b=0 keeps it
    // in the main mix (cue silent); b=1 moves it fully to the Cue bus (main
    // silent); b=0.5 splits. Energy is summed over a window long compared to the
    // ~5 ms declick so the first-block ramp does not skew the comparison.
    static void measureSplit(float b, double& mainE, double& cueE)
    {
        EngineHarness h;
        installDrum(h.processor(), 0);
        armStep0(h.processor(), 0);
        h.processor().setCueBalance(0, b);

        mainE = 0.0;
        cueE = 0.0;
        for (int blk = 0; blk < 24; ++blk)
        {
            h.renderBlocks(1);
            mainE += chEnergy(h.buffer(), kMainCh0, 2);
            cueE += chEnergy(h.buffer(), kCueCh0, 2);
        }
    }

    static void testCueCrossfade()
    {
        double main0 = 0, cue0 = 0, main1 = 0, cue1 = 0, mainH = 0, cueH = 0;
        measureSplit(0.0f, main0, cue0);
        measureSplit(1.0f, main1, cue1);
        measureSplit(0.5f, mainH, cueH);

        // b=0: audible in main, essentially nothing on cue.
        CHECK(main0 > 1e-3, "b=0: track should reach the main mix");
        CHECK(cue0 < main0 * 0.02, "b=0: cue bus should be silent");

        // b=1: gone from main, present on cue (allow a small first-block ramp leak).
        CHECK(cue1 > 1e-3, "b=1: track should reach the cue bus");
        CHECK(main1 < main0 * 0.05, "b=1: track should leave the main mix");

        // b=1 cue ~= b=0 main (same signal, moved), within the ramp/measurement slop.
        CHECK(cue1 > main0 * 0.6 && cue1 < main0 * 1.4,
              "b=1 cue energy should match b=0 main energy");

        // b=0.5: both carry a meaningful share.
        CHECK(mainH > main0 * 0.1 && cueH > main0 * 0.1,
              "b=0.5: signal should split between main and cue");
    }

    // -----------------------------------------------------------------------
    // Send fade: a Master-routed track with Send A into a master reverb. At b=0
    // main carries dry + wet return; at b=1 BOTH fade — if sends did not scale by
    // (1-b) the reverb WET would persist in main, so a tiny main at b=1 proves
    // the send faded (not just the dry route). Cue carries the dry (PFL), no wet.
    static void testCueSendFade()
    {
        auto run = [](float b, double& mainE, double& cueE)
        {
            EngineHarness h;
            installDrum(h.processor(), 0);
            armStep0(h.processor(), 0);
            h.processor().kit(0).channelState.sendA = 0.9f;
            h.processor().setMasterSend(0, "lockstep.reverb.v1");
            h.processor().setCueBalance(0, b);
            mainE = 0.0; cueE = 0.0;
            for (int blk = 0; blk < 24; ++blk)
            {
                h.renderBlocks(1);
                mainE += chEnergy(h.buffer(), kMainCh0, 2);
                cueE += chEnergy(h.buffer(), kCueCh0, 2);
            }
        };

        double main0 = 0, cue0 = 0, main1 = 0, cue1 = 0;
        run(0.0f, main0, cue0);
        run(1.0f, main1, cue1);

        CHECK(main0 > 1e-3, "send: b=0 main carries dry + reverb return");
        // b=1: dry route AND send both faded — main is near-silent. If the send
        // were not cue-scaled, the reverb wet would keep main well above this.
        CHECK(main1 < main0 * 0.08, "send: b=1 should remove dry AND send from main");
        // Cue still gets the dry signal (post-insert, pre-master-send — no wet).
        CHECK(cue1 > 1e-3, "send: b=1 cue carries the dry signal");
    }

    // -----------------------------------------------------------------------
    // Direct track tap bypasses cue. Track 0 (A) is Master-routed, trig'd, and
    // fully cued (b=1) — so A itself leaves the audience mix. Track 1 (B) is a
    // RouteMachine tapping A, Master-routed and NOT cued. Because the tap reads
    // A's post-chain buffer UPSTREAM of the cue split, B relays A's full signal
    // into the main mix: main energy stays high even though A is cued away.
    static void testCueDirectTapBypasses()
    {
        auto measure = [](bool tap, float aCue, double& mainE, double& cueE)
        {
            EngineHarness h;
            installDrum(h.processor(), 0);
            armStep0(h.processor(), 0);
            if (tap) installTapOfTrack(h.processor(), 1, 0);  // B taps A
            h.processor().setCueBalance(0, aCue);
            mainE = 0.0; cueE = 0.0;
            for (int blk = 0; blk < 24; ++blk)
            {
                h.renderBlocks(1);
                mainE += chEnergy(h.buffer(), kMainCh0, 2);
                cueE += chEnergy(h.buffer(), kCueCh0, 2);
            }
        };

        double refMain = 0, refCue = 0;   // A uncued, no tap: A's own main energy.
        measure(false, 0.0f, refMain, refCue);

        double tapMain = 0, tapCue = 0;   // A cued away, B taps A.
        measure(true, 1.0f, tapMain, tapCue);

        // A is cued, so it is present on the cue bus...
        CHECK(tapCue > refMain * 0.6, "tap: cued A still reaches the cue bus");
        // ...AND B's upstream tap relays A's full signal into main. If the tap
        // saw the cue-scaled (silent) A, main would collapse to ~0 here.
        CHECK(tapMain > refMain * 0.6,
              "tap: direct track tap bypasses cue (B relays full A into main)");
    }

    // -----------------------------------------------------------------------
    // Composition invariant (spec §4): cue only removes signal from the
    // master-reaching path — it never adds a routing edge — so
    // outputReachesMaster() is static under any balance.
    // (Master-tap-reflects-cue is proven by testCueCrossfade: at b=1 the main
    // bus, i.e. the master sum a Master tap reads, excludes the cued track.)
    static void testOutputReachesMasterStatic()
    {
        auto proc = std::make_unique<LockstepProcessor>();
        installDrum(*proc, 0);                     // Master-routed by default
        CHECK(proc->outputReachesMaster(0), "Master-routed track reaches master");
        proc->setCueBalance(0, 1.0f);
        CHECK(proc->outputReachesMaster(0),
              "cueing a Master track does not change outputReachesMaster");
        proc->setCueBalance(0, 0.5f);
        CHECK(proc->outputReachesMaster(0), "partial cue leaves the edge intact");
    }

    // -----------------------------------------------------------------------
    void runCueBalanceTests()
    {
        testCueBalanceDefaultAndSet();
        testCueBalanceToggle();
        testCueBalanceRoundTrip();
        testCueCrossfade();
        testCueSendFade();
        testCueDirectTapBypasses();
        testOutputReachesMasterStatic();
    }
}
