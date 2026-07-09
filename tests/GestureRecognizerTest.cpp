#include "TestHarness.h"
#include "../src/ui/mode/GestureRecognizer.h"

namespace lockstep
{
    using LPR = GestureRecognizer::LongPressResult;

    // ── doubleTap ─────────────────────────────────────────────────────────────

    static void testDoubleTapHitsOnSecond()
    {
        GestureRecognizer g;
        const double t0 = 1000.0;
        CHECK(!g.doubleTap(42, t0),              "first tap must not fire");
        CHECK( g.doubleTap(42, t0 + 200.0),     "second tap within threshold must fire");
    }

    static void testDoubleTapMissesOnThird()
    {
        GestureRecognizer g;
        const double t0 = 1000.0;
        g.doubleTap(42, t0);
        CHECK(g.doubleTap(42, t0 + 100.0),       "second tap fires");
        CHECK(!g.doubleTap(42, t0 + 200.0),      "third tap must not fire (detector reset)");
    }

    static void testDoubleTapMissesAfterThreshold()
    {
        GestureRecognizer g;
        const double t0 = 1000.0;
        g.doubleTap(42, t0);
        CHECK(!g.doubleTap(42, t0 + GestureRecognizer::kDoubleTapMs + 1.0),
              "tap after threshold must not fire");
    }

    static void testDoubleTapDifferentTokensDoNotInteract()
    {
        GestureRecognizer g;
        const double t0 = 1000.0;
        g.doubleTap(1, t0);
        CHECK(!g.doubleTap(2, t0 + 100.0),       "different token must not fire double-tap");
    }

    static void testInvalidatePreventsDoubleTap()
    {
        GestureRecognizer g;
        const double t0 = 1000.0;
        g.doubleTap(99, t0);
        g.invalidate();
        CHECK(!g.doubleTap(99, t0 + 100.0),      "after invalidate second tap must not fire");
    }

    static void testNavRightUnlockTokenValue()
    {
        // The named constant must equal the legacy literal so any serialised or
        // config-stored reference to 4000 stays consistent.
        CHECK(GestureRecognizer::kNavRightUnlock == 4000,
              "kNavRightUnlock must be 4000 for backwards compatibility");
    }

    // ── playDoubleTap ─────────────────────────────────────────────────────────

    static void testPlayDoubleTapHitsWithinThreshold()
    {
        GestureRecognizer g;
        g.playDoubleTap(1000.0);
        CHECK(g.playDoubleTap(1000.0 + 200.0),   "second play tap within threshold must fire");
    }

    static void testPlayDoubleTapMissesAfterThreshold()
    {
        GestureRecognizer g;
        g.playDoubleTap(1000.0);
        CHECK(!g.playDoubleTap(1000.0 + GestureRecognizer::kDoubleTapMs + 1.0),
              "second play tap after threshold must not fire");
    }

    static void testPlayDoubleTapIndependentFromTokenPool()
    {
        // A regular doubleTap between two play presses must not block playDoubleTap.
        GestureRecognizer g;
        g.playDoubleTap(1000.0);
        g.doubleTap(77, 1100.0);   // intervening non-play tap
        CHECK(g.playDoubleTap(1200.0),           "play double-tap survives intervening token press");
    }

    // ── playTapCount (layered stop: 1=graceful, 2=track cut, 3=master cut) ──────

    static void testPlayTapCountEscalatesWithinWindow()
    {
        GestureRecognizer g;
        const double t = 1000.0;
        const double dt = 200.0;  // < kDoubleTapMs
        CHECK(g.playTapCount(t) == 1,          "first play tap = 1 (graceful)");
        CHECK(g.playTapCount(t + dt) == 2,     "second within window = 2 (track cut)");
        CHECK(g.playTapCount(t + 2 * dt) == 3, "third within window = 3 (master cut)");
        CHECK(g.playTapCount(t + 3 * dt) == 3, "fourth clamps at 3 (master cut)");
    }

    static void testPlayTapCountResetsAfterGap()
    {
        GestureRecognizer g;
        g.playTapCount(1000.0);
        g.playTapCount(1000.0 + 100.0);  // -> 2
        const double late = 1000.0 + 100.0 + GestureRecognizer::kDoubleTapMs + 1.0;
        CHECK(g.playTapCount(late) == 1, "a gap past the window resets the count to 1");
    }

    // ── longPress ─────────────────────────────────────────────────────────────

    static void testLongPressShortHold()
    {
        GestureRecognizer g;
        const double t0 = 1000.0;
        g.armLongPress(kRestoreLongPressToken, t0);
        const auto r = g.checkLongPress(kRestoreLongPressToken,
                                        t0 + GestureRecognizer::kLongPressMs - 1.0);
        CHECK(r == LPR::ShortHold, "hold below threshold must be ShortHold");
    }

    static void testLongPressLongHold()
    {
        GestureRecognizer g;
        const double t0 = 1000.0;
        g.armLongPress(kRestoreLongPressToken, t0);
        const auto r = g.checkLongPress(kRestoreLongPressToken,
                                        t0 + GestureRecognizer::kLongPressMs + 1.0);
        CHECK(r == LPR::LongHold, "hold above threshold must be LongHold");
    }

    static void testLongPressNotArmedReturnsNotArmed()
    {
        GestureRecognizer g;
        const auto r = g.checkLongPress(kRestoreLongPressToken, 1000.0);
        CHECK(r == LPR::NotArmed, "un-armed check must return NotArmed");
    }

    static void testLongPressCancelReturnsNotArmed()
    {
        GestureRecognizer g;
        g.armLongPress(kRestoreLongPressToken, 1000.0);
        g.cancelLongPress();
        const auto r = g.checkLongPress(kRestoreLongPressToken, 1200.0);
        CHECK(r == LPR::NotArmed, "check after cancel must return NotArmed");
    }

    static void testLongPressWrongTokenReturnsNotArmed()
    {
        GestureRecognizer g;
        g.armLongPress(kRestoreLongPressToken, 1000.0);
        const auto r = g.checkLongPress(9999, 2000.0);
        CHECK(r == LPR::NotArmed, "wrong token must return NotArmed");
    }

    static void testLongPressResetAfterCheck()
    {
        GestureRecognizer g;
        g.armLongPress(kRestoreLongPressToken, 1000.0);
        g.checkLongPress(kRestoreLongPressToken, 1500.0);  // consume
        const auto r = g.checkLongPress(kRestoreLongPressToken, 2000.0);
        CHECK(r == LPR::NotArmed, "second check without re-arm must return NotArmed");
    }

    // ── token non-collision audit ─────────────────────────────────────────────

    static void testTokensDoNotCollide()
    {
        // Verify that the key token ranges do not overlap with each other.
        // modifier buttons: 1000..1000+255 (ControllerButton values < 256)
        // step indices:     0..63
        // nav past-end:     kNavRightUnlock = 4000
        // restore:          kRestoreLongPressToken = 5000
        CHECK(kRestoreLongPressToken > GestureRecognizer::kNavRightUnlock,
              "restore token must be above nav unlock token");
        CHECK(GestureRecognizer::kNavRightUnlock > 1255,
              "nav unlock token must be above the top of the modifier-button range");
        CHECK(1000 > 63,
              "modifier-button base must be above max step index");
    }

    // ─────────────────────────────────────────────────────────────────────────

    void runGestureRecognizerTests()
    {
        testDoubleTapHitsOnSecond();
        testDoubleTapMissesOnThird();
        testDoubleTapMissesAfterThreshold();
        testDoubleTapDifferentTokensDoNotInteract();
        testInvalidatePreventsDoubleTap();
        testNavRightUnlockTokenValue();
        testPlayDoubleTapHitsWithinThreshold();
        testPlayDoubleTapMissesAfterThreshold();
        testPlayDoubleTapIndependentFromTokenPool();
        testPlayTapCountEscalatesWithinWindow();
        testPlayTapCountResetsAfterGap();
        testLongPressShortHold();
        testLongPressLongHold();
        testLongPressNotArmedReturnsNotArmed();
        testLongPressCancelReturnsNotArmed();
        testLongPressWrongTokenReturnsNotArmed();
        testLongPressResetAfterCheck();
        testTokensDoNotCollide();
    }

} // namespace lockstep
