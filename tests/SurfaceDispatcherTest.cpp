// SurfaceDispatcherTest — the coalescing contract for the single surface
// invalidation channel (PRINCIPLES §22, DESIGN §35.9).
//
// The invariant under test: any number of invalidate() calls within one
// message-loop cycle collapse into exactly one frame (onFrame callback). We
// drive delivery synchronously via flushIfPending() (JUCE's
// handleUpdateNowIfNeeded), so the contract is asserted headless without
// spinning a real message loop. main() has already called initialiseJuce_GUI(),
// so a MessageManager exists on this (message) thread.

#include "TestHarness.h"
#include "../src/ui/SurfaceDispatcher.h"

namespace lockstep
{
    // N invalidate() in one cycle ⇒ one frame; a flush with nothing pending is a
    // no-op (no spurious frames).
    static void testCoalescesToOneFrame()
    {
        int frames = 0;
        SurfaceDispatcher d{ [&frames] { ++frames; } };

        CHECK(!d.isPending(), "fresh dispatcher has nothing pending");

        for (int i = 0; i < 5; ++i)
            d.invalidate();

        CHECK(d.isPending(), "invalidate() marks a frame pending");
        d.flushIfPending();
        CHECK(frames == 1, "five invalidate()s in one cycle deliver one frame");
        CHECK(!d.isPending(), "delivery clears the pending flag");

        // A second flush with nothing queued must not produce a frame.
        d.flushIfPending();
        CHECK(frames == 1, "flush with nothing pending is a no-op");
    }

    // A fresh invalidate() after a delivered frame produces the next frame —
    // coalescing is per-cycle, not a one-shot latch.
    static void testReArmsAfterDelivery()
    {
        int frames = 0;
        SurfaceDispatcher d{ [&frames] { ++frames; } };

        d.invalidate();
        d.flushIfPending();
        CHECK(frames == 1, "first cycle delivers one frame");

        d.invalidate();
        d.flushIfPending();
        CHECK(frames == 2, "a new invalidate after delivery re-arms the channel");
    }

    // Never invalidating means never painting — an idle surface issues no frames.
    static void testIdleProducesNoFrame()
    {
        int frames = 0;
        SurfaceDispatcher d{ [&frames] { ++frames; } };

        d.flushIfPending();
        CHECK(frames == 0, "no invalidate ⇒ no frame");
        CHECK(!d.isPending(), "idle dispatcher stays not-pending");
    }

    // A pending frame that is never delivered must not fire after destruction
    // (the dtor cancels it) — this just exercises that path without crashing.
    static void testPendingAtDestructionIsCancelled()
    {
        int frames = 0;
        {
            SurfaceDispatcher d{ [&frames] { ++frames; } };
            d.invalidate();
            CHECK(d.isPending(), "frame is queued");
            // leave scope without flushing
        }
        CHECK(frames == 0, "an undelivered frame does not fire after teardown");
    }

    void runSurfaceDispatcherTests()
    {
        testCoalescesToOneFrame();
        testReArmsAfterDelivery();
        testIdleProducesNoFrame();
        testPendingAtDestructionIsCancelled();
    }
}
