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
    // Number of host Aux output buses (DESIGN §31.1). Master + Cue + kNumAuxBuses.
    inline constexpr int kNumAuxBuses = 6;

    // Max tracks addressable as bus targets in the Out encoding. The Aux range
    // starts past this so a Track edge and an Aux edge never collide. Kept a fixed
    // constant (not kNumTracks) so the on-disk encoding is stable if track count
    // ever changes.
    inline constexpr int kOutTrackSpan = 32;

    // Encoding (float, stepped slot value):
    //   0                          -> Off     (silent at master; feeds no bus)
    //   1                          -> Master  (default; contributes to master sum)
    //   2 + N                      -> Track N (0-based; removed from master, fed to N)
    //   2 + kOutTrackSpan + A      -> Aux A   (0-based host Aux bus; §31.1)
    // The CHANNEL-block "Out" slot's stable param id (single source of truth —
    // both the ManipulationZone rotary and the editor's encoder path key off it
    // to treat Out as a filtered candidate rotary over validOutTargets()).
    inline constexpr const char* kOutSlotId = "lockstep.amp.out";

    enum class OutputDestKind : std::uint8_t { Off = 0, Master = 1, Track = 2, Aux = 3 };

    inline constexpr int kOutAuxBase = 2 + kOutTrackSpan;   // first Aux encoding value

    struct OutputDestSel
    {
        OutputDestKind kind;
        int track;  // 0-based destination track (kind == Track) OR aux index (kind == Aux)
    };

    inline OutputDestSel decodeOutputDest(float v) noexcept
    {
        const int idx = static_cast<int>(std::lround(v));
        if (idx <= 0) return { OutputDestKind::Off, -1 };
        if (idx == 1) return { OutputDestKind::Master, -1 };
        if (idx >= kOutAuxBase && idx < kOutAuxBase + kNumAuxBuses)
            return { OutputDestKind::Aux, idx - kOutAuxBase };
        return { OutputDestKind::Track, idx - 2 };
    }

    inline float encodeOutputDest(OutputDestKind kind, int index = 0) noexcept
    {
        switch (kind)
        {
            case OutputDestKind::Off:    return 0.0f;
            case OutputDestKind::Master: return 1.0f;
            case OutputDestKind::Track:  return static_cast<float>(index + 2);
            case OutputDestKind::Aux:    return static_cast<float>(kOutAuxBase + index);
        }
        return 1.0f;
    }
}
