#pragma once

#include "../core/Sequence.h"  // kNumTracks
#include "ChannelPolicy.h"     // kEngineChannels

#include <array>
#include <cmath>
#include <cstdint>

namespace lockstep
{
    // Audio-input source selection for input-consuming machines (DESIGN §27).
    //
    // A machine that consumes audio (Route now; Record / Loop later) declares
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

    // A deck has up to four sub-tracks, and each selects its own source (DESIGN
    // §40.3): sub-tracks down, sources across, which is the Route machine's grid
    // reused. Sub-track 0 keeps the canonical `input_source` id — it is the only
    // one a single-sub-track machine declares, so nothing about today's Record,
    // Loop or Route changes, on disk or in the graph.
    //
    // There is deliberately no push side. Every routing the push model expresses,
    // the pull model expresses from the place that already knows how: a track
    // reads a source. The CHANNEL "Out" enum stays {Master | Track N | Off}.
    inline constexpr int kMaxInputSubTracks = 4;
    // S3: the plugin declares this many stereo external input buses (In, In 2..4);
    // the input_source enum exposes them as Ext1..Ext4. Only the first is enabled
    // by default. Captured into inputCapture_ channel-pairs [2*ext, 2*ext+1].
    inline constexpr int kNumExtInputs = 4;
    // Channels per sub-track: a sub-track is a stereo pair, so this is the §40.7
    // engine invariant, not a number of its own. Deck-medium widths derive from it.
    inline constexpr int kDeckChannelsPerSubTrack = kEngineChannels;

    // The channel width of a full four-sub-track deck (§40.7): each sub-track is a
    // stereo pair, so four sub-tracks are eight channels held in one wide volatile
    // slot (§40.3). This is what a volatile slot must be *allocated* to cover; a
    // one-sub-track loop still captures only two.
    //
    // These two — kMaxInputSubTracks and kEngineChannels — are the ONLY homes for
    // the deck's width. `deck_core` declares neither (DESIGN §40.11): a dc::Deck is
    // constructed at whatever capacity its host asks for, and dc::Medium takes both
    // as runtime Config. Widening Lockstep's decks is an edit to this file alone.
    inline constexpr int kMaxDeckChannels = kDeckChannelsPerSubTrack * kMaxInputSubTracks;
    inline constexpr std::array<const char* const, kMaxInputSubTracks> kInputSourceSlotIds = {
        "input_source", "input_source_2", "input_source_3", "input_source_4"
    };

    [[nodiscard]] inline constexpr const char* inputSourceSlotId(int sub) noexcept
    {
        return (sub >= 0 && sub < kMaxInputSubTracks)
                   ? kInputSourceSlotIds[static_cast<std::size_t>(sub)]
                   : kInputSourceSlotIds[0];
    }

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
        int ext = 0;     // 0-based external bus (0..kNumExtInputs-1) when kind == External.
    };

    // Full label set for a tap-fork-capable input_source: None, Ext, Master,
    // then T1..T16 (index i>=3 decodes to Track i-3). Machines that support
    // tap-forking (Route / Record / Loop) declare their input_source slot with
    // these labels and maxValue = kInputSourceMaxValue, so the stepped param
    // surface renders the track picker directly. Cyclic / self selections are
    // refused at write time (LockstepProcessor::writeParam), not here.
    // Encoding is append-only (S3): 0 None, 1 Ext1, 2 Master, 3..18 T1..T16,
    // 19..21 Ext2..Ext4. The extra externals sit past the Track range so every
    // stored value keeps its meaning across the one-to-four-bus upgrade (legacy
    // "Ext" = 1 reads as Ext1). The odd ordering after T16 is the cost of that.
    static_assert(kNumTracks == 16, "kInputSourceLabels must match kNumTracks");
    inline constexpr std::array<const char* const, 3 + kNumTracks + (kNumExtInputs - 1)>
        kInputSourceLabels = {
        "None", "Ext1", "Master",
        "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8",
        "T9", "T10", "T11", "T12", "T13", "T14", "T15", "T16",
        "Ext2", "Ext3", "Ext4"
    };
    inline constexpr float kInputSourceMaxValue =
        static_cast<float>(kInputSourceLabels.size() - 1);

    // Labels for the shared volatile REC-slot bank (target_buffer on Record and
    // Loop). One per reserved slot; the count matches kNumVolatileSlots in the
    // processor. The capture machine's target_buffer is a stepped 0..N-1 ordinal
    // into pool.nthVolatileIndex().
    // One label per reserved volatile REC slot. Must stay the same length as
    // LockstepProcessor::kNumVolatileSlots — it is what sizes the target_buffer
    // rotary (A5 grew the bank from 8 to 16).
    inline constexpr std::array<const char* const, 16> kVolatileBufferLabels = {
        "REC1", "REC2", "REC3",  "REC4",  "REC5",  "REC6",  "REC7",  "REC8",
        "REC9", "REC10", "REC11", "REC12", "REC13", "REC14", "REC15", "REC16"
    };

    // Encode a source selection back to the stepped float (inverse of decode):
    // 0=None, 1=External, 2=Master, 3+N = Track N. Used to build the filtered
    // input rotary (LockstepProcessor::validInputSources), mirroring
    // encodeOutputDest on the routing side.
    [[nodiscard]] inline float encodeInputSource(InputSourceKind kind, int track = 0,
                                                 int ext = 0) noexcept
    {
        switch (kind)
        {
            case InputSourceKind::None:   return 0.0f;
            case InputSourceKind::External:
                // Ext1 = 1 (legacy); Ext2..Ext4 appended at 19..21 (ext = 1..3).
                return ext <= 0 ? 1.0f : static_cast<float>(18 + ext);
            case InputSourceKind::Master: return 2.0f;
            case InputSourceKind::Track:  return static_cast<float>(3 + (track < 0 ? 0 : track));
        }
        return 0.0f;
    }

    // Slot encoding (stepped float, append-only): 0 None, 1 Ext1, 2 Master,
    // 3..18 T1..T16, 19..21 Ext2..Ext4.
    [[nodiscard]] inline InputSourceSel decodeInputSource(float value) noexcept
    {
        const int iv = static_cast<int>(std::lround(value));
        if (iv <= 0)  return { InputSourceKind::None, -1, 0 };
        if (iv == 1)  return { InputSourceKind::External, -1, 0 };       // Ext1
        if (iv == 2)  return { InputSourceKind::Master, -1, 0 };
        if (iv <= 18) return { InputSourceKind::Track, iv - 3, 0 };      // T1..T16
        if (iv <= 18 + (kNumExtInputs - 1))
            return { InputSourceKind::External, -1, iv - 18 };           // Ext2..Ext4
        return { InputSourceKind::None, -1, 0 };
    }
}
