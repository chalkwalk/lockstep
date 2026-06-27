#pragma once
#include <cstdint>
#include "../../io/DoubleTapDetector.h"

namespace lockstep
{
    // Single home for all input-timing state: multi-token double-tap, a Play-specific
    // double-tap (separate timer for UX continuity), and long-press arm/check.
    //
    // Replaces three separate fields that were scattered across PluginEditor:
    //   double lastPlayPressTime_   — playDoubleTap()
    //   bool   restoreActive_       — armLongPress() / checkLongPress()
    //   double restoreKeyDownMs_    — armLongPress() / checkLongPress()
    // and the bare DoubleTapDetector doubleTap_ member — doubleTap() / invalidate().
    class GestureRecognizer
    {
    public:
        enum class LongPressResult : uint8_t { NotArmed, ShortHold, LongHold };

        static constexpr double kDoubleTapMs = DoubleTapDetector::kThresholdMs;  // 350 ms
        static constexpr double kLongPressMs = 350.0;                             // ms

        // Named token for the NavRight past-end unlock double-tap.
        // Replaces the magic literal 4000 at the dispatch callsite.
        static constexpr int kNavRightUnlock = 4000;

        // ── Multi-token double-tap ─────────────────────────────────────────────
        // Token scheme (non-colliding across all uses):
        //   modifier buttons  — 1000 + static_cast<int>(ControllerButton)
        //   step keys         — raw step index (0..63)
        //   nav past-end      — kNavRightUnlock (4000)
        bool doubleTap(int token, double nowMs) noexcept
        {
            return dtap_.recordAndCheck(token, nowMs);
        }
        void invalidate() noexcept { dtap_.invalidate(); }

        // ── Play-specific double-tap ───────────────────────────────────────────
        // Kept separate from the shared token pool so pressing another button
        // between two Play presses does not cancel the gesture (matches prior UX).
        bool playDoubleTap(double nowMs) noexcept
        {
            const bool r = (nowMs - playLastMs_) < kDoubleTapMs;
            playLastMs_ = nowMs;
            return r;
        }

        // ── Long-press ─────────────────────────────────────────────────────────
        // Arm on key-down; resolve on key-up with checkLongPress.
        void armLongPress(int token, double nowMs) noexcept
        {
            longPressToken_   = token;
            longPressStartMs_ = nowMs;
            longPressActive_  = true;
        }
        void cancelLongPress() noexcept { longPressActive_ = false; }

        // Non-consuming check: true once the armed token has been held past the
        // threshold. Lets a timer poll fire a long-press mid-hold (picker appears
        // while held) without disturbing the arm state; key-up still calls
        // checkLongPress for the tap/short-hold path.
        bool longPressElapsed(int token, double nowMs) const noexcept
        {
            return longPressActive_ && longPressToken_ == token
                && (nowMs - longPressStartMs_) >= kLongPressMs;
        }

        LongPressResult checkLongPress(int token, double nowMs) noexcept
        {
            if (!longPressActive_ || longPressToken_ != token)
                { return LongPressResult::NotArmed; }
            longPressActive_ = false;
            return (nowMs - longPressStartMs_) >= kLongPressMs
                       ? LongPressResult::LongHold : LongPressResult::ShortHold;
        }

    private:
        DoubleTapDetector dtap_;
        double playLastMs_     = 0.0;
        int    longPressToken_  = -1;
        double longPressStartMs_ = 0.0;
        bool   longPressActive_ = false;
    };

    // Token for the Restore long-press (does not collide with modifier tokens
    // at 1000+btn, step indices 0..63, or GestureRecognizer::kNavRightUnlock).
    static constexpr int kRestoreLongPressToken = 5000;

    // Token for the FX-section long-press (tap = navigate, hold = open picker).
    static constexpr int kFxSectionLongPressToken = 6000;

    // Token for the CAPTURE cell (tape deck): tap / double-tap / long-press all
    // resolve on this one token (double-tap and long-press use independent state
    // in GestureRecognizer, so sharing the token is safe).
    static constexpr int kCaptureToken = 7000;

} // namespace lockstep
