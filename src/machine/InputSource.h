#pragma once

#include <cmath>
#include <cstdint>

namespace lockstep
{
    // Audio-input source selection for input-consuming machines (DESIGN §27).
    //
    // A machine that consumes audio (Thru now; Recorder / Looper later) declares
    // a single stepped slot with this canonical id. The sequencer reads the
    // resolved value each block and fills the track's buffer from the chosen
    // source *before* calling process():
    //
    //   None     — the machine synthesises into an empty buffer (default).
    //   External — the plugin's audio input bus (sidechain / standalone input).
    //   Master   — the plugin's master sum, prior block (the one sanctioned tap).
    //   Track N  — track N's post-chain output (deferred to A2 / topo-sort).
    //
    // Routing is by source selection, not patch matrix: a track reads exactly one
    // declared source, and Track-N edges are topologically ordered with cycles
    // refused at assignment time (A2).
    inline constexpr const char* kInputSourceSlotId = "input_source";

    enum class InputSourceKind : std::uint8_t
    {
        None = 0,
        External = 1,
        Master = 2,
        Track = 3
    };

    struct InputSourceSel
    {
        InputSourceKind kind = InputSourceKind::None;
        int track = -1;  // 0-based track index when kind == Track; else -1.
    };

    // Slot encoding (stepped float): 0=None, 1=External, 2=Master, 3+N = Track N.
    // The value labels live with the declaring machine's ParamSpec.
    [[nodiscard]] inline InputSourceSel decodeInputSource(float value) noexcept
    {
        const int iv = static_cast<int>(std::lround(value));
        if (iv <= 0) return { InputSourceKind::None, -1 };
        if (iv == 1) return { InputSourceKind::External, -1 };
        if (iv == 2) return { InputSourceKind::Master, -1 };
        return { InputSourceKind::Track, iv - 3 };
    }
}
