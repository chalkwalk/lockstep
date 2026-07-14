#pragma once

// Meter geometry + gesture arithmetic (9.31). Pure and JUCE-free so the same
// numbers can be unit-tested and reused by an external controller surface --
// a level readout that only exists inside a paint() call cannot be mirrored to
// hardware, and a drag whose mapping lives inline in mouseDrag() cannot be
// tested at all. The paint paths own pixels; this owns the mapping.

#include <algorithm>
#include <cmath>

namespace lockstep::meter
{
    // Pixels of vertical travel for the master gain's full range. Matches the
    // convention the retired track-VU drag used, so the feel is unchanged.
    inline constexpr float kDragScalePx = 200.0f;

    // x offset of a track's level tick within a VU cell of `width` px.
    // Clamped to [0, width - 1] so a tick at full level stays inside the cell
    // (a 1 px tick drawn AT `width` is drawn outside it).
    [[nodiscard]] inline int levelTickX(float level, float maxValue, int width) noexcept
    {
        if (width <= 0) return 0;
        const float span = (maxValue > 0.0f) ? maxValue : 1.0f;
        const float n = std::clamp(level / span, 0.0f, 1.0f);
        const int x = static_cast<int>(std::lround(n * static_cast<float>(width)));
        return std::clamp(x, 0, width - 1);
    }

    // New master gain after dragging `dyPx` (screen-down positive) from `startDb`.
    // Dragging UP raises the level, so the caller passes startY - currentY.
    [[nodiscard]] inline float masterDragDb(float startDb, int dyPx,
                                            float minDb, float maxDb) noexcept
    {
        const float range = maxDb - minDb;
        const float delta = (static_cast<float>(dyPx) / kDragScalePx) * range;
        return std::clamp(startDb + delta, minDb, maxDb);
    }

    // y of the gain tick within a meter spanning [meterY, meterY + meterH), whose
    // scale runs -floorDb (bottom) .. 0 dBFS (top). The gain range extends above
    // 0 dB, which the meter's scale has no room for, so a boosted gain pins to the
    // top rather than being drawn off the meter -- the VOL chip carries the number.
    [[nodiscard]] inline int gainTickY(float gainDb, int meterY, int meterH,
                                       float floorDb) noexcept
    {
        if (meterH <= 0 || floorDb <= 0.0f) return meterY;
        const float n = std::clamp((gainDb + floorDb) / floorDb, 0.0f, 1.0f);
        const int y = meterY + meterH
                    - static_cast<int>(std::lround(n * static_cast<float>(meterH)));
        return std::clamp(y, meterY, meterY + meterH - 1);
    }
}
