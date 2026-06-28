#pragma once

#include <cmath>
#include <cstdint>

namespace lockstep
{
    // A2 (DESIGN §27): per-track output destination — the CHANNEL block "Out"
    // slot. A track's finished signal goes to the master sum (default), into
    // another track's bus input, or nowhere. Routing is output-directed: the
    // edge "A → B" lives on A's Out slot, not on B's input_source.
    //
    // Encoding (float, stepped slot value):
    //   0      -> Off     (silent at master; feeds no bus)
    //   1      -> Master  (default; contributes to the master sum, as today)
    //   2 + N  -> Track N  (0-based track index; removed from master, fed to N)
    enum class OutputDestKind : std::uint8_t { Off = 0, Master = 1, Track = 2 };

    struct OutputDestSel
    {
        OutputDestKind kind;
        int track;  // 0-based destination track; valid only when kind == Track
    };

    inline OutputDestSel decodeOutputDest(float v) noexcept
    {
        const int idx = static_cast<int>(std::lround(v));
        if (idx <= 0) return { OutputDestKind::Off, -1 };
        if (idx == 1) return { OutputDestKind::Master, -1 };
        return { OutputDestKind::Track, idx - 2 };
    }

    inline float encodeOutputDest(OutputDestKind kind, int track = 0) noexcept
    {
        switch (kind)
        {
            case OutputDestKind::Off:    return 0.0f;
            case OutputDestKind::Master: return 1.0f;
            case OutputDestKind::Track:  return static_cast<float>(track + 2);
        }
        return 1.0f;
    }
}
