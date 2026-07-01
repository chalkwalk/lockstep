#pragma once

#include "../core/Sequence.h"  // kNumTracks

#include <array>
#include <cmath>
#include <cstdint>

namespace lockstep
{
    // Audio-input source selection for input-consuming machines (DESIGN §27).
    //
    // A machine that consumes audio (Route now; Recorder / Looper later) declares
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

    // Full label set for a tap-fork-capable input_source: None, Ext, Master,
    // then T1..T16 (index i>=3 decodes to Track i-3). Machines that support
    // tap-forking (Route / Recorder / Looper) declare their input_source slot with
    // these labels and maxValue = kInputSourceMaxValue, so the stepped param
    // surface renders the track picker directly. Cyclic / self selections are
    // refused at write time (LockstepProcessor::writeParam), not here.
    static_assert(kNumTracks == 16, "kInputSourceLabels must match kNumTracks");
    inline constexpr std::array<const char* const, 3 + kNumTracks> kInputSourceLabels = {
        "None", "Ext", "Master",
        "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8",
        "T9", "T10", "T11", "T12", "T13", "T14", "T15", "T16"
    };
    inline constexpr float kInputSourceMaxValue =
        static_cast<float>(kInputSourceLabels.size() - 1);

    // Labels for the shared volatile REC-slot bank (target_buffer on Recorder and
    // Looper). One per reserved slot; the count matches kNumVolatileSlots in the
    // processor. The capture machine's target_buffer is a stepped 0..N-1 ordinal
    // into pool.nthVolatileIndex().
    inline constexpr std::array<const char* const, 8> kVolatileBufferLabels = {
        "REC1", "REC2", "REC3", "REC4", "REC5", "REC6", "REC7", "REC8"
    };

    // Encode a source selection back to the stepped float (inverse of decode):
    // 0=None, 1=External, 2=Master, 3+N = Track N. Used to build the filtered
    // input rotary (LockstepProcessor::validInputSources), mirroring
    // encodeOutputDest on the routing side.
    [[nodiscard]] inline float encodeInputSource(InputSourceKind kind, int track = 0) noexcept
    {
        switch (kind)
        {
            case InputSourceKind::None:     return 0.0f;
            case InputSourceKind::External: return 1.0f;
            case InputSourceKind::Master:   return 2.0f;
            case InputSourceKind::Track:    return static_cast<float>(3 + (track < 0 ? 0 : track));
        }
        return 0.0f;
    }

    // Slot encoding (stepped float): 0=None, 1=External, 2=Master, 3+N = Track N.
    [[nodiscard]] inline InputSourceSel decodeInputSource(float value) noexcept
    {
        const int iv = static_cast<int>(std::lround(value));
        if (iv <= 0) return { InputSourceKind::None, -1 };
        if (iv == 1) return { InputSourceKind::External, -1 };
        if (iv == 2) return { InputSourceKind::Master, -1 };
        return { InputSourceKind::Track, iv - 3 };
    }
}
