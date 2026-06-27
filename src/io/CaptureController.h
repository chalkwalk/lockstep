#pragma once

#include <cstdint>

namespace lockstep
{
    // ── CaptureController ─────────────────────────────────────────────────────
    // Pure decision logic for the "separate tape deck" performance-capture UX.
    // No juce:: — the editor owns the side effects (arming the WAV writer,
    // flushing, deleting files, revealing folders) and feeds this controller the
    // gesture + transport + level events. Unit-tested in isolation.
    //
    // Emergent-tail model (no auto-finalize "mode", no long-press toggle):
    //   * The silence tail is the only automatic stop. Stopping the transport
    //     ends a take for free, because a stopped transport goes quiet.
    //   * Silence only finalizes once "winding down" — armed by a transport
    //     stop *edge* or an explicit tap-stop — never during active playback, so
    //     a long musical rest mid-performance cannot chop the take.
    //   * double-tap = hard cut: the reliable stop, and the only way to end a
    //     drone / self-oscillating patch that never falls below threshold.
    //
    // Gesture grammar (uniform: tap = gentle, double-tap = decisive, long = rare):
    //   Idle      tap = arm (record on Play, or now if already playing)
    //             double = roll now (tape)        long = reveal folder
    //   Armed     tap = disarm                    double = roll now
    //   Recording tap = stop (activate tail)      double = hard cut
    //   JustSaved tap = arm next                  double = roll now
    //                                             long = discard last take
    class CaptureController
    {
    public:
        enum class Phase : uint8_t { Idle, Armed, Recording, JustSaved };

        // Side-effects the editor should perform after a call (then report back
        // via onStarted/onStartFailed/onFinalized for start/finalize).
        struct Out
        {
            bool start    = false;  // processor.startCapture(); then onStarted()/onStartFailed()
            bool finalize = false;  // processor.stopCapture();  then onFinalized()
            bool discard  = false;  // delete the last take file
            bool reveal   = false;  // reveal the captures folder
        };

        // Tuning (linear block-peak magnitude / milliseconds). Tunable after a
        // listen; -60 dBFS ~= 0.001 linear.
        static constexpr float  kSilenceThreshold = 0.001f;
        static constexpr double kTailMs           = 3000.0;
        static constexpr double kJustSavedMs      = 6000.0;

        [[nodiscard]] Phase phase()       const noexcept { return phase_; }
        [[nodiscard]] bool  windingDown() const noexcept { return windingDown_; }
        [[nodiscard]] bool  isRecording() const noexcept { return phase_ == Phase::Recording; }

        // ── Gestures ──────────────────────────────────────────────────────────
        Out onTap(bool transportPlaying) noexcept
        {
            Out out;
            switch (phase_)
            {
                case Phase::Idle:
                    if (transportPlaying) out.start = true;  // start now
                    else                  phase_ = Phase::Armed;
                    break;
                case Phase::Armed:
                    phase_ = Phase::Idle;  // disarm
                    break;
                case Phase::Recording:
                    windRequested_ = true;  // wind down; tail finalizes when quiet
                    break;
                case Phase::JustSaved:
                    // Arm next take (previous take is already saved on disk).
                    if (transportPlaying) out.start = true;
                    else                  phase_ = Phase::Armed;
                    break;
            }
            return out;
        }

        Out onDoubleTap() noexcept
        {
            Out out;
            switch (phase_)
            {
                case Phase::Idle:
                case Phase::Armed:
                case Phase::JustSaved:
                    out.start = true;  // roll immediately (tape)
                    break;
                case Phase::Recording:
                    out.finalize = true;  // hard cut now
                    break;
            }
            return out;
        }

        Out onLongPress() noexcept
        {
            Out out;
            switch (phase_)
            {
                case Phase::JustSaved:
                    out.discard = true;
                    phase_ = Phase::Idle;
                    break;
                case Phase::Idle:
                    out.reveal = true;
                    break;
                case Phase::Armed:
                case Phase::Recording:
                    break;  // reserved / inert
            }
            return out;
        }

        // ── Lifecycle callbacks (editor reports IO result) ────────────────────
        void onStarted(double /*nowMs*/) noexcept
        {
            phase_         = Phase::Recording;
            windRequested_ = false;
            windingDown_   = false;
            quietSinceMs_  = 0.0;
            // Do NOT reset lastPlaying_: it carries the real transport state from
            // the last tick, so the first stop edge after start is detected (and a
            // spurious play edge cannot clear a tap-stop).
        }

        void onStartFailed() noexcept { phase_ = Phase::Idle; }

        void onFinalized(double nowMs) noexcept
        {
            phase_          = Phase::JustSaved;
            justSavedUntil_ = nowMs + kJustSavedMs;
            windRequested_  = false;
            windingDown_    = false;
            quietSinceMs_   = 0.0;
        }

        // ── Periodic tick (from the editor's 30 Hz timer) ─────────────────────
        Out tick(double nowMs, float masterPeak, bool transportPlaying) noexcept
        {
            Out out;

            if (phase_ == Phase::Recording)
            {
                // Edge-driven winding: a stop edge (or tap-stop) arms the tail; a
                // play edge cancels it (resumed). Static state alone never winds.
                if (transportPlaying && !lastPlaying_) windRequested_ = false;
                if (!transportPlaying && lastPlaying_) windRequested_ = true;
            }
            lastPlaying_ = transportPlaying;

            switch (phase_)
            {
                case Phase::Armed:
                    if (transportPlaying) out.start = true;  // record on Play
                    break;
                case Phase::Recording:
                    windingDown_ = windRequested_;
                    if (windingDown_)
                    {
                        if (masterPeak < kSilenceThreshold)
                        {
                            if (quietSinceMs_ == 0.0) quietSinceMs_ = nowMs;
                            else if (nowMs - quietSinceMs_ >= kTailMs) out.finalize = true;
                        }
                        else { quietSinceMs_ = 0.0; }  // audio present: keep recording
                    }
                    else { quietSinceMs_ = 0.0; }
                    break;
                case Phase::JustSaved:
                    if (nowMs >= justSavedUntil_) phase_ = Phase::Idle;
                    break;
                case Phase::Idle:
                    break;
            }
            return out;
        }

    private:
        Phase  phase_          = Phase::Idle;
        bool   windRequested_  = false;  // tail armed (stop edge / tap-stop)
        bool   windingDown_    = false;  // mirror of windRequested_ while Recording
        bool   lastPlaying_    = false;  // transport edge detection
        double quietSinceMs_   = 0.0;    // 0 = not currently quiet
        double justSavedUntil_ = 0.0;
    };
}
