#pragma once

#include "../deckcore/Deck.h"
#include "../deckcore/Medium.h"
#include "ITempoAware.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace lockstep
{
    // deck_juce — the thin adapter between Lockstep and the JUCE-free deck engine
    // (DESIGN §40.11). This header is the ONLY place that knows both vocabularies.
    // Everything below it (`dc::`) is portable and licence-independent; everything
    // above it is Lockstep. The partner app writes its own adapter, or none at all.
    //
    // Nothing here allocates or copies audio. A juce::AudioBuffer becomes a medium
    // by lending its channel pointers, which is why dc::Medium is non-owning and
    // binds planes rather than one flat block: the pool slot stays exactly where
    // it is, owned by the pool.

    // Pack the transport authority's per-block snapshot. The deck is a client of
    // §25, never an owner: it reads this and cannot write back.
    [[nodiscard]] inline dc::TransportSnapshot toSnapshot(const TransportInfo& t) noexcept
    {
        dc::TransportSnapshot s;
        s.sampleRate = t.sampleRate;
        s.samplesPerBar = t.samplesPerBar;
        s.barPpq = t.barPpq;
        s.running = t.running;
        s.positionSamples = t.transportPhaseSamples;
        s.launchQuantPeriodSamples = t.launchQuantPeriodSamples;
        s.launchQuantPhaseOffsetSamples = t.launchQuantPhaseOffsetSamples;
        return s;
    }

    // Bind a juce::AudioBuffer as a single-sub-track medium of `lengthSamples`.
    // The buffer's channels are separate allocations, so they cross as planes.
    // Up to two channels bind — the stereo engine boundary (§40.7); a mono buffer
    // binds one and the caller duplicates at the engine edge.
    //
    // The high-water mark is NOT set here: whether the buffer's content is
    // recorded audio or uninitialised memory is the caller's knowledge. A caller
    // adopting existing content calls `medium.ensureCommitted(0, length)` after
    // binding; one about to record lets the write head commit as it goes.
    inline void bindBuffer(dc::Medium& m, juce::AudioBuffer<float>& buf, int lengthSamples,
                           dc::Topology topology, double mediumRate) noexcept
    {
        const int chans = juce::jmin(2, buf.getNumChannels());
        if (chans <= 0 || lengthSamples <= 0 || lengthSamples > buf.getNumSamples())
        {
            m.unbind();
            return;
        }

        dc::Medium::Config cfg;
        cfg.topology = topology;
        cfg.mediumRate = mediumRate;
        cfg.numSubTracks = 1;
        cfg.channelsPerSubTrack = chans;
        cfg.capacitySamples = lengthSamples;

        std::array<dc::Store, 2> planes{};
        for (int ch = 0; ch < chans; ++ch)
            planes[static_cast<std::size_t>(ch)] =
                dc::Store{ buf.getWritePointer(ch), static_cast<std::size_t>(lengthSamples) };

        m.bindPlanes(cfg, planes.data(), chans);
    }
}
