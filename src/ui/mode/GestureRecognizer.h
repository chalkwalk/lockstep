#pragma once
#include <algorithm>
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

        // Play tap counter for the layered stop: consecutive Play presses within
        // kDoubleTapMs increment the count (1, 2, 3, …); a longer gap resets to 1.
        // 1 = graceful stop, 2 = track cut, 3 = master cut (clamped at 3).
        int playTapCount(double nowMs) noexcept
        {
            playTapCount_ = (nowMs - playLastMs_) < kDoubleTapMs
                                ? std::min(3, playTapCount_ + 1) : 1;
            playLastMs_ = nowMs;
            return playTapCount_;
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
        int    playTapCount_   = 0;
        int    longPressToken_  = -1;
        double longPressStartMs_ = 0.0;
        bool   longPressActive_ = false;
    };

    // Token for the Restore long-press (does not collide with modifier tokens
    // at 1000+btn, step indices 0..63, or GestureRecognizer::kNavRightUnlock).
    static constexpr int kRestoreLongPressToken = 5000;

    // Token for a section-key long-press (tap = navigate, hold = open the picker the
    // TABLE names for that key + scope: FX inserts, master FX, the machine — 9.12 st.7d).
    // One token, because it is one gesture; which picker it opens is the binding's call.
    static constexpr int kSectionHoldToken = 6000;

    // Token for a long-press on the *loaded* FX-picker catalogue cell (tap =
    // bypass, long-press = remove the effect from the targeted slot).
    static constexpr int kFxPickerCellLongPressToken = 6001;

    // Token for a long-press on a machine's console-owning section key (tap =
    // page that section's params, long-press = open/close the OnDemand console, 7b).
    static constexpr int kMachineConsoleLongPressToken = 6002;

    // Token for the DELETE hold (9.29): scope + tap(Clear) clears that scope's
    // contents, scope + HOLD(Clear) deletes the entity. Delete left the Func
    // qualifier when Func+Track became the Machine scope; the gesture axis is the
    // better home anyway — the destructive verb should cost the deliberate gesture.
    static constexpr int kDeleteHoldToken = 6004;

    // Token for the CAPTURE cell (tape deck): tap / double-tap / long-press all
    // resolve on this one token (double-tap and long-press use independent state
    // in GestureRecognizer, so sharing the token is safe).
    static constexpr int kCaptureToken = 7000;

    // Tokens for the looper verbs (Track+Record / Track+Play on a focused looper):
    // a double-tap forces the edge instantly, overriding the sync-mode quantize (#2).
    static constexpr int kLooperRecordToken = 7100;
    static constexpr int kLooperPlayToken   = 7200;
    // S4: long-press on the looper console REC/DUB cell = momentary punch-replace.
    static constexpr int kLooperReplaceToken = 7400;
    // §40.13: double-tap on the Tape console REC cell while recording = retro
    // backfill the run-up before the punch-in (the deck remembers your first tap).
    static constexpr int kTapeRecordToken = 7500;

    // Hold-Record = transport reset (rewind to phrase start). Long-press on the
    // bare Record verb; the tap path keeps the ordinary record-arm/overdub.
    static constexpr int kTransportResetToken = 7300;

    // 9.17: per-view step-token bases for the launch-quantize instant override.
    // Each launch view double-taps `base + stepIndex` so a step double-tap in one
    // view never collides with the same step in another (Song / Mute / Phrase).
    static constexpr int kSongStepTokenBase     = 6000;   // Song + step
    static constexpr int kMuteStepTokenBase     = 6100;   // Mute + step (commit 4)
    static constexpr int kPhraseStepTokenBase   = 6200;   // Phrase + step
    static constexpr int kRelaunchStepTokenBase = 6300;   // Mute+Play + step (relaunch)

} // namespace lockstep
