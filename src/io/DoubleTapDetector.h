#pragma once

namespace lockstep
{
    // Detects double-tap: two presses of the same token within threshold ms.
    //
    // Token scheme (non-colliding):
    //   modifier buttons  — 1000 + static_cast<int>(ControllerButton)
    //   step keys         — step index (0..63, always < 1000)
    //
    // After a double-tap is confirmed the detector resets, so a triple-tap
    // does not register as two consecutive doubles.
    class DoubleTapDetector
    {
    public:
        static constexpr double kThresholdMs = 350.0;

        // Record a button-down event. Returns true iff this press is the second
        // tap of a double-tap sequence (same token, within threshold).
        bool recordAndCheck(int token, double nowMs) noexcept
        {
            const bool isDouble = (token == lastToken_)
                               && (nowMs - lastTimeMs_) < kThresholdMs;
            lastToken_  = isDouble ? -1 : token;  // reset after double so triple != two doubles
            lastTimeMs_ = nowMs;
            return isDouble;
        }

        // Invalidate the pending tap (e.g. on focus loss or mode change).
        void invalidate() noexcept { lastToken_ = -1; }

    private:
        int    lastToken_  = -1;
        double lastTimeMs_ = 0.0;
    };
}
