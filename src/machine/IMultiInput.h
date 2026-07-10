#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    // Optional mix-in for a machine that consumes MORE THAN ONE audio input — a
    // deck with several sub-tracks, each pulling its own source (DESIGN §40.3).
    //
    // Sub-track 0 is the ordinary track input: the processor fills `trackBuffers_`
    // from `input_source`, and the machine reads it as the `buffer` argument to
    // process(), exactly as a single-input machine does. This interface covers
    // only the *extra* sub-tracks (1..N-1): the machine owns a buffer for each, and
    // the processor fills it from `input_source_2..4` through the same tap
    // machinery (11.3a) before process(). The parallel to ITempoAware is
    // deliberate — one dynamic_cast, one per-block fill, no change to a machine
    // that does not implement it.
    //
    // A machine returning numInputSubTracks() <= 1 is invisible here (the processor
    // does nothing), so declaring the interface costs a single-sub-track deck
    // nothing. Buffers are the machine's to size (message thread) and the
    // processor's to fill (audio thread); the machine must size them to at least
    // the block length before the fill, or that sub-track is skipped for the block.
    class IMultiInput
    {
    public:
        virtual ~IMultiInput() = default;

        // How many input sub-tracks are live this block (1..kMaxInputSubTracks).
        // 1 = only sub-track 0 (the ordinary path); the processor fills no extras.
        [[nodiscard]] virtual int numInputSubTracks() const noexcept = 0;

        // A writable buffer for extra sub-track `sub` (1..N-1). The machine owns it;
        // the processor clears and fills it each block, and the machine reads it in
        // process(). Sub-track 0 is never requested here — it is the track buffer.
        [[nodiscard]] virtual juce::AudioBuffer<float>& inputSubTrackBuffer(int sub) noexcept = 0;
    };
}
