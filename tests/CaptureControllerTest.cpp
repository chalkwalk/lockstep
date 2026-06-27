#include "TestHarness.h"
#include "../src/io/CaptureController.h"

namespace lockstep
{
    using Phase = CaptureController::Phase;
    using Out   = CaptureController::Out;

    static constexpr float  kLoud  = 0.5f;
    static constexpr float  kQuiet = 0.0f;
    static constexpr double kTail  = CaptureController::kTailMs;

    // Drive a recording from Idle: tap-while-stopped arms, transport rise starts.
    static void startViaArm(CaptureController& c, double& t)
    {
        c.onTap(/*transportPlaying=*/false);                 // Idle -> Armed
        const Out o = c.tick(t, kLoud, /*playing=*/true);    // Armed: requests start
        CHECK(o.start, "armed + transport play must request start");
        c.onStarted(t);
        CHECK(c.phase() == Phase::Recording, "after onStarted -> Recording");
    }

    // ── Start ─────────────────────────────────────────────────────────────────

    static void testTapStoppedArms()
    {
        CaptureController c;
        const Out o = c.onTap(/*transportPlaying=*/false);
        CHECK(!o.start, "tap while stopped does not start immediately");
        CHECK(c.phase() == Phase::Armed, "tap while stopped arms");
    }

    static void testTapWhileRunningStartsNow()
    {
        CaptureController c;
        const Out o = c.onTap(/*transportPlaying=*/true);
        CHECK(o.start, "tap while transport already running starts immediately");
        CHECK(c.phase() == Phase::Idle, "phase stays until onStarted confirms");
        c.onStarted(0.0);
        CHECK(c.phase() == Phase::Recording, "edge case: tap-while-running records");
    }

    static void testDoubleTapRollsNow()
    {
        CaptureController c;
        const Out o = c.onDoubleTap();
        CHECK(o.start, "double-tap rolls immediately (tape)");
    }

    static void testArmDisarm()
    {
        CaptureController c;
        c.onTap(false);                       // -> Armed
        const Out o = c.onTap(false);         // tap in Armed = disarm
        CHECK(!o.start, "disarm requests nothing");
        CHECK(c.phase() == Phase::Idle, "tap in Armed disarms");
    }

    static void testStartFailedReturnsToIdle()
    {
        CaptureController c;
        c.onTap(true);            // requests start
        c.onStartFailed();
        CHECK(c.phase() == Phase::Idle, "failed start returns to Idle");
    }

    // ── Finalize rule (the heart) ──────────────────────────────────────────────

    static void testPlayingSilenceNeverFinalizes()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        // Transport keeps playing; audio goes silent for far longer than the tail
        // (a dramatic musical rest). Must NOT finalize.
        for (int i = 0; i < 200; ++i)
        {
            t += 33.0;
            const Out o = c.tick(t, kQuiet, /*playing=*/true);
            CHECK(!o.finalize, "silence during active playback must never finalize");
        }
        CHECK(c.phase() == Phase::Recording, "still recording through the rest");
        CHECK(!c.windingDown(), "not winding down while transport plays");
    }

    static void testTransportStopThenSilenceFinalizes()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        // Stop edge: transport falls. Now winding down.
        t += 33.0;
        c.tick(t, kLoud, /*playing=*/false);
        CHECK(c.windingDown(), "transport stop arms the tail");
        // Tail rings (still loud) just under the threshold time — no finalize yet.
        bool finalizedEarly = false;
        const double tStop = t;
        while (t < tStop + kTail - 100.0)
        {
            t += 33.0;
            if (c.tick(t, kLoud, false).finalize) finalizedEarly = true;
        }
        CHECK(!finalizedEarly, "loud tail must not finalize before silence");
        // Now go quiet; after the hold, finalize.
        const double tQuiet = t;
        bool finalized = false;
        while (t < tQuiet + kTail + 100.0)
        {
            t += 33.0;
            if (c.tick(t, kQuiet, false).finalize) finalized = true;
        }
        CHECK(finalized, "quiet for the tail after stop finalizes");
    }

    static void testQuietRecoversBeforeTailKeepsRecording()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        t += 33.0; c.tick(t, kLoud, false);           // stop edge -> winding
        // Quiet for half the tail, then audio returns -> timer resets, no finalize.
        const double tQuiet = t;
        while (t < tQuiet + kTail * 0.5) { t += 33.0; c.tick(t, kQuiet, false); }
        bool finalized = false;
        const double tBack = t;
        while (t < tBack + kTail * 0.9)
        {
            t += 33.0;
            if (c.tick(t, kLoud, false).finalize) finalized = true;   // audio present
        }
        CHECK(!finalized, "audio returning before the tail resets the silence timer");
        CHECK(c.phase() == Phase::Recording, "still recording after a brief gap");
    }

    static void testPlayEdgeCancelsWinding()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        t += 33.0; c.tick(t, kQuiet, false);          // stop edge -> winding
        CHECK(c.windingDown(), "winding after stop");
        t += 33.0; c.tick(t, kLoud, true);            // play edge -> resume
        CHECK(!c.windingDown(), "play edge cancels winding");
    }

    static void testTapStopWhilePlayingWindsButWaitsForSilence()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        c.onTap(/*transportPlaying=*/true);           // tap-stop while still playing
        t += 33.0; c.tick(t, kLoud, true);
        CHECK(c.windingDown(), "tap-stop arms the tail even while playing");
        // Still loud + playing: does not finalize until quiet.
        bool finalized = false;
        const double t0 = t;
        while (t < t0 + kTail + 200.0)
        {
            t += 33.0;
            if (c.tick(t, kLoud, true).finalize) finalized = true;
        }
        CHECK(!finalized, "tap-stop does not cut while audio continues");
    }

    static void testDoubleTapHardCutFinalizesImmediately()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        const Out o = c.onDoubleTap();                 // hard cut
        CHECK(o.finalize, "double-tap while recording finalizes now (drone case)");
    }

    // ── Just-saved window ──────────────────────────────────────────────────────

    static void testFinalizeEntersJustSavedThenLapses()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        c.onFinalized(t);
        CHECK(c.phase() == Phase::JustSaved, "finalize -> JustSaved");
        // Within the window: still JustSaved.
        c.tick(t + 1000.0, kQuiet, false);
        CHECK(c.phase() == Phase::JustSaved, "still JustSaved within window");
        // After the window: lapses to Idle.
        c.tick(t + CaptureController::kJustSavedMs + 1.0, kQuiet, false);
        CHECK(c.phase() == Phase::Idle, "JustSaved lapses to Idle, take kept");
    }

    static void testLongPressDiscardsInWindow()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        c.onFinalized(t);
        const Out o = c.onLongPress();
        CHECK(o.discard, "long-press in JustSaved discards");
        CHECK(c.phase() == Phase::Idle, "discard returns to Idle");
    }

    static void testTapInWindowArmsNext()
    {
        CaptureController c;
        double t = 1000.0;
        startViaArm(c, t);
        c.onFinalized(t);
        const Out o = c.onTap(/*transportPlaying=*/false);
        CHECK(!o.discard, "tap in window does not discard the prior take");
        CHECK(c.phase() == Phase::Armed, "tap in window arms the next take");
    }

    static void testLongPressIdleReveals()
    {
        CaptureController c;
        const Out o = c.onLongPress();
        CHECK(o.reveal, "long-press while idle reveals the folder");
    }

    void runCaptureControllerTests()
    {
        testTapStoppedArms();
        testTapWhileRunningStartsNow();
        testDoubleTapRollsNow();
        testArmDisarm();
        testStartFailedReturnsToIdle();
        testPlayingSilenceNeverFinalizes();
        testTransportStopThenSilenceFinalizes();
        testQuietRecoversBeforeTailKeepsRecording();
        testPlayEdgeCancelsWinding();
        testTapStopWhilePlayingWindsButWaitsForSilence();
        testDoubleTapHardCutFinalizesImmediately();
        testFinalizeEntersJustSavedThenLapses();
        testLongPressDiscardsInWindow();
        testTapInWindowArmsNext();
        testLongPressIdleReveals();
    }
}
