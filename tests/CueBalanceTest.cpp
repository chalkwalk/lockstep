// CueBalanceTest -- 6.4 per-track cue balance overlay (DESIGN §31).
//
// Task 1 covers the state layer: the cueBalance APVTS param + accessors and
// its free serialization round-trip. Audio-path cases (crossfade, send fade,
// tap semantics) are added by Tasks 2/3.
//
// NB: LockstepProcessor embeds the ~47 MB Arrangement — always heap-allocate
// it (a stack instance overflows the stack; see project memory).

#include "TestHarness.h"
#include "../src/PluginProcessor.h"
#include <memory>

namespace lockstep
{
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
    void runCueBalanceTests()
    {
        testCueBalanceDefaultAndSet();
        testCueBalanceToggle();
        testCueBalanceRoundTrip();
    }
}
